#include "telemetry_router.hpp"


sedsprintf::sedsprintf(transmit_helper_t *transmit_helpers, size_t num_helpers)
{
    this->cfg.transmit_helpers = transmit_helpers;
    this->cfg.num_helpers = num_helpers;
}


SEDSPRINTF_STATUS sedsprintf::log(message_type_t type, void *data)
{
    telemetry_packet_t packet{};
    packet.message_type = type;
    packet.data = data;
    packet.timestamp = std::time(nullptr);
    // For simplicity, we send to all endpoints defined in board_config
    SEDSPRINTF_STATUS status = transmit(&packet);
    return status;
}

SEDSPRINTF_STATUS sedsprintf::transmit(telemetry_packet_t *packet)
{
    if (packet->message_type.num_endpoints == 0)
        return OK;
    for (size_t i = 0; i < packet->message_type.num_endpoints; i++)
    {
        for (size_t j = 0; j < cfg.num_helpers; j++)
        {
            if (transmit(packet) != OK)
            {
                return ERROR;
            }
        }
    }
    return OK;

}