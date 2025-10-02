#ifndef SEDSPRINTF_TELEMETRY_PACKET_H
#define SEDSPRINTF_TELEMETRY_PACKET_H

#include "enum_setup.h"
#include <cstddef>
#include <ctime>
#include <memory>

typedef enum
{
    SEDSPRINTF_OK,
    SEDSPRINTF_ERROR,
} SEDSPRINTF_STATUS;

typedef struct
{
    data_type_t type;
    size_t data_size;
    // Managed array of endpoints + count
    std::shared_ptr<const data_endpoint_t[]> endpoints;
    size_t num_endpoints;
} message_type_t;

// Payload-bearing packet
typedef struct
{
    message_type_t message_type;
    std::time_t timestamp;
    // Managed ownership for payload (use the correct element type when creating it)
    std::shared_ptr<const void> data;
} telemetry_packet_t;

// Handlers now take/return smart-pointer-managed packets
typedef SEDSPRINTF_STATUS (* receive_helper_t)(std::shared_ptr<telemetry_packet_t> packet);

typedef struct
{
    data_endpoint_t local_endpoint;
    receive_helper_t receive_handler; // nullptr if unused
} data_endpoint_handler_t;

// Board-wide configuration: managed array of handlers
typedef struct
{
    std::shared_ptr<const data_endpoint_handler_t[]> local_data_endpoints;
    size_t num_local_endpoints;
} board_config_t;

#endif // SEDSPRINTF_TELEMETRY_PACKET_H
