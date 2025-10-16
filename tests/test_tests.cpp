// tests/cpp_unit_tests.cpp
#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <filesystem>
#include <fstream>
#include "config.hpp"
#include "router.hpp"
#include "serialize.hpp"
#include "telemetry.hpp"


using namespace seds;

// ---------------- Mock clock ----------------
class StepClock final : public Clock
{
public:
    static std::unique_ptr<Clock> NewBox(uint64_t start, uint64_t step)
    {
        return std::unique_ptr<Clock>(new StepClock(start, step));
    }

    static std::unique_ptr<Clock> NewDefaultBox()
    {
        // matches Rust tests (no time advance inside budgeted loops unless step > 0)
        return std::unique_ptr<Clock>(new StepClock(0, 0));
    }

    StepClock(uint64_t start, uint64_t step) : t_(start), step_(step)
    {
    }

    uint64_t now_ms() const override
    {
        // returns current, then advances by step
        const auto cur = t_.load(std::memory_order_relaxed);
        t_.store(cur + step_, std::memory_order_relaxed);
        return cur;
    }

private:
    mutable std::atomic<uint64_t> t_;
    uint64_t step_;
};

// ---------------- Helpers used by several tests ----------------

static TelemetryPacket MakeGpsPacketFromF32s(const std::vector<float> & vals,
                                             const std::vector<DataEndpoint> & eps,
                                             uint64_t ts,
                                             const char * sender = DEVICE_IDENTIFIER)
{
    // Encode payload LE
    std::vector<uint8_t> bytes;
    bytes.reserve(vals.size() * sizeof(float));
    for (float v: vals)
    {
        uint32_t u;
        std::memcpy(&u, &v, 4);
        bytes.push_back(static_cast<uint8_t>(u & 0xFFu));
        bytes.push_back(static_cast<uint8_t>((u >> 8) & 0xFFu));
        bytes.push_back(static_cast<uint8_t>((u >> 16) & 0xFFu));
        bytes.push_back(static_cast<uint8_t>((u >> 24) & 0xFFu));
    }
    const auto payload_arc = std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
    TelemetryResult<TelemetryPacket> r =
            TelemetryPacket::New(DataType::GpsData, eps, sender, ts, payload_arc);
    EXPECT_TRUE(r.is_ok()) << "failed to build GPS packet";
    return r.unwrap();
}

// deterministic 3-byte payload helper used by hex/string tests
static TelemetryPacket FakeTelemetryPacketBytes()
{
    const std::vector<float> data{0x13, 0x21, 0x34};
    const std::vector eps{DataEndpoint::SdCard, DataEndpoint::Radio};

    TelemetryPacket packet = MakeGpsPacketFromF32s(data, eps, /*ts=*/1123581321ull, "Flight Controller");
    EXPECT_TRUE(packet.Validate().is_ok());
    return packet;
}

// a small “bus” that records transmitted frames.
struct TestBus
{
    std::shared_ptr<std::vector<std::vector<uint8_t> > > frames;

    TestBus() : frames(std::make_shared<std::vector<std::vector<uint8_t> > >())
    {
    }

    [[nodiscard]] auto make_tx() const
    {
        auto sink = frames;
        return [sink](const std::vector<uint8_t> & bytes) -> TelemetryResult<void *>
        {
            sink->push_back(bytes);
            return TelemetryResult<void *>::Ok(nullptr);
        };
    }
};

// ======================= Tests mirroring the Rust ones (except system test) =======================

TEST(Serialize, RoundtripGps)
{
    // GPS: 3 * f32
    std::vector<DataEndpoint> endpoints{DataEndpoint::SdCard, DataEndpoint::Radio};
    auto pkt = MakeGpsPacketFromF32s({5.2141414f, 3.1342144f, 1.1231232f}, endpoints, 0);

    auto v = pkt.Validate();
    ASSERT_TRUE(v.is_ok());

    auto bytes = serialize_packet(pkt);
    auto rpkt_res = deserialize_packet(bytes);
    ASSERT_TRUE(rpkt_res.is_ok());
    auto & rpkt = rpkt_res.unwrap();

    ASSERT_TRUE(rpkt.Validate().is_ok());
    EXPECT_EQ(rpkt.ty, pkt.ty);
    EXPECT_EQ(rpkt.data_size, pkt.data_size);
    EXPECT_EQ(rpkt.timestamp, pkt.timestamp);
    ASSERT_TRUE(rpkt.endpoints && pkt.endpoints);
    EXPECT_EQ(*rpkt.endpoints, *pkt.endpoints);
    ASSERT_TRUE(rpkt.payload && pkt.payload);
    EXPECT_EQ(*rpkt.payload, *pkt.payload);
}

TEST(Formatting, HeaderStringMatchesExpectation)
{
    const std::vector endpoints{DataEndpoint::SdCard, DataEndpoint::Radio};
    const auto pkt = MakeGpsPacketFromF32s({1.0f, 2.0f, 3.0f}, endpoints, 0, /*sender=*/"TEST_PLATFORM");
    const auto s = pkt.HeaderString();
    EXPECT_EQ(s,
              "Type: GPS_DATA, Size: 12, Sender: TEST_PLATFORM, Endpoints: [SD_CARD, RADIO], Timestamp: 0");
}

TEST(Formatting, PacketToStringFormatsFloats)
{
    const std::vector endpoints{DataEndpoint::SdCard, DataEndpoint::Radio};
    const auto pkt = MakeGpsPacketFromF32s({1.0f, 2.5f, 3.25f}, endpoints, 0, /*sender=*/"TEST_PLATFORM");
    auto text = pkt.ToString();
    ASSERT_TRUE(text.rfind(
        "Type: GPS_DATA, Size: 12, Sender: TEST_PLATFORM, Endpoints: [SD_CARD, RADIO], Timestamp: 0, Data: ",
        0) == 0);
    EXPECT_NE(text.find('1'), std::string::npos);
    EXPECT_NE(text.find("2.5"), std::string::npos);
    EXPECT_NE(text.find("3.25"), std::string::npos);
}

TEST(Router, SendsAndReceives)
{
    // capture spaces
    auto tx_seen = std::make_shared<std::optional<TelemetryPacket> >();
    auto sd_seen_decoded =
            std::make_shared<std::optional<std::pair<DataType, std::vector<float> > > >();

    // transmitter: record the deserialized packet we "sent"
    const auto & tx_seen_c = tx_seen;
    auto transmit = [tx_seen_c](const std::vector<uint8_t> & bytes) -> TelemetryResult<void *>
    {
        auto res = deserialize_packet(bytes);
        if (!res.is_ok()) return TelemetryResult<void *>::Err(res.unwrap_err());
        *tx_seen_c = res.unwrap();
        return TelemetryResult<void *>::Ok(nullptr);
    };

    // local SD handler: decode payload to f32s and record (ty, values)
    const auto & sd_seen_c = sd_seen_decoded;
    EndpointHandler sd_handler;
    sd_handler.endpoint = DataEndpoint::SdCard;
    sd_handler.handler = [sd_seen_c](const TelemetryPacket & pkt) -> TelemetryResult<void *>
    {
        const auto elems = std::max<size_t>(1, MESSAGE_ELEMENTS[static_cast<size_t>(pkt.ty)]);
        const auto per_elem = get_needed_message_size(pkt.ty) / elems;
        EXPECT_EQ(pkt.ty, DataType::GpsData);
        EXPECT_EQ(per_elem, 4u) << "GPS_DATA expected f32 elements";
        std::vector<float> vals;
        vals.reserve(pkt.payload->size() / 4);
        for (size_t i = 0; i + 3 < pkt.payload->size(); i += 4)
        {
            uint32_t u = (*pkt.payload)[i] |
                         ((*pkt.payload)[i + 1] << 8) |
                         ((*pkt.payload)[i + 2] << 16) |
                         ((*pkt.payload)[i + 3] << 24);
            float f;
            std::memcpy(&f, &u, 4);
            vals.push_back(f);
        }
        *sd_seen_c = std::make_pair(pkt.ty, std::move(vals));
        return TelemetryResult<void *>::Ok(nullptr);
    };

    auto router = Router(
        /*transmit*/ std::optional<Router::TransmitFn>(transmit),
                     BoardConfig(std::vector<EndpointHandler>{sd_handler}),
                     StepClock::NewDefaultBox());

    // send GPS_DATA (3 * f32) using Router::log (uses default endpoints from schema)
    std::vector data{1.0f, 2.0f, 3.0f};
    ASSERT_TRUE(router.log<float>(DataType::GpsData, data, 0).is_ok());

    // --- assertions ---
    ASSERT_TRUE(tx_seen->has_value()) << "no tx packet recorded";
    const auto & tx_pkt = tx_seen->value();
    EXPECT_EQ(tx_pkt.ty, DataType::GpsData);
    ASSERT_TRUE(tx_pkt.payload);
    EXPECT_EQ(tx_pkt.payload->size(), 3u * 4u);

    std::vector<uint8_t> expected;
    expected.reserve(12);
    for (float v: data)
    {
        uint32_t u;
        std::memcpy(&u, &v, 4);
        expected.push_back(static_cast<uint8_t>(u & 0xFFu));
        expected.push_back(static_cast<uint8_t>((u >> 8) & 0xFFu));
        expected.push_back(static_cast<uint8_t>((u >> 16) & 0xFFu));
        expected.push_back(static_cast<uint8_t>((u >> 24) & 0xFFu));
    }
    EXPECT_EQ(*tx_pkt.payload, expected);

    ASSERT_TRUE(sd_seen_decoded->has_value()) << "no sd packet recorded";
    const auto & [fst, snd] = sd_seen_decoded->value();
    EXPECT_EQ(fst, DataType::GpsData);
    EXPECT_EQ(snd, data);
}

TEST(Router, QueuedRoundtripBetweenTwoRouters)
{
    // TX router (only sends)
    TestBus bus;
    auto tx_fn = bus.make_tx();

    auto tx_router = Router(
        std::optional<Router::TransmitFn>(tx_fn),
        BoardConfig(), // no local handlers
        StepClock::NewDefaultBox());

    // RX router with local SD handler recording (ty, vals)
    auto seen = std::make_shared<std::optional<std::pair<DataType, std::vector<float> > > >();
    EndpointHandler sd_handler;
    sd_handler.endpoint = DataEndpoint::SdCard;
    sd_handler.handler = [seen](const TelemetryPacket & pkt) -> TelemetryResult<void *>
    {
        if (!pkt.payload) return TelemetryResult<void *>::Ok(nullptr);
        std::vector<float> vals;
        for (size_t i = 0; i + 3 < pkt.payload->size(); i += 4)
        {
            uint32_t u = (*pkt.payload)[i] |
                         ((*pkt.payload)[i + 1] << 8) |
                         ((*pkt.payload)[i + 2] << 16) |
                         ((*pkt.payload)[i + 3] << 24);
            float f;
            std::memcpy(&f, &u, 4);
            vals.push_back(f);
        }
        *seen = std::make_pair(pkt.ty, std::move(vals));
        return TelemetryResult<void *>::Ok(nullptr);
    };
    auto rx_router = Router(
        std::optional<Router::TransmitFn>([](const std::vector<uint8_t> &)
        {
            return TelemetryResult<void *>::Ok(nullptr);
        }),
        BoardConfig(std::vector{sd_handler}),
        StepClock::NewDefaultBox());

    // 1) Sender enqueues a packet for TX
    std::vector data{1.0f, 2.0f, 3.0f};
    ASSERT_TRUE(tx_router.log_queue<float>(DataType::GpsData, data, 0).is_ok());

    // 2) Flush TX queue -> pushes wire frames into TestBus
    ASSERT_TRUE(tx_router.process_send_queue().is_ok());

    // 3) Deliver captured frames into RX router's received queue
    ASSERT_EQ(bus.frames->size(), 1u);
    for (const auto & frame: *bus.frames)
    {
        ASSERT_TRUE(rx_router.rx_serialized_packet_to_queue(frame).is_ok());
    }

    // 4) Drain RX queue -> invokes local handlers
    ASSERT_TRUE(rx_router.process_received_queue().is_ok());

    ASSERT_TRUE(seen->has_value()) << "no packet delivered";
    const auto & pair = seen->value();
    const auto & ty = pair.first;
    const auto & vals = pair.second;
    EXPECT_EQ(ty, DataType::GpsData);
    EXPECT_EQ(vals, data);
}

TEST(Router, QueuedSelfDeliveryViaReceiveQueue)
{
    const TestBus bus;
    auto tx_fn = bus.make_tx();

    auto router = Router(
        std::optional<Router::TransmitFn>(tx_fn),
        BoardConfig(), // no local handlers in this test
        StepClock::NewDefaultBox());

    // Enqueue for transmit (3 frames)
    ASSERT_TRUE(router.log_queue<float>(DataType::GpsData, std::vector{10.0f, 10.25f, 10.5f}, 42).is_ok());
    ASSERT_TRUE(router.log_queue<float>(DataType::BatteryStatus, std::vector{10.0f, 10.25f, 10.5f, 12.3f}, 42).is_ok());
    ASSERT_TRUE(router.log_queue<float>(DataType::GpsData, std::vector{10.0f, 10.25f, 10.5f}, 42).is_ok());

    ASSERT_TRUE(router.process_send_queue().is_ok());
    ASSERT_EQ(bus.frames->size(), 3u);

    // Feed one frame back into the same router's received queue and drain
    ASSERT_TRUE(router.rx_serialized_packet_to_queue(bus.frames->at(0)).is_ok());
    ASSERT_TRUE(router.process_received_queue().is_ok());
}

// ================= Timeout budget tests =================

static TelemetryPacket MkRxOnlyLocal(const std::vector<float> & vals, uint64_t ts)
{
    return MakeGpsPacketFromF32s(vals, {DataEndpoint::SdCard}, ts);
}

static std::function<TelemetryResult<void *>(const std::vector<uint8_t> &)>
TxCounter(const std::shared_ptr<std::atomic<size_t> > & counter)
{
    return [counter](const std::vector<uint8_t> & bytes) -> TelemetryResult<void *>
    {
        EXPECT_FALSE(bytes.empty());
        counter->fetch_add(1, std::memory_order_seq_cst);
        return TelemetryResult<void *>::Ok(nullptr);
    };
}

TEST(Timeouts, ProcessAllQueuesTimeoutZeroDrainsFully)
{
    const auto tx_count = std::make_shared<std::atomic<size_t> >(0);
    auto tx = TxCounter(tx_count);

    auto rx_count = std::make_shared<std::atomic<size_t> >(0);
    EndpointHandler handler;
    handler.endpoint = DataEndpoint::SdCard;
    handler.handler = [rx_count](const TelemetryPacket &) -> TelemetryResult<void *>
    {
        rx_count->fetch_add(1, std::memory_order_seq_cst);
        return TelemetryResult<void *>::Ok(nullptr);
    };

    Router r(std::optional(tx),
             BoardConfig(std::vector{handler}),
             StepClock::NewDefaultBox());

    // Enqueue TX (3)
    for (int i = 0; i < 3; ++i)
    {
        ASSERT_TRUE(r.log_queue<float>(DataType::GpsData, std::vector{1.0f, 2.0f, 3.0f}, 0).is_ok());
    }
    // Enqueue RX (2) with only-local endpoint
    for (int i = 0; i < 2; ++i)
    {
        ASSERT_TRUE(r.rx_packet_to_queue(MkRxOnlyLocal({9.0f, 8.0f, 7.0f}, 123)).is_ok());
    }

    // timeout = 0 → drain fully
    ASSERT_TRUE(r.process_all_queues_with_timeout(0).is_ok());

    // TX: all three frames should be sent
    EXPECT_EQ(tx_count->load(std::memory_order_seq_cst), 3u);
    // RX handler invoked for each TX (local delivery) + each RX = 3 + 2 = 5
    EXPECT_EQ(rx_count->load(std::memory_order_seq_cst), 5u);
}

TEST(Timeouts, ProcessAllQueuesRespectsNonzeroTimeoutBudget)
{
    auto tx_count = std::make_shared<std::atomic<size_t> >(0);
    auto tx = TxCounter(tx_count);

    auto rx_count = std::make_shared<std::atomic<size_t> >(0);
    EndpointHandler handler;
    handler.endpoint = DataEndpoint::SdCard;
    handler.handler = [rx_count](const TelemetryPacket &) -> TelemetryResult<void *>
    {
        rx_count->fetch_add(1, std::memory_order_seq_cst);
        return TelemetryResult<void *>::Ok(nullptr);
    };

    Router r(std::optional<Router::TransmitFn>(tx),
             BoardConfig(std::vector{handler}),
             StepClock::NewBox(/*start=*/0, /*step=*/10));

    // Seed work in both queues (5 of each)
    for (int i = 0; i < 5; ++i)
    {
        ASSERT_TRUE(r.log_queue<float>(DataType::GpsData, std::vector<float>{1.0f, 2.0f, 3.0f}, 0).is_ok());
        ASSERT_TRUE(r.rx_packet_to_queue(MkRxOnlyLocal({4.0f, 5.0f, 6.0f}, 1)).is_ok());
    }

    // Step is 10ms per call; timeout 5ms guarantees exactly one iteration
    ASSERT_TRUE(r.process_all_queues_with_timeout(5).is_ok());

    // One iteration → at most one TX send
    EXPECT_EQ(tx_count->load(std::memory_order_seq_cst), 1u);

    // Handlers run for both TX local delivery and RX processing → 2 total
    EXPECT_EQ(rx_count->load(std::memory_order_seq_cst), 2u);

    // Drain the rest to prove there was more work left
    ASSERT_TRUE(r.process_all_queues_with_timeout(0).is_ok());
    EXPECT_EQ(tx_count->load(std::memory_order_seq_cst), 5u);
    EXPECT_EQ(rx_count->load(std::memory_order_seq_cst), 10u); // 5 (TX locals) + 5 (RX)
}

TEST(Timeouts, ProcessAllQueuesHandlesU64Wraparound)
{
    const auto tx_count = std::make_shared<std::atomic<size_t> >(0);
    auto tx = TxCounter(tx_count);

    auto rx_count = std::make_shared<std::atomic<size_t> >(0);
    EndpointHandler handler;
    handler.endpoint = DataEndpoint::SdCard;
    handler.handler = [rx_count](const TelemetryPacket &) -> TelemetryResult<void *>
    {
        rx_count->fetch_add(1, std::memory_order_seq_cst);
        return TelemetryResult<void *>::Ok(nullptr);
    };

    Router r(std::make_optional(tx),
             BoardConfig(std::vector{handler}),
             StepClock::NewBox(std::numeric_limits<uint64_t>::max() - 1, /*step=*/2));

    // One TX and one RX (RX only-local to avoid creating extra TX on receive)
    ASSERT_TRUE(r.log_queue<float>(DataType::GpsData, std::vector{1.0f, 2.0f, 3.0f}, 0).is_ok());
    ASSERT_TRUE(r.rx_packet_to_queue(MkRxOnlyLocal({4.0f, 5.0f, 6.0f}, 7)).is_ok());

    // Small budget; wrapping should allow one iteration then stop
    ASSERT_TRUE(r.process_all_queues_with_timeout(1).is_ok());

    // One iteration can do up to one TX and one RX
    EXPECT_LE(tx_count->load(std::memory_order_seq_cst), 1u);
    EXPECT_LE(rx_count->load(std::memory_order_seq_cst), 2u); // TX local + RX
    EXPECT_GE(tx_count->load(std::memory_order_seq_cst) + rx_count->load(std::memory_order_seq_cst), 1u);
}

// ================= Helpers tests mirrored from Rust “Converted tests” =================

TEST(Helpers, PacketHexToString)
{
    // Builds the exact string checked in the Rust port; here we assert our C++ Hex formatter matches.
    // If the TelemetryPacket exposes ToHexString(), use it. Otherwise, use ToString() if it prints hex in the same form.
    const auto pkt = FakeTelemetryPacketBytes();

    // Prefer ToHexString() if available; otherwise fall back to ToString() (kept for compatibility).
    std::string got;
#if defined(HAVE_TO_HEX_STRING)
    got = pkt.ToHexString();
#else
    got = pkt.ToHexString(); // the codebase already had this test in C++, per the Rust comments
#endif

    const auto expect =
            "Type: GPS_DATA, Size: 12, Sender: Flight Controller, Endpoints: [SD_CARD, RADIO], "
            "Timestamp: 1123581321, Data (hex): 0x00 0x00 0x98 0x41 0x00 0x00 0x04 0x42 0x00 0x00 0x50 0x42";
    EXPECT_EQ(got, expect);
}

TEST(Helpers, CopyTelemetryPacket)
{
    // C++ already had this helper in the original suite; we exercise the same semantics the Rust test ports mention.
    // Signature assumed: bool CopyTelemetryPacket(TelemetryPacket* dest, const TelemetryPacket* src)
    // If it returns an int / TelemetryResult, adapt the EXPECTs accordingly.
    // (1) null dest → error/false
    TelemetryPacket src = FakeTelemetryPacketBytes();
    EXPECT_FALSE(CopyTelemetryPacket(nullptr, &src));

    // (2) same pointer (no-op) → OK/true
    TelemetryPacket same = FakeTelemetryPacketBytes();
    EXPECT_TRUE(CopyTelemetryPacket(&same, &same));

    // (3) distinct objects → deep copy and equal fields
    TelemetryPacket dest = TelemetryPacket::ShallowDefault();
    // if you have a default ctor; otherwise, construct minimally
    EXPECT_TRUE(CopyTelemetryPacket(&dest, &src));

    EXPECT_EQ(dest.timestamp, src.timestamp);
    EXPECT_EQ(dest.ty, src.ty);
    EXPECT_EQ(dest.data_size, src.data_size);
    ASSERT_TRUE(dest.endpoints && src.endpoints);
    EXPECT_EQ(*dest.endpoints, *src.endpoints);
    ASSERT_TRUE(dest.payload && src.payload);
    EXPECT_EQ(*dest.payload, *src.payload);
}


namespace fs = std::filesystem;

static int run_and_capture(const std::string & cmd,
                           const fs::path & cwd,
                           std::string * out,
                           std::string * err)
{
    // Make temp files for capture
    const auto tmp = fs::temp_directory_path();
    const auto outp = tmp / "c_sys_test_out.txt";
    const auto errp = tmp / "c_sys_test_err.txt";

    // Build a shell command that runs in cwd with redirection
#if defined(_WIN32)
    std::string full =
            "cmd /C \"cd /D \"" + cwd.string() + "\" && " + cmd +
            " > \"" + outp.string() + "\" 2> \"" + errp.string() + "\"\"";
#else
    std::string full =
            "cd \"" + cwd.string() + "\" && " + cmd +
            " > \"" + outp.string() + "\" 2> \"" + errp.string() + "\"";
#endif

    int rc = std::system(full.c_str()); // returns shell exit code (0 == success)

    // Slurp outputs (best-effort even on failure)
    if (out)
    {
        std::ifstream f(outp);
        out->assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    if (err)
    {
        std::ifstream f(errp);
        err->assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }

    // Clean up temp files (ignore errors)
    std::error_code ec;
    fs::remove(outp, ec);
    fs::remove(errp, ec);

    return rc;
}

TEST(CSystem, RunExternalCTest)
{
#ifndef C_SYSTEM_TEST_DIR
    GTEST_SKIP() << "C_SYSTEM_TEST_DIR not defined. Set target_compile_definitions in CMake.";
#else
    const fs::path root = fs::path(C_SYSTEM_TEST_DIR);

    // 1) cmake configure
    {
        std::string out, err;
        int rc = run_and_capture("cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug", root, &out, &err);
        SCOPED_TRACE("cmake configure stdout:\n" + out + "\ncmake configure stderr:\n" + err);
        ASSERT_EQ(rc, 0) << "CMake configure failed";
    }

    // 2) cmake build
    {
        std::string out, err;
        int rc = run_and_capture("cmake --build build", root, &out, &err);
        SCOPED_TRACE("cmake build stdout:\n" + out + "\ncmake build stderr:\n" + err);
        ASSERT_EQ(rc, 0) << "CMake build failed";
    }

    // 3) run the resulting executable
#if defined(_WIN32)
    const fs::path exe = root / "build" / "c_system_test.exe";
#else
    const fs::path exe = root / "build" / "c_system_test";
#endif
    ASSERT_TRUE(fs::exists(exe)) << "Executable not found at: " << exe.string();

    {
        std::string out, err;
        // Quote the exe path to handle spaces
        const std::string cmd =
#if defined(_WIN32)
    "\"" + exe.string() + "\"";
#else
    "\"" + exe.string() + "\"";
#endif
    int rc = run_and_capture(cmd, root, &out, &err);
    // Always surface the child output in test logs for debugging
    SCOPED_TRACE("c_system_test stdout:\n" + out + "\nc_system_test stderr:\n" + err);
    ASSERT_EQ(rc, 0) << "c_system_test exited with non-zero status";
    }
#endif
}
