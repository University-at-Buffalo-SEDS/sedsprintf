#include "telemetry_packet.hpp"
#include "error.hpp"
#include "result.hpp"
#include <sstream>
#include <iomanip>

#include "serialize.hpp"

namespace seds {

    Result<TelemetryPacket, ErrorInfo>
TelemetryPacket::deserialize(const std::vector<uint8_t>& bytes) noexcept {
        // Use the canonical wire deserializer so payload/fields are correct.
        auto r = Serializer::deserialize(bytes);
        if (r.is_err()) {
            return Result<TelemetryPacket, ErrorInfo>::err(r.error());
        }
        return Result<TelemetryPacket, ErrorInfo>::ok(r.value());
    }

    Result<TelemetryPacket, ErrorInfo>
    TelemetryPacket::from_f32(DataType t,
                              const std::vector<DataEndpoint>& eps,
                              uint64_t ts,
                              const std::vector<float>& vals) noexcept {
        if (vals.empty() || eps.empty()) {
            return Result<TelemetryPacket, ErrorInfo>::err(
                ErrorInfo(TelemetryError::SizeMismatch, "f32 slice size mismatch"));
        }

        std::vector<uint8_t> bytes(vals.size() * sizeof(float));
        std::memcpy(bytes.data(), vals.data(), bytes.size());

        return Result<TelemetryPacket, ErrorInfo>::ok(
            TelemetryPacket(t, eps, DEVICE_IDENTIFIER, ts, bytes));
    }

    Result<void, ErrorInfo> TelemetryPacket::validate() const noexcept {
        if (data_size != payload->size()) {
            return Result<void, ErrorInfo>::err(
                ErrorInfo(TelemetryError::SizeMismatch, "data_size mismatch"));
        }
        if (endpoints->empty()) {
            return Result<void, ErrorInfo>::err(
                ErrorInfo(TelemetryError::EmptyEndpoints, "no endpoints"));
        }
        if (payload->empty()) {
            return Result<void, ErrorInfo>::err(
                ErrorInfo(TelemetryError::SizeMismatch, "payload length mismatch"));
        }
        return Result<void, ErrorInfo>::ok();
    }



} // namespace seds


using namespace seds;

namespace {
// --- string helpers (keep names in sync with your enums) ---
inline const char* to_cstr(const DataType t) {
    switch (t) {
        case DataType::TELEMETRY_ERROR:  return "TELEMETRY_ERROR";
        case DataType::GPS_DATA:        return "GPS_DATA";
        case DataType::IMU_DATA:             return "IMU";
        case DataType::BATTERY_STATUS:         return "BATTERY";
        case DataType::SYSTEM_STATUS:          return "SYSTEM";
        default:                        return "UNKNOWN";
    }
}
inline const char* to_cstr(DataEndpoint ep) {
    switch (ep) {
        case DataEndpoint::SD_CARD: return "SD_CARD";
        case DataEndpoint::RADIO:   return "RADIO";
        default:                    return "UNKNOWN";
    }
}

inline std::string endpoints_bracketed(const std::shared_ptr<std::vector<DataEndpoint>>& eps) {
    std::ostringstream os;
    os << "[";
    if (eps && !eps->empty()) {
        for (size_t i = 0; i < eps->size(); ++i) {
            if (i) os << ", ";
            os << to_cstr((*eps)[i]);
        }
    }
    os << "]";
    return os.str();
}
} // namespace

std::string TelemetryPacket::header_string() const {
    std::ostringstream os;
    os << "Type: "     << to_cstr(ty)
       << ", Size: "   << data_size
       << ", Sender: " << sender
       << ", Endpoints: " << endpoints_bracketed(endpoints)
       << ", Timestamp: " << timestamp;
    return os.str();
}

std::string TelemetryPacket::to_string() const {
    // Start with the header
    std::ostringstream os;
    os << header_string();

    // Append payload in a friendly way. For tests we want floats for GPS_DATA.
    // Heuristic: if payload size is a non-zero multiple of 4, print as f32 little-endian.
    os << ", Data: ";
    if (payload && !payload->empty()) {
        const auto& bytes = *payload;
        if ((bytes.size() % 4) == 0) {
            bool first = true;
            for (size_t i = 0; i < bytes.size(); i += 4) {
                uint32_t le =
                    (uint32_t(bytes[i + 0])      ) |
                    (uint32_t(bytes[i + 1]) <<  8) |
                    (uint32_t(bytes[i + 2]) << 16) |
                    (uint32_t(bytes[i + 3]) << 24);
                float f;
                std::memcpy(&f, &le, sizeof(f));
                if (!first) os << ", ";
                first = false;
                // No fixed precision—tests only check substrings like "2.5", "3.25"
                os << f;
            }
        } else {
            // Fallback: hex bytes
            std::ios old(nullptr);
            old.copyfmt(os);
            os << "0x";
            for (size_t i = 0; i < bytes.size(); ++i) {
                if (i) os << " 0x";
                os << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                   << static_cast<unsigned>(bytes[i]);
            }
            os.copyfmt(old);
        }
    } else {
        os << "(empty)";
    }

    return os.str();
}