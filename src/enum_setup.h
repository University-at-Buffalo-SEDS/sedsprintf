// config.h
#ifndef SEDSPRINTF_CONFIG_H
#define SEDSPRINTF_CONFIG_H





// =========================== USER-EDITABLE SECTION ==========================
// Put ALL user-editable types here.

typedef enum
{
    // Add all possible data endpoints here
    SD_CARD,
    RADIO,

    NUM_DATA_ENDPOINTS
} data_endpoint_t;

typedef enum
{
    // Add all possible message types here
    GPS_DATA,
    IMU_DATA,
    BATTERY_STATUS,
    SYSTEM_STATUS,

    NUM_DATA_TYPES // Always keep this as the last entry
} data_type_t;

// ========================= END USER-EDITABLE SECTION ========================

#endif // SEDSPRINTF_CONFIG_H
