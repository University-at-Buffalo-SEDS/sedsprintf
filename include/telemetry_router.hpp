#ifndef SEDSPRINTF_LIBRARY_H
#define SEDSPRINTF_LIBRARY_H
#include "serialize.h"
#include "struct_setup.h"

typedef SEDSPRINTF_STATUS (* transmit_helper_t)(serialized_buffer_t * serialized_packet);

typedef struct
{
    transmit_helper_t * transmit_helpers;
    size_t num_helpers;
    board_config_t board_config;
} SEDSPRINTF_CONFIG;

class sedsprintf
{
public:
    sedsprintf(transmit_helper_t transmit_helpers[], size_t num_helpers, board_config_t config);

    SEDSPRINTF_STATUS log(message_type_t, void * data) const;

    static SEDSPRINTF_STATUS copy_telemetry_packet(telemetry_packet_t * dest, const telemetry_packet_t * src);

private:
    SEDSPRINTF_STATUS transmit(telemetry_packet_t * packet) const;

    SEDSPRINTF_STATUS receive(const serialized_buffer_t * serialized_buffer) const;

    SEDSPRINTF_CONFIG cfg{};
};

#endif // SEDSPRINTF_LIBRARY_H
