// NOTE: no #pragma once in a .cpp!
#include "config.hpp"

namespace seds
{
    // ----------------- Element counts (MESSAGE_ELEMENTS) -----------------
    // Order must match DataType enum!
    const std::array<std::size_t, DATA_TYPE_COUNT> MESSAGE_ELEMENTS = {
        /* TelemetryError */ 1, // a single "string" blob of MAX_STRING_LENGTH
        /* GpsData        */ 3, // lat, lon, alt
        /* ImuData        */ 6, // ax, ay, az, gx, gy, gz
        /* BatteryStatus  */ 4, // e.g., v, i, soc, temp
        /* SystemStatus   */ 1, // one u32 bitfield or code
        /* BarometerData  */ 3 // one float (pressure or altitude)
    };

    // ----------------- Data kinds (MESSAGE_DATA_TYPES) -----------------
    const std::array<MessageDataType, DATA_TYPE_COUNT> MESSAGE_DATA_TYPES = {
        /* TelemetryError */ MessageDataType::String,
        /* GpsData        */ MessageDataType::Float32,
        /* ImuData        */ MessageDataType::Float32,
        /* BatteryStatus  */ MessageDataType::Float32,
        /* SystemStatus   */ MessageDataType::UInt32,
        /* BarometerData  */ MessageDataType::Float32
    };

    // ----------------- Info/Error classification (MESSAGE_INFO_TYPES) -----------------
    const std::array<MessageType, DATA_TYPE_COUNT> MESSAGE_INFO_TYPES = {
        /* TelemetryError */ MessageType::Error,
        /* GpsData        */ MessageType::Info,
        /* ImuData        */ MessageType::Info,
        /* BatteryStatus  */ MessageType::Info,
        /* SystemStatus   */ MessageType::Info,
        /* BarometerData  */ MessageType::Info
    };

    // ----------------- Endpoint sets used by MESSAGE_TYPES -----------------
    namespace detail
    {
        const std::array<DataEndpoint, 2> ENDPOINTS_SD_AND_RADIO = {
            DataEndpoint::SdCard, DataEndpoint::Radio
        };
        const std::array<DataEndpoint, 1> ENDPOINTS_SD_ONLY = {
            DataEndpoint::SdCard
        };
        const std::array<DataEndpoint, 1> ENDPOINTS_RADIO_ONLY = {
            DataEndpoint::Radio
        };
    } // namespace detail

    // ----------------- MESSAGE_TYPES table (size + default endpoints) -----------------
    const std::array<MessageMeta, DATA_TYPE_COUNT> MESSAGE_TYPES = []()
    {
        std::array<MessageMeta, DATA_TYPE_COUNT> t{};

        auto make = [](DataType ty,
                       MessageDataType k,
                       std::size_t elems,
                       const DataEndpoint * eps,
                       std::size_t nep) -> MessageMeta
        {
            return MessageMeta{
                ty,
                /*data_size=*/data_type_size(k) * elems,
                eps,
                nep
            };
        };

        // Pointers to the static endpoint arrays
        const DataEndpoint * SD_RADIO = detail::ENDPOINTS_SD_AND_RADIO.data();
        const std::size_t N_SD_RADIO = detail::ENDPOINTS_SD_AND_RADIO.size();

        const DataEndpoint * SD_ONLY = detail::ENDPOINTS_SD_ONLY.data();
        const std::size_t N_SD_ONLY = detail::ENDPOINTS_SD_ONLY.size();

        const DataEndpoint * RADIO_ONLY = detail::ENDPOINTS_RADIO_ONLY.data();
        const std::size_t N_RADIO_ONLY = detail::ENDPOINTS_RADIO_ONLY.size();

        // Fill in order of DataType
        t[static_cast<std::size_t>(DataType::TelemetryError)] =
                make(DataType::TelemetryError,
                     MESSAGE_DATA_TYPES[static_cast<std::size_t>(DataType::TelemetryError)],
                     MESSAGE_ELEMENTS[static_cast<std::size_t>(DataType::TelemetryError)],
                     RADIO_ONLY, N_RADIO_ONLY);

        t[static_cast<std::size_t>(DataType::GpsData)] =
                make(DataType::GpsData,
                     MESSAGE_DATA_TYPES[static_cast<std::size_t>(DataType::GpsData)],
                     MESSAGE_ELEMENTS[static_cast<std::size_t>(DataType::GpsData)],
                     SD_RADIO, N_SD_RADIO);

        t[static_cast<std::size_t>(DataType::ImuData)] =
                make(DataType::ImuData,
                     MESSAGE_DATA_TYPES[static_cast<std::size_t>(DataType::ImuData)],
                     MESSAGE_ELEMENTS[static_cast<std::size_t>(DataType::ImuData)],
                     SD_RADIO, N_SD_RADIO);

        t[static_cast<std::size_t>(DataType::BatteryStatus)] =
                make(DataType::BatteryStatus,
                     MESSAGE_DATA_TYPES[static_cast<std::size_t>(DataType::BatteryStatus)],
                     MESSAGE_ELEMENTS[static_cast<std::size_t>(DataType::BatteryStatus)],
                     SD_RADIO, N_SD_RADIO);

        t[static_cast<std::size_t>(DataType::SystemStatus)] =
                make(DataType::SystemStatus,
                     MESSAGE_DATA_TYPES[static_cast<std::size_t>(DataType::SystemStatus)],
                     MESSAGE_ELEMENTS[static_cast<std::size_t>(DataType::SystemStatus)],
                     SD_ONLY, N_SD_RADIO);

        t[static_cast<std::size_t>(DataType::BarometerData)] =
                make(DataType::BarometerData,
                     MESSAGE_DATA_TYPES[static_cast<std::size_t>(DataType::BarometerData)],
                     MESSAGE_ELEMENTS[static_cast<std::size_t>(DataType::BarometerData)],
                     SD_RADIO, N_SD_RADIO);

        return t;
    }();
} // namespace seds
