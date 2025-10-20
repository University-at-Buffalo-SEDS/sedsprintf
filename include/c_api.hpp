#pragma once

#include <cstdint>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

// --------- public C ABI mirror of the Rust layout ---------

// constants (sizes/kinds)
enum : std::size_t { SIZE_OF_U8 = 1, SIZE_OF_U16 = 2, SIZE_OF_U32 = 4, SIZE_OF_F64 = 8 };

enum : std::uint32_t { SEDS_EK_UNSIGNED = 0, SEDS_EK_SIGNED = 1, SEDS_EK_FLOAT = 2 };

// forward decl of opaque router
struct SedsRouter;

// must match C header layout
struct SedsPacketView
{
    std::uint32_t ty; // DataType as u32
    std::size_t data_size; // payload expected size
    const char * sender; // pointer
    std::size_t sender_len; // length
    const std::uint32_t * endpoints;
    std::size_t num_endpoints;
    std::uint64_t timestamp;
    const std::uint8_t * payload;
    std::size_t payload_len;
};

typedef int (* CTransmit)(const std::uint8_t * bytes, std::size_t len, void * user);

typedef int (* CEndpointHandler)(const SedsPacketView * pkt, void * user);

typedef std::uint64_t (* CNowMs)(void * user);

struct SedsHandlerDesc
{
    std::uint32_t endpoint; // DataEndpoint as u32
    CEndpointHandler handler;
    void * user;
};

// ---- string helpers for packet pretty prints ----
int seds_pkt_header_string_len(const SedsPacketView * pkt);

int seds_pkt_to_string_len(const SedsPacketView * pkt);

int seds_pkt_header_string(const SedsPacketView * pkt, char * buf, std::size_t buf_len);

int seds_pkt_to_string(const SedsPacketView * pkt, char * buf, std::size_t buf_len);

// ---- router lifecycle ----
SedsRouter * seds_router_new(CTransmit tx,
                             void * tx_user,
                             CNowMs now_ms_cb,
                             const SedsHandlerDesc * handlers,
                             std::size_t n_handlers);

void seds_router_free(const SedsRouter * r);

// ---- logging (bytes / f32) ----
int seds_router_log_bytes(SedsRouter * r, std::uint32_t ty_u32,
                          const std::uint8_t * data, std::size_t len);

int seds_router_log_f32(SedsRouter * r, std::uint32_t ty_u32,
                        const float * vals, std::size_t n_vals);

// ---- receive serialized / packet view ----
int seds_router_receive_serialized(SedsRouter * r, const std::uint8_t * bytes, std::size_t len);

int seds_router_receive(SedsRouter * r, const SedsPacketView * view);

// ---- queues ----
int seds_router_process_send_queue(SedsRouter * r);

int seds_router_queue_tx_message(SedsRouter * r, const SedsPacketView * view);

int seds_router_process_received_queue(SedsRouter * r);

int seds_router_rx_serialized_packet_to_queue(SedsRouter * r, const std::uint8_t * bytes, std::size_t len);

int seds_router_rx_packet_to_queue(SedsRouter * r, const SedsPacketView * view);

int seds_router_process_all_queues(SedsRouter * r);

int seds_router_clear_queues(SedsRouter * r);

int seds_router_clear_rx_queue(SedsRouter * r);

int seds_router_clear_tx_queue(SedsRouter * r);

// ---- time-budgeted queue processing ----
int seds_router_process_tx_queue_with_timeout(SedsRouter * r, std::uint32_t timeout_ms);

int seds_router_process_rx_queue_with_timeout(SedsRouter * r, std::uint32_t timeout_ms);

int seds_router_process_all_queues_with_timeout(SedsRouter * r, std::uint32_t timeout_ms);

// ---- typed helpers (unaligned-safe) ----
int seds_pkt_get_typed(const SedsPacketView * pkt, void * out, std::size_t count,
                       std::size_t elem_size, std::uint32_t elem_kind);

int seds_pkt_get_f32(const SedsPacketView * pkt, float * out, std::size_t n);

// ---- generic typed logging (unaligned-safe) ----
int seds_router_log_typed(SedsRouter * r, std::uint32_t ty_u32,
                          const void * data, std::size_t count,
                          std::size_t elem_size, std::uint32_t elem_kind);

int seds_router_log_queue_typed(SedsRouter * r, std::uint32_t ty_u32,
                                const void * data, std::size_t count,
                                std::size_t elem_size, std::uint32_t elem_kind);

#ifdef __cplusplus
} // extern "C"
#endif
