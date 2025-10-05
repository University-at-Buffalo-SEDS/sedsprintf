#pragma once
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <memory>
#include <cstring>
#include "struct_setup.h"

// Owning byte buffer (shared so multiple packets/views can reference it)
struct serialized_buffer_t
{
    std::shared_ptr<uint8_t[]> data;
    size_t size = 0;
};

struct ByteReader
{
    const uint8_t * base = nullptr;
    std::size_t size = 0;
    std::size_t off = 0;

    [[nodiscard]] std::size_t remaining() const noexcept { return (off <= size) ? (size - off) : 0; }

    bool read_bytes(void * dst, std::size_t n)
    {
        if (n > remaining()) return false;
        std::memcpy(dst, base + off, n);
        off += n;
        return true;
    }

    template<class T>
    bool read(T & out)
    {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        return read_bytes(&out, sizeof(T));
    }

    // view current pointer without advancing (for zero-copy alias)
    [[nodiscard]] const uint8_t * current() const noexcept { return base + off; }

    bool skip(std::size_t n)
    {
        if (n > remaining()) return false;
        off += n;
        return true;
    }
};

// If message_type_t contains pointer fields (like endpoints), DO NOT serialize those pointers.
// Only serialize concrete header fields + payload length + payload.
size_t header_size_bytes();

// Safer: size of (header you serialize) + payload
size_t get_packet_size(const telemetry_packet_t & packet);

// Allocate a shared buffer of the requested size (zero-initialized)
std::shared_ptr<serialized_buffer_t> make_serialized_buffer(size_t size);

// Serialize into a pre-allocated buffer (must be >= get_packet_size()).
SEDSPRINTF_STATUS serialize_packet(const std::shared_ptr<telemetry_packet_t> & packet,
                                   const std::shared_ptr<serialized_buffer_t> & buffer);

// Zero-copy deserialize view: returns a new telemetry_packet_t whose .data points
// into the provided shared buffer (caller keeps the buffer alive by holding it).
std::shared_ptr<telemetry_packet_t> deserialize_packet(const std::shared_ptr<serialized_buffer_t> & serialized);
