#ifndef SEDSPRINTF_STRUCT_SETUP_H
#define SEDSPRINTF_STRUCT_SETUP_H

#include <algorithm>
#include <array>
#include <memory>
#include "telemetry_packet.h"

// ---------- helpers ----------
inline size_t endpoint_len(const data_endpoint_t * eps)
{
    size_t n = 0;
    while (eps[n] != NUM_DATA_ENDPOINTS) ++n;
    return n;
}

inline std::pair<std::shared_ptr<const data_endpoint_t[]>, size_t>
make_endpoint_list(const data_endpoint_t * sentinel_list)
{
    const size_t n = endpoint_len(sentinel_list);
    auto up = std::make_unique<data_endpoint_t[]>(n);
    std::copy_n(sentinel_list, n, up.get());
    std::shared_ptr<const data_endpoint_t[]> sp(up.release(),
                                                std::default_delete<const data_endpoint_t[]>());
    return {sp, n};
}

// ---------- USER-EDITABLE lists ----------
static data_endpoint_t gps_eps[] = {SD_CARD, RADIO, NUM_DATA_ENDPOINTS};
static data_endpoint_t imu_eps[] = {SD_CARD, RADIO, NUM_DATA_ENDPOINTS};
static data_endpoint_t batt_eps[] = {SD_CARD, RADIO, NUM_DATA_ENDPOINTS};
static data_endpoint_t sys_eps[] = {SD_CARD, NUM_DATA_ENDPOINTS};

inline constexpr size_t message_elements[NUM_DATA_TYPES] = {
    3, 6, 2, 8
};

inline constexpr size_t message_size[NUM_DATA_TYPES] = {
    sizeof(float) * message_elements[GPS_DATA],
    sizeof(float) * message_elements[IMU_DATA],
    sizeof(float) * message_elements[BATTERY_STATUS],
    sizeof(uint8_t) * message_elements[SYSTEM_STATUS]
};

// Build message_type[] once, safely (no raw new exposed)
inline std::array<message_type_t, NUM_DATA_TYPES> make_message_types()
{
    std::array<message_type_t, NUM_DATA_TYPES> mt{};

    {
        auto [eps, n] = make_endpoint_list(gps_eps);
        mt[GPS_DATA] = {GPS_DATA, message_size[GPS_DATA], eps, n};
    }
    {
        auto [eps, n] = make_endpoint_list(imu_eps);
        mt[IMU_DATA] = {IMU_DATA, message_size[IMU_DATA], eps, n};
    }
    {
        auto [eps, n] = make_endpoint_list(batt_eps);
        mt[BATTERY_STATUS] = {BATTERY_STATUS, message_size[BATTERY_STATUS], eps, n};
    }
    {
        auto [eps, n] = make_endpoint_list(sys_eps);
        mt[SYSTEM_STATUS] = {SYSTEM_STATUS, message_size[SYSTEM_STATUS], eps, n};
    }

    return mt;
}

inline const std::array<message_type_t, NUM_DATA_TYPES> message_type = make_message_types();

// Example board config (fill handlers as needed)
inline const board_config_t board_config = []
{
    constexpr size_t N = 2;
    auto up = std::make_unique<data_endpoint_handler_t[]>(N);
    up[0] = data_endpoint_handler_t{SD_CARD, nullptr};
    up[1] = data_endpoint_handler_t{RADIO, nullptr};
    std::shared_ptr<const data_endpoint_handler_t[]> sp(up.release(),
                                                        std::default_delete<const data_endpoint_handler_t[]>());
    return board_config_t{sp, N};
}();

#endif // SEDSPRINTF_STRUCT_SETUP_H
