#pragma once
#include "result.hpp"
#include "error.hpp"
#include "telemetry_packet.hpp"
#include <vector>
#include <cstring>  // <-- add this

namespace seds {


    /**
     * @brief Serializes and deserializes telemetry packets to/from bytes.
     */
    class Serializer {

    public:
        static Result<std::vector<uint8_t>, ErrorInfo>
        serialize(const TelemetryPacket& pkt) noexcept;

        static Result<TelemetryPacket, ErrorInfo>
        deserialize(const std::vector<uint8_t>& bytes) noexcept;
    };

    /**
     * @brief Helper for deserialization: read little-endian values from a buffer.
     */
    class ByteReader {
        const uint8_t* data;
        size_t len;
        size_t pos;

    public:
        explicit ByteReader(const std::vector<uint8_t>& b) noexcept
            : data(b.data()), len(b.size()), pos(0) {}
        ByteReader(const uint8_t* ptr, size_t size) noexcept
            : data(ptr), len(size), pos(0) {}

        template <typename T>
        Result<T, ErrorInfo> read_le() noexcept {
            if (pos + sizeof(T) > len) {
                return Result<T, ErrorInfo>::err(
                    ErrorInfo(TelemetryError::SizeMismatchError, "unexpected EOF"));
            }
            T val{};
            std::memcpy(&val, data + pos, sizeof(T));
            pos += sizeof(T);
            return Result<T, ErrorInfo>::ok(val);
        }

        Result<std::vector<uint8_t>, ErrorInfo> read_bytes(size_t count) noexcept {
            if (pos + count > len) {
                return Result<std::vector<uint8_t>, ErrorInfo>::err(
                    ErrorInfo(TelemetryError::SizeMismatchError, "unexpected EOF"));
            }
            std::vector<uint8_t> out(data + pos, data + pos + count);
            pos += count;
            return Result<std::vector<uint8_t>, ErrorInfo>::ok(std::move(out));
        }

        [[nodiscard]] bool eof() const noexcept { return pos >= len; }
    };

} // namespace seds
