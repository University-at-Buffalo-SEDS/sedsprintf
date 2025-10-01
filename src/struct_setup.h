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

// Add sentinel at the end of each row
static data_endpoint_t gps_eps[]    = { SD_CARD, RADIO, NUM_DATA_ENDPOINTS };
static data_endpoint_t imu_eps[]    = { SD_CARD, RADIO, NUM_DATA_ENDPOINTS };
static data_endpoint_t batt_eps[]   = { SD_CARD, RADIO, NUM_DATA_ENDPOINTS };
static data_endpoint_t sys_eps[]    = { SD_CARD, NUM_DATA_ENDPOINTS };

// Array of pointers
static data_endpoint_t* endpoints[NUM_DATA_TYPES] = {
    gps_eps, imu_eps, batt_eps, sys_eps
};

// Helper to measure a sentinel-terminated list
static inline size_t endpoint_len(const data_endpoint_t *eps) {
    size_t n = 0;
    while (eps[n] != NUM_DATA_ENDPOINTS) ++n;
    return n;
}
static_assert(sizeof(endpoints) / (sizeof(data_endpoint_t) * NUM_DATA_ENDPOINTS) == NUM_DATA_TYPES, "message_type_t size is incorrect");

static message_type_t message_type[] = {
    { GPS_DATA, sizeof(float)*3, endpoints[GPS_DATA], endpoint_len(endpoints[GPS_DATA]) },
    { IMU_DATA, sizeof(float)*6, endpoints[IMU_DATA], endpoint_len(endpoints[IMU_DATA]) },
    { BATTERY_STATUS, sizeof(float)*2, endpoints[BATTERY_STATUS], endpoint_len(endpoints[BATTERY_STATUS]) },
    { SYSTEM_STATUS, sizeof(int)*8, endpoints[SYSTEM_STATUS], endpoint_len(endpoints[SYSTEM_STATUS]) },
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
