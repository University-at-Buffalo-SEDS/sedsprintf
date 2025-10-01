//*****************************************************************************
//*****************************    C Source Code    ***************************
//*****************************************************************************
//
// DESIGNER NAME: Rylan Meilutis
//
//     FILE NAME: config.h
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

#ifndef SEDSPRINTF_CONFIG_H
#define SEDSPRINTF_CONFIG_H

typedef enum
{
    //add all possible data endpoints here. the provided ones are examples
    SD_CARD,
    RADIO,
} data_endpoint_t;

typedef enum
{
    //add all possible message types here. the provided ones are examples
    GPS_DATA,
    IMU_DATA,
    BATTERY_STATUS,
    SYSTEM_STATUS,
} message_type_t;


#endif //SEDSPRINTF_CONFIG_H
