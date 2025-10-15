#include "telemetry.hpp"
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace seds
{
    // ---------------------- TelemetryPacket impl ----------------------

    TelemetryResult<TelemetryPacket> TelemetryPacket::New(
        DataType ty,
        const std::vector<DataEndpoint> & eps,
        const char * sender_in,
        std::uint64_t ts,
        std::shared_ptr<const std::vector<std::uint8_t>> payload_in)
    {
        const auto & meta = message_meta(ty);
        if (eps.empty())
        {
            return TelemetryResult<TelemetryPacket>::Err(TelemetryError::EmptyEndpoints());
        }
        if (!payload_in || payload_in->size() != meta.data_size)
        {
            const std::size_t got = payload_in ? payload_in->size() : 0;
            return TelemetryResult<TelemetryPacket>::Err(
                TelemetryError::SizeMismatch(meta.data_size, got));
        }

        auto endpoints_arc = std::make_shared<const std::vector<DataEndpoint>>(eps);
        TelemetryPacket pkt;
        pkt.ty = ty;
        pkt.data_size = meta.data_size;
        pkt.sender = sender_in;
        pkt.endpoints = std::move(endpoints_arc);
        pkt.timestamp = ts;
        pkt.payload = std::move(payload_in);
        return TelemetryResult<TelemetryPacket>::Ok(std::move(pkt));
    }

    TelemetryResult<TelemetryPacket> TelemetryPacket::FromU8Slice(
        DataType ty,
        const std::vector<std::uint8_t> & bytes,
        const std::vector<DataEndpoint> & eps,
        std::uint64_t ts)
    {
        const auto & meta = message_meta(ty);
        if (bytes.size() != meta.data_size)
        {
            return TelemetryResult<TelemetryPacket>::Err(
                TelemetryError::SizeMismatch(meta.data_size, bytes.size()));
        }
        auto payload_arc = std::make_shared<const std::vector<std::uint8_t>>(bytes);
        return TelemetryPacket::New(ty, eps, DEVICE_IDENTIFIER, ts, std::move(payload_arc));
    }

    TelemetryResult<TelemetryPacket> TelemetryPacket::FromF32Slice(
        DataType ty,
        const std::vector<float> & values,
        const std::vector<DataEndpoint> & eps,
        std::uint64_t ts)
    {
        const auto & meta = message_meta(ty);
        const std::size_t need = values.size() * 4;
        if (need != meta.data_size)
        {
            return TelemetryResult<TelemetryPacket>::Err(
                TelemetryError::SizeMismatch(meta.data_size, need));
        }

        std::vector<std::uint8_t> bytes;
        bytes.reserve(need);
        for (float v: values)
        {
            std::uint32_t le;
            static_assert(sizeof(float) == 4, "float must be 32-bit");
            std::memcpy(&le, &v, sizeof(float)); // get host bytes
            // ensure little-endian emission:
            std::uint8_t out[4] = {
                static_cast<std::uint8_t>(le & 0xFFu),
                static_cast<std::uint8_t>((le >> 8) & 0xFFu),
                static_cast<std::uint8_t>((le >> 16) & 0xFFu),
                static_cast<std::uint8_t>((le >> 24) & 0xFFu),
            };
            bytes.insert(bytes.end(), out, out + 4);
        }
        auto payload_arc = std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
        return TelemetryPacket::New(ty, eps, DEVICE_IDENTIFIER, ts, std::move(payload_arc));
    }

    TelemetryResult<void *> TelemetryPacket::Validate() const
    {
        const auto & meta = message_meta(ty);
        if (data_size != meta.data_size)
        {
            return TelemetryResult<void *>::Err(TelemetryError::SizeMismatch(meta.data_size, data_size));
        }
        if (!endpoints || endpoints->empty())
        {
            return TelemetryResult<void *>::Err(TelemetryError::EmptyEndpoints());
        }
        if (!payload || payload->size() != data_size)
        {
            const std::size_t got = payload ? payload->size() : 0;
            return TelemetryResult<void *>::Err(TelemetryError::SizeMismatch(data_size, got));
        }
        return TelemetryResult<void *>::Ok(nullptr);
    }

    void TelemetryPacket::BuildEndpointString(std::string & out) const
    {
        if (!endpoints) return;
        for (std::size_t i = 0; i < endpoints->size(); ++i)
        {
            if (i > 0) out += ", ";
            out += data_endpoint_as_str((*endpoints)[i]);
        }
    }

    std::string TelemetryPacket::HeaderString() const
    {
        std::string endpoints_s;
        BuildEndpointString(endpoints_s);

        std::string s;
        s.reserve(96);
        s += "Type: ";
        s += data_type_as_str(ty);
        s += ", Size: ";
        s += std::to_string(data_size);
        s += ", Sender: ";
        s += (sender ? sender : "");
        s += ", Endpoints: [";
        s += endpoints_s;
        s += "], Timestamp: ";
        s += std::to_string(timestamp);
        return s;
    }

    std::optional<std::string> TelemetryPacket::DataAsUtf8() const
    {
        if (MESSAGE_DATA_TYPES[static_cast<std::size_t>(ty)] != MessageDataType::String)
        {
            return std::nullopt;
        }
        if (!payload) return std::string{};
        // trim trailing NULs
        const auto & bytes = *payload;
        std::size_t end = 0;
        for (std::size_t i = bytes.size(); i > 0; --i)
        {
            if (bytes[i - 1] != 0)
            {
                end = i;
                break;
            }
        }
        return TrimmedStr(std::vector(bytes.begin(), bytes.begin() + end));
    }

    std::optional<std::string> TelemetryPacket::TrimmedStr(const std::vector<std::uint8_t> & bytes)
    {
        if (bytes.empty()) return std::nullopt;
        // "UTF-8" trust as Rust did (unwrap_or("")); we’ll replace invalid with empty.
        return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    }

    MessageDataType TelemetryPacket::MsgTy() const
    {
        return MESSAGE_DATA_TYPES[static_cast<std::size_t>(ty)];
    }

    std::string TelemetryPacket::ToString() const
    {
        constexpr std::size_t MAX_PRECISION = 12;
        std::string s = HeaderString();

        if (!payload || payload->empty())
        {
            s += ", Data: <empty>";
            return s;
        }

        if (get_info_type(ty) == MessageType::Error)
        {
            s += ", Error: ";
        }
        else
        {
            s += ", Data: ";
        }

        // Strings first
        if (auto msg = DataAsUtf8())
        {
            s += *msg;
            return s;
        }

        // Non-string payloads
        switch (MsgTy())
        {
            case MessageDataType::Float32:
            {
                const auto & bytes = *payload;
                if (bytes.size() % 4 != 0)
                {
                    // defensive
                    return ToHexString();
                }
                std::ostringstream oss;
                oss.setf(std::ios::fixed, std::ios::floatfield);
                oss << std::setprecision(static_cast<int>(MAX_PRECISION));
                const std::size_t n = bytes.size() / 4;
                for (std::size_t i = 0; i < n; ++i)
                {
                    std::uint32_t le =
                            (static_cast<std::uint32_t>(bytes[i * 4 + 0])) |
                            (static_cast<std::uint32_t>(bytes[i * 4 + 1]) << 8) |
                            (static_cast<std::uint32_t>(bytes[i * 4 + 2]) << 16) |
                            (static_cast<std::uint32_t>(bytes[i * 4 + 3]) << 24);
                    float v;
                    std::memcpy(&v, &le, sizeof(float));
                    oss << v;
                    if (i + 1 < n) oss << ", ";
                }
                s += oss.str();
                break;
            }
            case MessageDataType::UInt32:
            {
                const auto & bytes = *payload;
                if (bytes.size() % 4 != 0)
                {
                    return ToHexString();
                }
                const std::size_t n = bytes.size() / 4;
                for (std::size_t i = 0; i < n; ++i)
                {
                    std::uint32_t v =
                            (static_cast<std::uint32_t>(bytes[i * 4 + 0])) |
                            (static_cast<std::uint32_t>(bytes[i * 4 + 1]) << 8) |
                            (static_cast<std::uint32_t>(bytes[i * 4 + 2]) << 16) |
                            (static_cast<std::uint32_t>(bytes[i * 4 + 3]) << 24);
                    s += std::to_string(v);
                    if (i + 1 < n) s += ", ";
                }
                break;
            }
            case MessageDataType::UInt8:
            {
                const auto & bytes = *payload;
                for (std::size_t i = 0; i < bytes.size(); ++i)
                {
                    s += std::to_string(bytes[i]);
                    if (i + 1 < bytes.size()) s += ", ";
                }
                break;
            }
            case MessageDataType::String:
            {
                // already handled above
                break;
            }
            case MessageDataType::Hex:
            {
                return ToHexString(); // matches Rust behavior (returns hex, which includes header)
            }
        }

        return s;
    }

    std::string TelemetryPacket::ToHexString() const
    {
        std::string s = HeaderString();

        std::string hex;
        if (payload && !payload->empty())
        {
            hex.reserve(payload->size() * 5); // " 0x??"
            for (uint8_t b: *payload)
            {
                char buf[6]; // " 0x" + 2 hex + '\0'
                std::snprintf(buf, sizeof(buf), " 0x%02x", static_cast<unsigned>(b));
                hex += buf;
            }
        }
        s += ", Data (hex):";
        if (!hex.empty()) s += hex;
        return s;
    }


    // ostream support
    std::ostream & operator<<(std::ostream & os, const TelemetryPacket & pkt)
    {
        os << pkt.ToString();
        return os;
    }
} // namespace seds
