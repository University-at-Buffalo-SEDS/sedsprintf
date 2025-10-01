#include "telemetry_router.hpp"


sedsprintf::sedsprintf(transmit_helper_t * transmit_helpers, size_t num_helpers)
{
    this->cfg.transmit_helpers = transmit_helpers;
    this->cfg.num_helpers = num_helpers;
}

SEDSPRINTF_STATUS sedsprintf::log(message_type_t type, void * data) const
{
    telemetry_packet_t packet{};
    packet.message_type = type;
    packet.data = data;
    packet.timestamp = std::time(nullptr);
    // For simplicity, we send to all endpoints defined in board_config
    SEDSPRINTF_STATUS status = transmit(&packet);
    return status;
}

SEDSPRINTF_STATUS sedsprintf::transmit(telemetry_packet_t * packet) const
{
    if (packet->message_type.num_endpoints == 0)
        return SEDSPRINTF_OK;
    for (size_t i = 0; i < packet->message_type.num_endpoints; i++)
    {
        for (size_t j = 0; j < cfg.num_helpers; j++)
        {
            if (cfg.transmit_helpers[j] != nullptr &&
                board_config.data_endpoints[j].endpoint != packet->message_type.endpoints[i])
            {
                if (cfg.transmit_helpers[j](packet) != SEDSPRINTF_OK)
                {
                    return SEDSPRINTF_ERROR;
                }
            }
        }
        for (size_t j = 0; j < board_config.num_endpoints; j++)
        {
            if (board_config.data_endpoints[j].receive_handler != nullptr &&
                board_config.data_endpoints[j].endpoint == packet->message_type.endpoints[i])
            {
                if (board_config.data_endpoints[j].receive_handler() != SEDSPRINTF_OK)
                {
                    return SEDSPRINTF_ERROR;
                }
            }
        }
    }
    return SEDSPRINTF_OK;
}

SEDSPRINTF_STATUS sedsprintf::receive(const telemetry_packet_t * packet)
{
    if (packet->message_type.num_endpoints == 0)
        return SEDSPRINTF_OK;
    for (size_t i = 0; i < packet->message_type.num_endpoints; i++)
    {
        for (size_t j = 0; j < board_config.num_endpoints; j++)
        {
            if (board_config.data_endpoints[j].receive_handler != nullptr &&
                board_config.data_endpoints[j].endpoint == packet->message_type.endpoints[i])
            {
                if (board_config.data_endpoints[j].receive_handler() != SEDSPRINTF_OK)
                {
                    return SEDSPRINTF_ERROR;
                }
            }
        }
    }
    return SEDSPRINTF_OK;
}
