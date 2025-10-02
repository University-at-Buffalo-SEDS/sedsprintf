#include "telemetry_router.hpp"
#include "serialize.h"

std::string sedsprintf::telemetry_packet_metadata_to_string(const telemetry_packet_t * packet)
{
    std::string type = message_names[packet->message_type.type];
    std::string dest_endpoints;
    for (size_t i = 0; i < packet->message_type.num_endpoints; i++)
    {
        dest_endpoints += (i == 0 ? "" : ", ") + data_endpoint_names[packet->message_type.endpoints[i]];
    }
    return "Type: " + type + ", Size: " + std::to_string(packet->message_type.data_size) +
           ", Endpoints: [" + dest_endpoints + "], Timestamp: " + std::to_string(packet->timestamp);
}


std::string sedsprintf::packet_to_hex_string(const telemetry_packet_t * packet,
                                             const void * data,
                                             size_t size_bytes)
{
    if (!packet || !data) return "ERROR: null packet or data";

    if (size_bytes == 0) size_bytes = packet->message_type.data_size;

    std::ostringstream os;
    os << telemetry_packet_metadata_to_string(packet) << ", Payload (hex): ";

    const auto * p = static_cast<const uint8_t *>(data);
    os << std::hex << std::setfill('0');
    for (size_t i = 0; i < size_bytes; ++i)
    {
        os << "0x" << std::setw(2) << static_cast<unsigned>(p[i]);
        if (i + 1 < size_bytes) os << ' ';
    }
    return os.str();
}

SEDSPRINTF_STATUS sedsprintf::copy_telemetry_packet(telemetry_packet_t * dest, const telemetry_packet_t * src)
{
    //copy the packet without using the heap
    if (!dest || !src) return SEDSPRINTF_ERROR;
    if (dest == src) return SEDSPRINTF_OK; //same packet,
    dest->message_type = src->message_type;
    dest->timestamp = src->timestamp;
    if (src->message_type.data_size > 0 && src->data)
    {
        //check buffer size?
        if (dest->data == nullptr || dest->message_type.data_size < src->message_type.data_size)
        {
            return SEDSPRINTF_ERROR;
        }
        memcpy(dest->data, src->data, src->message_type.data_size);
    }
    return SEDSPRINTF_OK;
}

sedsprintf::sedsprintf(transmit_helper_t transmit_helpers[], size_t num_helpers, board_config_t config)
{
    this->cfg.transmit_helpers = transmit_helpers;
    this->cfg.num_helpers = num_helpers;
    this->cfg.board_config = config;
}

SEDSPRINTF_STATUS sedsprintf::log(const message_type_t & type, void * data) const
{
    telemetry_packet_t packet{};
    packet.message_type = type;
    packet.data = data;
    packet.timestamp = std::time(nullptr);
    // For simplicity, we send to all endpoints defined in board_config
    const SEDSPRINTF_STATUS status = transmit(&packet);
    return status;
}

SEDSPRINTF_STATUS sedsprintf::transmit(telemetry_packet_t * packet) const
{
    const size_t size = get_packet_size(packet);
    uint8_t buff[size];
    serialized_buffer_t serialized = create_serialized_buffer(buff, size);
    serialize_packet(packet, &serialized);
    int transmitted = 0;
    if (packet->message_type.num_endpoints == 0)
        return SEDSPRINTF_OK;
    for (size_t i = 0; i < packet->message_type.num_endpoints; i++)
    {
        //transmit loop
        if (!transmitted)
        {
            //already transmitted, skip
            for (size_t j = 0; j < cfg.num_helpers; j++)
            {
                //transmit all remote endpoints
                if (cfg.transmit_helpers[j] != nullptr &&
                    cfg.board_config.local_data_endpoints[j].local_endpoint != packet->message_type.endpoints[i])
                {
                    if (cfg.transmit_helpers[j](&serialized) != SEDSPRINTF_OK)
                    {
                        return SEDSPRINTF_ERROR;
                    }
                    transmitted = 1;
                }
            }
        }
        //local save loop
        //handle all local endpoints
        for (size_t j = 0; j < cfg.board_config.num_local_endpoints; j++)
        {
            if (cfg.board_config.local_data_endpoints[j].receive_handler != nullptr &&
                cfg.board_config.local_data_endpoints[j].local_endpoint == packet->message_type.endpoints[i])
            {
                if (cfg.board_config.local_data_endpoints[j].receive_handler(packet) != SEDSPRINTF_OK)
                {
                    return SEDSPRINTF_ERROR;
                }
            }
        }
    }
    return SEDSPRINTF_OK;
}

SEDSPRINTF_STATUS sedsprintf::receive(const serialized_buffer_t * serialized_buffer) const
{
    telemetry_packet_t packet = deserialize_packet(serialized_buffer);
    if (packet.message_type.num_endpoints == 0)
        return SEDSPRINTF_OK;
    for (size_t i = 0; i < packet.message_type.num_endpoints; i++)
    {
        for (size_t j = 0; j < cfg.board_config.num_local_endpoints; j++)
        {
            if (cfg.board_config.local_data_endpoints[j].receive_handler != nullptr &&
                cfg.board_config.local_data_endpoints[j].local_endpoint == packet.message_type.endpoints[i])
            {
                if (cfg.board_config.local_data_endpoints[j].receive_handler(&packet) != SEDSPRINTF_OK)
                {
                    return SEDSPRINTF_ERROR;
                }
            }
        }
    }
    return SEDSPRINTF_OK;
}
