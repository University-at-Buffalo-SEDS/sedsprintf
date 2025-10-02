#include "serialize.h"
#include <cstring>
#include <type_traits>
#include <cstdint>
#include <memory>

// ---- Fixed wire sizes (portable) ----
static constexpr size_t kTypeSize = sizeof(uint32_t); // u32
static constexpr size_t kDataSizeSize = sizeof(uint32_t); // u32
static constexpr size_t kTimeSize = sizeof(uint64_t); // u64
static constexpr size_t kNumEndpointsSize = sizeof(uint32_t); // u32
static constexpr size_t kEndpointElemSize = sizeof(uint32_t); // each endpoint serialized as u32

// Fixed portion of the header (everything except the variable-length endpoints array)
size_t header_size_bytes()
{
    return kTypeSize + kDataSizeSize + kTimeSize + kNumEndpointsSize;
}

// Full packet size on the wire: fixed header + endpoints[] + payload
size_t get_packet_size(const telemetry_packet_t & packet)
{
    const size_t fixed = header_size_bytes();
    const size_t endpoints_bytes = packet.message_type.num_endpoints * kEndpointElemSize;
    const size_t payload_bytes = packet.message_type.data_size;
    return fixed + endpoints_bytes + payload_bytes;
}

std::shared_ptr<serialized_buffer_t> make_serialized_buffer(size_t size)
{
    auto out = std::make_shared<serialized_buffer_t>();
    out->data = std::shared_ptr<uint8_t[]>(new uint8_t[size],
                                           std::default_delete<uint8_t[]>());
    out->size = size;
    return out;
}

SEDSPRINTF_STATUS serialize_packet(const std::shared_ptr<telemetry_packet_t> & packet,
                                   const std::shared_ptr<serialized_buffer_t> & buffer)
{
    if (!packet || !buffer || !buffer->data) return SEDSPRINTF_ERROR;

    const size_t need = get_packet_size(*packet);
    if (buffer->size < need) return SEDSPRINTF_ERROR;

    uint8_t * p = buffer->begin();

    // ---- header (fixed part) ----
    const auto type_u32 = static_cast<uint32_t>(packet->message_type.type);
    std::memcpy(p, &type_u32, kTypeSize);
    p += kTypeSize;

    const auto dsz_u32 = static_cast<uint32_t>(packet->message_type.data_size);
    std::memcpy(p, &dsz_u32, kDataSizeSize);
    p += kDataSizeSize;

    const auto ts_u64 = static_cast<uint64_t>(packet->timestamp);
    std::memcpy(p, &ts_u64, kTimeSize);
    p += kTimeSize;

    const auto nep_u32 = static_cast<uint32_t>(packet->message_type.num_endpoints);
    std::memcpy(p, &nep_u32, kNumEndpointsSize);
    p += kNumEndpointsSize;

    // ---- endpoints array (values only; never serialize pointers) ----
    for (int i = 0; i < packet->message_type.num_endpoints; ++i)
    {
        const auto ep_u32 = static_cast<uint32_t>(packet->message_type.endpoints[i]);
        std::memcpy(p, &ep_u32, kEndpointElemSize);
        p += kEndpointElemSize;
    }

    // ---- payload ----
    if (packet->message_type.data_size > 0)
    {
        if (!packet->data) return SEDSPRINTF_ERROR;
        std::memcpy(p, packet->data.get(), packet->message_type.data_size);
        // p += data_size; // not needed after last write
    }

    return SEDSPRINTF_OK;
}

std::shared_ptr<telemetry_packet_t>
deserialize_packet(const std::shared_ptr<serialized_buffer_t> & serialized)
{
    if (!serialized || !serialized->data) return {};

    const uint8_t * p = serialized->cbegin();
    const size_t sz = serialized->size;

    if (sz < header_size_bytes()) return {};

    uint32_t type_u32 = 0;
    uint32_t dsz_u32 = 0;
    uint64_t ts_u64 = 0;
    uint32_t nep_u32 = 0;

    // ---- read fixed header ----
    std::memcpy(&type_u32, p, kTypeSize);
    p += kTypeSize;
    std::memcpy(&dsz_u32, p, kDataSizeSize);
    p += kDataSizeSize;
    std::memcpy(&ts_u64, p, kTimeSize);
    p += kTimeSize;
    std::memcpy(&nep_u32, p, kNumEndpointsSize);
    p += kNumEndpointsSize;

    // Bounds / sanity
    const size_t endpoints_bytes = static_cast<size_t>(nep_u32) * kEndpointElemSize;
    const size_t total_need = header_size_bytes() + endpoints_bytes + static_cast<size_t>(dsz_u32);
    if (sz < total_need) return {};

    // ---- reconstruct endpoints managed array ----
    std::unique_ptr<data_endpoint_t[]> eps_up;
    if (nep_u32 > 0)
    {
        eps_up = std::make_unique<data_endpoint_t[]>(nep_u32);
        for (size_t i = 0; i < static_cast<size_t>(nep_u32); ++i)
        {
            uint32_t ep_u32 = 0;
            std::memcpy(&ep_u32, p, kEndpointElemSize);
            p += kEndpointElemSize;
            eps_up[i] = static_cast<data_endpoint_t>(ep_u32);
        }
    }
    else
    {
        // no endpoints; p stays where it is (immediately before payload)
    }

    // alias payload into same allocation (zero-copy)
    const uint8_t * payload = p; // p now points at payload start

    // ---- build packet ----
    auto pkt = std::make_shared<telemetry_packet_t>();
    pkt->message_type.type = static_cast<data_type_t>(type_u32);
    pkt->message_type.data_size = static_cast<size_t>(dsz_u32);
    pkt->message_type.num_endpoints = static_cast<size_t>(nep_u32);

    if (nep_u32 > 0)
    {
        // move unique_ptr -> shared_ptr<const T[]> with array deleter
        std::shared_ptr<const data_endpoint_t[]> eps_sp(
            eps_up.release(),
            std::default_delete<const data_endpoint_t[]>()
        );
        pkt->message_type.endpoints = std::move(eps_sp);
    }
    else
    {
        pkt->message_type.endpoints.reset(); // nullptr
    }

    pkt->timestamp = static_cast<std::time_t>(ts_u64);

    if (pkt->message_type.data_size > 0)
    {
        // alias payload's lifetime to the underlying serialized buffer allocation
        pkt->data = std::shared_ptr<const void>(
            serialized->data, static_cast<const void *>(payload));
    }
    else
    {
        pkt->data.reset();
    }

    return pkt;
}
