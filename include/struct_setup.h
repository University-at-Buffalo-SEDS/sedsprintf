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
// ========================= DO NOT EDIT THIS SECTION =========================

// Helper to measure a sentinel-terminated list
static size_t endpoint_len(const data_endpoint_t * eps)
{
    size_t n = 0;
    while (eps[n] != NUM_DATA_ENDPOINTS) ++n;
    return n;
}

// ===========================================================================


// =========================== USER-EDITABLE SECTION ==========================

// Add sentinel at the end of each row
static data_endpoint_t gps_eps[] = {SD_CARD, RADIO, NUM_DATA_ENDPOINTS};
static data_endpoint_t imu_eps[] = {SD_CARD, RADIO, NUM_DATA_ENDPOINTS};
static data_endpoint_t batt_eps[] = {SD_CARD, RADIO, NUM_DATA_ENDPOINTS};
static data_endpoint_t sys_eps[] = {SD_CARD, NUM_DATA_ENDPOINTS};

// Array of pointers (this is needed to ensure all message types have their own endpoint lists)
static data_endpoint_t * endpoints[NUM_DATA_TYPES] = {
    gps_eps, imu_eps, batt_eps, sys_eps
};

static constexpr size_t message_elements[NUM_DATA_TYPES] = {
    //this is the number of elements in each messages data array
    3, // GPS_DATA
    6, // IMU_DATA
    2, // BATTERY_STATUS
    8 // SYSTEM_STATUS
};

static constexpr size_t message_size[NUM_DATA_TYPES] = {
    sizeof(float) * message_elements[GPS_DATA], // GPS_DATA
    sizeof(float) * message_elements[IMU_DATA], // IMU_DATA
    sizeof(float) * message_elements[BATTERY_STATUS], // BATTERY_STATUS
    sizeof(int) * message_elements[SYSTEM_STATUS] // SYSTEM_STATUS
};

static const message_type_t message_type[NUM_DATA_TYPES] = {
    {GPS_DATA, message_size[GPS_DATA], endpoints[GPS_DATA], endpoint_len(endpoints[GPS_DATA])},
    {IMU_DATA, message_size[IMU_DATA], endpoints[IMU_DATA], endpoint_len(endpoints[IMU_DATA])},
    {BATTERY_STATUS, message_size[BATTERY_STATUS], endpoints[BATTERY_STATUS], endpoint_len(endpoints[BATTERY_STATUS])},
    {SYSTEM_STATUS, message_size[SYSTEM_STATUS], endpoints[SYSTEM_STATUS], endpoint_len(endpoints[SYSTEM_STATUS])},
};
//these should be changed to the correct config for each board
static const board_config_t example_board_config = {
    // Define the data endpoints available on this board
    .local_data_endpoints = (data_endpoint_handler_t[]){
        {SD_CARD, nullptr}, // SD_CARD does not have a receive handler
        {RADIO, nullptr}, // RADIO does not have a receive handler
    },
    .num_local_endpoints = 2 // Number of endpoints defined above
};
// ========================= END USER-EDITABLE SECTION ========================
#endif //SEDSPRINTF_STRUCT_SETUP_H
