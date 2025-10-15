#ifndef SEDSPRINTF_C_H
#define SEDSPRINTF_C_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==============================
// Note: Besides the macros, everything in this file is designed to match the Rust API
// for easy mapping into C code. It contains no state or complex logic nor any live data.
// Any changes to the Rust API must be reflected here as well.
// ==============================


// ==============================
// Public enums / constants
// ==============================

/** \brief Telemetry data types (keep in sync with Rust DataType order). */
typedef enum SedsDataType
{
    SEDS_DT_TELEMETRY_ERROR = 0,
    SEDS_DT_GPS             = 1,
    SEDS_DT_IMU             = 2,
    SEDS_DT_BATTERY         = 3,
    SEDS_DT_SYSTEM          = 4,
    SEDS_DT_BAROMETER       = 5,
    SEDS_IMU_HEX_DATA       = 6
} SedsDataType;

/** \brief Telemetry endpoints (keep in sync with Rust DataEndpoint order). */
typedef enum SedsDataEndpoint
{
    SEDS_EP_SD    = 0,
    SEDS_EP_RADIO = 1,
} SedsDataEndpoint;

/** \brief Status codes returned by the C API. */
typedef enum
{
    SEDS_OK            =  0,
    SEDS_ERR           = -1,
    SEDS_BAD_ARG       = -2,
    SEDS_INVALID_TYPE  = -3,
    SEDS_SIZE_MISMATCH = -4,
    SEDS_DESERIALIZE   = -5,
    SEDS_HANDLER_ERROR = -6,
} SedsResult;

/** \brief Opaque Router handle. */
typedef struct SedsRouter SedsRouter;

/** \brief Lightweight view of a telemetry packet (valid only during a callback). */
typedef struct SedsPacketView
{
    uint32_t ty;             /**< DataType as u32 */
    size_t   data_size;      /**< Payload size in bytes */

    /* Rust `&str` is (ptr,len). Not NUL-terminated. Lifetime = callback. */
    const char * sender;     /**< UTF-8 bytes; may not be NUL-terminated */
    size_t       sender_len; /**< Number of bytes at `sender` */

    const uint32_t * endpoints; /**< Array of DataEndpoint (as u32) */
    size_t           num_endpoints;
    uint64_t         timestamp;  /**< Timestamp units as defined by your system */
    const uint8_t  * payload;    /**< Raw payload bytes */
    size_t           payload_len;/**< == data_size */
} SedsPacketView;

/** \brief Transmit callback: return 0 on success, non-zero on failure. */
typedef SedsResult (* SedsTransmitFn)(const uint8_t * bytes, size_t len, void * user);

/** \brief Local endpoint handler: return 0 on success, non-zero on failure. */
typedef SedsResult (* SedsEndpointHandlerFn)(const SedsPacketView * pkt, void * user);

/** \brief Endpoint handler descriptor (used when constructing a router). */
typedef struct SedsLocalEndpointDesc
{
    uint32_t              endpoint; /**< DataEndpoint as u32 */
    SedsEndpointHandlerFn handler;  /**< Callback function */
    void                * user;     /**< Opaque user context passed to callback */
} SedsLocalEndpointDesc;

/**
 * \brief Get required buffer length for seds_pkt_header_string() (includes NUL).
 * \param pkt Pointer to a valid SedsPacketView.
 * \return Required number of bytes (>=1) or negative on error.
 */
int32_t seds_pkt_header_string_len(const SedsPacketView * pkt);

/**
 * \brief Get required buffer length for seds_pkt_to_string() (includes NUL).
 * \param pkt Pointer to a valid SedsPacketView.
 * \return Required number of bytes (>=1) or negative on error.
 */
int32_t seds_pkt_to_string_len(const SedsPacketView * pkt);

// ==============================
// Packet -> string formatting
// ==============================

/**
 * \brief Write the packet header as text.
 *
 * If \p buf is NULL or \p buf_len == 0, no data is written and the function returns the
 * required buffer length **including** the terminating NUL. If \p buf is non-NULL, up to
 * (\p buf_len - 1) characters are written and the output is always NUL-terminated on success.
 *
 * \return Required length including NUL (>=1), or a negative error code.
 */
int32_t seds_pkt_header_string(const SedsPacketView * pkt, char * buf, size_t buf_len);

/**
 * \brief Write the full packet (header + formatted payload) as text.
 *
 * Same sizing rules as seds_pkt_header_string().
 *
 * \return Required length including NUL (>=1), or a negative error code.
 */
int32_t seds_pkt_to_string(const SedsPacketView * pkt, char * buf, size_t buf_len);


// ==============================
// Router lifecycle
// ==============================

/**
 * \brief Create a router with an optional transmit callback and an array of local handlers.
 *
 * \param tx         Optional transmit function (NULL if no remote transmit).
 * \param tx_user    Opaque user pointer for \p tx.
 * \param handlers   Array of handler descriptors (may be NULL if n_handlers==0).
 * \param n_handlers Number of entries in \p handlers.
 * \return           New router instance, or NULL on failure.
 */
SedsRouter * seds_router_new(SedsTransmitFn tx,
                             void * tx_user,
                             const SedsLocalEndpointDesc * handlers,
                             size_t n_handlers);

/**
 * \brief Destroy a router created by seds_router_new().
 *
 * \param r Router handle (NULL is safe and a no-op).
 */
void seds_router_free(SedsRouter * r);


// ==============================
// Generic typed logging
// ==============================

/** \brief Element kind used by the generic logging/extraction APIs. */
typedef enum SedsElemKind
{
    SEDS_EK_UNSIGNED = 0, /**< u8/u16/u32/u64/... */
    SEDS_EK_SIGNED   = 1, /**< i8/i16/i32/i64/... */
    SEDS_EK_FLOAT    = 2  /**< f32/f64 */
} SedsElemKind;

/**
 * \brief Log a typed slice; the router encodes elements to little-endian bytes internally.
 *
 * The total number of bytes (count * elem_size) **must** match the schema size for \p ty.
 * Supported element sizes are 1, 2, 4, and 8 bytes.
 */
SedsResult seds_router_log_typed(SedsRouter * r,
                                 SedsDataType ty,
                                 const void * data,
                                 size_t count,
                                 size_t elem_size,
                                 SedsElemKind elem_kind,
                                 uint64_t timestamp);

/**
 * \brief Queue a typed slice to the TX queue.
 *
 * Same constraints as seds_router_log_typed().
 */
SedsResult seds_router_log_queue_typed(SedsRouter * r,
                                       SedsDataType ty,
                                       const void * data,
                                       size_t count,
                                       size_t elem_size,
                                       SedsElemKind elem_kind,
                                       uint64_t timestamp);

// ==============================
// Convenience logging forms
// ==============================

SedsResult seds_router_log_bytes(SedsRouter * r,
                                 SedsDataType ty,
                                 const uint8_t * data,
                                 size_t len,
                                 uint64_t timestamp);

SedsResult seds_router_log_f32(SedsRouter * r,
                               SedsDataType ty,
                               const float * vals,
                               size_t n_vals,
                               uint64_t timestamp);


// ==============================
// Wire receive / helpers
// ==============================

SedsResult seds_router_receive_serialized(SedsRouter * r,
                                          const uint8_t * bytes,
                                          size_t len);

SedsResult seds_router_receive(SedsRouter * r,
                               SedsPacketView * view);

SedsResult seds_router_process_send_queue(SedsRouter * r);

SedsResult seds_router_queue_tx_message(SedsRouter * r,
                                        const SedsPacketView * view);

/* ---------------------- RX (receive) side helpers --------------------- */

SedsResult seds_router_process_received_queue(SedsRouter * r);

SedsResult seds_router_rx_serialized_packet_to_queue(SedsRouter * r,
                                                     const uint8_t * bytes,
                                                     size_t len);

SedsResult seds_router_rx_packet_to_queue(SedsRouter * r,
                                          const SedsPacketView * view);


// ==============================
// Monotonic clock + timeout processing
// ==============================

/** Monotonic clock callback: must return milliseconds since an arbitrary epoch. */
typedef uint64_t (* SedsNowMsFn)(void * user);

SedsResult seds_router_process_tx_queue_with_timeout(
    SedsRouter * r,
    SedsNowMsFn now_ms_cb,
    void * user,
    uint32_t timeout_ms
);

SedsResult seds_router_process_rx_queue_with_timeout(
    SedsRouter * r,
    SedsNowMsFn now_ms_cb,
    void * user,
    uint32_t timeout_ms
);

SedsResult seds_router_process_all_queues_with_timeout(
    SedsRouter * r,
    SedsNowMsFn now_ms_cb,
    void * user,
    uint32_t timeout_ms
);


// ==============================
// Queue utilities
// ==============================

SedsResult seds_router_process_all_queues(SedsRouter * r);
SedsResult seds_router_clear_queues(SedsRouter * r);
SedsResult seds_router_clear_rx_queue(SedsRouter * r);
SedsResult seds_router_clear_tx_queue(SedsRouter * r);

// ==============================
// Payload extraction helpers
// ==============================

SedsResult seds_pkt_get_f32(const SedsPacketView * pkt, float * out, size_t n);

SedsResult seds_pkt_get_typed(const SedsPacketView * pkt,
                              void * out,
                              size_t count,
                              size_t elem_size,
                              SedsElemKind elem_kind);

#ifdef __cplusplus
} // extern "C"
#endif


// ============================================================================
// Header-only helpers for nicer C++ call sites (MUST NOT be in extern "C")
// ============================================================================
#if defined(__cplusplus)

namespace seds_detail {
    template <typename T> struct elem_traits;

    template<> struct elem_traits<uint8_t>  { static constexpr SedsElemKind kind = SEDS_EK_UNSIGNED; static constexpr size_t size = 1; };
    template<> struct elem_traits<uint16_t> { static constexpr SedsElemKind kind = SEDS_EK_UNSIGNED; static constexpr size_t size = 2; };
    template<> struct elem_traits<uint32_t> { static constexpr SedsElemKind kind = SEDS_EK_UNSIGNED; static constexpr size_t size = 4; };
    template<> struct elem_traits<uint64_t> { static constexpr SedsElemKind kind = SEDS_EK_UNSIGNED; static constexpr size_t size = 8; };

    template<> struct elem_traits<int8_t>   { static constexpr SedsElemKind kind = SEDS_EK_SIGNED;   static constexpr size_t size = 1; };
    template<> struct elem_traits<int16_t>  { static constexpr SedsElemKind kind = SEDS_EK_SIGNED;   static constexpr size_t size = 2; };
    template<> struct elem_traits<int32_t>  { static constexpr SedsElemKind kind = SEDS_EK_SIGNED;   static constexpr size_t size = 4; };
    template<> struct elem_traits<int64_t>  { static constexpr SedsElemKind kind = SEDS_EK_SIGNED;   static constexpr size_t size = 8; };

    template<> struct elem_traits<float>    { static constexpr SedsElemKind kind = SEDS_EK_FLOAT;    static constexpr size_t size = 4; };
    template<> struct elem_traits<double>   { static constexpr SedsElemKind kind = SEDS_EK_FLOAT;    static constexpr size_t size = 8; };
}

/**
 * \brief C++ convenience wrapper that deduces element kind/size from \p T and logs immediately.
 */
template<typename T>
static inline SedsResult seds_router(SedsRouter * router,
                                     SedsDataType datatype,
                                     const T * data,
                                     size_t count,
                                     uint64_t timestamp)
{
    return seds_router_log_typed(router,
                                 datatype,
                                 (const void *) data,
                                 count,
                                 seds_detail::elem_traits<T>::size,
                                 seds_detail::elem_traits<T>::kind,
                                 timestamp);
}

/**
 * \brief C++ convenience wrapper that deduces element kind/size from \p T and queues the log.
 */
template<typename T>
static inline SedsResult seds_router_queue(SedsRouter * router,
                                           SedsDataType datatype,
                                           const T * data,
                                           size_t count,
                                           uint64_t timestamp)
{
    return seds_router_log_queue_typed(router,
                                       datatype,
                                       (const void *) data,
                                       count,
                                       seds_detail::elem_traits<T>::size,
                                       seds_detail::elem_traits<T>::kind,
                                       timestamp);
}

/**
 * \brief C++ convenience extractor that deduces element kind/size from \p T.
 */
template<typename T>
static inline SedsResult seds_pkt_get(const SedsPacketView * pkt, T * out, size_t count)
{
    return seds_pkt_get_typed(pkt,
                              (void *) out,
                              count,
                              seds_detail::elem_traits<T>::size,
                              seds_detail::elem_traits<T>::kind);
}
#endif // defined(__cplusplus)


// ---------- C11 _Generic convenience macros ----------
#if !defined(__cplusplus) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L

/**
 * \brief Internal: infer (kind,size) from pointer expression \p x.
 */
#define SEDS__KIND_SIZE(x, KIND_OUT, SIZE_OUT) do {                                    \
    _Static_assert(sizeof(*(x))==1 || sizeof(*(x))==2 ||                                \
                   sizeof(*(x))==4 || sizeof(*(x))==8,                                  \
                   "element size must be 1,2,4,8");                                     \
    (SIZE_OUT) = (size_t)sizeof(*(x));                                                  \
    (KIND_OUT) = _Generic(*(x),                                                         \
        unsigned char: SEDS_EK_UNSIGNED,                                                \
        uint16_t:      SEDS_EK_UNSIGNED,                                                \
        uint32_t:      SEDS_EK_UNSIGNED,                                                \
        uint64_t:      SEDS_EK_UNSIGNED,                                                \
        signed char:   SEDS_EK_SIGNED,                                                  \
        int16_t:       SEDS_EK_SIGNED,                                                  \
        int32_t:       SEDS_EK_SIGNED,                                                  \
        int64_t:       SEDS_EK_SIGNED,                                                  \
        float:         SEDS_EK_FLOAT,                                                   \
        double:        SEDS_EK_FLOAT,                                                   \
        default:       SEDS_EK_UNSIGNED /* fallback for custom integer-like data */     \
    );                                                                                  \
} while(0)

/** C11 convenience wrapper that deduces element kind/size from \p data. */
#define seds_router_log(router, datatype, data, count, timestamp)                       \
    (__extension__({                                                                    \
        const void   *_seds_data  = (const void*)(data);                                \
        size_t        _seds_count = (size_t)(count);                                    \
        size_t        _seds_esize;                                                      \
        SedsElemKind  _seds_kind;                                                       \
        SEDS__KIND_SIZE((data), _seds_kind, _seds_esize);                               \
        seds_router_log_typed((router), (datatype), _seds_data, _seds_count,            \
                              _seds_esize, _seds_kind, (timestamp));                    \
    }))

/** C11 convenience wrapper that deduces element kind/size from \p data and queues it. */
#define seds_router_log_queue(router, datatype, data, count, timestamp)                 \
    (__extension__({                                                                    \
        const void   *_seds_data  = (const void*)(data);                                \
        size_t        _seds_count = (size_t)(count);                                    \
        size_t        _seds_esize;                                                      \
        SedsElemKind  _seds_kind;                                                       \
        SEDS__KIND_SIZE((data), _seds_kind, _seds_esize);                               \
        seds_router_log_queue_typed((router), (datatype), _seds_data, _seds_count,      \
                                    _seds_esize, _seds_kind, (timestamp));              \
    }))

/** C11 convenience extractor that deduces element kind/size from \p out. */
#define SEDS__PKT_KIND_SIZE(x, KIND_OUT, SIZE_OUT) SEDS__KIND_SIZE(x, KIND_OUT, SIZE_OUT)

#define seds_pkt_get(pkt, out, count)                                                   \
    (__extension__({                                                                    \
        void       *_s_out     = (void*)(out);                                          \
        size_t      _s_count   = (size_t)(count);                                       \
        size_t      _s_esize;                                                           \
        SedsElemKind _s_kind;                                                            \
        SEDS__PKT_KIND_SIZE((out), _s_kind, _s_esize);                                  \
        seds_pkt_get_typed((pkt), _s_out, _s_count, _s_esize, _s_kind);                 \
    }))
#endif // C11 generic

#endif // SEDSPRINTF_C_H
