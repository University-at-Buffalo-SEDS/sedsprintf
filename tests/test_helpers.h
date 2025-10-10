#pragma once

#include "telemetry_packet.h"
#include "telemetry_router.hpp"
#include <memory>

// Creates a deterministic telemetry packet
std::shared_ptr<telemetry_packet_t> fake_telemetry_packet();

// Performs element-by-element comparison of two packets. To be used in tests.
void compare_packets(const ConstPacketPtr &p1, const ConstPacketPtr &p2);