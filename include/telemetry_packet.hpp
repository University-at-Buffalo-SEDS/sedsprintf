#pragma once
#include "result.hpp"
#include "error.hpp"
#include "config.hpp"
#include <vector>
#include <memory>
#include <string>

namespace seds {
    class TelemetryPacket {
    public:
        DataType ty{};
        size_t data_size{};
        std::shared_ptr<std::vector<DataEndpoint>> endpoints;
        std::string sender;
        uint64_t timestamp{};
        std::shared_ptr<std::vector<uint8_t>> payload;

        TelemetryPacket() noexcept = default;

        TelemetryPacket(const DataType t,
                        std::vector<DataEndpoint> eps,
                        std::string sender_id,
                        const uint64_t ts,
                        const std::vector<uint8_t>& data)
            : ty(t),
              data_size(data.size()),
              endpoints(std::make_shared<std::vector<DataEndpoint>>(std::move(eps))),
              sender(std::move(sender_id)),
              timestamp(ts),
              payload(std::make_shared<std::vector<uint8_t>>(data)) {}

        static Result<TelemetryPacket, ErrorInfo>
        deserialize(const std::vector<uint8_t>& bytes) noexcept;

        static Result<TelemetryPacket, ErrorInfo>
        from_f32(DataType t,
                 const std::vector<DataEndpoint>& eps,
                 uint64_t ts,
                 const std::vector<float>& vals) noexcept;

        [[nodiscard]] Result<void, ErrorInfo> validate() const noexcept;

        [[nodiscard]] std::string header_string() const;

        [[nodiscard]] std::string to_string() const;

        [[nodiscard]] static size_t expected_size(const std::vector<uint8_t>& bytes) noexcept {
            return bytes.size();
        }

        [[nodiscard]] DataEndpoint primary_endpoint() const noexcept {
            return (endpoints && !endpoints->empty())
                       ? (*endpoints)[0]
                       : DataEndpoint::RADIO;
        }
    };

    inline size_t packet_wire_size(const TelemetryPacket& pkt) noexcept {
        return sizeof(uint32_t) * 2 + sizeof(uint64_t)
             + pkt.payload->size()
             + pkt.endpoints->size() * sizeof(uint32_t);
    }

} // namespace seds
