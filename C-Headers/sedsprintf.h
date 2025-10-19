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

/**
 * @brief Telemetry data types (keep in sync with Rust DataType order).
 */
typedef enum SedsDataType
{
    SEDS_DT_TELEMETRY_ERROR = 0,
    SEDS_DT_GPS = 1,
    SEDS_DT_IMU = 2,
    SEDS_DT_BATTERY = 3,
    SEDS_DT_SYSTEM = 4,
    SEDS_DT_BAROMETER = 5,
    SEDS_IMU_HEX_DATA = 6
} SedsDataType;

/**
 * @brief Telemetry endpoints (keep in sync with Rust DataEndpoint order).
 */
typedef enum SedsDataEndpoint
{
    SEDS_EP_SD = 0,
    SEDS_EP_RADIO = 1,
} SedsDataEndpoint;

/**
 * @brief Status codes returned by the C API.
 */
typedef enum
{
    SEDS_OK = 0, /**< Success. */
    SEDS_ERR = -1, /**< Unspecified error. */
    SEDS_BAD_ARG = -2, /**< One or more arguments are invalid (NULL/size mismatch/etc). */
    SEDS_INVALID_TYPE = -3, /**< Unsupported/unknown type or endpoint value. */
    SEDS_SIZE_MISMATCH = -4, /**< Element/byte length does not match schema requirement. */
    SEDS_DESERIALIZE = -5, /**< Failed to deserialize a packet from bytes. */
    SEDS_HANDLER_ERROR = -6, /**< A user-provided endpoint handler returned an error. */
} SedsResult;

/**
 * @brief Monotonic clock callback: must return milliseconds since an arbitrary epoch.
 *
 * @param user Opaque user pointer supplied at router construction.
 * @return Current monotonic time in milliseconds.
 *
 * @note The epoch is implementation-defined; only monotonicity matters.
 */
typedef uint64_t (* SedsNowMsFn)(void * user);

/** @brief Opaque Router handle. */
typedef struct SedsRouter SedsRouter;

/**
 * @brief Lightweight view of a telemetry packet (valid only during a callback).
 *
 * The view borrows data owned by the router. Fields must not be retained past the
 * callback that provided the view. Strings are not NUL-terminated unless specified.
 */
typedef struct SedsPacketView
{
    uint32_t ty; /**< DataType as u32. */
    size_t data_size; /**< Payload size in bytes. */

    /* Rust `&str` is (ptr,len). Not NUL-terminated. Lifetime = callback. */
    const char * sender; /**< UTF-8 bytes; may not be NUL-terminated. */
    size_t sender_len; /**< Number of bytes at `sender`. */

    const uint32_t * endpoints; /**< Array of DataEndpoint (as u32). */
    size_t num_endpoints;
    uint64_t timestamp; /**< Timestamp units as defined by your system. */
    const uint8_t * payload; /**< Raw payload bytes. */
    size_t payload_len; /**< == data_size. */
} SedsPacketView;


typedef enum SedsElemKind
{
    SEDS_EK_UNSIGNED = 0, /**< u8/u16/u32/u64/... */
    SEDS_EK_SIGNED = 1, /**< i8/i16/i32/i64/... */
    SEDS_EK_FLOAT = 2 /**< f32/f64 */
} SedsElemKind;

/**
 * @brief Transmit callback.
 *
 * @param bytes Pointer to serialized bytes to transmit.
 * @param len   Number of bytes at @p bytes.
 * @param user  Opaque user pointer supplied at router construction.
 * @return SEDS_OK on success, or a negative @ref SedsResult error.
 *
 * @note Returning an error stops the current send processing.
 */
typedef SedsResult (* SedsTransmitFn)(const uint8_t * bytes, size_t len, void * user);

/**
 * @brief Local endpoint handler callback.
 *
 * @param pkt  Borrowed view of the packet. Valid only during the call.
 * @param user Opaque user pointer supplied in the handler descriptor.
 * @return SEDS_OK on success, or a negative @ref SedsResult error.
 *
 * @warning Do not retain pointers from @p pkt after returning.
 */
typedef SedsResult (* SedsEndpointHandlerFn)(const SedsPacketView * pkt, void * user);

/**
 * @brief Endpoint handler descriptor (used when constructing a router).
 */
typedef struct SedsLocalEndpointDesc
{
    uint32_t endpoint; /**< DataEndpoint as u32. */
    SedsEndpointHandlerFn handler; /**< Callback function. */
    void * user; /**< Opaque user context passed to callback. */
} SedsLocalEndpointDesc;

/**
 * @brief Get required buffer length for @ref seds_pkt_header_string (includes NUL).
 *
 * @param pkt Pointer to a valid @ref SedsPacketView.
 * @return Required number of bytes (>=1) including the terminating NUL,
 *         or a negative @ref SedsResult on error (e.g., SEDS_BAD_ARG).
 *
 * @note Use this to size your buffer prior to calling @ref seds_pkt_header_string.
 */
int32_t seds_pkt_header_string_len(const SedsPacketView * pkt);

/**
 * @brief Get required buffer length for @ref seds_pkt_to_string (includes NUL).
 *
 * @param pkt Pointer to a valid @ref SedsPacketView.
 * @return Required number of bytes (>=1) including the terminating NUL,
 *         or a negative @ref SedsResult on error (e.g., SEDS_BAD_ARG).
 *
 * @note Use this to size your buffer prior to calling @ref seds_pkt_to_string.
 */
int32_t seds_pkt_to_string_len(const SedsPacketView * pkt);

// ==============================
// Packet -> string formatting
// ==============================

/**
 * @brief Write the packet header as text.
 *
 * If @p buf is NULL or @p buf_len == 0, no data is written and the function returns the
 * required buffer length **including** the terminating NUL. If @p buf is non-NULL, up to
 * (@p buf_len - 1) characters are written and the output is always NUL-terminated on success.
 *
 * @param pkt     Pointer to a valid @ref SedsPacketView.
 * @param buf     Destination buffer or NULL to query the required length.
 * @param buf_len Size of @p buf in bytes when non-NULL.
 * @return Required length including NUL (>=1) on success, or a negative @ref SedsResult.
 *
 * @retval SEDS_BAD_ARG If @p pkt is NULL, or @p buf is non-NULL but @p buf_len is 0.
 */
int32_t seds_pkt_header_string(const SedsPacketView * pkt, char * buf, size_t buf_len);

/**
 * @brief Write the full packet (header + formatted payload) as text.
 *
 * Same sizing rules as @ref seds_pkt_header_string.
 *
 * @param pkt     Pointer to a valid @ref SedsPacketView.
 * @param buf     Destination buffer or NULL to query the required length.
 * @param buf_len Size of @p buf in bytes when non-NULL.
 * @return Required length including NUL (>=1) on success, or a negative @ref SedsResult.
 */
int32_t seds_pkt_to_string(const SedsPacketView * pkt, char * buf, size_t buf_len);


// ==============================
// Router lifecycle
// ==============================

/**
 * @brief Create a router with an optional transmit callback and an array of local handlers.
 *
 * @param tx         Optional transmit function (NULL if no remote transmit).
 * @param tx_user    Opaque user pointer for @p tx.
 * @param now_ms_cb  Callback to get current time in milliseconds (must not be NULL).
 * @param handlers   Array of handler descriptors (may be NULL if @p n_handlers==0).
 * @param n_handlers Number of entries in @p handlers.
 * @return New router instance, or NULL on failure.
 *
 * @retval NULL If allocation fails or @p now_ms_cb is NULL.
 * @note Each entry in @p handlers registers a local endpoint handler invoked
 *       when packets targeting that endpoint are received locally.
 * @warning This function does not copy @p handlers; it copies only the descriptors’ values.
 */
SedsRouter * seds_router_new(SedsTransmitFn tx,
                             void * tx_user,
                             SedsNowMsFn now_ms_cb,
                             const SedsLocalEndpointDesc * handlers,
                             size_t n_handlers);

/**
 * @brief Destroy a router created by @ref seds_router_new.
 *
 * @param r Router handle (NULL is safe and a no-op).
 *
 * @post All queues and internal resources are freed.
 * @warning Do not use @p r after calling this function.
 */
void seds_router_free(SedsRouter * r);


// ==============================
// Generic typed logging
// ==============================

/**
 * @brief Log a typed slice; the router encodes elements to little-endian bytes internally.
 *
 * The total number of bytes (count * elem_size) **must** match the schema size for @p ty.
 * Supported element sizes are 1, 2, 4, and 8 bytes.
 *
 * @param r          Router handle (must not be NULL).
 * @param ty         Telemetry data type.
 * @param data       Pointer to the first element of the typed slice (must not be NULL if count>0).
 * @param count      Number of elements at @p data.
 * @param elem_size  Size of each element in bytes (1,2,4, or 8).
 * @param elem_kind  Element kind (unsigned/signed/float).
 * @param timestamp  Sample timestamp (router does not alter this).
 * @return SEDS_OK on success, or a negative @ref SedsResult.
 *
 * @retval SEDS_BAD_ARG If any argument is invalid.
 * @retval SEDS_SIZE_MISMATCH If the total bytes do not match the schema for @p ty.
 * @note This call logs immediately to all relevant local handlers and/or queues a copy for TX,
 *       depending on router configuration.
 */
SedsResult seds_router_log_typed(SedsRouter * r,
                                 SedsDataType ty,
                                 const void * data,
                                 size_t count,
                                 size_t elem_size,
                                 SedsElemKind elem_kind,
                                 uint64_t timestamp);

/**
 * @brief Queue a typed slice to the TX queue (no immediate transmit).
 *
 * Same constraints as @ref seds_router_log_typed. Use @ref seds_router_process_send_queue
 * to flush the queued packets through the transmit callback.
 *
 * @param r          Router handle (must not be NULL).
 * @param ty         Telemetry data type.
 * @param data       Pointer to typed data.
 * @param count      Number of elements.
 * @param elem_size  Size of each element (1,2,4,8).
 * @param elem_kind  Element kind (unsigned/signed/float).
 * @param timestamp  Sample timestamp.
 * @return SEDS_OK on success, or a negative @ref SedsResult.
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

/**
 * @brief Log a raw byte slice for @p ty.
 *
 * @param r         Router handle.
 * @param ty        Telemetry data type.
 * @param data      Pointer to bytes (must not be NULL if @p len>0).
 * @param len       Number of bytes at @p data.
 * @param timestamp Sample timestamp.
 * @return SEDS_OK on success, or a negative @ref SedsResult.
 *
 * @note The router validates that @p len matches the schema for @p ty.
 */
SedsResult seds_router_log_bytes(SedsRouter * r,
                                 SedsDataType ty,
                                 const uint8_t * data,
                                 size_t len,
                                 uint64_t timestamp);

/**
 * @brief Log an array of 32-bit floats for @p ty.
 *
 * @param r         Router handle.
 * @param ty        Telemetry data type.
 * @param vals      Pointer to @p n_vals f32 values.
 * @param n_vals    Number of float values (bytes = n_vals * 4).
 * @param timestamp Sample timestamp.
 * @return SEDS_OK on success, or a negative @ref SedsResult.
 */
SedsResult seds_router_log_f32(SedsRouter * r,
                               SedsDataType ty,
                               const float * vals,
                               size_t n_vals,
                               uint64_t timestamp);


// ==============================
// Wire receive / helpers
// ==============================

/**
 * @brief Deserialize a serialized packet and deliver/queue it as appropriate.
 *
 * @param r     Router handle.
 * @param bytes Pointer to serialized bytes (must not be NULL if @p len>0).
 * @param len   Number of bytes.
 * @return SEDS_OK on success, or a negative @ref SedsResult.
 *
 * @retval SEDS_DESERIALIZE If decoding fails.
 */
SedsResult seds_router_receive_serialized(SedsRouter * r,
                                          const uint8_t * bytes,
                                          size_t len);

/**
 * @brief Deliver a packet already represented as a @ref SedsPacketView.
 *
 * @param r    Router handle.
 * @param view Pointer to a view describing the packet.
 * @return SEDS_OK on success, or a negative @ref SedsResult.
 *
 * @note The router copies the data it needs; ownership of @p view remains with the caller.
 */
SedsResult seds_router_receive(SedsRouter * r,
                               SedsPacketView * view);

/**
 * @brief Process all packets in the TX queue: serialize and send them out.
 *
 * If a transmit callback was provided at router creation, it is invoked for each
 * packet. If the callback returns an error, processing stops and the error is returned.
 *
 * If no transmit callback was provided, this function is a no-op that returns SEDS_OK.
 *
 * @param r Router handle.
 * @return SEDS_OK if all packets were sent (or no TX callback was set),
 *         or a negative @ref SedsResult (e.g., callback failure).
 */
SedsResult seds_router_process_send_queue(SedsRouter * r);

/**
 * @brief Queue a packet for transmission.
 *
 * @param r    Router handle.
 * @param view Packet view to enqueue for TX.
 * @return SEDS_OK on success, or a negative @ref SedsResult.
 *
 * @note Use @ref seds_router_process_send_queue to flush the queue.
 */
SedsResult seds_router_queue_tx_message(SedsRouter * r,
                                        const SedsPacketView * view);

/* ---------------------- RX (receive) side helpers --------------------- */

/**
 * @brief Process all received packets in the RX queue, invoking local handlers.
 *
 * @param r Router handle.
 * @return SEDS_OK on success, or a negative @ref SedsResult.
 *
 * @note If a handler returns an error, processing stops and that error is returned.
 */
SedsResult seds_router_process_received_queue(SedsRouter * r);

/**
 * @brief Deserialize and queue a serialized packet into the RX queue (do not process).
 *
 * @param r     Router handle.
 * @param bytes Serialized bytes.
 * @param len   Number of bytes.
 * @return SEDS_OK on success, or a negative @ref SedsResult (e.g., SEDS_DESERIALIZE).
 *
 * @note Use @ref seds_router_process_received_queue to deliver queued RX packets.
 */
SedsResult seds_router_rx_serialized_packet_to_queue(SedsRouter * r,
                                                     const uint8_t * bytes,
                                                     size_t len);

/**
 * @brief Queue a packet view into the RX queue (do not process).
 *
 * @param r    Router handle.
 * @param view Packet view to enqueue.
 * @return SEDS_OK on success, or a negative @ref SedsResult.
 */
SedsResult seds_router_rx_packet_to_queue(SedsRouter * r,
                                          const SedsPacketView * view);


// ==============================
// Monotonic clock + timeout processing
// ==============================

/**
 * @brief Process the TX queue until empty or until @p timeout_ms elapses.
 *
 * @param r           Router handle.
 * @param timeout_ms  Maximum processing time in milliseconds.
 * @return SEDS_OK if processing completed within the timeout or there was nothing to send;
 *         otherwise a negative @ref SedsResult (e.g., callback error).
 *
 * @note Time measurement uses the router’s @ref SedsNowMsFn.
 */
SedsResult seds_router_process_tx_queue_with_timeout(
    SedsRouter * r,
    uint32_t timeout_ms
);

/**
 * @brief Process the RX queue until empty or until @p timeout_ms elapses.
 *
 * @param r           Router handle.
 * @param timeout_ms  Maximum processing time in milliseconds.
 * @return SEDS_OK on success; a negative @ref SedsResult if a handler fails.
 */
SedsResult seds_router_process_rx_queue_with_timeout(
    SedsRouter * r,
    uint32_t timeout_ms
);

/**
 * @brief Process both RX and TX queues until empty or until @p timeout_ms elapses.
 *
 * @param r           Router handle.
 * @param timeout_ms  Maximum processing time in milliseconds.
 * @return SEDS_OK on success; otherwise a negative @ref SedsResult.
 */
SedsResult seds_router_process_all_queues_with_timeout(
    SedsRouter * r,
    uint32_t timeout_ms
);


// ==============================
// Queue utilities
// ==============================

/**
 * @brief Process both RX and TX queues fully (no time limit).
 *
 * @param r Router handle.
 * @return SEDS_OK on success; otherwise a negative @ref SedsResult.
 */
SedsResult seds_router_process_all_queues(SedsRouter * r);

/**
 * @brief Clear both RX and TX queues without processing.
 *
 * @param r Router handle.
 * @return SEDS_OK on success; otherwise a negative @ref SedsResult.
 */
SedsResult seds_router_clear_queues(SedsRouter * r);

/**
 * @brief Clear the RX queue without processing.
 *
 * @param r Router handle.
 * @return SEDS_OK on success; otherwise a negative @ref SedsResult.
 */
SedsResult seds_router_clear_rx_queue(SedsRouter * r);

/**
 * @brief Clear the TX queue without processing.
 *
 * @param r Router handle.
 * @return SEDS_OK on success; otherwise a negative @ref SedsResult.
 */
SedsResult seds_router_clear_tx_queue(SedsRouter * r);

// ==============================
// Payload extraction helpers
// ==============================

/**
 * @brief Extract up to @p n floats from @p pkt's payload into @p out.
 *
 * @param pkt Pointer to packet view.
 * @param out Destination buffer for floats (must hold @p n elements).
 * @param n   Number of floats to extract.
 * @return SEDS_OK on success; a negative @ref SedsResult on error.
 *
 * @retval SEDS_SIZE_MISMATCH If the payload size is not a multiple of sizeof(float) or
 *         does not match the schema for the packet’s type.
 */
SedsResult seds_pkt_get_f32(const SedsPacketView * pkt, float * out, size_t n);

/**
 * @brief Get a direct pointer to the raw payload bytes.
 *
 * @param pkt     Packet view (must not be NULL).
 * @param out_len Optional; if non-NULL, receives the payload length in bytes.
 * @return Pointer to the payload bytes, or NULL on error.
 *
 * @warning The returned pointer is only valid for the lifetime of @p pkt (i.e., during the
 *          callback that provided the view). Do not retain it after the callback returns.
 */
const void * seds_pkt_bytes_ptr(const SedsPacketView * pkt, size_t * out_len);

/**
 * @brief Get a direct pointer to the payload, validated as an array of fixed-size elements.
 *
 * Validates that @p elem_size is one of {1,2,4,8} and that payload_len is an exact multiple
 * of @p elem_size. If validation passes, returns the same underlying pointer as
 * @ref seds_pkt_bytes_ptr and (optionally) the element count.
 *
 * @param pkt        Packet view (must not be NULL).
 * @param elem_size  Element size in bytes (must be 1, 2, 4, or 8).
 * @param out_count  Optional; if non-NULL, receives payload_len / elem_size on success (0 on fail).
 * @return Pointer to the payload bytes, or NULL if arguments are invalid or sizes don’t divide evenly.
 *
 * @note No endianness conversion is performed. If interpreting multi-byte scalars on a
 *       big-endian host, convert from little-endian after casting.
 * @warning The returned pointer is only valid for the lifetime of @p pkt (see warning above).
 */
const void * seds_pkt_data_ptr(const SedsPacketView * pkt, size_t elem_size, size_t * out_count);

/**
 * @brief Extract a typed slice from @p pkt into @p out.
 *
 * @param pkt       Packet view.
 * @param out       Destination buffer for elements (must hold @p count elements).
 * @param count     Number of elements to extract.
 * @param elem_size Element size in bytes (1,2,4,8).
 * @param elem_kind Element kind (unsigned/signed/float).
 * @return SEDS_OK on success; a negative @ref SedsResult on error.
 *
 * @retval SEDS_SIZE_MISMATCH If (count * elem_size) does not match the payload length
 *         or the schema for the packet’s @p ty.
 * @note Elements are read in little-endian order from the payload.
 */
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

namespace seds_detail
{
    template<typename T>
    struct elem_traits;

    template<>
    struct elem_traits<uint8_t>
    {
        static constexpr SedsElemKind kind = SEDS_EK_UNSIGNED;
        static constexpr size_t size = 1;
    };

    template<>
    struct elem_traits<uint16_t>
    {
        static constexpr SedsElemKind kind = SEDS_EK_UNSIGNED;
        static constexpr size_t size = 2;
    };

    template<>
    struct elem_traits<uint32_t>
    {
        static constexpr SedsElemKind kind = SEDS_EK_UNSIGNED;
        static constexpr size_t size = 4;
    };

    template<>
    struct elem_traits<uint64_t>
    {
        static constexpr SedsElemKind kind = SEDS_EK_UNSIGNED;
        static constexpr size_t size = 8;
    };

    template<>
    struct elem_traits<int8_t>
    {
        static constexpr SedsElemKind kind = SEDS_EK_SIGNED;
        static constexpr size_t size = 1;
    };

    template<>
    struct elem_traits<int16_t>
    {
        static constexpr SedsElemKind kind = SEDS_EK_SIGNED;
        static constexpr size_t size = 2;
    };

    template<>
    struct elem_traits<int32_t>
    {
        static constexpr SedsElemKind kind = SEDS_EK_SIGNED;
        static constexpr size_t size = 4;
    };

    template<>
    struct elem_traits<int64_t>
    {
        static constexpr SedsElemKind kind = SEDS_EK_SIGNED;
        static constexpr size_t size = 8;
    };

    template<>
    struct elem_traits<float>
    {
        static constexpr SedsElemKind kind = SEDS_EK_FLOAT;
        static constexpr size_t size = 4;
    };

    template<>
    struct elem_traits<double>
    {
        static constexpr SedsElemKind kind = SEDS_EK_FLOAT;
        static constexpr size_t size = 8;
    };
}

/**
 * @brief C++ convenience wrapper that deduces element kind/size from @p T and logs immediately.
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
 * @brief C++ convenience wrapper that deduces element kind/size from @p T and queues the log.
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
 * @brief C++ convenience extractor that deduces element kind/size from @p T.
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
 * @brief Internal: infer (kind,size) from pointer expression @p x.
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

/**
 * @brief C11 convenience wrapper that deduces element kind/size from @p data and logs immediately.
 */
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

/**
 * @brief C11 convenience wrapper that deduces element kind/size from @p data and queues it.
 */
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

/**
 * @brief C11 convenience extractor that deduces element kind/size from @p out.
 */
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
