// router.cpp — leak-free, Rust-parity rewrite
#include "router.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>     // if Router uses std::queue internally, fine; we only interact via methods
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "serialize.hpp"

extern "C" int swprintf(wchar_t * s, size_t n, const wchar_t * fmt, ...);

namespace seds
{
    // Simple stdout fallback (works under std; no-op alternative could be added)
    static void fallback_stdout(const std::string & msg)
    {
        std::printf("%s\n", msg.c_str());
    }

    // -------------------- Router impl --------------------

    TelemetryResult<void *> Router::process_send_queue()
    {
        while (!transmit_queue_.empty())
        {
            TelemetryPacket pkt = std::move(transmit_queue_.front());
            transmit_queue_.pop();
            if (auto r = send(pkt); r.is_err()) return r;
        }
        return TelemetryResult<void *>::Ok(nullptr);
    }

    TelemetryResult<void *> Router::process_all_queues()
    {
        if (const auto r1 = process_send_queue(); r1.is_err()) return r1;
        return process_received_queue();
    }

    void Router::clear_queues()
    {
        clear_rx_queue();
        clear_tx_queue();
    }

    void Router::clear_rx_queue() { received_queue_ = {}; }
    void Router::clear_tx_queue() { transmit_queue_ = {}; }

    TelemetryResult<void *> Router::process_tx_queue_with_timeout(const std::uint32_t timeout_ms)
    {
        const std::uint64_t start = clock_->now_ms();
        while (!transmit_queue_.empty())
        {
            TelemetryPacket pkt = std::move(transmit_queue_.front());
            transmit_queue_.pop();
            if (auto r = send(pkt); r.is_err()) return r;
            if (clock_->now_ms() - start >= static_cast<std::uint64_t>(timeout_ms)) break;
        }
        return TelemetryResult<void *>::Ok(nullptr);
    }

    TelemetryResult<void *> Router::handle_rx_queue_item(RxQueueItem item)
    {
        if (std::holds_alternative<TelemetryPacket>(item))
        {
            return receive(std::get<TelemetryPacket>(item));
        }
        return receive_serialized(std::get<std::vector<std::uint8_t> >(item));
    }

    TelemetryResult<void *> Router::process_rx_queue_with_timeout(const std::uint32_t timeout_ms)
    {
        const std::uint64_t start = clock_->now_ms();
        while (!received_queue_.empty())
        {
            RxQueueItem it = std::move(received_queue_.front());
            received_queue_.pop();
            if (auto r = handle_rx_queue_item(std::move(it)); r.is_err()) return r;
            if (clock_->now_ms() - start >= static_cast<std::uint64_t>(timeout_ms)) break;
        }
        return TelemetryResult<void *>::Ok(nullptr);
    }

    TelemetryResult<void *> Router::process_all_queues_with_timeout(const std::uint32_t timeout_ms)
    {
        const bool drain_fully = (timeout_ms == 0);
        const std::uint64_t start = drain_fully ? 0 : clock_->now_ms();

        for (;;)
        {
            bool did_any = false;

            if (!transmit_queue_.empty())
            {
                TelemetryPacket pkt = std::move(transmit_queue_.front());
                transmit_queue_.pop();
                if (auto r = send(pkt); r.is_err()) return r;
                did_any = true;
            }
            if (!drain_fully && (clock_->now_ms() - start >= static_cast<std::uint64_t>(timeout_ms)))
            {
                break;
            }
            if (!received_queue_.empty())
            {
                RxQueueItem it = std::move(received_queue_.front());
                received_queue_.pop();
                if (auto r = handle_rx_queue_item(std::move(it)); r.is_err()) return r;
                did_any = true;
            }
            if (!drain_fully && (clock_->now_ms() - start >= static_cast<std::uint64_t>(timeout_ms)))
            {
                break;
            }
            if (!did_any) break;
        }

        return TelemetryResult<void *>::Ok(nullptr);
    }

    TelemetryResult<void *> Router::queue_tx_message(TelemetryPacket pkt)
    {
        if (auto v = pkt.Validate(); v.is_err()) return TelemetryResult<void *>::Err(v.unwrap_err());
        transmit_queue_.push(std::move(pkt));
        return TelemetryResult<void *>::Ok(nullptr);
    }

    TelemetryResult<void *> Router::process_received_queue()
    {
        while (!received_queue_.empty())
        {
            RxQueueItem it = std::move(received_queue_.front());
            received_queue_.pop();
            if (auto r = handle_rx_queue_item(std::move(it)); r.is_err()) return r;
        }
        return TelemetryResult<void *>::Ok(nullptr);
    }

    TelemetryResult<void *> Router::rx_serialized_packet_to_queue(const std::vector<std::uint8_t> & bytes)
    {
        received_queue_.emplace(bytes);
        return TelemetryResult<void *>::Ok(nullptr);
    }

    TelemetryResult<void *> Router::rx_packet_to_queue(TelemetryPacket pkt)
    {
        if (auto v = pkt.Validate(); v.is_err()) return TelemetryResult<void *>::Err(v.unwrap_err());
        received_queue_.emplace(std::move(pkt));
        return TelemetryResult<void *>::Ok(nullptr);
    }

    TelemetryResult<void *> Router::handle_callback_error(const TelemetryPacket & pkt,
                                                          std::optional<DataEndpoint> dest,
                                                          const TelemetryError & e)
    {
        // Compose message once (owned std::string)
        std::string error_msg;
        if (dest.has_value())
        {
            // local handler failed
            error_msg = "Handler for endpoint ";
            error_msg += data_endpoint_as_str(*dest);
            error_msg += " failed on device ";
            error_msg += DEVICE_IDENTIFIER;
            error_msg += ": ";
            error_msg += (e.msg ? e.msg : "error");
        }
        else
        {
            // TX failed
            error_msg = "TX Handler failed on device ";
            error_msg += DEVICE_IDENTIFIER;
            error_msg += ": ";
            error_msg += (e.msg ? e.msg : "error");
        }

        // Gather local endpoints referenced by this packet
        std::vector<DataEndpoint> locals;
        if (pkt.endpoints)
        {
            for (auto ep: *pkt.endpoints)
            {
                if (cfg_.is_local_endpoint(ep)) locals.push_back(ep);
            }
        }
        std::sort(locals.begin(), locals.end());
        locals.erase(std::unique(locals.begin(), locals.end()), locals.end());

        // If a local handler failed, exclude just that one
        if (dest.has_value())
        {
            locals.erase(std::remove(locals.begin(), locals.end(), *dest), locals.end());
        }

        if (!dest.has_value())
        {
            // TX failure
            if (locals.empty())
            {
                fallback_stdout(error_msg);
                return TelemetryResult<void *>::Ok(nullptr);
            }
            // else: broadcast to all locals
        }
        else
        {
            // local handler failure
            if (locals.empty())
            {
                // nothing else local to notify
                return TelemetryResult<void *>::Ok(nullptr);
            }
        }

        // Build zero-padded payload to TelemetryError schema
        const auto & meta = message_meta(DataType::TelemetryError);
        std::vector<std::uint8_t> buf(meta.data_size, 0);
        const auto * msg_bytes = reinterpret_cast<const std::uint8_t *>(error_msg.data());
        const std::size_t copy_n = std::min(buf.size(), error_msg.size());
        if (copy_n > 0) std::memcpy(buf.data(), msg_bytes, copy_n);

        // Target only the chosen local endpoints
        auto payload_arc = std::make_shared<const std::vector<std::uint8_t>>(std::move(buf));
        TelemetryResult<TelemetryPacket> pkt_res =
                TelemetryPacket::New(DataType::TelemetryError,
                                     locals,
                                     std::make_shared<std::string>(DEVICE_IDENTIFIER),
                                     clock_->now_ms(),
                                     std::move(payload_arc));
        if (pkt_res.is_err()) return TelemetryResult<void *>::Err(pkt_res.unwrap_err());

        // Normal path: since endpoints are local, send() will deliver via local handlers only.
        return send(pkt_res.unwrap());
    }

    TelemetryResult<void *> Router::send(const TelemetryPacket & pkt)
    {
        if (auto v = pkt.Validate(); v.is_err()) return TelemetryResult<void *>::Err(v.unwrap_err());

        // Decide whether to transmit remotely (any endpoint that is NOT local)
        bool send_remote = false;
        if (pkt.endpoints)
        {
            for (const auto ep: *pkt.endpoints)
            {
                if (!cfg_.is_local_endpoint(ep))
                {
                    send_remote = true;
                    break;
                }
            }
        }

        // Serialize exactly once (by value, no dangling views).
        const std::vector<std::uint8_t> bytes = serialize_packet(pkt);

        if (send_remote && transmit_)
        {
            bool ok = false;
            TelemetryError last_err;
            for (std::size_t i = 0; i < MAX_NUMBER_OF_RETRYS; ++i)
            {
                auto r = transmit_(bytes);
                if (r.is_ok())
                {
                    ok = true;
                    break;
                }
                last_err = r.unwrap_err();
            }
            if (!ok)
            {
                if (const auto h = handle_callback_error(pkt, std::nullopt, last_err); h.is_err()) return h;
                return TelemetryResult<void *>::Err(TelemetryError::HandlerError("TX failed"));
            }
        }

        // Local dispatch to matching handlers
        if (pkt.endpoints)
        {
            for (DataEndpoint dest: *pkt.endpoints)
            {
                for (const auto & [endpoint, handler]: cfg_.handlers)
                {
                    if (endpoint == dest)
                    {
                        bool ok = false;
                        TelemetryError last_err;
                        for (std::size_t i = 0; i < MAX_NUMBER_OF_RETRYS; ++i)
                        {
                            auto r = handler(pkt);
                            if (r.is_ok())
                            {
                                ok = true;
                                break;
                            }
                            last_err = r.unwrap_err();
                        }
                        if (!ok)
                        {
                            if (const auto cb = handle_callback_error(pkt, dest, last_err); cb.is_err()) return cb;
                            return TelemetryResult<void *>::Err(
                                TelemetryError::HandlerError("local handler failed"));
                        }
                    }
                }
            }
        }

        return TelemetryResult<void *>::Ok(nullptr);
    }

    TelemetryResult<void *> Router::receive_serialized(const std::vector<std::uint8_t> & bytes)
    {
        auto pkt_res = deserialize_packet(bytes);
        if (pkt_res.is_err()) return TelemetryResult<void *>::Err(pkt_res.unwrap_err());
        const auto & pkt = pkt_res.unwrap();
        if (auto v = pkt.Validate(); v.is_err()) return TelemetryResult<void *>::Err(v.unwrap_err());
        return receive(pkt);
    }

    TelemetryResult<void *> Router::receive(const TelemetryPacket & pkt)
    {
        if (auto v = pkt.Validate(); v.is_err()) return TelemetryResult<void *>::Err(v.unwrap_err());

        if (pkt.endpoints)
        {
            for (DataEndpoint dest: *pkt.endpoints)
            {
                for (const auto & [endpoint, handler]: cfg_.handlers)
                {
                    if (endpoint == dest)
                    {
                        bool ok = false;
                        TelemetryError last_err;
                        for (std::size_t i = 0; i < MAX_NUMBER_OF_RETRYS; ++i)
                        {
                            auto r = handler(pkt);
                            if (r.is_ok())
                            {
                                ok = true;
                                break;
                            }
                            last_err = r.unwrap_err();
                        }
                        if (!ok)
                        {
                            if (auto cb = handle_callback_error(pkt, dest, last_err); cb.is_err()) return cb;
                        }
                    }
                }
            }
        }

        return TelemetryResult<void *>::Ok(nullptr);
    }

} // namespace seds
