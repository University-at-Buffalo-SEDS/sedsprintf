#include "serialize.h"
#include <cstring>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <memory>
#include <vector>
#include <type_traits>

// Dear programmer:
// When I wrote this code, only god and I knew how it worked.
// Now, only god knows it!
// Therefore, if you are trying to optimize
// this routine, and it fails (it most surely will),
// please increase this counter as a warning for the next person:
// total hours_wasted_here = 24


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

std::shared_ptr<serialized_buffer_t> make_serialized_buffer(const size_t size)
{
    auto out = std::make_shared<serialized_buffer_t>();
    out->data = std::shared_ptr<uint8_t[]>(new uint8_t[size],
                                           std::default_delete<uint8_t[]>());
    out->size = size;
    return out;
}


template<class T>
static void append_trivial(std::vector<std::uint8_t> & out, const T & v)
{
    static_assert(std::is_trivially_copyable_v<T>, "serialization requires trivially copyable");
    auto p = reinterpret_cast<const std::uint8_t *>(&v);
    out.insert(out.end(), p, p + sizeof(T));
}

static void append_bytes(std::vector<std::uint8_t> & out, const void * src, std::size_t n)
{
    const auto * p = static_cast<const std::uint8_t *>(src);
    out.insert(out.end(), p, p + n);
}

SEDSPRINTF_STATUS serialize_packet(const std::shared_ptr<telemetry_packet_t> & packet,
                                   const std::shared_ptr<serialized_buffer_t> & buffer)
{
    if (!packet || !buffer || !buffer->data) return SEDSPRINTF_ERROR;

    const std::size_t size = get_packet_size(*packet);

    // Build into a temporary vector first (no pointer math)
    std::vector<std::uint8_t> bytes;
    bytes.reserve(size); // avoid reallocations

    // ---- header (fixed part) ----
    append_trivial<std::uint32_t>(bytes, static_cast<std::uint32_t>(packet->message_type.type));
    append_trivial<std::uint32_t>(bytes, static_cast<std::uint32_t>(packet->message_type.data_size));
    append_trivial<std::uint64_t>(bytes, static_cast<std::uint64_t>(packet->timestamp));
    append_trivial<std::uint32_t>(bytes, static_cast<std::uint32_t>(packet->message_type.num_endpoints));

    // ---- endpoints array (values only; never serialize pointers) ----
    for (int i = 0; i < packet->message_type.num_endpoints; ++i)
    {
        append_trivial<std::uint32_t>(bytes, static_cast<std::uint32_t>(packet->message_type.endpoints[i]));
    }

    // ---- payload ----
    if (packet->message_type.data_size > 0)
    {
        if (!packet->data) return SEDSPRINTF_ERROR;
        append_bytes(bytes, packet->data.get(), packet->message_type.data_size);
    }

    // Sanity check: computed size should match expected
    if (bytes.size() != size)
    {
        return SEDSPRINTF_ERROR;
    }

    // Copy once into the caller-provided buffer
    if (buffer->size < bytes.size()) return SEDSPRINTF_ERROR;
    std::memcpy(buffer->data.get(), bytes.data(), bytes.size());

    return SEDSPRINTF_OK;
}


std::shared_ptr<telemetry_packet_t>
deserialize_packet(const std::shared_ptr<serialized_buffer_t> & serialized)
{
    if (!serialized || !serialized->data) return {};

    ByteReader r{serialized->data.get(), serialized->size, 0};

    if (r.size < header_size_bytes()) return {};

    // ---- read fixed header ----
    uint32_t type_u32 = 0;
    uint32_t dsz_u32 = 0;
    uint64_t ts_u64 = 0;
    uint32_t nep_u32 = 0;

    if (!r.read(type_u32)) return {};
    if (!r.read(dsz_u32)) return {};
    if (!r.read(ts_u64)) return {};
    if (!r.read(nep_u32)) return {};

    // ---- bounds check for endpoints + payload ----
    const std::size_t endpoints_bytes = static_cast<std::size_t>(nep_u32) * kEndpointElemSize;
    const std::size_t total_need =
            header_size_bytes() + endpoints_bytes + static_cast<std::size_t>(dsz_u32);
    if (r.size < total_need) return {};

    // ---- endpoints ----
    std::shared_ptr<const data_endpoint_t[]> eps_sp;
    if (nep_u32 > 0)
    {
        auto eps_up = std::make_unique<data_endpoint_t[]>(nep_u32);
        for (std::size_t i = 0; i < static_cast<std::size_t>(nep_u32); ++i)
        {
            uint32_t ep_u32 = 0;
            if (!r.read(ep_u32)) return {};
            eps_up[i] = static_cast<data_endpoint_t>(ep_u32);
        }
        // move unique_ptr -> shared_ptr with array deleter
        eps_sp = std::shared_ptr<const data_endpoint_t[]>(
            eps_up.release(), std::default_delete<const data_endpoint_t[]>());
    }

    // ---- payload (zero-copy alias into serialized buffer allocation) ----
    const uint8_t * payload = r.current();
    if (!r.skip(dsz_u32)) return {};

    // ---- build packet ----
    auto pkt = std::make_shared<telemetry_packet_t>();
    pkt->message_type.type = static_cast<data_type_t>(type_u32);
    pkt->message_type.data_size = static_cast<std::size_t>(dsz_u32);
    pkt->message_type.num_endpoints = static_cast<std::size_t>(nep_u32);
    pkt->message_type.endpoints = std::move(eps_sp);
    pkt->timestamp = static_cast<std::time_t>(ts_u64);

    if (pkt->message_type.data_size > 0)
    {
        // alias lifetime to the serialized buffer’s allocation
        pkt->data = std::shared_ptr<const void>(
            serialized->data, static_cast<const void *>(payload));
    }
    else
    {
        pkt->data.reset();
    }

    return pkt;
}
