#include "config.hpp"

namespace seds
{
    // Device identifier constant
    const char * DEVICE_IDENTIFIER = "TEST_PLATFORM";

    // Message metadata table (mirrors Rust MESSAGE_ELEMENTS)
    const std::array<MessageMeta, MAX_MESSAGE_TYPES> MESSAGE_ELEMENTS = {
        {
            {DataType::GPS_DATA, MessageDataType::Float32, MessageType::Info, 24, { DataEndpoint::SD_CARD, DataEndpoint::RADIO }},
            {DataType::IMU_DATA, MessageDataType::Float32, MessageType::Info, 36, { DataEndpoint::SD_CARD, DataEndpoint::RADIO }},
            {DataType::BATTERY_STATUS, MessageDataType::UInt32, MessageType::Info, 8, { DataEndpoint::SD_CARD, DataEndpoint::RADIO }},
            {DataType::SYSTEM_STATUS, MessageDataType::UInt8, MessageType::Info, 4, { DataEndpoint::SD_CARD}},
            {DataType::BAROMETER_DATA, MessageDataType::Float32, MessageType::Info, 4, { DataEndpoint::SD_CARD, DataEndpoint::RADIO }},

        }
    };

    // Data-type lookup table (parallel to MESSAGE_DATA_TYPES)
    const std::array<MessageDataType, MAX_MESSAGE_TYPES> MESSAGE_DATA_TYPES = {
        {
            MessageDataType::Float32,
            MessageDataType::Float32,
            MessageDataType::UInt32,
            MessageDataType::UInt8,
            MessageDataType::Float32,
            MessageDataType::Float32,
        }
    };

    // ---- Functions ----
    const MessageMeta & message_meta(DataType t) noexcept
    {
        if (const auto idx = static_cast<size_t>(t); idx < MESSAGE_ELEMENTS.size())
            return MESSAGE_ELEMENTS[idx];
        return MESSAGE_ELEMENTS.back();
    }

    MessageType get_info_type(DataType t) noexcept
    {
        if (const auto idx = static_cast<size_t>(t); idx < MESSAGE_ELEMENTS.size())
            return MESSAGE_ELEMENTS[idx].msgType;
        return MessageType::Error;
    }
} // namespace seds
