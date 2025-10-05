//
// 2025 Fuad Veliev <fuad@grrlz.net>
//

#include "enum_setup.h"
#include "telemetry_packet.h"
#include "telemetry_router.hpp"
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <sys/types.h>

// Assemble fake struct stacking doll
// TODO Have someone review these shared pointer declarations
static std::shared_ptr<const telemetry_packet_t> fake_telemetry_packet() {
    telemetry_packet_t p;

    constexpr uint8_t data[message_elements[GPS_DATA]] = {
        0x13, 0x21, 0x34
    };

    // Header
    p.message_type.type = GPS_DATA;
    p.message_type.data_size = sizeof(data);
    p.message_type.endpoints = std::shared_ptr<const data_endpoint_t[]>(
        new data_endpoint_t[2] {
            SD_CARD,
            RADIO
        }
    );
    p.message_type.num_endpoints = 2;
    p.timestamp = static_cast<time_t>(1123581321);

    // Payload

    /* If I didn't mess up, this lambda delete function should receive a pointer to the first
     * element of a byte array, casted back from a void pointer (which is used by struct).
     * N.B. Don't take my word on this :) */
    auto payload = std::shared_ptr<const void>(
        static_cast<const void *>(new uint8_t[sizeof(data)]),
        [](const void *k) {
            delete[] static_cast<const uint8_t *>(k);
        }
    );

    // The same cast from void pointer, but also remove const before passing to memcpy().
    uint8_t *raw_ptr = const_cast<uint8_t *>(static_cast<const uint8_t *>(payload.get()));
    std::memcpy(raw_ptr, data, sizeof(data));
    p.data = payload;
    
    return std::make_shared<const telemetry_packet_t>(p);
}

TEST(Converters, PacketHexToString) {
    std::string expect;

    expect = "ERROR: null packet or data";
    std::string invalid = sedsprintf::packet_to_hex_string(nullptr, nullptr, 0);
    ASSERT_EQ(expect, invalid);

    expect = "Type: GPS_DATA, Size: 3, Endpoints: [SD_CARD, RADIO], Timestamp: 1123581321"
                ", Payload (hex): 0x13 0x21 0x34";
    auto fake_packet = fake_telemetry_packet();
    std::string wrong_explicit_size = sedsprintf::packet_to_hex_string(fake_packet, fake_packet->data, 0);
    ASSERT_EQ(expect, wrong_explicit_size);
}