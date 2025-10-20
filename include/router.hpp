#pragma once

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <functional>
#include <cstring>   // for std::memcpy

#include "config.hpp"
#include "telemetry.hpp"

namespace seds
{
    // -------------------- RxQueueItem --------------------
    using RxQueueItem = std::variant<TelemetryPacket, std::vector<std::uint8_t> >;

    // -------------------- endpoint + board config --------------------
    inline constexpr std::size_t MAX_NUMBER_OF_RETRYS = 3;

    // Local handler bound to a specific endpoint.
    struct EndpointHandler
    {
        DataEndpoint endpoint;
        // Rust: Fn(&TelemetryPacket) -> TelemetryResult<()>
        std::function<TelemetryResult<void *>(const TelemetryPacket &)> handler;
    };

    // Clock trait
    struct Clock
    {
        virtual ~Clock() = default;

        [[nodiscard]] virtual std::uint64_t now_ms() const = 0;
    };

    // Helper to wrap a lambda/std::function<u64()> as Clock
    template<typename F>
    std::unique_ptr<Clock> make_clock(F fn)
    {
        struct Impl : Clock
        {
            explicit Impl(F f) : f_(std::move(f))
            {
            }

            [[nodiscard]] std::uint64_t now_ms() const override { return f_(); }
            F f_;
        };
        return std::unique_ptr<Clock>(new Impl(std::move(fn)));
    }

    // Board configuration: which local endpoints exist and how to deliver to them.
    struct BoardConfig
    {
        std::vector<EndpointHandler> handlers;

        BoardConfig() = default;

        explicit BoardConfig(std::vector<EndpointHandler> h) : handlers(std::move(h))
        {
        }

        [[nodiscard]] bool is_local_endpoint(const DataEndpoint ep) const
        {
            return std::any_of(handlers.begin(), handlers.end(),
                               [&](const EndpointHandler & h) { return h.endpoint == ep; });
        }
    };

    // -------------------- generic little-endian serialization --------------------

    // “Trait” for any type that knows how to write itself as little-endian bytes.
    template<typename T>
    struct LeBytes
    {
        static constexpr std::size_t WIDTH = 0; // must be specialized
        static void write_le(T, std::uint8_t *)
        {
        } // must be specialized
    };

    // Encode a slice of T:LeBytes to a single contiguous LE buffer.
    template<typename T>
    std::vector<std::uint8_t> encode_slice_le(const std::vector<T> & data)
    {
        const std::size_t total = data.size() * LeBytes<T>::WIDTH;
        std::vector<std::uint8_t> buf(total);
        for (std::size_t i = 0; i < data.size(); ++i)
        {
            LeBytes<T>::write_le(data[i], buf.data() + i * LeBytes<T>::WIDTH);
        }
        return buf;
    }

    template<typename T>
    std::vector<std::uint8_t> encode_slice_le(const T * ptr, std::size_t n)
    {
        const std::size_t total = n * LeBytes<T>::WIDTH;
        std::vector<std::uint8_t> buf(total);
        for (std::size_t i = 0; i < n; ++i)
        {
            LeBytes<T>::write_le(ptr[i], buf.data() + i * LeBytes<T>::WIDTH);
        }
        return buf;
    }

    // -------------------- Router --------------------
    class Router
    {
    public:
        using TransmitFn = std::function<TelemetryResult<void *>(const std::vector<std::uint8_t> &)>;
        // Tx: std::function<TelemetryResult<void*>(const std::vector<uint8_t>&)>
        template<typename Tx>
        Router(std::optional<Tx> transmit, BoardConfig cfg, std::unique_ptr<Clock> clock)
            : cfg_(std::move(cfg)), clock_(std::move(clock))
        {
            if (transmit)
            {
                transmit_ = std::function<TelemetryResult<void *>(const std::vector<std::uint8_t> &)>(
                    *transmit
                );
            }
        }

        // Queues mgmt
        TelemetryResult<void *> process_send_queue();

        TelemetryResult<void *> process_all_queues();

        void clear_queues();

        void clear_rx_queue();

        void clear_tx_queue();

        TelemetryResult<void *> process_tx_queue_with_timeout(std::uint32_t timeout_ms);

        TelemetryResult<void *> process_rx_queue_with_timeout(std::uint32_t timeout_ms);

        TelemetryResult<void *> process_all_queues_with_timeout(std::uint32_t timeout_ms);

        TelemetryResult<void *> queue_tx_message(TelemetryPacket pkt);

        TelemetryResult<void *> process_received_queue();

        TelemetryResult<void *> rx_serialized_packet_to_queue(const std::vector<std::uint8_t> & bytes);

        TelemetryResult<void *> rx_packet_to_queue(TelemetryPacket pkt);

        // Core ops
        TelemetryResult<void *> send(const TelemetryPacket & pkt);

        TelemetryResult<void *> receive_serialized(const std::vector<std::uint8_t> & bytes);

        TelemetryResult<void *> receive(const TelemetryPacket & pkt);

        // Generic logging over LeBytes
        template<typename T>
        TelemetryResult<void *> log(DataType ty, const T * data, std::size_t n);

        template<typename T>
        TelemetryResult<void *> log_queue(DataType ty, const T * data, std::size_t n);

        template<typename T>
        TelemetryResult<void *> log(DataType ty, const std::vector<T> & data)
        {
            return log<T>(ty, data.data(), data.size());
        }

        template<typename T>
        TelemetryResult<void *> log_queue(DataType ty, const std::vector<T> & data)
        {
            return log_queue<T>(ty, data.data(), data.size());
        }

        TelemetryResult<void *> log_bytes(DataType ty, const std::vector<std::uint8_t> & bytes)
        {
            return log<std::uint8_t>(ty, bytes);
        }

        TelemetryResult<void *> log_f32(DataType ty, const std::vector<float> & vals)
        {
            return log<float>(ty, vals);
        }

    private:
        TelemetryResult<void *> handle_rx_queue_item(RxQueueItem item);

        TelemetryResult<void *> handle_callback_error(const TelemetryPacket & pkt,
                                                      std::optional<DataEndpoint> dest,
                                                      const TelemetryError & e);

        std::function<TelemetryResult<void *>(const std::vector<std::uint8_t> &)> transmit_;
        BoardConfig cfg_;
        std::queue<RxQueueItem> received_queue_;
        std::queue<TelemetryPacket> transmit_queue_;
        std::unique_ptr<Clock> clock_;
    };

    // ---------- LeBytes specializations (u8/u16/u32/u64, i8/i16/i32/i64, f32/f64) ----------
    template<>
    struct LeBytes<std::uint8_t>
    {
        static constexpr std::size_t WIDTH = 1;
        static void write_le(std::uint8_t v, std::uint8_t * out) { out[0] = v; }
    };

    template<>
    struct LeBytes<std::uint16_t>
    {
        static constexpr std::size_t WIDTH = 2;

        static void write_le(std::uint16_t v, std::uint8_t * out)
        {
            out[0] = static_cast<std::uint8_t>(v & 0xFFu);
            out[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
        }
    };

    template<>
    struct LeBytes<std::uint32_t>
    {
        static constexpr std::size_t WIDTH = 4;

        static void write_le(std::uint32_t v, std::uint8_t * out)
        {
            out[0] = static_cast<std::uint8_t>(v & 0xFFu);
            out[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
            out[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
            out[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
        }
    };

    template<>
    struct LeBytes<std::uint64_t>
    {
        static constexpr std::size_t WIDTH = 8;

        static void write_le(std::uint64_t v, std::uint8_t * out)
        {
            for (int i = 0; i < 8; ++i) out[i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu);
        }
    };

    template<>
    struct LeBytes<std::int8_t>
    {
        static constexpr std::size_t WIDTH = 1;
        static void write_le(std::int8_t v, std::uint8_t * out) { out[0] = static_cast<std::uint8_t>(v); }
    };

    template<>
    struct LeBytes<std::int16_t>
    {
        static constexpr std::size_t WIDTH = 2;

        static void write_le(std::int16_t v, std::uint8_t * out)
        {
            const auto u = static_cast<std::uint16_t>(v);
            LeBytes<std::uint16_t>::write_le(u, out);
        }
    };

    template<>
    struct LeBytes<std::int32_t>
    {
        static constexpr std::size_t WIDTH = 4;

        static void write_le(std::int32_t v, std::uint8_t * out)
        {
            const auto u = static_cast<std::uint32_t>(v);
            LeBytes<std::uint32_t>::write_le(u, out);
        }
    };

    template<>
    struct LeBytes<std::int64_t>
    {
        static constexpr std::size_t WIDTH = 8;

        static void write_le(std::int64_t v, std::uint8_t * out)
        {
            const auto u = static_cast<std::uint64_t>(v);
            LeBytes<std::uint64_t>::write_le(u, out);
        }
    };

    template<>
    struct LeBytes<float>
    {
        static constexpr std::size_t WIDTH = 4;

        static void write_le(float v, std::uint8_t * out)
        {
            static_assert(sizeof(float) == 4, "float must be 32-bit");
            std::uint32_t u;
            std::memcpy(&u, &v, 4);
            LeBytes<std::uint32_t>::write_le(u, out);
        }
    };

    template<>
    struct LeBytes<double>
    {
        static constexpr std::size_t WIDTH = 8;

        static void write_le(double v, std::uint8_t * out)
        {
            static_assert(sizeof(double) == 8, "double must be 64-bit");
            std::uint64_t u;
            std::memcpy(&u, &v, 8);
            LeBytes<std::uint64_t>::write_le(u, out);
        }
    };

    // -------------------- Template definitions (headerized) --------------------
    template<typename T>
    TelemetryResult<void *> Router::log(const DataType ty, const T * data, std::size_t n)
    {
        const auto & meta = message_meta(ty);
        if (const std::size_t got = n * LeBytes<T>::WIDTH; got != meta.data_size)
        {
            return TelemetryResult<void *>::Err(TelemetryError::SizeMismatch(meta.data_size, got));
        }

        // Encode to LE
        std::vector<std::uint8_t> payload_vec = encode_slice_le<T>(data, n);

        auto payload_arc = std::make_shared<const std::vector<std::uint8_t>>(std::move(payload_vec));
        TelemetryResult<TelemetryPacket> pkt_res =
                TelemetryPacket::New(ty,
                                     std::vector(meta.endpoints),
                                     DEVICE_IDENTIFIER,
                                     clock_->now_ms(),
                                     std::move(payload_arc));
        if (pkt_res.is_err()) return TelemetryResult<void *>::Err(pkt_res.unwrap_err());
        return send(pkt_res.unwrap());
    }

    template<typename T>
    TelemetryResult<void *> Router::log_queue(DataType ty, const T * data, std::size_t n)
    {
        const auto & meta = message_meta(ty);
        const std::size_t got = n * LeBytes<T>::WIDTH;
        if (got != meta.data_size)
        {
            return TelemetryResult<void *>::Err(TelemetryError::SizeMismatch(meta.data_size, got));
        }

        // Encode to LE
        std::vector<std::uint8_t> payload_vec = encode_slice_le<T>(data, n);

        auto payload_arc = std::make_shared<const std::vector<std::uint8_t>>(std::move(payload_vec));
        TelemetryResult<TelemetryPacket> pkt_res =
                TelemetryPacket::New(ty,
                                     std::vector(meta.endpoints),
                                     DEVICE_IDENTIFIER,
                                     clock_->now_ms(),
                                     std::move(payload_arc));
        if (pkt_res.is_err()) return TelemetryResult<void *>::Err(pkt_res.unwrap_err());
        return queue_tx_message(pkt_res.unwrap());
    }
} // namespace seds
