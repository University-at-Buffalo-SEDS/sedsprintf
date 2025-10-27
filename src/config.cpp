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
        /* BarometerData  */ 3 // 3 floats pressure, altitude, temperature
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

    // ----------------- MESSAGE_TYPES table (size + default endpoints) -----------------
    const std::array<MessageMeta, DATA_TYPE_COUNT> MESSAGE_TYPES = []
    {
        std::array<MessageMeta, DATA_TYPE_COUNT> t{};

        auto make = [](const DataType ty,
                       const MessageDataType k,
                       const std::size_t elems,
                       const std::initializer_list<DataEndpoint> eps) -> MessageMeta
        {
            return MessageMeta{
                ty,
                /*data_size=*/data_type_size(k) * elems,
                std::vector(eps),
                eps.size()
            };
        };

        // Pointers to the static endpoint arrays

        // Fill in order of DataType
        t[static_cast<std::size_t>(DataType::TelemetryError)] =
                make(DataType::TelemetryError,
                     MESSAGE_DATA_TYPES[static_cast<std::size_t>(DataType::TelemetryError)],
                     MESSAGE_ELEMENTS[static_cast<std::size_t>(DataType::TelemetryError)],
                     {DataEndpoint::Radio, DataEndpoint::SdCard});

        t[static_cast<std::size_t>(DataType::GpsData)] =
                make(DataType::GpsData,
                     MESSAGE_DATA_TYPES[static_cast<std::size_t>(DataType::GpsData)],
                     MESSAGE_ELEMENTS[static_cast<std::size_t>(DataType::GpsData)],
                     {DataEndpoint::Radio, DataEndpoint::SdCard});

        t[static_cast<std::size_t>(DataType::ImuData)] =
                make(DataType::ImuData,
                     MESSAGE_DATA_TYPES[static_cast<std::size_t>(DataType::ImuData)],
                     MESSAGE_ELEMENTS[static_cast<std::size_t>(DataType::ImuData)],
                     {DataEndpoint::Radio, DataEndpoint::SdCard});

        t[static_cast<std::size_t>(DataType::BatteryStatus)] =
                make(DataType::BatteryStatus,
                     MESSAGE_DATA_TYPES[static_cast<std::size_t>(DataType::BatteryStatus)],
                     MESSAGE_ELEMENTS[static_cast<std::size_t>(DataType::BatteryStatus)],
                     {DataEndpoint::Radio, DataEndpoint::SdCard});

        t[static_cast<std::size_t>(DataType::SystemStatus)] =
                make(DataType::SystemStatus,
                     MESSAGE_DATA_TYPES[static_cast<std::size_t>(DataType::SystemStatus)],
                     MESSAGE_ELEMENTS[static_cast<std::size_t>(DataType::SystemStatus)],
                     {DataEndpoint::SdCard});

        t[static_cast<std::size_t>(DataType::BarometerData)] =
                make(DataType::BarometerData,
                     MESSAGE_DATA_TYPES[static_cast<std::size_t>(DataType::BarometerData)],
                     MESSAGE_ELEMENTS[static_cast<std::size_t>(DataType::BarometerData)],
                     {DataEndpoint::Radio, DataEndpoint::SdCard});

        return t;
    }();
} // namespace seds