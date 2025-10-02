#include <gtest/gtest.h>
#include <cstring>

#include "serialize.h"
#include "telemetry_router.hpp"
//==========================setup for router testing=================================
uint8_t sd_card_called = 0;
uint8_t transmit_called = 0;

float sd_buff[message_size[GPS_DATA]]; // use the existing structures to define the buffers
float tx_buff[message_size[GPS_DATA]]; // use the existing structures to define the buffers

telemetry_packet_t sd_card_data = {
    .message_type = message_type[GPS_DATA],
    .timestamp = 0,
    .data = sd_buff // buffer must already exist and be properly sized
};
telemetry_packet_t transmit_data = {
    .message_type = message_type[GPS_DATA],
    .timestamp = 0,
    .data = tx_buff // buffer must already exist and be properly sized
};
// SD card receive handler (in practice, this would write the data to the sd card, it would also probably utilize the to_string methods to format the data.)
SEDSPRINTF_STATUS sd_card_handler(telemetry_packet_t * packet)
{
    sd_card_called = 1;
    sedsprintf::copy_telemetry_packet(&sd_card_data, packet);
    return SEDSPRINTF_OK;
}

// Transmit helper (in practice, this would send the data over the bus of our choosing, for the foreseeable future this would be the can bus)
SEDSPRINTF_STATUS transmit_helper(serialized_buffer_t * serialized_buffer)
{
    transmit_called = 1;
    telemetry_packet_t packet = deserialize_packet(serialized_buffer);
    sedsprintf::copy_telemetry_packet(&transmit_data, &packet);
    if (sedsprintf::validate_telemetry_packet(&packet) != SEDSPRINTF_OK)
    {
        return SEDSPRINTF_ERROR;
    }
    return SEDSPRINTF_OK;
}

// Board config for testing (has sd card only)
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
    // setup the transmit helpers (if multiple buses need to be sent over, have the helper call multiple other functions.)
    const sedsprintf router = sedsprintf(transmit_helper, board_config); // create the router
    float data[message_elements[GPS_DATA]] = {5.214141324324f, 3.1342143243214132f, 1.123123123123f}; //fake data

    sd_card_called = 0;
    transmit_called = 0;
    router.log(message_type[GPS_DATA], data);

    ASSERT_EQ(sd_card_called, 1);
    ASSERT_EQ(transmit_called, 1);
    ASSERT_EQ(sedsprintf::validate_telemetry_packet(&sd_card_data), SEDSPRINTF_OK);
    ASSERT_EQ(sedsprintf::validate_telemetry_packet(&transmit_data), SEDSPRINTF_OK);
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

//this function is an example of how to use a switch case to make a global tostring that can be used on any packet and will return the string for it.
std::string example_tostring_for_any_packet(const telemetry_packet_t * packet)
{
    switch (packet->message_type.type)
    {
        case GPS_DATA:
        case IMU_DATA:
        case BATTERY_STATUS:
            return sedsprintf::packet_to_string<float>(packet);
        case SYSTEM_STATUS:
            return sedsprintf::packet_to_string<uint8_t>(packet);

        case NUM_DATA_TYPES:
            //we should never ever ever hit this case because if the type is this then something was done very, very wrong
            return
                    "Your data type is set to NUM_DATA_TYPES. This should never happen and is most likely unrecoverable. "
                    "Please fix your code";
    }
    return "Not sure how you got here, but something went catastrophically wrong";
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

    SEDSPRINTF_STATUS status = serialize_packet(&test_packet, &serialized);

    ASSERT_EQ(status, SEDSPRINTF_OK);

    // Assert basic buffer sanity
    ASSERT_NE(serialized.buffer, nullptr);
    ASSERT_EQ(serialized.size, serialized.size);

    telemetry_packet_t deserialized = deserialize_packet(&serialized);

    ASSERT_EQ(sedsprintf::validate_telemetry_packet(&deserialized), SEDSPRINTF_OK);

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

//test that the headers tostring works correcly
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

//check that the tostring for the headers and data works correctly
TEST(PacketToStringTest, ToStringWorks)
{
    float data[message_elements[GPS_DATA]] = {5.214141324324f, 3.134214324321f, 1.123123123123f};

    telemetry_packet_t test_packet = {
        .message_type = message_type[GPS_DATA], // must have .data_size == sizeof(data)
        .timestamp = 0,
        .data = data
    };
    const std::string packet_string = example_tostring_for_any_packet(&test_packet);
    std::ostringstream expected;
    expected.setf(std::ios::fixed, std::ios::floatfield);
    expected << "Type: GPS_DATA, Size: 12, Endpoints: [SD_CARD, RADIO], Timestamp: 0, Data: ";
    expected << std::setprecision(MAX_PRECISION)
            << data[0] << ", " << data[1] << ", " << data[2];


    EXPECT_EQ(packet_string, expected.str());
}
