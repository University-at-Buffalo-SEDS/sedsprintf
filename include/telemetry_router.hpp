#ifndef SEDSPRINTF_LIBRARY_H
#define SEDSPRINTF_LIBRARY_H

#include "serialize.h"
#include "struct_setup.h"

#include <array>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstring>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

// ReSharper disable once CppUnusedIncludeDirective
#include <vector>
#define MAX_PRECISION 12

// Transport helper: send a fully serialized buffer
using transmit_helper_t = SEDSPRINTF_STATUS (*)(const std::shared_ptr<serialized_buffer_t> &);

// Configuration bundle
struct SEDSPRINTF_CONFIG
{
    transmit_helper_t transmit_helper{};
    board_config_t board_config;
};


enum class PayloadFormat
{
    Auto, // pick based on config tables
    Float32,
    Float64,
    U8, U16, U32, U64,
    Hex
};

enum class ElementType
{
    Auto, // infer from message_size/message_elements
    F32, F64,
    U8, U16, U32, U64
};

// Smart-pointer aliases
using PacketPtr = std::shared_ptr<telemetry_packet_t>;
using ConstPacketPtr = std::shared_ptr<const telemetry_packet_t>;

class sedsprintf
{
public:
    explicit sedsprintf(transmit_helper_t transmit_helper, const board_config_t & config);

    static SEDSPRINTF_STATUS validate_telemetry_packet(const ConstPacketPtr & packet);

    static std::string telemetry_packet_metadata_to_string(const ConstPacketPtr & packet);

    // Logging API:
    // Pass the type and a managed payload buffer (must be at least type.data_size bytes)
    // If you still have raw memory sometimes, see helper wrap_raw() below.

    // Helper for raw pointers (copies into managed block)
    template<class T, std::size_t N>
    SEDSPRINTF_STATUS log(const message_type_t & type, const T (& arr)[N]) const
    {
        static_assert(std::is_trivially_copyable_v<T>, "payload must be trivially copyable");
        const std::size_t need = sizeof(T) * N;
        if (need != type.data_size) return SEDSPRINTF_ERROR;

        const auto block = std::shared_ptr<uint8_t[]>(new uint8_t[need], std::default_delete<uint8_t[]>());
        std::memcpy(block.get(), arr, need);
        std::shared_ptr<const void> payload(block, block.get()); // aliasing, owns same block
        return log_handler(type, payload);
    }

    template<class T>
    SEDSPRINTF_STATUS log_object(const message_type_t & type, const T & obj) const
    {
        static_assert(std::is_trivially_copyable_v<T>, "payload must be trivially copyable");
        const std::size_t need = sizeof(T);
        if (need != type.data_size) return SEDSPRINTF_ERROR;

        auto block = std::shared_ptr<uint8_t[]>(new uint8_t[need], std::default_delete<uint8_t[]>());
        std::memcpy(block.get(), &obj, need);
        const std::shared_ptr<const void> payload(block, block.get());
        return log_handler(type, payload);
    }

    template<class T>
    SEDSPRINTF_STATUS log(const message_type_t & type, const std::vector<T> & v) const
    {
        static_assert(std::is_trivially_copyable_v<T>, "payload must be trivially copyable");
        const std::size_t need = sizeof(T) * v.size();
        if (need != type.data_size) return SEDSPRINTF_ERROR;

        auto block = std::shared_ptr<uint8_t[]>(new uint8_t[need], std::default_delete<uint8_t[]>());
        if (need) std::memcpy(block.get(), v.data(), need);
        const std::shared_ptr<const void> payload(block, block.get());
        return log_handler(type, payload);
    }

    template<class T, std::size_t N>
    SEDSPRINTF_STATUS log(const message_type_t & type, const std::array<T, N> & a) const
    {
        return log(type, std::vector<T>(a.begin(), a.end())); // or memcpy from a.data()
    }

    static SEDSPRINTF_STATUS copy_telemetry_packet(const PacketPtr & dest,
                                                   const ConstPacketPtr & src);

    static std::string packet_to_hex_string(const ConstPacketPtr & packet,
                                            const std::shared_ptr<const void> & data,
                                            size_t size_bytes);

    // ---------- Data extraction ----------

    // Infer element type/size from packet + requested hint
    static std::pair<ElementType, size_t>
    infer_element_type(const ConstPacketPtr & packet, const ElementType requested)
    {
        if (requested != ElementType::Auto)
        {
            switch (requested)
            {
                case ElementType::F32: return {ElementType::F32, 4};
                case ElementType::F64: return {ElementType::F64, 8};
                case ElementType::U8: return {ElementType::U8, 1};
                case ElementType::U16: return {ElementType::U16, 2};
                case ElementType::U32: return {ElementType::U32, 4};
                case ElementType::U64: return {ElementType::U64, 8};
                // ReSharper disable once CppDFAUnreachableCode
                case ElementType::Auto: break; // handled below
            }
        }

        // Auto: infer element size from your config
        const auto t = static_cast<size_t>(packet->message_type.type);
        const size_t elems = message_elements[t] ? message_elements[t] : 1;

        switch (message_size[t] / elems)
        {
            case 1: return {ElementType::U8, 1};
            case 2: return {ElementType::U16, 2};
            case 4:
            {
                // heuristic: most of your messages are float32 when elem=4
                // If you prefer unsigned view, change to U32
                return {ElementType::F32, 4};
            }
            case 8: return {ElementType::F64, 8};
            default: // unknown granularities fall back to bytes
                return {ElementType::U8, 1};
        }
    }

    // ---- BYTES: always safe copy of the whole payload into a vector<uint8_t> ----
    static SEDSPRINTF_STATUS get_data_bytes(const ConstPacketPtr & packet,
                                            std::vector<std::uint8_t> & out)
    {
        if (!packet || !packet->data) return SEDSPRINTF_ERROR;
        const size_t sz = packet->message_type.data_size;
        out.resize(sz);
        if (sz) std::memcpy(out.data(), packet->data.get(), sz);
        return SEDSPRINTF_OK;
    }

    // ---- TYPED: pointer + count ----
    static SEDSPRINTF_STATUS get_data_typed(const ConstPacketPtr & packet,
                                            ElementType type,
                                            void * out,
                                            size_t count)
    {
        if (!packet || !packet->data || !out) return SEDSPRINTF_ERROR;

        auto [chosen, elem_size] = infer_element_type(packet, type);
        const size_t expected = elem_size * count;
        if (packet->message_type.data_size != expected) return SEDSPRINTF_ERROR;

        // Just a byte copy; caller interprets memory according to 'type'
        std::memcpy(out, packet->data.get(), expected);
        return SEDSPRINTF_OK;
    }

    // ---- TYPED: single element ----
    static SEDSPRINTF_STATUS get_data_one(const ConstPacketPtr & packet,
                                          ElementType type,
                                          void * out /* exactly one element */)
    {
        if (!packet || !packet->data || !out) return SEDSPRINTF_ERROR;

        auto [chosen, elem_size] = infer_element_type(packet, type);
        if (packet->message_type.data_size != elem_size) return SEDSPRINTF_ERROR;

        std::memcpy(out, packet->data.get(), elem_size);
        return SEDSPRINTF_OK;
    }

    // ---- Convenience non-template wrappers for common types ----
    // pointer + count
    static SEDSPRINTF_STATUS get_data_f32(const ConstPacketPtr & p, float * out, size_t n)
    {
        return get_data_typed(p, ElementType::F32, out, n);
    }

    static SEDSPRINTF_STATUS get_data_f64(const ConstPacketPtr & p, double * out, size_t n)
    {
        return get_data_typed(p, ElementType::F64, out, n);
    }

    static SEDSPRINTF_STATUS get_data_u8(const ConstPacketPtr & p, std::uint8_t * out, size_t n)
    {
        return get_data_typed(p, ElementType::U8, out, n);
    }

    static SEDSPRINTF_STATUS get_data_u16(const ConstPacketPtr & p, std::uint16_t * out, size_t n)
    {
        return get_data_typed(p, ElementType::U16, out, n);
    }

    static SEDSPRINTF_STATUS get_data_u32(const ConstPacketPtr & p, std::uint32_t * out, size_t n)
    {
        return get_data_typed(p, ElementType::U32, out, n);
    }

    static SEDSPRINTF_STATUS get_data_u64(const ConstPacketPtr & p, std::uint64_t * out, size_t n)
    {
        return get_data_typed(p, ElementType::U64, out, n);
    }

    // single element
    static SEDSPRINTF_STATUS get_one_f32(const ConstPacketPtr & p, float * out)
    {
        return get_data_one(p, ElementType::F32, out);
    }

    static SEDSPRINTF_STATUS get_one_f64(const ConstPacketPtr & p, double * out)
    {
        return get_data_one(p, ElementType::F64, out);
    }

    static SEDSPRINTF_STATUS get_one_u8(const ConstPacketPtr & p, std::uint8_t * out)
    {
        return get_data_one(p, ElementType::U8, out);
    }

    static SEDSPRINTF_STATUS get_one_u16(const ConstPacketPtr & p, std::uint16_t * out)
    {
        return get_data_one(p, ElementType::U16, out);
    }

    static SEDSPRINTF_STATUS get_one_u32(const ConstPacketPtr & p, std::uint32_t * out)
    {
        return get_data_one(p, ElementType::U32, out);
    }

    static SEDSPRINTF_STATUS get_one_u64(const ConstPacketPtr & p, std::uint64_t * out)
    {
        return get_data_one(p, ElementType::U64, out);
    }

    // ---------- To-string helpers ----------

    static std::string packet_to_string(const ConstPacketPtr & packet,
                                        PayloadFormat fmt = PayloadFormat::Auto)
    {
        if (!packet) return "ERROR: null packet";
        const auto t = static_cast<size_t>(packet->message_type.type);
        if (t >= NUM_DATA_TYPES) return "ERROR: invalid type";
        const size_t sz = packet->message_type.data_size;

        // Header
        std::ostringstream os;
        os << telemetry_packet_metadata_to_string(packet);

        // Empty payload?
        if (sz == 0 || !packet->data) return os.str() + ", Data: <empty>";

        // Decide element size + interpretation
        auto decide_elem = [&](PayloadFormat chosen) -> std::pair<size_t, PayloadFormat>
        {
            if (chosen != PayloadFormat::Auto) return {0, chosen};
            // Infer element size from your config arrays
            // (message_size[t] / message_elements[t]) is the element size you intended.
            const size_t elem_size = message_elements[t] ? (message_size[t] / message_elements[t]) : 1;
            // Heuristic: if element size is 4, assume float32 for GPS/IMU/BATTERY; if 1, bytes; otherwise hex fallback.
            if (elem_size == 4) return {elem_size, PayloadFormat::Float32};
            if (elem_size == 8) return {elem_size, PayloadFormat::Float64};
            if (elem_size == 1) return {elem_size, PayloadFormat::U8};
            if (elem_size == 2) return {elem_size, PayloadFormat::U16};
            return {elem_size, PayloadFormat::Hex};
        };

        size_t elem_size = 0;
        std::tie(elem_size, fmt) = decide_elem(fmt);

        // Helpers
        auto fail_mod = [&](size_t el)
        {
            std::ostringstream err;
            err << os.str()
                    << ", ERROR: payload size (" << sz << " bytes) not multiple of elem_size=" << el;
            return err.str();
        };
        auto pbytes = static_cast<const std::uint8_t *>(packet->data.get());

        os << ", Data: ";
        os.setf(std::ios::fixed, std::ios::floatfield);

        switch (fmt)
        {
            case PayloadFormat::Float32:
            {
                elem_size = 4;
                if (sz % elem_size) return fail_mod(elem_size);
                const size_t n = sz / elem_size;
                const auto * p = reinterpret_cast<const float *>(pbytes);
                for (size_t i = 0; i < n; ++i)
                {
                    os << std::setprecision(MAX_PRECISION) << p[i];
                    if (i + 1 < n) os << ", ";
                }
                break;
            }
            case PayloadFormat::Float64:
            {
                elem_size = 8;
                if (sz % elem_size) return fail_mod(elem_size);
                const size_t n = sz / elem_size;
                const auto * p = reinterpret_cast<const double *>(pbytes);
                for (size_t i = 0; i < n; ++i)
                {
                    os << std::setprecision(MAX_PRECISION) << p[i];
                    if (i + 1 < n) os << ", ";
                }
                break;
            }
            case PayloadFormat::U8:
            {
                const size_t n = sz;
                const std::uint8_t * p = pbytes;
                for (size_t i = 0; i < n; ++i)
                {
                    os << static_cast<unsigned>(p[i]);
                    if (i + 1 < n) os << ", ";
                }
                break;
            }
            case PayloadFormat::U16:
            {
                elem_size = 2;
                if (sz % elem_size) return fail_mod(elem_size);
                const size_t n = sz / elem_size;
                const auto * p = reinterpret_cast<const std::uint16_t *>(pbytes);
                for (size_t i = 0; i < n; ++i)
                {
                    os << p[i];
                    if (i + 1 < n) os << ", ";
                }
                break;
            }
            case PayloadFormat::U32:
            {
                elem_size = 4;
                if (sz % elem_size) return fail_mod(elem_size);
                const size_t n = sz / elem_size;
                const auto * p = reinterpret_cast<const std::uint32_t *>(pbytes);
                for (size_t i = 0; i < n; ++i)
                {
                    os << p[i];
                    if (i + 1 < n) os << ", ";
                }
                break;
            }
            case PayloadFormat::U64:
            {
                elem_size = 8;
                if (sz % elem_size) return fail_mod(elem_size);
                const size_t n = sz / elem_size;
                const auto * p = reinterpret_cast<const std::uint64_t *>(pbytes);
                for (size_t i = 0; i < n; ++i)
                {
                    os << p[i];
                    if (i + 1 < n) os << ", ";
                }
                break;
            }
            case PayloadFormat::Hex:
            default:
            {
                // pure hex dump, space-separated
                os << std::hex << std::setfill('0');
                for (size_t i = 0; i < sz; ++i)
                {
                    os << "0x" << std::setw(2) << static_cast<unsigned>(pbytes[i]);
                    if (i + 1 < sz) os << ' ';
                }
                break;
            }
        }

        return os.str();
    }

private:
    [[nodiscard]] SEDSPRINTF_STATUS transmit(const ConstPacketPtr & packet) const;

    [[nodiscard]] SEDSPRINTF_STATUS log_handler(const message_type_t & type, std::shared_ptr<const void> data) const;

    [[nodiscard]] SEDSPRINTF_STATUS receive(const std::shared_ptr<serialized_buffer_t> & serialized_buffer) const;

    SEDSPRINTF_CONFIG cfg{};
};

#endif // SEDSPRINTF_LIBRARY_H
