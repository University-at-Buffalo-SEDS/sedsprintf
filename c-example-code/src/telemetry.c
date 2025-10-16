#include "telemetry.h"
#include <stdio.h>
#include <string.h>
#include "sedsprintf.h"
#include <time.h>

// --- Monotonic milliseconds provider for the FFI timeout calls ---
static uint64_t host_now_ms(void * user)
{
    (void) user;

    // Windows
#ifdef _WIN32
    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER ctr;
    if (freq.QuadPart == 0)
    {
        QueryPerformanceFrequency(&freq);
    }
    QueryPerformanceCounter(&ctr);
    // convert to ms
    return (uint64_t) ((ctr.QuadPart * 1000ULL) / (uint64_t) freq.QuadPart);

    // POSIX: prefer CLOCK_MONOTONIC if available
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000ULL + (uint64_t) (ts.tv_nsec / 1000000ULL);

    // Fallback: gettimeofday (not strictly monotonic, but OK for tests)
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t) tv.tv_sec * 1000ULL + (uint64_t) (tv.tv_usec / 1000ULL);
#endif
}

// Define the global router state here (one definition only)
RouterState g_router = {.r = NULL, .created = 0};

// --- TX: convert your bytes to CAN frames and send via HAL later; stub for now ---
SedsResult tx_send(const uint8_t * bytes, size_t len, void * user)
{
    (void) user;
    (void) bytes;
    (void) len;
    return SEDS_OK;
}

//example function that receives the data from the can bus or similar and passes the serialized packet to the router for decoding and handling.
void rx_synchronous(const uint8_t * bytes, size_t len)
{
    if (!g_router.r)
    {
        // lazy init if not yet created
        if (init_telemetry_router() != SEDS_OK) return;
    }
    if (!bytes || len == 0) return;
    seds_router_receive_serialized(g_router.r, bytes, len);
}


void rx_asynchronous(const uint8_t * bytes, size_t len)
{
    if (!g_router.r)
    {
        // lazy init if not yet created
        if (init_telemetry_router() != SEDS_OK) return;
    }
    if (!bytes || len == 0) return;
    seds_router_rx_serialized_packet_to_queue(g_router.r, bytes, len);
}

// --- Simple radio handler ---
SedsResult on_radio_packet(const SedsPacketView * pkt, void * user)
{
    (void) user;
    char buf[seds_pkt_to_string_len(pkt)];
    SedsResult s = seds_pkt_to_string(pkt, buf, sizeof(buf));
    if (s != SEDS_OK)
    {
        printf("on_radio_packet: seds_pkt_to_string failed: %d\n", s);
        return s;
    }
    printf("on_radio_packet: %s\n", buf);
    return SEDS_OK;
}

SedsResult init_telemetry_router(void)
{
    if (g_router.created && g_router.r)
    {
        return SEDS_OK;
    }

    // Local endpoint table
    const SedsLocalEndpointDesc locals[] = {
        {.endpoint = SEDS_EP_RADIO, .handler = on_radio_packet, .user = NULL},
    };

    SedsRouter * r = seds_router_new(
        tx_send,
        NULL, // tx_user
        host_now_ms,
        locals,
        sizeof(locals) / sizeof(locals[0])
    );

    if (!r)
    {
        printf("Error: failed to create router\n");
        g_router.r = NULL;
        g_router.created = 0;
        return SEDS_ERR;
    }

    g_router.r = r;
    g_router.created = 1;
    return SEDS_OK;
}

SedsResult log_telemetry_synchronous(SedsDataType data_type, const float * data, size_t data_len)
{
    if (!g_router.r)
    {
        // lazy init if not yet created
        if (init_telemetry_router() != SEDS_OK) return SEDS_ERR;
    }
    if (!data || data_len == 0) return SEDS_ERR;

    // If you want timestamps from HAL, you can expose HAL_GetTick() here via a weak symbol or
    // pass a timestamp of 0 to use the router’s internal time policy, if supported.
    uint64_t ts = 0; // replace with board tick if your C API accepts a timestamp parameter elsewhere

    // If your C API uses an overload with timestamp, call that; otherwise plain log:
    // e.g., seds_router_log_ts(g_router.r, data_type, data, (uint32_t)data_len, ts);
    return seds_router_log(g_router.r, data_type, data, data_len, ts);
}

SedsResult dispatch_tx_queue(void)
{
    if (!g_router.r)
    {
        // lazy init if not yet created
        if (init_telemetry_router() != SEDS_OK) return SEDS_ERR;
    }
    return seds_router_process_send_queue(g_router.r);
}

SedsResult process_rx_queue(void)
{
    if (!g_router.r)
    {
        // lazy init if not yet created
        if (init_telemetry_router() != SEDS_OK) return SEDS_ERR;
    }
    return seds_router_process_received_queue(g_router.r);
}


SedsResult log_telemetry_asynchronous(SedsDataType data_type, const float * data, size_t data_len)
{
    if (!g_router.r)
    {
        // lazy init if not yet created
        if (init_telemetry_router() != SEDS_OK) return SEDS_ERR;
    }
    if (!data || data_len == 0) return SEDS_ERR;

    // If you want timestamps from HAL, you can expose HAL_GetTick() here via a weak symbol or
    // pass a timestamp of 0 to use the router’s internal time policy, if supported.
    uint64_t ts = 0; // replace with board tick if your C API accepts a timestamp parameter elsewhere

    // If your C API uses an overload with timestamp, call that; otherwise plain log:
    // e.g., seds_router_log_ts(g_router.r, data_type, data, (uint32_t)data_len, ts);
    return seds_router_log_queue(g_router.r, data_type, data, data_len, ts);
}


// Pump TX for up to `timeout_ms` (0 means "check once, no waiting")
SedsResult dispatch_tx_queue_timeout(uint32_t timeout_ms)
{
    if (!g_router.r)
    {
        if (init_telemetry_router() != SEDS_OK) return SEDS_ERR;
    }
    return seds_router_process_tx_queue_with_timeout(
        g_router.r, /* router */
        timeout_ms /* timeout in ms */
    );
}

// Pump RX for up to `timeout_ms`
SedsResult process_rx_queue_timeout(uint32_t timeout_ms)
{
    if (!g_router.r)
    {
        if (init_telemetry_router() != SEDS_OK) return SEDS_ERR;
    }
    return seds_router_process_rx_queue_with_timeout(
        g_router.r,
        timeout_ms
    );
}


SedsResult process_all_queues_timeout(uint32_t timeout_ms)
{
    if (!g_router.r)
    {
        if (init_telemetry_router() != SEDS_OK) return SEDS_ERR;
    }
    return seds_router_process_all_queues_with_timeout(
        g_router.r,
        timeout_ms
    );
}
