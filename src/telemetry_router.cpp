#include "telemetry_router.hpp"
#include "serialize.h"
#include <iomanip>
#include <sstream>
// ReSharper disable once CppUnusedIncludeDirective
#include <vector>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstring>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
// ReSharper disable once CppUnusedIncludeDirective
#include <ctime>
#include <memory>

namespace
{
    template<typename Enum, typename Limit>
    bool index_in_range(Enum e, Limit limit)
    {
        const auto idx = static_cast<size_t>(e);
        return idx < static_cast<size_t>(limit);
    }
} // namespace

// ---------- Validation ----------
SEDSPRINTF_STATUS sedsprintf::validate_telemetry_packet(const ConstPacketPtr & packet)
{
    if (!packet) return SEDSPRINTF_ERROR;

    if (!index_in_range(packet->message_type.type, NUM_DATA_TYPES))
        return SEDSPRINTF_ERROR;

    if (const auto t = static_cast<size_t>(packet->message_type.type);
        packet->message_type.data_size != message_size[t])
        return SEDSPRINTF_ERROR;

    if (packet->message_type.num_endpoints == 0 || !packet->message_type.endpoints)
        return SEDSPRINTF_ERROR;

    if (packet->timestamp == static_cast<decltype(packet->timestamp)>(-1))
        return SEDSPRINTF_ERROR;

    for (int i = 0; i < packet->message_type.num_endpoints; ++i)
    {
        if (!index_in_range(packet->message_type.endpoints[i], NUM_DATA_ENDPOINTS))
            return SEDSPRINTF_ERROR;
    }

    if (packet->message_type.data_size > 0 && !packet->data)
        return SEDSPRINTF_ERROR;

    return SEDSPRINTF_OK;
}

// ---------- Metadata ----------
std::string sedsprintf::telemetry_packet_metadata_to_string(const ConstPacketPtr & packet)
{
    const auto t = static_cast<size_t>(packet->message_type.type);
    const std::string type = message_names[t];

    std::string dest_endpoints;
    for (int i = 0; i < packet->message_type.num_endpoints; ++i)
    {
        const auto ep = static_cast<size_t>(packet->message_type.endpoints[i]);
        if (i) dest_endpoints += ", ";
        dest_endpoints += data_endpoint_names[ep];
    }

    return "Type: " + type +
           ", Size: " + std::to_string(packet->message_type.data_size) +
           ", Endpoints: [" + dest_endpoints + "]" +
           ", Timestamp: " + std::to_string(packet->timestamp);
}

// ---------- Hex dump ----------
std::string sedsprintf::packet_to_hex_string(const ConstPacketPtr & packet,
                                             const std::shared_ptr<const void> & data,
                                             size_t size_bytes)
{
    if (!packet || !data) return "ERROR: null packet or data";

    if (size_bytes == 0) size_bytes = packet->message_type.data_size;

    std::ostringstream os;
    os << telemetry_packet_metadata_to_string(packet) << ", Payload (hex): ";

    const auto * p = static_cast<const uint8_t *>(data.get());
    os << std::hex << std::setfill('0');
    for (size_t i = 0; i < size_bytes; ++i)
    {
        os << "0x" << std::setw(2) << static_cast<unsigned>(p[i]);
        if (i + 1 < size_bytes) os << ' ';
    }
    return os.str();
}

// ---------- Copy (share ownership of payload/endpoints) ----------
SEDSPRINTF_STATUS sedsprintf::copy_telemetry_packet(const PacketPtr & dest,
                                                    const ConstPacketPtr & src)
{
    if (!dest || !src) return SEDSPRINTF_ERROR;
    if (dest.get() == src.get()) return SEDSPRINTF_OK;

    dest->message_type = src->message_type; // endpoints pointer + count copied (shared)
    dest->timestamp = src->timestamp;
    dest->data = src->data; // share payload
    return SEDSPRINTF_OK;
}

// ---------- Ctor ----------
sedsprintf::sedsprintf(const transmit_helper_t transmit_helper, const board_config_t & config)
{
    this->cfg.transmit_helper = transmit_helper;
    this->cfg.board_config = config;
}



// ---------- Log (build packet with managed payload) ----------
SEDSPRINTF_STATUS sedsprintf::log_handler(const message_type_t & type, std::shared_ptr<const void> data) const
{
    const auto packet = std::make_shared<telemetry_packet_t>();
    packet->message_type = type;
    packet->data = std::move(data);
    packet->timestamp = std::time(nullptr);

    return transmit(packet);
}


// ---------- Transmit ----------
SEDSPRINTF_STATUS sedsprintf::transmit(const ConstPacketPtr & packet) const
{
    if (!packet) return SEDSPRINTF_ERROR;

    // Serialize into a managed buffer
    const size_t size = get_packet_size(*packet);
    auto sbuf = make_serialized_buffer(size);
    auto mpacket = std::const_pointer_cast<telemetry_packet_t>(packet);
    if (serialize_packet(mpacket, sbuf) != SEDSPRINTF_OK)
        return SEDSPRINTF_ERROR;

    if (packet->message_type.num_endpoints == 0)
        return SEDSPRINTF_OK;

    bool transmitted = false;

    for (int i = 0; i < packet->message_type.num_endpoints; ++i)
    {
        const data_endpoint_t target = packet->message_type.endpoints[i];

        // Remote transmit: do once if helper exists and there exists any non-target local endpoint.
        if (!transmitted && cfg.transmit_helper)
        {
            for (int j = 0; j < cfg.board_config.num_local_endpoints; ++j)
            {
                if (cfg.board_config.local_data_endpoints[j].local_endpoint != target)
                {
                    if (cfg.transmit_helper(sbuf) != SEDSPRINTF_OK)
                        return SEDSPRINTF_ERROR;
                    transmitted = true;
                    break;
                }
            }
        }

        // Local dispatch to matching endpoints
        for (int j = 0; j < cfg.board_config.num_local_endpoints; ++j)
        {
            if (const auto & [local_endpoint, receive_handler] = cfg.board_config.local_data_endpoints[j];
                receive_handler && local_endpoint == target)
            {
                // Handlers accept shared_ptr<telemetry_packet_t>
                auto mutable_packet = std::const_pointer_cast<telemetry_packet_t>(packet);
                if (receive_handler(mutable_packet) != SEDSPRINTF_OK)
                    return SEDSPRINTF_ERROR;
            }
        }
    }

    return SEDSPRINTF_OK;
}

// ---------- Receive ----------
SEDSPRINTF_STATUS sedsprintf::receive(const std::shared_ptr<serialized_buffer_t> & serialized_buffer) const
{
    if (!serialized_buffer) return SEDSPRINTF_ERROR;

    auto packet = deserialize_packet(serialized_buffer);
    if (!packet) return SEDSPRINTF_ERROR;

    if (packet->message_type.num_endpoints == 0)
        return SEDSPRINTF_OK;

    for (int i = 0; i < packet->message_type.num_endpoints; ++i)
    {
        const data_endpoint_t target = packet->message_type.endpoints[i];

        for (int j = 0; j < cfg.board_config.num_local_endpoints; ++j)
        {
            if (const auto & [local_endpoint, receive_handler] = cfg.board_config.local_data_endpoints[j];
                receive_handler && local_endpoint == target)
            {
                if (receive_handler(packet) != SEDSPRINTF_OK)
                    return SEDSPRINTF_ERROR;
            }
        }
    }

    return SEDSPRINTF_OK;
}
