#include <gtest/gtest.h>
#include <cstring>

#include "serialize.h"
#include "telemetry_router.hpp"
//==========================setup for router testing=================================
int sd_card_called = 0;
int transmit_called = 0;

int sd_buff[message_size[GPS_DATA]];
int tx_buff[message_size[GPS_DATA]];

telemetry_packet_t sd_card_data = {
    .message_type = message_type[GPS_DATA], // must have .data_size == sizeof(data)
    .timestamp = 0,
    .data = sd_buff
};
telemetry_packet_t transmit_data = {
    .message_type = message_type[GPS_DATA], // must have .data_size == sizeof(data)
    .timestamp = 0,
    .data = tx_buff
};

SEDSPRINTF_STATUS sd_card_handler(telemetry_packet_t * buffer)
{
    sedsprintf::copy_telemetry_packet(&sd_card_data, buffer);
    sd_card_called = 1;
    return SEDSPRINTF_OK;
}


SEDSPRINTF_STATUS transmit_helper(serialized_buffer_t * data)
{
    telemetry_packet_t packet = deserialize_packet(data);
    sedsprintf::copy_telemetry_packet(&transmit_data, &packet);
    transmit_called = 1;
    return SEDSPRINTF_OK;
}


static board_config_t board_config = {
    // Define the data endpoints available on this board
    .local_data_endpoints = (data_endpoint_handler_t[]){
        {SD_CARD, sd_card_handler}, // setup the sd receive handler
    },
    .num_local_endpoints = 1 // Number of endpoints defined above
};
//============================================================================

//========================== TESTS ===========================

//Test that the telemetry router correctly routes data to the sd card and transmission functions and presents data in the right format
TEST(TelemetryRouterTest, HandlesDataFlow)
{
    transmit_helper_t transmit_helpers[] = {transmit_helper};
    // setup the transmit helpers (may be more than one element than one if sending over multiple buses
    const sedsprintf router = sedsprintf(transmit_helpers, 1, board_config); // create the router
    float data[message_elements[GPS_DATA]] = {5.214141324324f, 3.1342143243214132f, 1.123123123123f}; //fake data

    sd_card_called = 0;
    transmit_called = 0;
    router.log(message_type[GPS_DATA], data);

    ASSERT_EQ(sd_card_called, 1);
    ASSERT_EQ(transmit_called, 1);

    ASSERT_EQ(sd_card_data.message_type.type, transmit_data.message_type.type);
    ASSERT_EQ(sd_card_data.timestamp, transmit_data.timestamp);
    ASSERT_NE(sd_card_data.data, nullptr);
    ASSERT_NE(transmit_data.data, nullptr);
    ASSERT_EQ(
        std::memcmp(sd_card_data.data,
            transmit_data.data,
            sd_card_data.message_type.data_size),
        0);
}


// Test that serialization and deserialization work correctly
TEST(SerializationTest, HandlesSerializationAndDeserialization)
{
    float data[message_elements[GPS_DATA]] = {5.214141324324f, 3.1342143243214132f, 1.123123123123f};

    telemetry_packet_t test_packet = {
        .message_type = message_type[GPS_DATA], // must have .data_size == sizeof(data)
        .timestamp = 0,
        .data = data
    };

    size_t size = get_packet_size(&test_packet);
    uint8_t buff[size];
    serialized_buffer_t serialized = create_serialized_buffer(buff, size);
    ASSERT_GT(serialized.size, 0u);

    serialize_packet(&test_packet, &serialized);

    // Assert basic buffer sanity
    ASSERT_NE(serialized.buffer, nullptr);
    ASSERT_EQ(serialized.size, serialized.size);

    telemetry_packet_t deserialized = deserialize_packet(&serialized);

    // Assert header fields
    EXPECT_EQ(deserialized.message_type.type, test_packet.message_type.type);
    EXPECT_EQ(deserialized.message_type.data_size, test_packet.message_type.data_size);
    EXPECT_EQ(deserialized.timestamp, test_packet.timestamp);

    // Assert payload bytes equal
    ASSERT_NE(deserialized.data, nullptr);
    EXPECT_EQ(
        std::memcmp(deserialized.data,
            test_packet.data,
            test_packet.message_type.data_size),
        0);
    float received_data[message_elements[GPS_DATA]];
    sedsprintf::get_data_from_packet(&deserialized, received_data);
    float local_data[message_elements[GPS_DATA]];
    sedsprintf::get_data_from_packet(&test_packet, local_data);
    ASSERT_EQ(local_data[0], received_data[0]);
    ASSERT_EQ(local_data[1], received_data[1]);
    ASSERT_EQ(local_data[2], received_data[2]);
    ASSERT_EQ(test_packet.message_type.data_size, sizeof(data));
}


TEST(HeaderToStringTest, ToStringWorks)
{
    float data[message_elements[GPS_DATA]] = {5.214141324324f, 3.1342143243214132f, 1.123123123123f};

    telemetry_packet_t test_packet = {
        .message_type = message_type[GPS_DATA], // must have .data_size == sizeof(data)
        .timestamp = 0,
        .data = data
    };

    const std::string header_str = sedsprintf::telemetry_packet_metadata_to_string(&test_packet);
    const std::string expected_str = "Type: GPS_DATA, Size: 12, Endpoints: [SD_CARD, RADIO], Timestamp: 0";
    EXPECT_EQ(header_str, expected_str);
}


TEST(PacketToStringTest, ToStringWorks)
{
    float data[message_elements[GPS_DATA]] = {5.214141324324, 3.134214324321, 1.123123123123};

    telemetry_packet_t test_packet = {
        .message_type = message_type[GPS_DATA], // must have .data_size == sizeof(data)
        .timestamp = 0,
        .data = data
    };
    float packet_data[message_elements[GPS_DATA]];
    sedsprintf::get_data_from_packet(&test_packet, packet_data);
    const std::string packet_string = sedsprintf::packet_to_string(&test_packet, packet_data,
                                                                   message_elements[GPS_DATA]);
    std::ostringstream expected;
    expected.setf(std::ios::fixed, std::ios::floatfield);
    expected << "Type: GPS_DATA, Size: 12, Endpoints: [SD_CARD, RADIO], Timestamp: 0, Data: ";
    expected << std::setprecision(MAX_PRECISION)
            << data[0] << ", " << data[1] << ", " << data[2];


    EXPECT_EQ(packet_string, expected.str());
}
