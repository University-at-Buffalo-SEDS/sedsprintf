//*****************************************************************************
//*****************************    C Source Code    ***************************
//*****************************************************************************
//
// DESIGNER NAME: Rylan Meilutis
//
//     FILE NAME: struct_setup.h
//
//          DATE: 9/30/25
//
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//    <ENTER-FILE-DESCRIPTION>
//
//*****************************************************************************
//
// Created by Rylan Meilutis on 9/30/25.
//

#ifndef SEDSPRINTF_STRUCT_SETUP_H
#define SEDSPRINTF_STRUCT_SETUP_H
#include "telemetry_packet.h"

static constexpr message_type_t message_type[] = {
    {GPS_DATA, sizeof(float) * 3}, // GPS_DATA: latitude, longitude, altitude
    {IMU_DATA, sizeof(float) * 6}, // IMU_DATA: accel_x, accel_y, accel_z, gyro_x, gyro_y, gyro_z
    {BATTERY_STATUS, sizeof(float) * 2}, // BATTERY_STATUS: voltage, current
    {SYSTEM_STATUS, sizeof(int) * 8},


};
static_assert(sizeof(message_type) / sizeof(message_type_t) == NUM_DATA_TYPES, "message_type_t size is incorrect");
//these should be changed to the correct config for each board
static const board_config_t board_config = {
    // Define the data endpoints available on this board
    .data_endpoints = (data_endpoint_handler_t[]){
        {SD_CARD, nullptr}, // SD_CARD does not have a receive handler
        {RADIO, nullptr}, // RADIO does not have a receive handler
    },
    .num_endpoints = 2 // Number of endpoints defined above
};


#endif //SEDSPRINTF_STRUCT_SETUP_H
