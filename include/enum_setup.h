// Description: This file contains enum definitions and their string representations
//              for configuring the telemetry system. Users can add new data types and
//              endpoints as needed.
//              This file is intended to be user-editable to allow for easy configuration of the telemetry system.
//*****************************************************************************
//

#ifndef SEDSPRINTF_CONFIG_H
#define SEDSPRINTF_CONFIG_H
#include <string>


// =========================== USER-EDITABLE SECTION ==========================
// Put ALL user-editable types here.

typedef enum
{
    // Add all possible data endpoints here
    SD_CARD,
    RADIO,

    NUM_DATA_ENDPOINTS
} data_endpoint_t;

static const std::string data_endpoint_names[NUM_DATA_ENDPOINTS] = {
    "SD_CARD",
    "RADIO",
};

typedef enum
{
    // Add all possible message types here
    GPS_DATA,
    IMU_DATA,
    BATTERY_STATUS,
    SYSTEM_STATUS,

    NUM_DATA_TYPES // Always keep this as the last entry
} data_type_t;

static const std::string message_names[NUM_DATA_TYPES] = {
    "GPS_DATA",
    "IMU_DATA",
    "BATTERY_STATUS",
    "SYSTEM_STATUS",

};

// ========================= END USER-EDITABLE SECTION ========================
#endif // SEDSPRINTF_CONFIG_H
