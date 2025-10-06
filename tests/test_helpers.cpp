//
// 2025 Fuad Veliev <fuad@grrlz.net>
//

#include "enum_setup.h"
#include "telemetry_packet.h"
#include "telemetry_router.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <memory>
#include <string>

// Creates a deterministic telemetry packet
static std::shared_ptr<telemetry_packet_t> fake_telemetry_packet() {
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
    // Cast to void pointer as used by structs, when passing to deleter cast back to our type
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
    
    return std::make_shared<telemetry_packet_t>(p);
}

// Performs element-by-element comparison of two packets. To be used in tests.
static void compare_packets(const ConstPacketPtr &p1, const ConstPacketPtr &p2) {
    EXPECT_EQ(p1->timestamp, p2->timestamp);

    ASSERT_EQ(p1->message_type.type, p2->message_type.type);
    ASSERT_EQ(p1->message_type.data_size, p2->message_type.data_size);
    ASSERT_EQ(p1->message_type.num_endpoints, p2->message_type.num_endpoints);

    if (p1->message_type.endpoints != p2->message_type.endpoints) {
        for (size_t i = 0; i < p1->message_type.num_endpoints; i++) {
            ASSERT_EQ(p1->message_type.endpoints[i], p2->message_type.endpoints[i]);
        }
    }

    /* This logic is effective for deterministic (fake) packets used in tests.
     * Real packets may differ to a negligible extent, making memcmp() produce undesired results. */
    if (p1->data != p2->data) {
        int res = std::memcmp(p1->data.get(), p2->data.get(), p1->message_type.data_size);
        ASSERT_EQ(0, res);
    }

    /* More tolerant (but boilerplate) version
     * Casts from the void pointer according to inferred type. */
    
    // if (p1->data == p2->data) return;

    // auto [t, s] = sedsprintf::infer_element_type(p1, ElementType::Auto);
    // size_t num_elems = p1->message_type.data_size / s;
    // switch (t) {
    //     case ElementType::F64:
    //     {
    //         auto val1 = static_cast<const double *>(p1->data.get());
    //         auto val2 = static_cast<const double *>(p2->data.get());
    //         for (size_t i = 0; i < num_elems; i++)
    //             ASSERT_DOUBLE_EQ(val1[i], val2[i]);
    //         break;
    //     }
    //     case ElementType::F32:
    //     {
    //         auto val1 = static_cast<const float *>(p1->data.get());
    //         auto val2 = static_cast<const float *>(p2->data.get());
    //         for (size_t i = 0; i < num_elems; i++)
    //             ASSERT_FLOAT_EQ(val1[i], val2[i]);
    //         break;
    //     }
    //     case ElementType::U32:
    //     {
    //         auto val1 = static_cast<const uint32_t *>(p1->data.get());
    //         auto val2 = static_cast<const uint32_t *>(p2->data.get());
    //         for (size_t i = 0; i < num_elems; i++)
    //             ASSERT_EQ(val1[i], val2[i]);
    //         break;
    //     }
    //     case ElementType::U16:
    //     {
    //         auto val1 = static_cast<const uint16_t *>(p1->data.get());
    //         auto val2 = static_cast<const uint16_t *>(p2->data.get());
    //         for (size_t i = 0; i < num_elems; i++)
    //             ASSERT_EQ(val1[i], val2[i]);
    //         break;
    //     }
    //     default:
    //     {
    //         auto val1 = static_cast<const uint8_t *>(p1->data.get());
    //         auto val2 = static_cast<const uint8_t *>(p2->data.get());
    //         for (size_t i = 0; i < num_elems; i++)
    //             ASSERT_EQ(val1[i], val2[i]);
    //     }
    // }
}

TEST(Helpers, PacketHexToString) {
    std::string expect, res;
    const auto fake_packet = fake_telemetry_packet();

    // Passing nullptr
    expect = "ERROR: null packet or data";
    res = sedsprintf::packet_to_hex_string(nullptr, nullptr, 0);
    ASSERT_EQ(expect, res);

    // Passing normal packet
    expect = "Type: GPS_DATA, Size: 3, Endpoints: [SD_CARD, RADIO], Timestamp: 1123581321"
                ", Payload (hex): 0x13 0x21 0x34";
    res = sedsprintf::packet_to_hex_string(fake_packet, fake_packet->data, 0);
    ASSERT_EQ(expect, res);
}

TEST(Helpers, CopyTelemetryPacket) {
    SEDSPRINTF_STATUS st;
    const auto src = fake_telemetry_packet();

    // Passing nullptr for destination 
    st = sedsprintf::copy_telemetry_packet(nullptr, src);
    ASSERT_EQ(SEDSPRINTF_ERROR, st);

    // Passing the same pointer for source and destination
    st = sedsprintf::copy_telemetry_packet(src, src);
    ASSERT_EQ(SEDSPRINTF_OK, st);

    // Passing two distinct non-null pointers
    PacketPtr dest = std::make_shared<telemetry_packet_t>();
    st = sedsprintf::copy_telemetry_packet(dest, src);
    EXPECT_EQ(SEDSPRINTF_OK, st);
    compare_packets(dest, src);
}