#pragma once
#include <vector>
#include <array>

// include/config.hpp
#pragma once
#include <cstdint>
#include <cstddef>
#include <array>
#include <vector>

// --- Defuse bad macros coming from vendor headers (Cube/CMSIS/etc.)
#ifdef DataType
#  undef DataType
#endif
#ifdef MessageType
#  undef MessageType
#endif
#ifdef MessageDataType
#  undef MessageDataType
#endif
#ifdef DataEndpoint
#  undef DataEndpoint
#endif
namespace seds {

// ---- DataEndpoint ----
enum class DataEndpoint : uint32_t {
    SD_CARD = 0,
    RADIO,
    MAX
};

inline const char* endpoint_to_str(DataEndpoint e) noexcept {
    switch (e) {
        case DataEndpoint::SD_CARD:   return "SD_CARD";
        case DataEndpoint::RADIO:     return "RADIO";
        default:                      return "UNKNOWN";
    }
}

inline DataEndpoint endpoint_from_u32(uint32_t v) noexcept {
    if (v >= static_cast<uint32_t>(DataEndpoint::MAX))
        return DataEndpoint::SD_CARD; // fallback
    return static_cast<DataEndpoint>(v);
}

// ---- DataType ----
enum class DataType : uint32_t {
    TELEMETRY_ERROR = 0,
    GPS_DATA,
    IMU_DATA,
    BATTERY_STATUS,
    SYSTEM_STATUS,
    BAROMETER_DATA,
    MAX
};

inline const char* datatype_to_str(DataType t) noexcept {
    switch (t) {
        case DataType::GPS_DATA:        return "GPS_DATA";
        case DataType::IMU_DATA:        return "IMU_DATA";
        case DataType::BATTERY_STATUS:  return "BATTERY_STATUS";
        case DataType::SYSTEM_STATUS:   return "SYSTEM_STATUS";
        case DataType::BAROMETER_DATA:     return "BAROMETER_DATA";
        default:                        return "UNKNOWN";
    }
}

inline DataType datatype_from_u32(uint32_t v) noexcept {
    if (v >= static_cast<uint32_t>(DataType::MAX))
        return DataType::TELEMETRY_ERROR;
    return static_cast<DataType>(v);
}

// ---- MessageDataType ----
enum class MessageDataType : uint8_t {
    UInt8,
    UInt32,
    Float32,
    String,
    Hex
};

// ---- MessageType ----
enum class MessageType : uint8_t {
    Info,
    Error
};

// ---- MessageMeta ----
struct MessageMeta {
    DataType type;
    MessageDataType dataType;
    MessageType msgType;
    size_t dataSize;
    std::vector<DataEndpoint> endpoints; // NEW: default endpoints for this message

};

// ---- Constants ----
constexpr size_t MAX_MESSAGE_TYPES = static_cast<size_t>(DataType::MAX);

extern const std::array<MessageMeta, MAX_MESSAGE_TYPES> MESSAGE_ELEMENTS;
extern const std::array<MessageDataType, MAX_MESSAGE_TYPES> MESSAGE_DATA_TYPES;
extern const char* DEVICE_IDENTIFIER;

// ---- Utility functions ----
const MessageMeta& message_meta(DataType t) noexcept;
MessageType get_info_type(DataType t) noexcept;

} // namespace seds
