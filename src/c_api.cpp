#include "sedsprintf.h"
#include "router.hpp"
#include "serialize.hpp"
#include "error.hpp"
#include "result.hpp"
#include "config.hpp"
#include <memory>
#include <climits>   // INT32_MAX


using namespace seds;

typedef enum {
    SEDS_ELEM_U8   = 0,
    SEDS_ELEM_U32  = 1,
    SEDS_ELEM_F32 = 2,
} SedsMessageDataType;


// ------------------------------------------------------------
// Internal conversion helpers
// ------------------------------------------------------------
namespace
{
    SedsResult to_c_result(const Result<void, ErrorInfo> & r) noexcept
    {
        return r.is_err() ? SEDS_ERR : SEDS_OK;
    }

    // Bridge a C handler to the internal Router handler type.
    struct CHandlerWrapper
    {
        SedsEndpointHandlerFn fn;
        void * user;

        Result<void, ErrorInfo> operator()(const TelemetryPacket & pkt) const noexcept
        {
            if (!fn) return Result<void, ErrorInfo>::ok();

            SedsPacketView view{};
            view.ty = static_cast<uint32_t>(pkt.ty);
            view.data_size = pkt.data_size;
            view.sender = nullptr;
            view.sender_len = 0;
            view.timestamp = pkt.timestamp;
            view.payload = pkt.payload->data();
            view.payload_len = pkt.payload->size();

            static thread_local std::vector<uint32_t> eps;
            eps.clear();
            eps.reserve(pkt.endpoints->size());
            for (auto e: *pkt.endpoints)
                eps.push_back(static_cast<uint32_t>(e));

            view.endpoints = eps.data();
            view.num_endpoints = eps.size();

            auto rc = fn(&view, user);
            return rc == SEDS_OK
                       ? Result<void, ErrorInfo>::ok()
                       : Result<void, ErrorInfo>::err(
                           ErrorInfo(TelemetryError::HandlerError, "handler error"));
        }
    };

    // Clock adapter for timeouts
    struct CCallbackClock : public Clock
    {
        SedsNowMsFn fn;
        void * user;

        explicit CCallbackClock(SedsNowMsFn f, void * u) : fn(f), user(u)
        {
        }

        [[nodiscard]] uint64_t now_ms() const noexcept override
        {
            return fn ? fn(user) : 0ULL;
        }
    };
} // namespace

// ------------------------------------------------------------
// Router lifecycle
// ------------------------------------------------------------
extern "C" SedsRouter * seds_router_new(SedsTransmitFn tx,
                                        void * tx_user,
                                        const SedsLocalEndpointDesc * handlers,
                                        size_t n_handlers)
{
    BoardConfig cfg{};
    cfg.handlers.reserve(n_handlers);

    for (size_t i = 0; i < n_handlers; ++i)
    {
        auto & h = handlers[i];
        cfg.handlers.push_back({
            static_cast<DataEndpoint>(h.endpoint),
            CHandlerWrapper{h.handler, h.user}
        });
    }

    Router::TxFn tx_fn;
    if (tx)
    {
        tx_fn = [tx, tx_user](const std::vector<uint8_t> & bytes)
            -> Result<void, ErrorInfo>
                {
                    auto rc = tx(bytes.data(), bytes.size(), tx_user);
                    return rc == SEDS_OK
                               ? Result<void, ErrorInfo>::ok()
                               : Result<void, ErrorInfo>::err(
                                   ErrorInfo(TelemetryError::Io, "transmit failed"));
                };
    }

    auto r = std::make_unique<Router>(tx_fn, cfg);
    return reinterpret_cast<SedsRouter *>(r.release());
}

extern "C" void seds_router_free(SedsRouter * r)
{
    delete reinterpret_cast<Router *>(r);
}

// ------------------------------------------------------------
// Logging / queueing
// ------------------------------------------------------------
extern "C" SedsResult seds_router_log_bytes(SedsRouter * r,
                                            SedsDataType ty,
                                            const uint8_t * data,
                                            size_t len,
                                            uint64_t ts)
{
    if (!r || !data) return SEDS_BAD_ARG;
    auto * router = reinterpret_cast<Router *>(r);
    std::vector<uint8_t> buf(data, data + len);
    return to_c_result(router->log(static_cast<DataType>(ty), buf, ts));
}

extern "C" SedsResult seds_router_log_f32(SedsRouter * r,
                                          SedsDataType ty,
                                          const float * vals,
                                          size_t n_vals,
                                          uint64_t ts)
{
    if (!r || !vals) return SEDS_BAD_ARG;
    std::vector<uint8_t> bytes(n_vals * sizeof(float));
    std::memcpy(bytes.data(), vals, bytes.size());
    auto * router = reinterpret_cast<Router *>(r);
    return to_c_result(router->log(static_cast<DataType>(ty), bytes, ts));
}

// ------------------------------------------------------------
// Receive side
// ------------------------------------------------------------
extern "C" SedsResult seds_router_receive_serialized(SedsRouter * r,
                                                     const uint8_t * bytes,
                                                     size_t len)
{
    if (!r || !bytes) return SEDS_BAD_ARG;
    std::vector<uint8_t> data(bytes, bytes + len);
    return to_c_result(reinterpret_cast<Router *>(r)->receive_serialized(data));
}

extern "C" SedsResult seds_router_receive(SedsRouter * r, SedsPacketView * view)
{
    if (!r || !view) return SEDS_BAD_ARG;

    std::vector<DataEndpoint> eps;
    eps.reserve(view->num_endpoints);
    for (size_t i = 0; i < view->num_endpoints; ++i)
        eps.push_back(static_cast<DataEndpoint>(view->endpoints[i]));

    std::vector<uint8_t> payload(view->payload, view->payload + view->payload_len);
    TelemetryPacket pkt(static_cast<DataType>(view->ty), eps, "", view->timestamp, payload);

    return to_c_result(reinterpret_cast<Router *>(r)->receive(pkt));
}

// ------------------------------------------------------------
// Queue processing
// ------------------------------------------------------------
extern "C" SedsResult seds_router_process_send_queue(SedsRouter * r)
{
    if (!r) return SEDS_BAD_ARG;
    return to_c_result(reinterpret_cast<Router *>(r)->process_send_queue());
}

extern "C" SedsResult seds_router_process_received_queue(SedsRouter * r)
{
    if (!r) return SEDS_BAD_ARG;
    return to_c_result(reinterpret_cast<Router *>(r)->process_received_queue());
}

extern "C" SedsResult seds_router_process_all_queues(SedsRouter * r)
{
    if (!r) return SEDS_BAD_ARG;
    return to_c_result(reinterpret_cast<Router *>(r)->process_all_queues());
}

// ------------------------------------------------------------
// Timeout-based processing
// ------------------------------------------------------------
extern "C" SedsResult seds_router_process_tx_queue_with_timeout(
    SedsRouter * r, SedsNowMsFn now_cb, void * user, uint32_t timeout_ms)
{
    if (!r) return SEDS_BAD_ARG;
    CCallbackClock clk(now_cb, user);
    return to_c_result(
        reinterpret_cast<Router *>(r)->process_tx_queue_with_timeout(&clk, timeout_ms));
}

extern "C" SedsResult seds_router_process_rx_queue_with_timeout(
    SedsRouter * r, SedsNowMsFn now_cb, void * user, uint32_t timeout_ms)
{
    if (!r) return SEDS_BAD_ARG;
    CCallbackClock clk(now_cb, user);
    return to_c_result(
        reinterpret_cast<Router *>(r)->process_rx_queue_with_timeout(&clk, timeout_ms));
}

extern "C" SedsResult seds_router_process_all_queues_with_timeout(
    SedsRouter * r, SedsNowMsFn now_cb, void * user, uint32_t timeout_ms)
{
    if (!r) return SEDS_BAD_ARG;
    CCallbackClock clk(now_cb, user);
    auto * router = reinterpret_cast<Router *>(r);
    auto res1 = router->process_tx_queue_with_timeout(&clk, timeout_ms);
    if (res1.is_err()) return SEDS_ERR;
    auto res2 = router->process_rx_queue_with_timeout(&clk, timeout_ms);
    return res2.is_err() ? SEDS_ERR : SEDS_OK;
}

// Private, internal definition that matches the ABI intended by your C API
struct SedsRouter {
    void* impl;   // holds `new seds::Router(...)`
};


// If your SedsRouter handle stores the C++ object differently, tweak this one-liner.
static inline Router* as_cpp(SedsRouter* r) noexcept {
    return r ? reinterpret_cast<Router*>(r->impl) : nullptr;
}

// Convert SedsPacketView -> TelemetryPacket (no validation needed for string length funcs)
static inline TelemetryPacket make_pkt_from_view(const SedsPacketView* v) {
    // Endpoints: convert u32 -> DataEndpoint
    std::vector<DataEndpoint> eps;
    eps.reserve(v->num_endpoints);
    for (size_t i = 0; i < v->num_endpoints; ++i) {
        eps.push_back(endpoint_from_u32(v->endpoints[i]));
    }

    // Sender may NOT be NUL-terminated; build from (ptr,len)
    std::string sender;
    if (v->sender && v->sender_len) {
        sender.assign(v->sender, v->sender + v->sender_len);
    }

    // Payload
    std::vector<uint8_t> payload;
    if (v->payload && v->payload_len) {
        payload.assign(v->payload, v->payload + v->payload_len);
    }

    return TelemetryPacket(
        static_cast<DataType>(v->ty),
        std::move(eps),
        std::move(sender),
        v->timestamp,
        payload
    );
}
extern "C" SedsResult seds_router_log_queue_typed(SedsRouter* r,
                                                  SedsDataType ty,
                                                  const void* data,
                                                  size_t count,
                                                  size_t elem_size,
                                                  SedsElemKind elem_kind,
                                                  uint64_t timestamp)
{
    Router* cpp = as_cpp(r);
    if (!cpp) return SEDS_BAD_ARG;

    const DataType cpp_ty = static_cast<DataType>(ty);

    // Allow empty payloads (OK); otherwise require data != nullptr
    if (count > 0 && data == nullptr) return SEDS_BAD_ARG;

    switch (elem_kind) {
        case SEDS_ELEM_F32: {
            if (elem_size != 4) return SEDS_BAD_ARG;
            const float* f = static_cast<const float*>(data);
            std::vector<float> vals;
            vals.reserve(count);
            for (size_t i = 0; i < count; ++i) vals.push_back(f[i]);
            auto res = cpp->log_queue(cpp_ty, vals, timestamp);
            return res.is_ok() ? SEDS_OK : SEDS_ERR;
        }

        case SEDS_ELEM_U8: {
            if (elem_size != 1) return SEDS_BAD_ARG;
            const uint8_t* u8 = static_cast<const uint8_t*>(data);
            std::vector<uint8_t> bytes;
            if (count) bytes.assign(u8, u8 + count);
            auto res = cpp->log_queue(cpp_ty, bytes, timestamp);
            return res.is_ok() ? SEDS_OK : SEDS_ERR;
        }

        case SEDS_ELEM_U32: {
            if (elem_size != 4) return SEDS_BAD_ARG;
            const uint32_t* u32 = static_cast<const uint32_t*>(data);
            std::vector<uint8_t> bytes;
            bytes.reserve(count * 4);
            for (size_t i = 0; i < count; ++i) {
                const uint32_t x = u32[i];
                // little-endian
                bytes.push_back(static_cast<uint8_t>( x        & 0xFF));
                bytes.push_back(static_cast<uint8_t>((x >>  8) & 0xFF));
                bytes.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
                bytes.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
            }
            auto res = cpp->log_queue(cpp_ty, bytes, timestamp);
            return res.is_ok() ? SEDS_OK : SEDS_ERR;
        }

        default:
            return SEDS_BAD_ARG;
    }
}

// --- internal helpers shared by header+full string ---
static inline int32_t seds__header_len_only(const SedsPacketView* pkt) {
    if (!pkt) return -1;
    // Worst-case lengths (decimal), + punctuation/spaces, + NUL
    // "Type: 4294967295, Size: 4294967295, Sender: <sender>, Endpoints: [..], Timestamp: 18446744073709551615"
    // Be safe: compute exactly like we print
    int n = 0;

    // Type, Size, Sender (bytes)
    n += std::snprintf(nullptr, 0, "Type: %u, Size: %zu, Sender: ", pkt->ty, pkt->data_size);
    // Sender may not be NUL-terminated; print exactly sender_len bytes
    n += (int)pkt->sender_len;

    // Endpoints header + commas between items + each endpoint value
    n += std::snprintf(nullptr, 0, ", Endpoints: [");
    for (size_t i = 0; i < pkt->num_endpoints; ++i) {
        if (i) n += 2; // ", "
        n += std::snprintf(nullptr, 0, "%u", pkt->endpoints[i]);
    }
    n += 1; // ']'

    // Timestamp
    n += std::snprintf(nullptr, 0, ", Timestamp: %llu",
                       (unsigned long long)pkt->timestamp);

    return n + 1; // +NUL
}

int32_t seds_pkt_header_string_len(const SedsPacketView* pkt) {
    return seds__header_len_only(pkt);
}

// Full string len = header + ", Data: " + payload rendering (float or hex) + NUL
int32_t seds_pkt_to_string_len(const SedsPacketView* pkt) {
    if (!pkt) return -1;
    int32_t n = seds__header_len_only(pkt);
    if (n < 0) return n;

    // ", Data: "
    int payload = 0;
    payload += 8;

    if (pkt->payload && pkt->payload_len) {
        if ((pkt->payload_len % 4) == 0) {
            // Render as little-endian f32 values: e.g., "3.5, 2.0, ..."
            const size_t n_f = pkt->payload_len / 4;
            bool first = true;
            for (size_t i = 0; i < n_f; ++i) {
                // each float: up to ~15 chars incl. minus/decimal/exponent is plenty; add comma+space for non-first
                payload += (first ? 0 : 2) + 16;
                first = false;
            }
        } else {
            // Hex bytes like "0xAB 0xCD ..."
            if (pkt->payload_len) {
                // first "0xXX" is 4 chars, subsequent add " 0xXX" (5)
                payload += 4 + (int)((pkt->payload_len > 1) ? (pkt->payload_len - 1) * 5 : 0);
            }
        }
    } else {
        // "(empty)"
        payload += 7;
    }

    return n + payload + 1; // +NUL
}

// Print header
static char* seds__print_header(char* out, char* end, const SedsPacketView* pkt) {
    auto putc = [&](char c) {
        if (out < end) *out = c;
        ++out;
    };
    auto puts_n = [&](const char* s, size_t nbytes) {
        for (size_t i = 0; i < nbytes; ++i) putc(s[i]);
    };
    int w = std::snprintf(out, (out < end) ? (size_t)(end - out) : 0,
                          "Type: %u, Size: %zu, Sender: ",
                          pkt->ty, pkt->data_size);
    out += (w > 0 ? w : 0);

    // sender (not NUL-terminated)
    if (pkt->sender && pkt->sender_len) {
        puts_n(pkt->sender, pkt->sender_len);
    }

    w = std::snprintf(out, (out < end) ? (size_t)(end - out) : 0, ", Endpoints: [");
    out += (w > 0 ? w : 0);

    for (size_t i = 0; i < pkt->num_endpoints; ++i) {
        if (i) {
            putc(',');
            putc(' ');
        }
        w = std::snprintf(out, (out < end) ? (size_t)(end - out) : 0, "%u", pkt->endpoints[i]);
        out += (w > 0 ? w : 0);
    }
    putc(']');

    w = std::snprintf(out, (out < end) ? (size_t)(end - out) : 0,
                      ", Timestamp: %llu",
                      (unsigned long long)pkt->timestamp);
    out += (w > 0 ? w : 0);

    return out;
}

// Exported: write full packet string
extern "C" int32_t seds_pkt_to_string(const SedsPacketView* pkt, char* out, size_t out_len) {
    if (!pkt || !out || out_len == 0) return -1;

    char* const begin = out;
    char* const end   = begin + out_len;

    out = seds__print_header(out, end, pkt);

    // ", Data: "
    int w = std::snprintf(out, (out < end) ? (size_t)(end - out) : 0, ", Data: ");
    out += (w > 0 ? w : 0);

    if (!pkt->payload || pkt->payload_len == 0) {
        const char* s = "(empty)";
        size_t n = 7;
        if ((size_t)(end - out) > 0) {
            size_t copy = (n < (size_t)(end - out-1)) ? n : (size_t)(end - out-1);
            if (copy > 0) std::memcpy(out, s, copy);
        }
        out += n;
    } else if ((pkt->payload_len % 4) == 0) {
        // Render as f32 little-endian list
        const uint8_t* p = pkt->payload;
        const size_t nf = pkt->payload_len / 4;
        for (size_t i = 0; i < nf; ++i) {
            if (i) {
                if (out < end) *out = ','; ++out;
                if (out < end) *out = ' '; ++out;
            }
            uint32_t le = (uint32_t)p[0] | (uint32_t(p[1])<<8) | (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24);
            p += 4;
            float f;
            static_assert(sizeof(f) == 4, "float not 4 bytes");
            std::memcpy(&f, &le, 4);
            int n = std::snprintf(out, (out < end) ? (size_t)(end - out) : 0, "%g", (double)f);
            out += (n > 0 ? n : 0);
        }
    } else {
        // Hex bytes: 0xAB 0xCD ...
        for (size_t i = 0; i < pkt->payload_len; ++i) {
            if (i) {
                if (out < end) *out = ' '; ++out;
            }
            int n = std::snprintf(out, (out < end) ? (size_t)(end - out) : 0, "0x%02X", pkt->payload[i]);
            out += (n > 0 ? n : 0);
        }
    }

    // NUL terminate if space
    if (begin < end) {
        begin[out_len - 1] = '\0';
        if (out <= end - 1) *out = '\0';
    }
    // return required length (like snprintf) — recompute once
    return seds_pkt_to_string_len(pkt);
}