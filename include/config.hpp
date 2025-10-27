#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <vector>


namespace seds
{
    // ---------------------- User Editable ----------------------
    inline constexpr auto DEVICE_IDENTIFIER = "TEST_PLATFORM";

    // Mirrors Rust #[repr(u32)]
    enum class DataEndpoint : std::uint32_t
    {
        SdCard = 0,
        Radio = 1,
    };

    inline constexpr std::uint32_t MAX_VALUE_DATA_ENDPOINT =
            static_cast<std::uint32_t>(DataEndpoint::Radio);

    enum class MessageDataType
    {
        Float32,
        UInt8,
        UInt32,
        String,
        Hex,
    };

    enum class MessageType
    {
        Info,
        Error,
    };

    constexpr const char * data_endpoint_as_str(const DataEndpoint ep)
    {
        switch (ep)
        {
            case DataEndpoint::SdCard: return "SD_CARD";
            case DataEndpoint::Radio: return "RADIO";
        }
        return "UNKNOWN_ENDPOINT";
    }

    // Mirrors Rust #[repr(u32)]
    enum class DataType : std::uint32_t
    {
        TelemetryError = 0,
        GpsData = 1,
        ImuData = 2,
        BatteryStatus = 3,
        SystemStatus = 4,
        BarometerData = 5,
    };

    inline constexpr std::uint32_t MAX_VALUE_DATA_TYPE =
            static_cast<std::uint32_t>(DataType::BarometerData);

    // Rust has `pub const COUNT: usize = 6;`
    inline constexpr std::size_t DATA_TYPE_COUNT = 6;

    constexpr const char * data_type_as_str(const DataType dt)
    {
        switch (dt)
        {
            case DataType::TelemetryError: return "TELEMETRY_ERROR";
            case DataType::GpsData: return "GPS_DATA";
            case DataType::ImuData: return "IMU_DATA";
            case DataType::BatteryStatus: return "BATTERY_STATUS";
            case DataType::SystemStatus: return "SYSTEM_STATUS";
            case DataType::BarometerData: return "BAROMETER_DATA";
        }
        return "UNKNOWN_DATA_TYPE";
    }

    // Fixed maximum lengths to match Rust
    inline constexpr std::size_t MAX_STRING_LENGTH = 1024;
    inline constexpr std::size_t MAX_HEX_LENGTH = 1024;

    // Size per element for each MessageDataType (Rust const fn data_type_size)
    constexpr std::size_t data_type_size(const MessageDataType dt)
    {
        switch (dt)
        {
            case MessageDataType::Float32: return sizeof(float);
            case MessageDataType::UInt8: return sizeof(std::uint8_t);
            case MessageDataType::UInt32: return sizeof(std::uint32_t);
            case MessageDataType::String: return MAX_STRING_LENGTH;
            case MessageDataType::Hex: return MAX_HEX_LENGTH;
        }
        return 0;
    }

    // how many elements each message carries (Rust MESSAGE_ELEMENTS)
    extern const std::array<std::size_t, DATA_TYPE_COUNT> MESSAGE_ELEMENTS;

    // These mirror Rust’s const arrays (MESSAGE_DATA_TYPES / MESSAGE_INFO_TYPES)
    extern const std::array<MessageDataType, DATA_TYPE_COUNT> MESSAGE_DATA_TYPES;
    extern const std::array<MessageType, DATA_TYPE_COUNT> MESSAGE_INFO_TYPES;

    // ---------------------- Not User Editable ----------------------
    struct MessageMeta {
        DataType type;
        std::size_t data_size;
        std::vector<DataEndpoint> endpoints;
        std::size_t num_endpoints;
    };

    // Rust: get_needed_message_size(ty) = data_type_size(MESSAGE_DATA_TYPES[ty]) * MESSAGE_ELEMENTS[ty]
    inline std::size_t get_needed_message_size(DataType ty)
    {
        const auto idx = static_cast<std::size_t>(ty);
        return data_type_size(MESSAGE_DATA_TYPES[idx]) * MESSAGE_ELEMENTS[idx];
    }

    // Rust: get_info_type(ty) = MESSAGE_INFO_TYPES[ty]
    inline MessageType get_info_type(DataType ty)
    {
        return MESSAGE_INFO_TYPES[static_cast<std::size_t>(ty)];
    }

    // Rust: static table MESSAGE_TYPES and accessor message_meta(ty)
    extern const std::array<MessageMeta, DATA_TYPE_COUNT> MESSAGE_TYPES;

    inline const MessageMeta & message_meta(DataType ty)
    {
        return MESSAGE_TYPES[static_cast<std::size_t>(ty)];
    }

} // namespace seds
