#include "telemetry.hpp"
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace seds
{
    static constexpr std::uint64_t EPOCH_MS_THRESHOLD = 1'000'000'000'000ULL;

    // ---------------------- TelemetryPacket impl ----------------------

    TelemetryResult<TelemetryPacket> TelemetryPacket::New(
        const DataType ty,
        const std::vector<DataEndpoint> & endpoints,
        const std::shared_ptr<const std::string> & sender,
        const std::uint64_t timestamp,
        std::shared_ptr<const std::vector<std::uint8_t>> payload)
    {
        const auto & meta = message_meta(ty);
        if (endpoints.empty())
        {
            return TelemetryResult<TelemetryPacket>::Err(TelemetryError::EmptyEndpoints());
        }
        if (!payload || payload->size() != meta.data_size)
        {
            const std::size_t got = payload ? payload->size() : 0;
            return TelemetryResult<TelemetryPacket>::Err(
                TelemetryError::SizeMismatch(meta.data_size, got));
        }

        auto endpoints_arc = std::make_shared<const std::vector<DataEndpoint>>(endpoints);
        TelemetryPacket pkt;
        pkt.ty = ty;
        pkt.data_size = meta.data_size;
        pkt.sender = sender;
        pkt.endpoints = std::move(endpoints_arc);
        pkt.timestamp = timestamp;
        pkt.payload = std::move(payload);
        return TelemetryResult<TelemetryPacket>::Ok(std::move(pkt));
    }

    TelemetryResult<TelemetryPacket> TelemetryPacket::FromU8Slice(
        const DataType ty,
        const std::vector<std::uint8_t> & bytes,
        const std::vector<DataEndpoint> & endpoints,
        std::uint64_t timestamp)
    {
        const auto & meta = message_meta(ty);
        if (bytes.size() != meta.data_size)
        {
            return TelemetryResult<TelemetryPacket>::Err(
                TelemetryError::SizeMismatch(meta.data_size, bytes.size()));
        }
        auto payload_arc = std::make_shared<const std::vector<std::uint8_t>>(bytes);
        return New(ty, endpoints, std::make_shared<std::string>(DEVICE_IDENTIFIER), timestamp, std::move(payload_arc));
    }

    TelemetryResult<TelemetryPacket> TelemetryPacket::FromF32Slice(
        const DataType ty,
        const std::vector<float> & values,
        const std::vector<DataEndpoint> & endpoints,
        const std::uint64_t timestamp)
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
        return TelemetryPacket::New(ty, endpoints, std::make_shared<std::string>(DEVICE_IDENTIFIER), timestamp,
                                    std::move(payload_arc));
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

        const uint64_t total_ms = timestamp;

        // --------- Human-readable time ---------
        std::ostringstream human_time;

        if (total_ms >= EPOCH_MS_THRESHOLD)
        {
            // Treat as Unix epoch (milliseconds) and format as UTC
            const auto secs = static_cast<std::time_t>(total_ms / 1000ULL);
            const auto sub_ms = static_cast<uint32_t>(total_ms % 1000ULL);

            std::tm tm_utc{};
            bool ok;

            // Cross-platform gmtime (UTC) handling
#if defined(_WIN32)
            ok = (gmtime_s(&tm_utc, &secs) == 0);
#elif defined(__unix__) || defined(__APPLE__)
            ok = (gmtime_r(&secs, &tm_utc) != nullptr);
#else
            if (auto * ptm = std::gmtime(&secs)) tm_utc = *ptm;
            else ok = false;
#endif

            if (ok)
            {
                human_time << std::setfill('0')
                        << std::setw(4) << (tm_utc.tm_year + 1900) << '-'
                        << std::setw(2) << (tm_utc.tm_mon + 1) << '-'
                        << std::setw(2) << tm_utc.tm_mday << ' '
                        << std::setw(2) << tm_utc.tm_hour << ':'
                        << std::setw(2) << tm_utc.tm_min << ':'
                        << std::setw(2) << tm_utc.tm_sec << '.'
                        << std::setw(3) << sub_ms << 'Z';
            }
            else
            {
                human_time << "Invalid epoch (" << total_ms << ')';
            }
        }
        else
        {
            // Treat as uptime (milliseconds since boot)
            const uint64_t hours = total_ms / 3'600'000ULL;
            const uint64_t minutes = (total_ms % 3'600'000ULL) / 60'000ULL;
            const uint64_t seconds = (total_ms % 60'000ULL) / 1'000ULL;
            const uint64_t milliseconds = total_ms % 1'000ULL;

            if (hours > 0)
            {
                human_time << hours << "h "
                        << std::setw(2) << std::setfill('0') << minutes << "m "
                        << std::setw(2) << seconds << "s "
                        << std::setw(3) << milliseconds << "ms";
            }
            else if (minutes > 0)
            {
                human_time << minutes << "m "
                        << std::setw(2) << std::setfill('0') << seconds << "s "
                        << std::setw(3) << milliseconds << "ms";
            }
            else
            {
                human_time << seconds << "s "
                        << std::setw(3) << std::setfill('0') << milliseconds << "ms";
            }
        }

        // --------- Build the full header string ---------
        std::ostringstream out;
        out << "Type: " << data_type_as_str(ty)
                << ", Size: " << data_size
                << ", Sender: " << (sender ? *sender : "")
                << ", Endpoints: [" << endpoints_s << "]"
                << ", Timestamp: " << timestamp
                << " (" << human_time.str() << ")";

        return out.str();
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
        return TrimmedStr(std::vector(bytes.begin(), bytes.begin() + static_cast<const unsigned char>(end)));
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
                constexpr std::size_t MAX_PRECISION = 12;
                const auto & bytes = *payload;
                if (bytes.size() % 4 != 0)
                {
                    // defensive
                    return ToHexString();
                }
                std::ostringstream oss;
                oss.setf(std::ios::fixed, std::ios::floatfield);
                oss << std::setprecision(MAX_PRECISION);
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
            for (const uint8_t b: *payload)
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
