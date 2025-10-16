#pragma once
#include "sedsprintf.h"   // must define SedsRouter, SedsResult, SedsPacketView, SedsDataType
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {



#endif

// Router state type
typedef struct
{
    SedsRouter * r;
    uint8_t created;
} RouterState;

// A single global router state (defined in telemetry.c)
extern RouterState g_router;

// Transmit and radio handlers implemented in telemetry.c
SedsResult tx_send(const uint8_t * bytes, size_t len, void * user);

SedsResult on_radio_packet(const SedsPacketView * pkt, void * user);

// Initialize router once; safe to call multiple times.
SedsResult init_telemetry_router(void);

// Log a telemetry sample (1+ floats) with the given SedsDataType.
SedsResult log_telemetry_synchronous(SedsDataType data_type, const float * data, size_t data_len);

SedsResult log_telemetry_asynchronous(SedsDataType data_type, const float * data, size_t data_len);

SedsResult dispatch_tx_queue(void);

void rx_asynchronous(const uint8_t * bytes, size_t len);

SedsResult process_rx_queue(void);


SedsResult dispatch_tx_queue_timeout(uint32_t timeout_ms);


SedsResult process_rx_queue_timeout(uint32_t timeout_ms);

SedsResult process_all_queues_timeout(uint32_t timeout_ms);


#ifdef __cplusplus
}
#endif
