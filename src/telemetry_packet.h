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
//    <ENTER-FILE-DESCRIPTION>
//
//*****************************************************************************
//
// Created by Rylan Meilutis on 9/30/25.
//
#ifndef SEDSPRINTF_TELEMETRY_PACKET_H
#define SEDSPRINTF_TELEMETRY_PACKET_H
#include "config.h"

typedef struct
    {
        data_endpoint_t* endpoints; //array of endpoints to send data to
        message_type_t message_type;
        void* data;               //pointer to data to be sent
    } telemetry_packet_t;






#endif //SEDSPRINTF_TELEMETRY_PACKET_H