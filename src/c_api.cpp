// c_api.cpp — leak-free, Rust-parity rewrite

#include "c_api.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "config.hpp"
#include "serialize.hpp"
#include "telemetry.hpp"
#include "router.hpp"

using namespace seds;

// ----------------- internal wrappers / bridge -----------------

// Opaque wrapper mirrors Rust #[repr(C)] struct holding Router
struct SedsRouter
{
    Router inner;
    explicit SedsRouter(Router&& r) : inner(std::move(r)) {}
};

// ============================ status / error helpers ============================

static int status_from_err(const TelemetryError& e)
{
    switch (e.kind)
    {
        case TelemetryError::Kind::InvalidType:   return -3;
        case TelemetryError::Kind::SizeMismatch:  return -4;
        case TelemetryError::Kind::Deserialize:   return -5;
        case TelemetryError::Kind::HandlerError:  return -6;
        case TelemetryError::Kind::BadArg:        return -2;
        default:                                   return -1;
    }
}

enum class SedsResult : int { SedsOk = 0, SedsErr = 1 };

static int status_from_result_code(SedsResult e)
{
    return (e == SedsResult::SedsOk) ? 0 : 1;
}

// Ok(()) -> 0 ; Err(e) -> mapped negative
template<typename T>
static int ok_or_status(const TelemetryResult<T>& r)
{
    if (r.is_ok()) return status_from_result_code(SedsResult::SedsOk);
    return status_from_err(r.unwrap_err());
}

static TelemetryResult<DataType> dtype_from_u32(std::uint32_t x)
{
    const auto o = try_enum_from_u32(x);
    if (!o) return TelemetryResult<DataType>::Err(TelemetryError::InvalidType());
    return TelemetryResult<DataType>::Ok(*o);
}

static TelemetryResult<DataEndpoint> endpoint_from_u32(std::uint32_t x)
{
    const auto o = try_enum_from_u32_endpoint(x);
    if (!o) return TelemetryResult<DataEndpoint>::Err(TelemetryError::Deserialize("bad endpoint"));
    return TelemetryResult<DataEndpoint>::Ok(*o);
}

// ---- simple UTF-8 validator (same as used in serialize.cpp) ----
static bool is_valid_utf8(const std::uint8_t* s, std::size_t n)
{
    std::size_t i = 0;
    while (i < n)
    {
        std::uint8_t c = s[i];
        if ((c & 0x80u) == 0x00u) { ++i; }
        else if ((c & 0xE0u) == 0xC0u)
        {
            if (i + 1 >= n) return false;
            if ((s[i + 1] & 0xC0u) != 0x80u) return false;
            std::uint8_t c0 = c & 0x1Fu;
            if (c0 == 0) return false;
            i += 2;
        }
        else if ((c & 0xF0u) == 0xE0u)
        {
            if (i + 2 >= n) return false;
            if ((s[i + 1] & 0xC0u) != 0x80u) return false;
            if ((s[i + 2] & 0xC0u) != 0x80u) return false;
            i += 3;
        }
        else if ((c & 0xF8u) == 0xF0u)
        {
            if (i + 3 >= n) return false;
            if ((s[i + 1] & 0xC0u) != 0x80u) return false;
            if ((s[i + 2] & 0xC0u) != 0x80u) return false;
            if ((s[i + 3] & 0xC0u) != 0x80u) return false;
            i += 4;
        }
        else
        {
            return false;
        }
    }
    return true;
}

// ============================ SedsPacketView helpers ============================

extern "C" const void* seds_pkt_bytes_ptr(const SedsPacketView* pkt, std::size_t* out_len)
{
    if (!pkt)
    {
        if (out_len) *out_len = 0;
        return nullptr;
    }
    if (out_len) *out_len = pkt->payload_len;
    return reinterpret_cast<const void*>(pkt->payload);
}

extern "C" const void* seds_pkt_data_ptr(const SedsPacketView* pkt,
                                         std::size_t elem_size,
                                         std::size_t* out_count)
{
    if (out_count) *out_count = 0;
    if (!pkt) return nullptr;

    if (!(elem_size == 1 || elem_size == 2 || elem_size == 4 || elem_size == 8))
        return nullptr;

    if (elem_size == 0 || (pkt->payload_len % elem_size) != 0)
        return nullptr;

    if (out_count) *out_count = pkt->payload_len / elem_size;
    return pkt->payload;
}

// ---- string write helper ----
static int write_str_to_buf(const std::string& s, char* buf, std::size_t buf_len)
{
    const std::size_t needed = s.size() + 1; // include NUL

    if (buf == nullptr && buf_len != 0)
        return status_from_err(TelemetryError::BadArg());

    // query mode: tell required size
    if (buf == nullptr || buf_len == 0)
        return static_cast<int>(needed);

    const std::size_t ncopy = std::min(s.size(), buf_len > 0 ? buf_len - 1 : 0);
    if (ncopy) std::memcpy(buf, s.data(), ncopy);
    buf[ncopy] = '\0';

    // if too small, return required size (not success)
    if (buf_len < needed)
        return static_cast<int>(needed);

    return status_from_result_code(SedsResult::SedsOk);
}

// ============================ view_to_packet (NO LEAKS) ============================
//
// Rust parity:
// - sender      => Arc<str>      -> std::shared_ptr<std::string>
// - endpoints   => Arc<[Endpoint]> -> std::shared_ptr<const std::vector<DataEndpoint>>
// - payload     => Arc<[u8]>     -> std::shared_ptr<const std::vector<uint8_t>>
//
static std::optional<TelemetryPacket> view_to_packet(const SedsPacketView* view)
{
    if (!view) return std::nullopt;

    // type
    const auto ty_opt = try_enum_from_u32(view->ty);
    if (!ty_opt) return std::nullopt;

    // endpoints
    if (view->num_endpoints > 0 && view->endpoints == nullptr) return std::nullopt;
    std::vector<DataEndpoint> eps;
    eps.reserve(view->num_endpoints);
    for (std::size_t i = 0; i < view->num_endpoints; ++i)
    {
        auto ep_o = try_enum_from_u32_endpoint(view->endpoints[i]);
        if (!ep_o) return std::nullopt;
        eps.push_back(*ep_o);
    }
    auto endpoints_arc = std::make_shared<const std::vector<DataEndpoint>>(std::move(eps));

    // sender (owned, UTF-8 validated)
    std::shared_ptr<std::string> sender_arc;
    if (view->sender && view->sender_len > 0)
    {
        auto* sb = reinterpret_cast<const std::uint8_t*>(view->sender);
        if (!is_valid_utf8(sb, view->sender_len)) return std::nullopt;
        sender_arc = std::make_shared<std::string>(reinterpret_cast<const char*>(sb), view->sender_len);
    }
    else
    {
        sender_arc = std::make_shared<std::string>("");
    }

    // payload (owned)
    std::shared_ptr<const std::vector<std::uint8_t>> payload_arc;
    if (view->payload && view->payload_len > 0)
    {
        auto payload = std::make_shared<std::vector<std::uint8_t>>(view->payload,
                                                                   view->payload + view->payload_len);
        payload_arc = std::move(payload);
    }
    else
    {
        payload_arc = std::make_shared<std::vector<std::uint8_t>>();
    }

    TelemetryPacket pkt{};
    pkt.ty        = *ty_opt;
    pkt.data_size = view->data_size;
    pkt.sender    = std::move(sender_arc);
    pkt.endpoints = std::move(endpoints_arc);
    pkt.timestamp = view->timestamp;
    pkt.payload   = std::move(payload_arc);

    return pkt;
}

// ============================ Clock bridge from C ============================

struct FfiClock final : Clock
{
    CNowMs cb{};
    std::uintptr_t user_addr{0};

    [[nodiscard]] std::uint64_t now_ms() const override
    {
        if (cb) return cb(reinterpret_cast<void*>(user_addr));
        return 0;
    }
};

// ============================ exported functions ============================

extern "C" int seds_pkt_header_string_len(const SedsPacketView* pkt)
{
    if (!pkt) return status_from_err(TelemetryError::BadArg());
    const auto opt = view_to_packet(pkt);
    if (!opt) return status_from_err(TelemetryError::BadArg());
    const auto s = opt->HeaderString();
    return static_cast<int>(s.size() + 1);
}

extern "C" int seds_pkt_to_string_len(const SedsPacketView* pkt)
{
    if (!pkt) return status_from_err(TelemetryError::BadArg());
    const auto opt = view_to_packet(pkt);
    if (!opt) return status_from_err(TelemetryError::BadArg());
    const auto s = opt->ToString();
    return static_cast<int>(s.size() + 1);
}

extern "C" int seds_pkt_header_string(const SedsPacketView* pkt, char* buf, std::size_t buf_len)
{
    if (!pkt) return status_from_err(TelemetryError::BadArg());
    const auto opt = view_to_packet(pkt);
    if (!opt) return status_from_err(TelemetryError::BadArg());
    return write_str_to_buf(opt->HeaderString(), buf, buf_len);
}

extern "C" int seds_pkt_to_string(const SedsPacketView* pkt, char* buf, std::size_t buf_len)
{
    if (!pkt) return status_from_err(TelemetryError::BadArg());
    const auto opt = view_to_packet(pkt);
    if (!opt) return status_from_err(TelemetryError::BadArg());
    return write_str_to_buf(opt->ToString(), buf, buf_len);
}

// ----------------- router construction -----------------

extern "C" SedsRouter* seds_router_new(CTransmit tx,
                                        void* tx_user,
                                        CNowMs now_ms_cb,
                                        const SedsHandlerDesc* handlers,
                                        std::size_t n_handlers)
{
    // Build transmit closure if provided
    std::optional<std::function<TelemetryResult<void*>(const std::vector<std::uint8_t>&)>> transmit;
    if (tx)
    {
        auto ctx_user = tx_user;
        transmit = [tx, ctx_user](const std::vector<std::uint8_t>& bytes) -> TelemetryResult<void*>
        {
            if (const int code = tx(bytes.data(), bytes.size(), ctx_user); code == 0)
                return TelemetryResult<void*>::Ok(nullptr);
            return TelemetryResult<void*>::Err(TelemetryError::Io("tx error"));
        };
    }

    // Handlers
    std::vector<EndpointHandler> v;
    if (n_handlers > 0 && handlers)
    {
        v.reserve(n_handlers);
        for (std::size_t i = 0; i < n_handlers; ++i)
        {
            auto ep_res = endpoint_from_u32(handlers[i].endpoint);
            if (ep_res.is_err()) return nullptr;
            const DataEndpoint endpoint = ep_res.unwrap();

            auto cb  = handlers[i].handler;
            auto usr = handlers[i].user;

            EndpointHandler eh;
            eh.endpoint = endpoint;
            eh.handler = [cb, usr](const TelemetryPacket& pkt) -> TelemetryResult<void*>
            {
                // Build transient view into owned fields (no allocations/leaks)
                std::vector<std::uint32_t> eps_u32;
                if (pkt.endpoints)
                {
                    eps_u32.reserve(pkt.endpoints->size());
                    for (auto e : *pkt.endpoints) eps_u32.push_back(static_cast<std::uint32_t>(e));
                }

                const char* sender_ptr = pkt.sender ? pkt.sender->c_str() : "";
                const std::size_t sender_len = pkt.sender ? pkt.sender->size() : 0;

                const SedsPacketView view{
                    static_cast<std::uint32_t>(pkt.ty),
                    pkt.data_size,
                    sender_ptr,
                    sender_len,
                    eps_u32.data(),
                    eps_u32.size(),
                    pkt.timestamp,
                    pkt.payload ? pkt.payload->data() : nullptr,
                    pkt.payload ? pkt.payload->size() : 0
                };

                if (const int code = cb ? cb(&view, usr) : 0; code == 0)
                    return TelemetryResult<void*>::Ok(nullptr);

                return TelemetryResult<void*>::Err(TelemetryError::Io("handler error"));
            };
            v.push_back(std::move(eh));
        }
    }

    // Clock
    auto clk = make_clock([now_ms_cb, tx_user]() -> std::uint64_t
    {
        if (now_ms_cb) return now_ms_cb(tx_user);
        return 0;
    });

    BoardConfig cfg(std::move(v));
    Router router(std::move(transmit), std::move(cfg), std::move(clk));
    return new SedsRouter(std::move(router));
}

extern "C" void seds_router_free(const SedsRouter* r)
{
    delete r;
}

// ----------------- logging (bytes/f32) -----------------

extern "C" int seds_router_log_bytes(SedsRouter* r, const std::uint32_t ty_u32,
                                     const std::uint8_t* data, const std::size_t len)
{
    if (!r || (len > 0 && !data)) return status_from_err(TelemetryError::BadArg());
    auto ty = dtype_from_u32(ty_u32);
    if (ty.is_err()) return status_from_err(ty.unwrap_err());
    const std::vector<std::uint8_t> v(data, data + len);
    return ok_or_status(r->inner.log<std::uint8_t>(ty.unwrap(), v));
}

extern "C" int seds_router_log_f32(SedsRouter* r, const std::uint32_t ty_u32,
                                   const float* vals, const std::size_t n_vals)
{
    if (!r || (n_vals > 0 && !vals)) return status_from_err(TelemetryError::BadArg());
    auto ty = dtype_from_u32(ty_u32);
    if (ty.is_err()) return status_from_err(ty.unwrap_err());
    const std::vector<float> v(vals, vals + n_vals);
    return ok_or_status(r->inner.log<float>(ty.unwrap(), v));
}

// ----------------- receive serialized / packet view -----------------

extern "C" int seds_router_receive_serialized(SedsRouter* r, const std::uint8_t* bytes, std::size_t len)
{
    if (!r || (len > 0 && !bytes)) return status_from_err(TelemetryError::BadArg());
    const std::vector<std::uint8_t> v(bytes, bytes + len);
    return ok_or_status(r->inner.receive_serialized(v));
}

extern "C" int seds_router_receive(SedsRouter* r, const SedsPacketView* view)
{
    if (!r || !view) return status_from_err(TelemetryError::BadArg());
    const auto opt = view_to_packet(view);
    if (!opt) return status_from_err(TelemetryError::InvalidType());
    return ok_or_status(r->inner.receive(*opt));
}

// ----------------- TX/RX queues -----------------

extern "C" int seds_router_process_send_queue(SedsRouter* r)
{
    if (!r) return status_from_err(TelemetryError::BadArg());
    return ok_or_status(r->inner.process_send_queue());
}

extern "C" int seds_router_queue_tx_message(SedsRouter* r, const SedsPacketView* view)
{
    if (!r || !view) return status_from_err(TelemetryError::BadArg());
    const auto opt = view_to_packet(view);
    if (!opt) return status_from_err(TelemetryError::InvalidType());
    return ok_or_status(r->inner.queue_tx_message(*opt));
}

extern "C" int seds_router_process_received_queue(SedsRouter* r)
{
    if (!r) return status_from_err(TelemetryError::BadArg());
    return ok_or_status(r->inner.process_received_queue());
}

extern "C" int seds_router_rx_serialized_packet_to_queue(SedsRouter* r, const std::uint8_t* bytes, std::size_t len)
{
    if (!r || (len > 0 && !bytes)) return status_from_err(TelemetryError::BadArg());
    const std::vector<std::uint8_t> v(bytes, bytes + len);
    return ok_or_status(r->inner.rx_serialized_packet_to_queue(v));
}

extern "C" int seds_router_rx_packet_to_queue(SedsRouter* r, const SedsPacketView* view)
{
    if (!r || !view) return status_from_err(TelemetryError::BadArg());
    const auto opt = view_to_packet(view);
    if (!opt) return status_from_err(TelemetryError::InvalidType());
    return ok_or_status(r->inner.rx_packet_to_queue(*opt));
}

// ----------------- queue utilities -----------------

extern "C" int seds_router_process_all_queues(SedsRouter* r)
{
    if (!r) return status_from_err(TelemetryError::BadArg());
    return ok_or_status(r->inner.process_all_queues());
}

extern "C" int seds_router_clear_queues(SedsRouter* r)
{
    if (!r) return status_from_err(TelemetryError::BadArg());
    r->inner.clear_queues();
    return status_from_result_code(SedsResult::SedsOk);
}

extern "C" int seds_router_clear_rx_queue(SedsRouter* r)
{
    if (!r) return status_from_err(TelemetryError::BadArg());
    r->inner.clear_rx_queue();
    return status_from_result_code(SedsResult::SedsOk);
}

extern "C" int seds_router_clear_tx_queue(SedsRouter* r)
{
    if (!r) return status_from_err(TelemetryError::BadArg());
    r->inner.clear_tx_queue();
    return status_from_result_code(SedsResult::SedsOk);
}

// ----------------- time-budgeted queue processing -----------------

extern "C" int seds_router_process_tx_queue_with_timeout(SedsRouter* r, std::uint32_t timeout_ms)
{
    if (!r) return status_from_err(TelemetryError::BadArg());
    return ok_or_status(r->inner.process_tx_queue_with_timeout(timeout_ms));
}

extern "C" int seds_router_process_rx_queue_with_timeout(SedsRouter* r, std::uint32_t timeout_ms)
{
    if (!r) return status_from_err(TelemetryError::BadArg());
    return ok_or_status(r->inner.process_rx_queue_with_timeout(timeout_ms));
}

extern "C" int seds_router_process_all_queues_with_timeout(SedsRouter* r, std::uint32_t timeout_ms)
{
    if (!r) return status_from_err(TelemetryError::BadArg());
    return ok_or_status(r->inner.process_all_queues_with_timeout(timeout_ms));
}

// ----------------- typed extractors (unaligned-safe) -----------------

static int pkt_get_into_t(const SedsPacketView* pkt, void* out, std::size_t count,
                          std::size_t elem_size, std::uint32_t elem_kind);

extern "C" int seds_pkt_get_typed(const SedsPacketView* pkt, void* out, std::size_t count,
                                  std::size_t elem_size, std::uint32_t elem_kind)
{
    if (!pkt || (count > 0 && !out)) return status_from_err(TelemetryError::BadArg());
    return pkt_get_into_t(pkt, out, count, elem_size, elem_kind);
}

extern "C" int seds_pkt_get_f32(const SedsPacketView* pkt, float* out, std::size_t n)
{
    return seds_pkt_get_typed(pkt, out, n, 4, 2 /* SEDS_EK_FLOAT */);
}

// ---- payload decode helpers (unaligned-safe) ----

static int pkt_get_into_t(const SedsPacketView* pkt, void* out, const std::size_t count,
                          const std::size_t elem_size, const std::uint32_t elem_kind)
{
    // 0=unsigned,1=signed,2=float (same tags as Rust)
    if (elem_kind == 0u)
    {
        switch (elem_size)
        {
            case 1:
                if (pkt->payload_len != count) return status_from_err(TelemetryError::SizeMismatchError());
                std::memcpy(out, pkt->payload, count);
                return 0;
            case 2:
            {
                if (pkt->payload_len != count * 2) return status_from_err(TelemetryError::SizeMismatchError());
                for (std::size_t i = 0; i < count; ++i)
                {
                    const std::uint8_t* b = pkt->payload + i * 2;
                    std::uint16_t v = static_cast<std::uint16_t>(b[0]) |
                                      (static_cast<std::uint16_t>(b[1]) << 8);
                    std::memcpy(static_cast<std::uint8_t*>(out) + i * 2, &v, 2);
                }
                return 0;
            }
            case 4:
            {
                if (pkt->payload_len != count * 4) return status_from_err(TelemetryError::SizeMismatchError());
                for (std::size_t i = 0; i < count; ++i)
                {
                    const std::uint8_t* b = pkt->payload + i * 4;
                    std::uint32_t v = (static_cast<std::uint32_t>(b[0])) |
                                      (static_cast<std::uint32_t>(b[1]) << 8) |
                                      (static_cast<std::uint32_t>(b[2]) << 16) |
                                      (static_cast<std::uint32_t>(b[3]) << 24);
                    std::memcpy(static_cast<std::uint8_t*>(out) + i * 4, &v, 4);
                }
                return 0;
            }
            case 8:
            {
                if (pkt->payload_len != count * 8) return status_from_err(TelemetryError::SizeMismatchError());
                for (std::size_t i = 0; i < count; ++i)
                {
                    const std::uint8_t* b = pkt->payload + i * 8;
                    std::uint64_t v =
                        (static_cast<std::uint64_t>(b[0])) |
                        (static_cast<std::uint64_t>(b[1]) << 8) |
                        (static_cast<std::uint64_t>(b[2]) << 16) |
                        (static_cast<std::uint64_t>(b[3]) << 24) |
                        (static_cast<std::uint64_t>(b[4]) << 32) |
                        (static_cast<std::uint64_t>(b[5]) << 40) |
                        (static_cast<std::uint64_t>(b[6]) << 48) |
                        (static_cast<std::uint64_t>(b[7]) << 56);
                    std::memcpy(static_cast<std::uint8_t*>(out) + i * 8, &v, 8);
                }
                return 0;
            }
            default: return status_from_err(TelemetryError::BadArg());
        }
    }
    if (elem_kind == 1u)
    {
        switch (elem_size)
        {
            case 1:
                if (pkt->payload_len != count) return status_from_err(TelemetryError::SizeMismatchError());
                std::memcpy(out, pkt->payload, count);
                return 0;
            case 2:
            {
                if (pkt->payload_len != count * 2) return status_from_err(TelemetryError::SizeMismatchError());
                for (std::size_t i = 0; i < count; ++i)
                {
                    const std::uint8_t* b = pkt->payload + i * 2;
                    auto v = static_cast<std::int16_t>(
                        (static_cast<std::uint16_t>(b[0])) |
                        (static_cast<std::uint16_t>(b[1]) << 8));
                    std::memcpy(static_cast<std::uint8_t*>(out) + i * 2, &v, 2);
                }
                return 0;
            }
            case 4:
            {
                if (pkt->payload_len != count * 4) return status_from_err(TelemetryError::SizeMismatchError());
                for (std::size_t i = 0; i < count; ++i)
                {
                    const std::uint8_t* b = pkt->payload + i * 4;
                    auto v = static_cast<std::int32_t>(
                        (static_cast<std::uint32_t>(b[0])) |
                        (static_cast<std::uint32_t>(b[1]) << 8) |
                        (static_cast<std::uint32_t>(b[2]) << 16) |
                        (static_cast<std::uint32_t>(b[3]) << 24));
                    std::memcpy(static_cast<std::uint8_t*>(out) + i * 4, &v, 4);
                }
                return 0;
            }
            case 8:
            {
                if (pkt->payload_len != count * 8) return status_from_err(TelemetryError::SizeMismatchError());
                for (std::size_t i = 0; i < count; ++i)
                {
                    const std::uint8_t* b = pkt->payload + i * 8;
                    auto v = static_cast<std::int64_t>(
                        (static_cast<std::uint64_t>(b[0])) |
                        (static_cast<std::uint64_t>(b[1]) << 8) |
                        (static_cast<std::uint64_t>(b[2]) << 16) |
                        (static_cast<std::uint64_t>(b[3]) << 24) |
                        (static_cast<std::uint64_t>(b[4]) << 32) |
                        (static_cast<std::uint64_t>(b[5]) << 40) |
                        (static_cast<std::uint64_t>(b[6]) << 48) |
                        (static_cast<std::uint64_t>(b[7]) << 56));
                    std::memcpy(static_cast<std::uint8_t*>(out) + i * 8, &v, 8);
                }
                return 0;
            }
            default: return status_from_err(TelemetryError::BadArg());
        }
    }
    if (elem_kind == 2u)
    {
        switch (elem_size)
        {
            case 4:
            {
                if (pkt->payload_len != count * 4) return status_from_err(TelemetryError::SizeMismatchError());
                for (std::size_t i = 0; i < count; ++i)
                {
                    const std::uint8_t* b = pkt->payload + i * 4;
                    std::uint32_t u = (static_cast<std::uint32_t>(b[0])) |
                                      (static_cast<std::uint32_t>(b[1]) << 8) |
                                      (static_cast<std::uint32_t>(b[2]) << 16) |
                                      (static_cast<std::uint32_t>(b[3]) << 24);
                    float v;
                    std::memcpy(&v, &u, 4);
                    std::memcpy(static_cast<std::uint8_t*>(out) + i * 4, &v, 4);
                }
                return 0;
            }
            case 8:
            {
                if (pkt->payload_len != count * 8) return status_from_err(TelemetryError::SizeMismatchError());
                for (std::size_t i = 0; i < count; ++i)
                {
                    const std::uint8_t* b = pkt->payload + i * 8;
                    std::uint64_t u =
                        (static_cast<std::uint64_t>(b[0])) |
                        (static_cast<std::uint64_t>(b[1]) << 8) |
                        (static_cast<std::uint64_t>(b[2]) << 16) |
                        (static_cast<std::uint64_t>(b[3]) << 24) |
                        (static_cast<std::uint64_t>(b[4]) << 32) |
                        (static_cast<std::uint64_t>(b[5]) << 40) |
                        (static_cast<std::uint64_t>(b[6]) << 48) |
                        (static_cast<std::uint64_t>(b[7]) << 56);
                    double v;
                    std::memcpy(&v, &u, 8);
                    std::memcpy(static_cast<std::uint8_t*>(out) + i * 8, &v, 8);
                }
                return 0;
            }
            default: return status_from_err(TelemetryError::BadArg());
        }
    }
    return status_from_err(TelemetryError::BadArg());
}

// ----------------- typed logging (unaligned-safe) -----------------

// Send-now variant: copies unaligned source to aligned tmp, then Router::log<T>
template<typename T>
static int log_unaligned_slice_send(Router& router, DataType ty, const void* data, std::size_t count)
{
    if (!data && count) return status_from_err(TelemetryError::BadArg());
    std::vector<T> tmp(count);
    std::memcpy(tmp.data(), data, count * sizeof(T));  // safe: tmp is aligned
    return ok_or_status(router.log<T>(ty, tmp));
}

// Queue variant: performs same as Rust queue path (if you expose one)
template<typename T>
static int log_unaligned_slice_queue(Router& router, DataType ty, const void* data, std::size_t count)
{
    if (!data && count) return status_from_err(TelemetryError::BadArg());
    std::vector<T> tmp(count);
    std::memcpy(tmp.data(), data, count * sizeof(T));
    // If your Router has a queue variant, call it; otherwise reuse log<T> and queue the packet.
    return ok_or_status(router.log<T>(ty, tmp));
}

extern "C" int seds_router_log_typed(SedsRouter* r, std::uint32_t ty_u32,
                                     const void* data, std::size_t count,
                                     std::size_t elem_size, std::uint32_t elem_kind)
{
    if (!r || (count > 0 && !data)) return status_from_err(TelemetryError::BadArg());
    auto ty = dtype_from_u32(ty_u32);
    if (ty.is_err()) return status_from_err(ty.unwrap_err());
    const DataType dt = ty.unwrap();

    switch (elem_kind)
    {
        case 0: // unsigned
            switch (elem_size)
            {
                case 1: return log_unaligned_slice_send<std::uint8_t >(r->inner, dt, data, count);
                case 2: return log_unaligned_slice_send<std::uint16_t>(r->inner, dt, data, count);
                case 4: return log_unaligned_slice_send<std::uint32_t>(r->inner, dt, data, count);
                case 8: return log_unaligned_slice_send<std::uint64_t>(r->inner, dt, data, count);
                default: return status_from_err(TelemetryError::BadArg());
            }
        case 1: // signed
            switch (elem_size)
            {
                case 1: return log_unaligned_slice_send<std::int8_t >(r->inner, dt, data, count);
                case 2: return log_unaligned_slice_send<std::int16_t>(r->inner, dt, data, count);
                case 4: return log_unaligned_slice_send<std::int32_t>(r->inner, dt, data, count);
                case 8: return log_unaligned_slice_send<std::int64_t>(r->inner, dt, data, count);
                default: return status_from_err(TelemetryError::BadArg());
            }
        case 2: // float
            switch (elem_size)
            {
                case 4: return log_unaligned_slice_send<float >(r->inner, dt, data, count);
                case 8: return log_unaligned_slice_send<double>(r->inner, dt, data, count);
                default: return status_from_err(TelemetryError::BadArg());
            }
        default: return status_from_err(TelemetryError::BadArg());
    }
}

extern "C" int seds_router_log_queue_typed(SedsRouter* r, std::uint32_t ty_u32,
                                           const void* data, std::size_t count,
                                           std::size_t elem_size, std::uint32_t elem_kind)
{
    if (!r || (count > 0 && !data)) return status_from_err(TelemetryError::BadArg());
    auto ty = dtype_from_u32(ty_u32);
    if (ty.is_err()) return status_from_err(ty.unwrap_err());
    const DataType dt = ty.unwrap();

    switch (elem_kind)
    {
        case 0: // unsigned
            switch (elem_size)
            {
                case 1: return log_unaligned_slice_queue<std::uint8_t >(r->inner, dt, data, count);
                case 2: return log_unaligned_slice_queue<std::uint16_t>(r->inner, dt, data, count);
                case 4: return log_unaligned_slice_queue<std::uint32_t>(r->inner, dt, data, count);
                case 8: return log_unaligned_slice_queue<std::uint64_t>(r->inner, dt, data, count);
                default: return status_from_err(TelemetryError::BadArg());
            }
        case 1: // signed
            switch (elem_size)
            {
                case 1: return log_unaligned_slice_queue<std::int8_t >(r->inner, dt, data, count);
                case 2: return log_unaligned_slice_queue<std::int16_t>(r->inner, dt, data, count);
                case 4: return log_unaligned_slice_queue<std::int32_t>(r->inner, dt, data, count);
                case 8: return log_unaligned_slice_queue<std::int64_t>(r->inner, dt, data, count);
                default: return status_from_err(TelemetryError::BadArg());
            }
        case 2: // float
            switch (elem_size)
            {
                case 4: return log_unaligned_slice_queue<float >(r->inner, dt, data, count);
                case 8: return log_unaligned_slice_queue<double>(r->inner, dt, data, count);
                default: return status_from_err(TelemetryError::BadArg());
            }
        default: return status_from_err(TelemetryError::BadArg());
    }
}
