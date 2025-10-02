//
// Created by Rylan Meilutis on 10/1/25.
//

#include "serialize.h"
#include <cstring>

size_t get_packet_size(const telemetry_packet_t * packet)
{
    // Bytes needed: message_type.type + message_type.data_size + timestamp + payload
    return sizeof(packet->message_type) +
            sizeof(packet->timestamp) +
            packet->message_type.data_size;
}

serialized_buffer_t create_serialized_buffer(uint8_t buff[], const size_t size)
{
    serialized_buffer_t out = {nullptr, 0};

    // no heap: tell caller how much is needed; they provide storage later
    out.buffer = buff;
    out.size = size;
    return out;
}

SEDSPRINTF_STATUS serialize_packet(const telemetry_packet_t * packet, const serialized_buffer_t * buffer)
{
    if (!packet || !buffer || !buffer->buffer) return SEDSPRINTF_ERROR;

    if (const size_t required_space = get_packet_size(packet); buffer->size < required_space) return SEDSPRINTF_ERROR; // not enough space

    uint8_t * p = buffer->buffer;

    // Header
    memcpy(p, &packet->message_type.type, sizeof(packet->message_type.type));
    p += sizeof(packet->message_type.type);

    memcpy(p, &packet->message_type.data_size, sizeof(packet->message_type.data_size));
    p += sizeof(packet->message_type.data_size);

    memcpy(p, &packet->message_type.endpoints, sizeof(packet->message_type.endpoints));
    p += sizeof(packet->message_type.endpoints);

    memcpy(p, &packet->message_type.num_endpoints, sizeof(packet->message_type.num_endpoints));
    p += sizeof(packet->message_type.num_endpoints);

    memcpy(p, &packet->timestamp, sizeof(packet->timestamp));
    p += sizeof(packet->timestamp);

    // Payload
    if (packet->message_type.data_size > 0 && packet->data)
    {
        memcpy(p, packet->data, packet->message_type.data_size);
    }
    return SEDSPRINTF_OK;
}

telemetry_packet_t deserialize_packet(const serialized_buffer_t * serialized_buffer)
{
    telemetry_packet_t pkt{
        {}, -1, nullptr
    };
    if (!serialized_buffer || !serialized_buffer->buffer) return pkt;

    const uint8_t * p = serialized_buffer->buffer;
    const size_t min_hdr = get_packet_size(&pkt);

    if (serialized_buffer->size < min_hdr) return pkt; // incomplete header

    // Header
    memcpy(&pkt.message_type.type, p, sizeof(pkt.message_type.type));
    p += sizeof(pkt.message_type.type);

    memcpy(&pkt.message_type.data_size, p, sizeof(pkt.message_type.data_size));
    p += sizeof(pkt.message_type.data_size);

    memcpy(&pkt.message_type.endpoints, p, sizeof(pkt.message_type.endpoints));
    p += sizeof(pkt.message_type.endpoints);

    memcpy(&pkt.message_type.num_endpoints, p, sizeof(pkt.message_type.num_endpoints));
    p += sizeof(pkt.message_type.num_endpoints);

    memcpy(&pkt.timestamp, p, sizeof(pkt.timestamp));
    p += sizeof(pkt.timestamp);

    // Bounds check for payload
    const size_t expected = min_hdr + pkt.message_type.data_size;
    if (serialized_buffer->size < expected)
    {
        constexpr telemetry_packet_t empty{};
        return empty;
    }

    // Zero-copy payload view
    pkt.data = (void *) p;

    return pkt;
}
