#ifndef SEDSPRINTF_LIBRARY_H
#define SEDSPRINTF_LIBRARY_H
#include "serialize.h"
#include "struct_setup.h"
#include <string>
#include <sstream>
#include <iomanip>
#include <cstring>


#define MAX_PRECISION 12

typedef SEDSPRINTF_STATUS (* transmit_helper_t)(serialized_buffer_t * serialized_packet);

typedef struct
{
    transmit_helper_t * transmit_helpers;
    size_t num_helpers;
    board_config_t board_config;
} SEDSPRINTF_CONFIG;

class sedsprintf
{
public:
    template<typename T>
    static SEDSPRINTF_STATUS get_data_from_packet(const telemetry_packet_t * packet, T & out)
    {
        if (!packet || !packet->data) return SEDSPRINTF_ERROR;
        if (packet->message_type.data_size != sizeof(T)) return SEDSPRINTF_ERROR;
        std::memcpy(&out, packet->data, sizeof(T));
        return SEDSPRINTF_OK;
    }

    // Pointer + count (e.g., float*, count)
    template<typename T>
    static SEDSPRINTF_STATUS get_data_from_packet(const telemetry_packet_t * packet, T * out, size_t count)
    {
        if (!packet || !packet->data || !out) return SEDSPRINTF_ERROR;
        const size_t need = sizeof(T) * count;
        if (packet->message_type.data_size != need) return SEDSPRINTF_ERROR;
        std::memcpy(out, packet->data, need);
        return SEDSPRINTF_OK;
    }

    // std::array overload (size known at compile time)
    template<typename T, size_t N>
    static SEDSPRINTF_STATUS get_data_from_packet(const telemetry_packet_t * packet, std::array<T, N> & out)
    {
        if (!packet || !packet->data) return SEDSPRINTF_ERROR;
        constexpr size_t need = sizeof(T) * N;
        if (packet->message_type.data_size != need) return SEDSPRINTF_ERROR;
        std::memcpy(out.data(), packet->data, need);
        return SEDSPRINTF_OK;
    }

    //to string functions
    // 1) Deduce count from packet->message_type.data_size
    template<typename T>
    static std::string packet_to_string(const telemetry_packet_t * packet, const T * data)
    {
        if (!packet || !data) return "ERROR: null packet or data";
        const size_t sz = packet->message_type.data_size;

        if (sizeof(T) == 0 || (sz % sizeof(T)) != 0)
        {
            std::ostringstream err;
            err << telemetry_packet_metadata_to_string(packet)
                    << ", ERROR: payload size (" << sz << " bytes) is not a multiple of sizeof(T)="
                    << sizeof(T) << " bytes";
            return err.str();
        }

        const size_t count = sz / sizeof(T);

        std::ostringstream os;
        os << telemetry_packet_metadata_to_string(packet) << ", Data: ";
        os.setf(std::ios::fixed, std::ios::floatfield);

        for (size_t i = 0; i < count; ++i)
        {
            if constexpr (std::is_floating_point_v<T>)
                os << std::setprecision(MAX_PRECISION) << data[i];
            else
                os << data[i];

            if (i + 1 < count) os << ", ";
        }
        return os.str();
    }

    // 2) Convenience: read directly from packet->data (no external buffer)
    template<typename T>
    static std::string packet_to_string(const telemetry_packet_t * packet)
    {
        if (!packet || !packet->data) return "ERROR: null packet or packet->data is null";
        const T * data = static_cast<const T *>(packet->data);
        // Delegate to the overload above (which infers count)
        return packet_to_string<T>(packet, data);
    }

    // 3) Nice extras: std::array and std::vector overloads
    template<typename T, size_t N>
    static std::string packet_to_string(const telemetry_packet_t * packet, const std::array<T, N> & arr)
    {
        // Verify the packet really contains N elements of T
        const size_t need = sizeof(T) * N;
        if (!packet) return "ERROR: null packet";
        if (packet->message_type.data_size != need)
        {
            std::ostringstream err;
            err << telemetry_packet_metadata_to_string(packet)
                    << ", ERROR: size mismatch (packet=" << packet->message_type.data_size
                    << " bytes, expected=" << need << " bytes)";
            return err.str();
        }
        return packet_to_string<T>(packet, arr.data(), N);
    }

    template<typename T>
    static std::string packet_to_string(const telemetry_packet_t * packet, const std::vector<T> & vec)
    {
        if (!packet) return "ERROR: null packet";
        const size_t need = sizeof(T) * vec.size();
        if (packet->message_type.data_size != need)
        {
            std::ostringstream err;
            err << telemetry_packet_metadata_to_string(packet)
                    << ", ERROR: size mismatch (packet=" << packet->message_type.data_size
                    << " bytes, expected=" << need << " bytes)";
            return err.str();
        }
        return packet_to_string<T>(packet, vec.data(), vec.size());
    }


    template<typename T>
    static std::string packet_to_string(const telemetry_packet_t * packet, const T * data, size_t count)
    {
        if (!packet || !data) return "ERROR: null packet or data";

        const size_t need = sizeof(T) * count;
        if (packet->message_type.data_size != need)
        {
            std::ostringstream err;
            err << telemetry_packet_metadata_to_string(packet)
                    << ", ERROR: size mismatch (packet=" << packet->message_type.data_size
                    << " bytes, expected=" << need << " bytes)";
            return err.str();
        }

        std::ostringstream os;
        os << telemetry_packet_metadata_to_string(packet) << ", Data: ";

        os.setf(std::ios::fixed, std::ios::floatfield);
        for (size_t i = 0; i < count; ++i)
        {
            if constexpr (std::is_floating_point_v<T>)
                os << std::setprecision(MAX_PRECISION) << data[i];
            else
                os << data[i];

            if (i + 1 < count) os << ", ";
        }
        return os.str();
    }

    // Raw hex fallback
    static std::string packet_to_hex_string(const telemetry_packet_t * packet, const void * data)
    {
        return (!packet || !data)
                   ? "ERROR: null packet or data"
                   : packet_to_hex_string(packet, data, packet->message_type.data_size);
    }

    static std::string packet_to_hex_string(const telemetry_packet_t * packet)
    {
        return (!packet || !packet->data)
                   ? "ERROR: null packet or packet->data is null"
                   : packet_to_hex_string(packet, packet->data,
                                          packet->message_type.data_size);
    }

    static std::string packet_to_hex_string(const telemetry_packet_t * packet, const void * data, size_t size_bytes);

    sedsprintf(transmit_helper_t transmit_helpers[], size_t num_helpers, board_config_t config);

    SEDSPRINTF_STATUS log(message_type_t, void * data) const;

    static SEDSPRINTF_STATUS copy_telemetry_packet(telemetry_packet_t * dest, const telemetry_packet_t * src);

    static std::string telemetry_packet_metadata_to_string(const telemetry_packet_t * packet);

private:
    SEDSPRINTF_STATUS transmit(telemetry_packet_t * packet) const;

    SEDSPRINTF_STATUS receive(const serialized_buffer_t * serialized_buffer) const;

    SEDSPRINTF_CONFIG cfg{};
};

#endif // SEDSPRINTF_LIBRARY_H
