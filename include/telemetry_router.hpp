#ifndef SEDSPRINTF_LIBRARY_H
#define SEDSPRINTF_LIBRARY_H
#include "struct_setup.h"

typedef struct
{
    transmit_helper_t * transmit_helpers;
    size_t num_helpers;
} SEDSPRINTF_CONFIG;

class sedsprintf
{
public:
    sedsprintf(transmit_helper_t * transmit_helpers, size_t num_helpers);

    SEDSPRINTF_STATUS log(message_type_t, void * data) const;

private:
    SEDSPRINTF_STATUS transmit(telemetry_packet_t * packet) const;

    static SEDSPRINTF_STATUS receive(const telemetry_packet_t * packet);

    SEDSPRINTF_CONFIG cfg{};
};

#endif // SEDSPRINTF_LIBRARY_H
