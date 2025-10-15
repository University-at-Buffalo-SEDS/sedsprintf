// reimpl_helpers.cpp
#include "router.hpp"
#include "serialize.hpp"
#include "config.hpp"
#include "error.hpp"
#include "result.hpp"
#include "telemetry_packet.hpp"

#include <atomic>
#include <mutex>
#include <memory>

namespace testhelpers {
using namespace seds;

// get_handler: increments a counter whenever invoked
inline EndpointHandler get_handler(const std::shared_ptr<std::atomic<size_t>>& rx_count_c) {
    return EndpointHandler{
        DataEndpoint::SD_CARD,
        [rx_count_c](const TelemetryPacket&) -> Result<void, ErrorInfo> {
            rx_count_c->fetch_add(1, std::memory_order_seq_cst);
            return Result<void, ErrorInfo>::ok();
        }
    };
}

// get_sd_card_handler: asserts GPS_DATA + f32 layout, decodes little-endian f32s
inline EndpointHandler get_sd_card_handler(
    const std::shared_ptr<std::mutex>& mtx,
    const std::shared_ptr<std::optional<std::pair<DataType, std::vector<float>>>>& captured
) {
    return EndpointHandler{
        DataEndpoint::SD_CARD,
        [mtx, captured](const TelemetryPacket& pkt) -> Result<void, ErrorInfo> {
            // sanity checks (guarded so tests don't SEGV even if tables aren't wired yet)
            if (pkt.ty != DataType::GPS_DATA) {
                return Result<void, ErrorInfo>::err(
                    ErrorInfo(TelemetryError::BadArg, "expected GPS_DATA in test handler"));
            }

            // If you have MESSAGE_ELEMENTS + get_needed_message_size in C++:
            // (If not available, you can comment these out for now.)
            #ifdef SEDS_HAS_MESSAGE_TABLES
            const size_t elems    = std::max<size_t>(1, MESSAGE_ELEMENTS[static_cast<size_t>(pkt.ty)]);
            const size_t per_elem = get_needed_message_size(pkt.ty) / elems;
            assert(per_elem == 4 && "GPS_DATA expected f32 elements");
            #endif

            std::vector<float> vals;
            vals.reserve(pkt.payload->size() / 4);
            for (size_t i = 0; i + 4 <= pkt.payload->size(); i += 4) {
                uint32_t le = (uint32_t)pkt.payload->at(i)
                            | (uint32_t)pkt.payload->at(i+1) << 8
                            | (uint32_t)pkt.payload->at(i+2) << 16
                            | (uint32_t)pkt.payload->at(i+3) << 24;
                float f;
                static_assert(sizeof(float)==4, "float must be 4 bytes");
                std::memcpy(&f, &le, 4);
                vals.push_back(f);
            }

            std::lock_guard<std::mutex> lk(*mtx);
            *captured = std::make_pair(pkt.ty, std::move(vals));
            return Result<void, ErrorInfo>::ok();
        }
    };
}

// handle_errors: expect HandlerError (like Rust test)
inline void handle_errors(const Result<void, ErrorInfo>& r) {
    if (!r.is_err()) {
        throw std::runtime_error("Expected error (handler failure), got Ok()");
    }
    const auto& e = r.error();
    if (e.code != TelemetryError::HandlerError) {
        throw std::runtime_error("Expected TelemetryError::HandlerError");
    }
}

} // namespace testhelpers
