#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <ostream>

#include "config.hpp"  // uses DataType, DataEndpoint, MessageDataType, message_meta, MESSAGE_DATA_TYPES, get_info_type, MessageType, DEVICE_IDENTIFIER



namespace seds
{
    // ---------------------- Result alias (Rust-like) ----------------------
    template<typename T, typename E>
    class Result
    {
    public:
        static Result Ok(T v) { return Result(std::move(v)); }
        static Result Err(E e) { return Result(std::move(e)); }

        bool is_ok() const { return ok_; }
        bool is_err() const { return !ok_; }

        const T & unwrap() const { return value_; }
        T & unwrap() { return value_; }

        const E & unwrap_err() const { return error_; }
        E & unwrap_err() { return error_; }

    private:
        // success ctor
        explicit Result(T v) : ok_(true), value_(std::move(v))
        {
        }

        // error ctor
        explicit Result(E e) : ok_(false), error_(std::move(e))
        {
        }

        bool ok_{false};
        T value_{};
        E error_{};
    };

    // ---------------------- TelemetryError ----------------------
    struct TelemetryError
    {
        enum class Kind
        {
            InvalidType,
            SizeMismatch,
            SizeMismatchError,
            EmptyEndpoints,
            TimestampInvalid,
            MissingPayload,
            HandlerError,
            BadArg,
            Deserialize,
            Io,
        };

        Kind kind{Kind::BadArg};
        // Optional fields used by some variants
        std::size_t expected{0};
        std::size_t got{0};
        const char * msg{nullptr};

        static TelemetryError InvalidType()
        {
            return TelemetryError{Kind::InvalidType};
        }

        static TelemetryError SizeMismatch(std::size_t exp, std::size_t g)
        {
            TelemetryError e{Kind::SizeMismatch};
            e.expected = exp;
            e.got = g;
            return e;
        }

        static TelemetryError SizeMismatchError()
        {
            return TelemetryError{Kind::SizeMismatchError};
        }

        static TelemetryError EmptyEndpoints() { return TelemetryError{Kind::EmptyEndpoints}; }
        static TelemetryError TimestampInvalid() { return TelemetryError{Kind::TimestampInvalid}; }
        static TelemetryError MissingPayload() { return TelemetryError{Kind::MissingPayload}; }

        static TelemetryError HandlerError(const char * m)
        {
            TelemetryError e{Kind::HandlerError};
            e.msg = m;
            return e;
        }

        static TelemetryError BadArg() { return TelemetryError{Kind::BadArg}; }

        static TelemetryError Deserialize(const char * m)
        {
            TelemetryError e{Kind::Deserialize};
            e.msg = m;
            return e;
        }

        static TelemetryError Io(const char * m)
        {
            TelemetryError e{Kind::Io};
            e.msg = m;
            return e;
        }
    };

    // Convenience alias (Rust: pub type TelemetryResult<T> = Result<T, TelemetryError>)
    template<typename T>
    using TelemetryResult = Result<T, TelemetryError>;

    // ---------------------- TelemetryPacket ----------------------
    struct TelemetryPacket
    {
        DataType ty{};
        std::size_t data_size{0};
        const char * sender{nullptr};
        std::shared_ptr<const std::vector<DataEndpoint>> endpoints; // Arc<[DataEndpoint]>
        std::uint64_t timestamp{0};
        std::shared_ptr<const std::vector<std::uint8_t>> payload; // Arc<[u8]>

        static TelemetryPacket ShallowDefault()
        {
            TelemetryPacket p{};
            p.ty = DataType::GpsData;
            p.data_size = 0;
            p.payload = std::make_shared<const std::vector<uint8_t>>();
            p.endpoints = std::make_shared<const std::vector<DataEndpoint>>();
            p.sender = DEVICE_IDENTIFIER;
            p.timestamp = 0;
            return p;
        }

        // Create a packet from raw payload (validated against message_meta(ty)).
        static TelemetryResult<TelemetryPacket> New(
            DataType ty,
            const std::vector<DataEndpoint> & endpoints,
            const char * sender,
            std::uint64_t timestamp,
            std::shared_ptr<const std::vector<std::uint8_t>> payload);

        // Convenience: from u8 slice (copied).
        static TelemetryResult<TelemetryPacket> FromU8Slice(
            DataType ty,
            const std::vector<std::uint8_t> & bytes,
            const std::vector<DataEndpoint> & endpoints,
            std::uint64_t timestamp);

        // Convenience: from f32 slice (copied, little-endian).
        static TelemetryResult<TelemetryPacket> FromF32Slice(
            DataType ty,
            const std::vector<float> & values,
            const std::vector<DataEndpoint> & endpoints,
            std::uint64_t timestamp);

        // Validate internal invariants.
        TelemetryResult<void *> Validate() const;

        // Header line without data payload.
        std::string HeaderString() const;

        // If type is String, decode UTF-8 with trailing NULs trimmed.
        std::optional<std::string> DataAsUtf8() const;

        // Full pretty string including decoded data portion.
        std::string ToString() const;

        // Hex view; includes header and hex list.
        std::string ToHexString() const;

    private:
        void BuildEndpointString(std::string & out) const;

        MessageDataType MsgTy() const;

        std::optional<std::string> TrimmedStr(const std::vector<std::uint8_t> & bytes) const;
    };

    // ostream support (Rust Display)
    std::ostream & operator<<(std::ostream & os, const TelemetryPacket & pkt);

        inline bool CopyTelemetryPacket(TelemetryPacket * dest, const TelemetryPacket * src)
        {
            if (!dest || !src) return false;
            if (dest == src) return true;
            dest->ty = src->ty;
            dest->data_size = src->data_size;
            dest->payload = src->payload;
            dest->endpoints = src->endpoints;
            dest->sender = src->sender;
            dest->timestamp = src->timestamp;
            return true;
        }
    }
 // namespace seds
