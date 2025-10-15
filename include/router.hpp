#pragma once
#include "telemetry_packet.hpp"
#include "error.hpp"
#include "result.hpp"
#include <functional>
#include <queue>
#include <vector>

namespace seds
{
    struct Clock
    {
        virtual ~Clock() = default;
        [[nodiscard]] virtual uint64_t now_ms() const noexcept = 0;
    };

    struct EndpointHandler
    {
        DataEndpoint endpoint;
        std::function<Result<void, ErrorInfo>(const TelemetryPacket &)> handler;
    };

    class BoardConfig
    {
    public:
        std::vector<EndpointHandler> handlers;
        std::vector<DataEndpoint>    local_endpoints;

        BoardConfig() = default;

        explicit BoardConfig(std::vector<EndpointHandler> h) noexcept
            : handlers(std::move(h)) {}
    };

    class Router
    {
    public:
        // IMPORTANT: TxFn transmits BYTES, not packets.
        using TxFn = std::function<Result<void, ErrorInfo>(const std::vector<uint8_t> &)>;

    private:
        TxFn                   transmit_;
        BoardConfig            config_;
        std::queue<TelemetryPacket> send_queue;
        std::queue<TelemetryPacket> received_queue;

    public:
        Router(TxFn tx, BoardConfig cfg);

        [[nodiscard]] Result<void, ErrorInfo> log(DataType ty,
                                                  const std::vector<float> & values,
                                                  uint64_t ts) const noexcept;

        Result<void, ErrorInfo> log_queue(DataType ty,
                                          const std::vector<float> & values,
                                          uint64_t ts) noexcept;

        // Logging (bytes)
        [[nodiscard]] Result<void, ErrorInfo> log(DataType ty,
                                                  const std::vector<uint8_t> & data,
                                                  uint64_t ts) const noexcept;

        Result<void, ErrorInfo> log_queue(DataType ty,
                                          const std::vector<uint8_t> & data,
                                          uint64_t ts) noexcept;

        // Queue helpers
        Result<void, ErrorInfo> queue_tx_message(const TelemetryPacket & pkt) noexcept;

        // RX path
        [[nodiscard]] Result<void, ErrorInfo> receive(const TelemetryPacket & pkt) const noexcept;

        [[nodiscard]] Result<void, ErrorInfo> receive_serialized(const std::vector<uint8_t> & bytes) const noexcept;

        Result<void, ErrorInfo> rx_serialized_packet_to_queue(const std::vector<uint8_t> & bytes) noexcept;

        Result<void, ErrorInfo> rx_packet_to_queue(const TelemetryPacket & pkt) noexcept;

        // Pump queues
        Result<void, ErrorInfo> process_send_queue() noexcept;

        Result<void, ErrorInfo> process_received_queue() noexcept;

        Result<void, ErrorInfo> process_all_queues() noexcept;

        // Time-bound pumps
        Result<void, ErrorInfo> process_tx_queue_with_timeout(const Clock * clock, uint32_t timeout_ms) noexcept;

        Result<void, ErrorInfo> process_rx_queue_with_timeout(const Clock * clock, uint32_t timeout_ms) noexcept;

        Result<void, ErrorInfo> process_all_queues_with_timeout(const Clock * clock, uint32_t timeout_ms) noexcept;
    };
} // namespace seds
