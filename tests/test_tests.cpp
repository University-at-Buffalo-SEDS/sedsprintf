// reimpl_tests.cpp
#include <gtest/gtest.h>
#include "router.hpp"
#include "serialize.hpp"
#include "telemetry_packet.hpp"
#include "config.hpp"
#include "error.hpp"
#include "result.hpp"
#include <atomic>
#include <mutex>
#include "helpers.cpp"

using namespace seds;
using testhelpers::get_handler;
using testhelpers::get_sd_card_handler;
using testhelpers::handle_errors;

TEST(NamesTables, MessageTableCountMatchesEnum) {
#ifdef SEDS_HAS_MESSAGE_TABLES
    EXPECT_EQ(MESSAGE_ELEMENTS.size(), static_cast<size_t>(DataType::_COUNT));
#else
    GTEST_SKIP() << "MESSAGE_ELEMENTS not wired in C++ build.";
#endif
}


static std::vector<uint8_t> f32_le(const std::vector<float>& v) {
    std::vector<uint8_t> out;
    out.reserve(v.size() * 4);
    for (float f : v) {
        static_assert(sizeof(float)==4, "float must be 4 bytes");
        uint32_t le;
        std::memcpy(&le, &f, 4);              // copy float bits
        // ensure little-endian order explicitly
        out.push_back(uint8_t(le & 0xFF));
        out.push_back(uint8_t((le >> 8) & 0xFF));
        out.push_back(uint8_t((le >> 16) & 0xFF));
        out.push_back(uint8_t((le >> 24) & 0xFF));
    }
    return out;
}

TEST(Serialize, RoundtripGps) {
    std::vector<DataEndpoint> endpoints{DataEndpoint::SD_CARD, DataEndpoint::RADIO};
    auto pkt_r = TelemetryPacket::from_f32(DataType::GPS_DATA,
                          endpoints,
                          0,
                          std::vector<float>{5.2141414f, 3.1342144f, 1.1231232f});
    ASSERT_FALSE(pkt_r.is_err());
    auto pkt = pkt_r.value();

    ASSERT_TRUE(pkt.validate().is_ok());

    auto ser_r = Serializer::serialize(pkt);
    ASSERT_FALSE(ser_r.is_err());
    const auto& bytes = ser_r.value();

    auto rpkt_r = Serializer::deserialize(bytes);
    ASSERT_FALSE(rpkt_r.is_err());
    const auto& rpkt = rpkt_r.value();
    ASSERT_TRUE(rpkt.validate().is_ok());

    EXPECT_EQ(rpkt.ty, pkt.ty);
    EXPECT_EQ(rpkt.data_size, pkt.data_size);
    EXPECT_EQ(rpkt.timestamp, pkt.timestamp);
    EXPECT_EQ(*rpkt.endpoints, *pkt.endpoints);
    EXPECT_EQ(*rpkt.payload, *pkt.payload);
}

TEST(Formatting, HeaderMatches) {
    const std::vector endpoints{DataEndpoint::SD_CARD, DataEndpoint::RADIO};
    auto pkt_r = TelemetryPacket::from_f32(DataType::GPS_DATA,
                          endpoints,
                          0,
                          std::vector{1.0f, 2.0f, 3.0f});
    ASSERT_FALSE(pkt_r.is_err());
    auto s = pkt_r.value().header_string();
    // adjust expected "Sender" if your C++ sender differs
    EXPECT_EQ(s, "Type: GPS_DATA, Size: 12, Sender: TEST_PLATFORM, Endpoints: [SD_CARD, RADIO], Timestamp: 0");
}

TEST(Router, SendsAndReceives) {
    // capture tx
    auto tx_seen = std::make_shared<std::optional<TelemetryPacket>>(std::nullopt);
    auto tx_seen_m = std::make_shared<std::mutex>();
    Router::TxFn tx = [tx_seen, tx_seen_m](const std::vector<uint8_t>& bytes) -> Result<void, ErrorInfo> {
        auto rpkt_r = Serializer::deserialize(bytes);
        if (rpkt_r.is_err()) return Result<void, ErrorInfo>::err(rpkt_r.error());
        std::lock_guard<std::mutex> lk(*tx_seen_m);
        *tx_seen = rpkt_r.value();
        return Result<void, ErrorInfo>::ok();
    };

    // local sd handler (decode floats)
    auto mtx = std::make_shared<std::mutex>();
    auto captured = std::make_shared<std::optional<std::pair<DataType, std::vector<float>>>>(std::nullopt);
    EndpointHandler sd = get_sd_card_handler(mtx, captured);

    Router r(tx, BoardConfig({sd}));

    std::vector<float> data{1.f, 2.f, 3.f};
    auto res = r.log(DataType::GPS_DATA, f32_le(data), 0);
    ASSERT_TRUE(res.is_ok());

    // tx saw same type & bytes
    {
        std::lock_guard<std::mutex> lk(*tx_seen_m);
        ASSERT_TRUE(tx_seen->has_value());
        EXPECT_EQ(tx_seen->value().ty, DataType::GPS_DATA);
        EXPECT_EQ(tx_seen->value().payload->size(), 3u * 4u);
    }

    // local handler decoded floats
    {
        std::lock_guard<std::mutex> lk(*mtx);
        ASSERT_TRUE(captured->has_value());
        EXPECT_EQ(captured->value().first, DataType::GPS_DATA);
        EXPECT_EQ(captured->value().second, data);
    }
}
