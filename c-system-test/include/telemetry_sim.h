#pragma once
#include "sedsprintf.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

    // Forward declare via typedef, then define the named struct.
    typedef struct SimNode SimNode;

    typedef struct {
        SimNode **nodes;
        size_t count;
        size_t cap;
    } SimBus;

    struct SimNode {
        SedsRouter *r;
        const char *name;
        SimBus *bus;
        int has_radio;

        int has_sdcard;
        // --- NEW: simple stats for tests ---
        unsigned radio_hits;   // times RADIO handler ran
        unsigned sd_hits;      // times SD handler ran

    };

    // Bus API unchanged...
    void bus_init(SimBus *bus);
    void bus_free(SimBus *bus);
    size_t bus_register(SimBus *bus, SimNode *n);
    SedsResult bus_send(SimBus *bus, const SimNode *from, const uint8_t *bytes, size_t len);

    // Node API unchanged...
    SedsResult node_init(SimNode *n, SimBus *bus, const char *name, int radio, int sdcard);
    void node_free(SimNode *n);
    void node_rx(SimNode *n, const uint8_t *bytes, size_t len);
    SedsResult node_log(SimNode *n, SedsDataType dt, const float *data, size_t data_len);

    // Handlers
    SedsResult radio_handler(const SedsPacketView *pkt, void *user);
    SedsResult sdcard_handler(const SedsPacketView *pkt, void *user);
    uint64_t host_now_ms(void * user);

#ifdef __cplusplus
}
#endif
