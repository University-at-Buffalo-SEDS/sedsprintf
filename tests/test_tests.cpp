#include <gtest/gtest.h>
#include <cstring>
#include <memory>
#include <sstream>
#include <iomanip>

#include "serialize.h"
#include "telemetry_router.hpp"

// ========================== helpers ==========================

// make a managed payload by copying from a C array
template<typename T, size_t N>
static std::shared_ptr<const void> make_payload_copy(const T (& arr)[N])
{
    const size_t bytes = sizeof(T) * N;
    const std::shared_ptr<uint8_t[]> block(new uint8_t[bytes], std::default_delete<uint8_t[]>());
    std::memcpy(block.get(), arr, bytes);
    // alias as shared_ptr<const void> to the same allocation
    return std::shared_ptr<const void>(block, block.get());
}

static ConstPacketPtr to_const(const PacketPtr & p)
{
    return std::const_pointer_cast<const telemetry_packet_t>(p);
}

// ====================== setup for router testing ============================
static uint8_t sd_card_called = 0;
static uint8_t transmit_called = 0;


// Smart-pointer packets to capture results
static auto sd_card_data = std::make_shared<telemetry_packet_t>();
static auto transmit_data = std::make_shared<telemetry_packet_t>();

// SD card receive handler (now takes shared_ptr<telemetry_packet_t>)
static SEDSPRINTF_STATUS sd_card_handler(std::shared_ptr<telemetry_packet_t> packet)
{
    sd_card_called = 1;
    // share endpoint list + payload into our capture packet
    return sedsprintf::copy_telemetry_packet(sd_card_data, packet);
}

// Transmit helper (now takes shared_ptr<serialized_buffer_t>)
static SEDSPRINTF_STATUS transmit_helper(const std::shared_ptr<serialized_buffer_t> & serialized_buffer)
{
    transmit_called = 1;

    const auto packet = deserialize_packet(serialized_buffer);
    if (!packet) return SEDSPRINTF_ERROR;

    if (sedsprintf::copy_telemetry_packet(transmit_data, packet) != SEDSPRINTF_OK)
        return SEDSPRINTF_ERROR;

    if (sedsprintf::validate_telemetry_packet(to_const(packet)) != SEDSPRINTF_OK)
        return SEDSPRINTF_ERROR;

    return SEDSPRINTF_OK;
}

// Board config for testing (one local endpoint: SD_CARD -> sd_card_handler)
static board_config_t make_test_board_config()
{
    constexpr size_t N = 1;
    auto up = std::make_unique<data_endpoint_handler_t[]>(N);
    up[0] = data_endpoint_handler_t{SD_CARD, sd_card_handler};
    std::shared_ptr<const data_endpoint_handler_t[]> sp(up.release(),
                                                        std::default_delete<const data_endpoint_handler_t[]>());
    return board_config_t{sp, N};
}

// =========================== TESTS ===========================

TEST(TelemetryRouterTest, HandlesDataFlow)
{
    // prepare capture packets (types set; payload will be shared into them)
    *sd_card_data = telemetry_packet_t{message_type[GPS_DATA], 0, nullptr};
    *transmit_data = telemetry_packet_t{message_type[GPS_DATA], 0, nullptr};

    const board_config_t cfg = make_test_board_config();
    const sedsprintf router(transmit_helper, cfg);

    constexpr float data[message_elements[GPS_DATA]] = {
        5.214141324324f, 3.1342143243214132f, 1.123123123123f
    };

    sd_card_called = 0;
    transmit_called = 0;

    // Log using convenience that copies raw -> managed payload
    ASSERT_EQ(router.log<float>(message_type[GPS_DATA],
                  data, message_elements[GPS_DATA]),
              SEDSPRINTF_OK);

    ASSERT_EQ(sd_card_called, 1);
    ASSERT_EQ(transmit_called, 1);

    // Validate packets
    ASSERT_EQ(sedsprintf::validate_telemetry_packet(to_const(sd_card_data)), SEDSPRINTF_OK);
    ASSERT_EQ(sedsprintf::validate_telemetry_packet(to_const(transmit_data)), SEDSPRINTF_OK);

    ASSERT_EQ(sd_card_data->message_type.type, transmit_data->message_type.type);
    ASSERT_EQ(sd_card_data->timestamp, transmit_data->timestamp);
    ASSERT_TRUE(sd_card_data->data);
    ASSERT_TRUE(transmit_data->data);

    // Compare payload bytes
    ASSERT_EQ(
        std::memcmp(sd_card_data->data.get(),
            transmit_data->data.get(),
            sd_card_data->message_type.data_size),
        0);
}

TEST(SerializationTest, HandlesSerializationAndDeserialization)
{
    // Build a managed packet with payload
    constexpr float data[message_elements[GPS_DATA]] = {
        5.214141324324f, 3.1342143243214132f, 1.123123123123f
    };

    const auto test_packet = std::make_shared<telemetry_packet_t>();
    test_packet->message_type = message_type[GPS_DATA];
    test_packet->timestamp = 0;
    test_packet->data = make_payload_copy(data); // managed payload

    const size_t size = get_packet_size(*test_packet);
    auto serialized = make_serialized_buffer(size);

    ASSERT_TRUE(serialized);
    ASSERT_EQ(serialized->size, size);

    ASSERT_EQ(serialize_packet(test_packet, serialized), SEDSPRINTF_OK);

    // sanity on buffer
    ASSERT_TRUE(serialized->data);
    ASSERT_EQ(serialized->size, size);

    auto deserialized = deserialize_packet(serialized);
    ASSERT_TRUE(deserialized);

    ASSERT_EQ(sedsprintf::validate_telemetry_packet(to_const(deserialized)), SEDSPRINTF_OK);

    // header fields
    EXPECT_EQ(deserialized->message_type.type, test_packet->message_type.type);
    EXPECT_EQ(deserialized->message_type.data_size, test_packet->message_type.data_size);
    EXPECT_EQ(deserialized->timestamp, test_packet->timestamp);

    // payload bytes
    ASSERT_TRUE(deserialized->data);
    EXPECT_EQ(
        std::memcmp(deserialized->data.get(),
            test_packet->data.get(),
            test_packet->message_type.data_size),
        0);

    // extract into local arrays
    float received[message_elements[GPS_DATA]]{};
    ASSERT_EQ(sedsprintf::get_data_f32(to_const(deserialized),
                  received,
                  message_elements[GPS_DATA]),
              SEDSPRINTF_OK);

    float local_copy[message_elements[GPS_DATA]]{};
    ASSERT_EQ(sedsprintf::get_data_f32(to_const(test_packet),
                  local_copy,
                  message_elements[GPS_DATA]),
              SEDSPRINTF_OK);

    EXPECT_EQ(local_copy[0], received[0]);
    EXPECT_EQ(local_copy[1], received[1]);
    EXPECT_EQ(local_copy[2], received[2]);
    EXPECT_EQ(test_packet->message_type.data_size, sizeof(data));
}

TEST(HeaderToStringTest, ToStringWorks)
{
    constexpr float data[message_elements[GPS_DATA]] = {
        5.214141324324f, 3.1342143243214132f, 1.123123123123f
    };

    const auto packet = std::make_shared<telemetry_packet_t>();
    packet->message_type = message_type[GPS_DATA];
    packet->timestamp = 0;
    packet->data = make_payload_copy(data);

    const std::string header_str = sedsprintf::telemetry_packet_metadata_to_string(to_const(packet));
    const std::string expected_str = "Type: GPS_DATA, Size: 12, Endpoints: [SD_CARD, RADIO], Timestamp: 0";
    EXPECT_EQ(header_str, expected_str);
}

TEST(PacketToStringTest, ToStringWorks)
{
    constexpr float data[message_elements[GPS_DATA]] = {
        5.214141324324f, 3.134584235724385843295243214324321f, 1.123123123123f
    };

    const auto packet = std::make_shared<telemetry_packet_t>();
    packet->message_type = message_type[GPS_DATA];
    packet->timestamp = 0;
    packet->data = make_payload_copy(data);

    const std::string packet_string = sedsprintf::packet_to_string(to_const(packet));

    std::ostringstream expected;
    expected.setf(std::ios::fixed, std::ios::floatfield);
    expected << "Type: GPS_DATA, Size: 12, Endpoints: [SD_CARD, RADIO], Timestamp: 0, Data: ";
    expected << std::setprecision(MAX_PRECISION)
            << data[0] << ", " << data[1] << ", " << data[2];

    EXPECT_EQ(packet_string, expected.str());
}
