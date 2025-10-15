#include "router.hpp"
#include "serialize.hpp"
#include "config.hpp"
#include <cstring>   // for memcpy

#include <algorithm> // std::find

namespace seds
{
    // ---- internal helpers (cpp-only) ----

    // Pick default endpoints from your schema entry for this DataType.
    static inline const std::vector<DataEndpoint>& default_endpoints_for(DataType ty)
    {
        // Assumes your MESSAGE_ELEMENTS table’s entry stores a std::vector<DataEndpoint> named `endpoints`.
        // This matches your earlier change to MessageMeta.
        return MESSAGE_ELEMENTS[static_cast<int>(ty)].endpoints;
    }

    // ---- ctor ----
    Router::Router(TxFn tx, BoardConfig cfg)
        : transmit_(std::move(tx)), config_(std::move(cfg))
    {
    }

    // ---- float logging → pack to bytes, reuse byte path ----
    Result<void, ErrorInfo>
    Router::log(const DataType ty, const std::vector<float> & values, const uint64_t ts) const noexcept
    {
        std::vector<uint8_t> bytes(values.size() * sizeof(float));
        if (!values.empty())
        {
            std::memcpy(bytes.data(), values.data(), bytes.size());
        }
        return log(ty, bytes, ts);
    }

    Result<void, ErrorInfo>
    Router::log_queue(const DataType ty, const std::vector<float> & values, const uint64_t ts) noexcept
    {
        std::vector<uint8_t> bytes(values.size() * sizeof(float));
        if (!values.empty())
        {
            std::memcpy(bytes.data(), values.data(), bytes.size());
        }
        return log_queue(ty, bytes, ts);
    }

    // ---- bytes logging ----
    Result<void, ErrorInfo>
    Router::log(DataType ty, const std::vector<uint8_t> & data, const uint64_t ts) const noexcept
    {
        const auto & eps = default_endpoints_for(ty);

        // Build the packet
        const TelemetryPacket pkt(ty, eps, DEVICE_IDENTIFIER, ts, data);

        // Serialize
        auto bytes = Serializer::serialize(pkt);
        if (bytes.is_err())
            return Result<void, ErrorInfo>::err(bytes.error());

        // Transmit if callback exists
        if (transmit_)
        {
            auto tx_r = transmit_(bytes.value());
            if (tx_r.is_err())
                return Result<void, ErrorInfo>::err(tx_r.error());
        }

        // Also deliver locally to any handler whose endpoint appears in the packet
        for (const auto & eh : config_.handlers)
        {
            if (std::find(eps.begin(), eps.end(), eh.endpoint) != eps.end())
            {
                if (auto r = eh.handler(pkt); r.is_err())
                    return Result<void, ErrorInfo>::err(r.error());
            }
        }

        return Result<void, ErrorInfo>::ok();
    }

    Result<void, ErrorInfo>
    Router::log_queue(const DataType ty, const std::vector<uint8_t> & data, const uint64_t ts) noexcept
    {
        // If the board has explicit local_endpoints configured, prefer those;
        // otherwise fall back to the schema defaults so the queued packet
        // looks like an immediate log() packet.
        const auto& eps = !config_.local_endpoints.empty()
                        ? config_.local_endpoints
                        : default_endpoints_for(ty);

        const TelemetryPacket pkt(ty, eps, DEVICE_IDENTIFIER, ts, data);
        return queue_tx_message(pkt);
    }

    // ---- queue helpers ----
    Result<void, ErrorInfo>
    Router::queue_tx_message(const TelemetryPacket & pkt) noexcept
    {
        send_queue.push(pkt);
        return Result<void, ErrorInfo>::ok();
    }

    // ---- RX path (direct) ----
    Result<void, ErrorInfo>
    Router::receive(const TelemetryPacket & pkt) const noexcept
    {
        // Deliver to any matching handler (endpoint present in pkt.endpoints)
        for (const auto & eh : config_.handlers)
        {
            const auto & eps = *pkt.endpoints;
            if (std::find(eps.begin(), eps.end(), eh.endpoint) != eps.end())
            {
                auto r = eh.handler(pkt);
                if (r.is_err())
                    return Result<void, ErrorInfo>::err(r.error());
            }
        }
        return Result<void, ErrorInfo>::ok();
    }

    Result<void, ErrorInfo>
    Router::receive_serialized(const std::vector<uint8_t> & bytes) const noexcept
    {
        auto pkt_r = TelemetryPacket::deserialize(bytes);
        if (pkt_r.is_err())
            return Result<void, ErrorInfo>::err(pkt_r.error());
        return receive(pkt_r.value());
    }

    // ---- RX path (queue) ----
    Result<void, ErrorInfo>
    Router::rx_serialized_packet_to_queue(const std::vector<uint8_t> & bytes) noexcept
    {
        auto pkt_r = TelemetryPacket::deserialize(bytes);
        if (pkt_r.is_err())
            return Result<void, ErrorInfo>::err(pkt_r.error());
        received_queue.push(pkt_r.value());
        return Result<void, ErrorInfo>::ok();
    }

    Result<void, ErrorInfo>
    Router::rx_packet_to_queue(const TelemetryPacket & pkt) noexcept
    {
        received_queue.push(pkt);
        return Result<void, ErrorInfo>::ok();
    }

    // ---- pump queues ----
    Result<void, ErrorInfo>
    Router::process_send_queue() noexcept
    {
        while (!send_queue.empty())
        {
            const auto & pkt = send_queue.front();
            auto ser_r = Serializer::serialize(pkt);
            if (ser_r.is_err())
                return Result<void, ErrorInfo>::err(ser_r.error());

            if (transmit_)
            {
                auto tx_result = transmit_(ser_r.value());
                if (tx_result.is_err())
                    return Result<void, ErrorInfo>::err(tx_result.error());
            }

            send_queue.pop();
        }
        return Result<void, ErrorInfo>::ok();
    }

    Result<void, ErrorInfo>
    Router::process_received_queue() noexcept
    {
        while (!received_queue.empty())
        {
            const auto & pkt = received_queue.front();
            auto r = receive(pkt);
            if (r.is_err())
                return Result<void, ErrorInfo>::err(r.error());
            received_queue.pop();
        }
        return Result<void, ErrorInfo>::ok();
    }

    Result<void, ErrorInfo>
    Router::process_all_queues() noexcept
    {
        auto a = process_send_queue();
        if (a.is_err())
            return Result<void, ErrorInfo>::err(a.error());
        return process_received_queue();
    }

    // ---- time-bound pumps ----
    Result<void, ErrorInfo>
    Router::process_tx_queue_with_timeout(const Clock * clock, uint32_t timeout_ms) noexcept
    {
        if (!clock)
            return Result<void, ErrorInfo>::err(ErrorInfo(TelemetryError::BadArg, "null clock"));

        const uint64_t end = clock->now_ms() + timeout_ms;
        while (clock->now_ms() < end)
        {
            if (send_queue.empty()) break;
            auto r = process_send_queue();
            if (r.is_err())
                return Result<void, ErrorInfo>::err(r.error());
        }
        return Result<void, ErrorInfo>::ok();
    }

    Result<void, ErrorInfo>
    Router::process_rx_queue_with_timeout(const Clock * clock, uint32_t timeout_ms) noexcept
    {
        if (!clock)
            return Result<void, ErrorInfo>::err(ErrorInfo(TelemetryError::BadArg, "null clock"));

        const uint64_t end = clock->now_ms() + timeout_ms;
        while (clock->now_ms() < end)
        {
            if (received_queue.empty()) break;
            auto r = process_received_queue();
            if (r.is_err())
                return Result<void, ErrorInfo>::err(r.error());
        }
        return Result<void, ErrorInfo>::ok();
    }

    Result<void, ErrorInfo>
    Router::process_all_queues_with_timeout(const Clock * clock, uint32_t timeout_ms) noexcept
    {
        if (!clock)
            return Result<void, ErrorInfo>::err(ErrorInfo(TelemetryError::BadArg, "null clock"));

        const uint64_t end = clock->now_ms() + timeout_ms;
        while (clock->now_ms() < end)
        {
            auto r = process_all_queues();
            if (r.is_err())
                return Result<void, ErrorInfo>::err(r.error());
            if (send_queue.empty() && received_queue.empty()) break;
        }
        return Result<void, ErrorInfo>::ok();
    }

} // namespace seds
