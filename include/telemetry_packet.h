//*****************************************************************************
//*****************************    C Source Code    ***************************
//*****************************************************************************
//
// DESIGNER NAME: Rylan Meilutis
//
//     FILE NAME: telemetry_packet.h
//
//          DATE: 9/30/25
//
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//    This defines the necessary custom data types for a telemetry packet.
//    Nothing in this file should need to be user-edited.
//
//
//*****************************************************************************
//
// Created by Rylan Meilutis on 9/30/25.
//
#ifndef SEDSPRINTF_TELEMETRY_PACKET_H
#define SEDSPRINTF_TELEMETRY_PACKET_H
#include "enum_setup.h"
// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
// ReSharper disable once CppUnusedIncludeDirective
#include <ctime>

// ========================= DO NOT EDIT THIS SECTION =========================
typedef enum
{
    SEDSPRINTF_OK,
    SEDSPRINTF_ERROR,
} SEDSPRINTF_STATUS;


typedef struct
{
    data_type_t type;
    size_t data_size;
    data_endpoint_t * endpoints; // list of endpoints to send this data to
    size_t num_endpoints; // number of endpoints in the list
} message_type_t;

// Table entry describing a supported endpoint on this board.


typedef struct
{
    message_type_t message_type;
    time_t timestamp;
    //the data can be any type, so we use a void pointer
    void * data;
} telemetry_packet_t;

typedef SEDSPRINTF_STATUS (* receive_helper_t)(telemetry_packet_t * buffer);

typedef struct
{
    data_endpoint_t local_endpoint;
    receive_helper_t receive_handler; // set NULL if unused
} data_endpoint_handler_t;

// Board-wide configuration (what endpoints exist, etc.)
typedef struct
{
    const data_endpoint_handler_t * local_data_endpoints;
    size_t num_local_endpoints;
} board_config_t;

// ===========================================================================

#endif //SEDSPRINTF_TELEMETRY_PACKET_H
