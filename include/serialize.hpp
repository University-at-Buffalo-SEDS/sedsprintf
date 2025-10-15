#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "config.hpp"
#include "telemetry.hpp"

namespace seds
{
    // ---- sizes (mirror Rust consts) ----
    inline constexpr std::size_t TYPE_SIZE = sizeof(std::uint32_t);
    inline constexpr std::size_t DATA_SIZE_SIZE = sizeof(std::uint32_t);
    inline constexpr std::size_t TIME_SIZE = sizeof(std::uint64_t);
    inline constexpr std::size_t NUM_ENDPOINTS_SIZE = sizeof(std::uint32_t);
    inline constexpr std::size_t ENDPOINT_ELEM_SIZE = sizeof(std::uint32_t);
    inline constexpr std::size_t SENDER_LEN_SIZE = sizeof(std::uint32_t);

    inline constexpr std::size_t header_size_bytes()
    {
        return TYPE_SIZE + DATA_SIZE_SIZE + SENDER_LEN_SIZE + TIME_SIZE + NUM_ENDPOINTS_SIZE;
    }

    inline std::size_t packet_wire_size(const TelemetryPacket & pkt)
    {
        const std::size_t nep = pkt.endpoints ? pkt.endpoints->size() : 0;
        return header_size_bytes() + ENDPOINT_ELEM_SIZE * nep + pkt.data_size;
    }

    // ---- ByteReader (small helper like Rust) ----
    class ByteReader
    {
    public:
        ByteReader(const std::uint8_t * buf, std::size_t len) : buf_(buf), len_(len), off_(0)
        {
        }

        explicit ByteReader(const std::vector<std::uint8_t> & v)
            : buf_(v.data()), len_(v.size()), off_(0)
        {
        }

        [[nodiscard]] std::size_t remaining() const
        {
            return (off_ <= len_) ? (len_ - off_) : 0;
        }

        // Returns pointer to n bytes (no copy); sets *err on failure.
        std::optional<const std::uint8_t *> read_bytes(std::size_t n, const char ** err)
        {
            if (remaining() < n)
            {
                if (err) *err = "short read";
                return std::nullopt;
            }
            const std::uint8_t * p = buf_ + off_;
            off_ += n;
            return p;
        }

        std::optional<std::uint32_t> read_u32(const char ** err)
        {
            auto p = read_bytes(4, err);
            if (!p) return std::nullopt;
            const std::uint8_t * b = *p;
            std::uint32_t v =
                    (static_cast<std::uint32_t>(b[0])) |
                    (static_cast<std::uint32_t>(b[1]) << 8) |
                    (static_cast<std::uint32_t>(b[2]) << 16) |
                    (static_cast<std::uint32_t>(b[3]) << 24);
            return v;
        }

        std::optional<std::uint64_t> read_u64(const char ** err)
        {
            auto p = read_bytes(8, err);
            if (!p) return std::nullopt;
            const std::uint8_t * b = *p;
            std::uint64_t v =
                    (static_cast<std::uint64_t>(b[0])) |
                    (static_cast<std::uint64_t>(b[1]) << 8) |
                    (static_cast<std::uint64_t>(b[2]) << 16) |
                    (static_cast<std::uint64_t>(b[3]) << 24) |
                    (static_cast<std::uint64_t>(b[4]) << 32) |
                    (static_cast<std::uint64_t>(b[5]) << 40) |
                    (static_cast<std::uint64_t>(b[6]) << 48) |
                    (static_cast<std::uint64_t>(b[7]) << 56);
            return v;
        }

    private:
        const std::uint8_t * buf_{nullptr};
        std::size_t len_{0};
        std::size_t off_{0};
    };

    // ---- Strong, simple enum converters (no template trait in headers) ----
    inline std::optional<DataType> try_enum_from_u32(std::uint32_t x)
    {
        if (x > MAX_VALUE_DATA_TYPE) return std::nullopt;
        return static_cast<DataType>(x);
    }

    inline std::optional<DataEndpoint> try_enum_from_u32_endpoint(std::uint32_t x)
    {
        if (x > MAX_VALUE_DATA_ENDPOINT) return std::nullopt;
        return static_cast<DataEndpoint>(x);
    }

    // ---- API parity with Rust ----
    std::vector<std::uint8_t> serialize_packet(const TelemetryPacket & pkt);

    // On success, returns TelemetryPacket; on error, TelemetryError::Deserialize(...) or others.
    TelemetryResult<TelemetryPacket> deserialize_packet(const std::vector<std::uint8_t> & buf);
} // namespace seds
