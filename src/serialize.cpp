#include "serialize.hpp"
#include <algorithm>
#include "telemetry_packet.hpp"
#include "error.hpp"
#include "result.hpp"

#include <vector>
#include <string>
namespace seds {

static inline void put_le32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v & 0xFF));
    out.push_back(uint8_t((v >> 8) & 0xFF));
    out.push_back(uint8_t((v >> 16) & 0xFF));
    out.push_back(uint8_t((v >> 24) & 0xFF));
}
static inline void put_le64(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(uint8_t((v >> (8*i)) & 0xFF));
}
static inline bool get_le32(const std::vector<uint8_t>& b, size_t& p, uint32_t& v) {
    if (p + 4 > b.size()) return false;
    v =  (uint32_t)b[p+0]
       | (uint32_t(b[p+1]) << 8)
       | (uint32_t(b[p+2]) << 16)
       | (uint32_t(b[p+3]) << 24);
    p += 4;
    return true;
}
static inline bool get_le64(const std::vector<uint8_t>& b, size_t& p, uint64_t& v) {
    if (p + 8 > b.size()) return false;
    v =  (uint64_t)b[p+0]
       | (uint64_t(b[p+1]) << 8)
       | (uint64_t(b[p+2]) << 16)
       | (uint64_t(b[p+3]) << 24)
       | (uint64_t(b[p+4]) << 32)
       | (uint64_t(b[p+5]) << 40)
       | (uint64_t(b[p+6]) << 48)
       | (uint64_t(b[p+7]) << 56);
    p += 8;
    return true;
}

// Wire format (simple & symmetric):
// [u32 type][u64 ts][u32 n_endpoints][u32 endpoints...][u32 data_size][bytes payload...]
Result<std::vector<uint8_t>, ErrorInfo> Serializer::serialize(const TelemetryPacket & pkt) noexcept{
    std::vector<uint8_t> out;
    out.reserve(4 + 8 + 4 + (pkt.endpoints ? pkt.endpoints->size() : 0)*4 + 4 +
                (pkt.payload ? pkt.payload->size() : 0));

    put_le32(out, static_cast<uint32_t>(pkt.ty));
    put_le64(out, pkt.timestamp);

    const auto n_eps = pkt.endpoints ? static_cast<uint32_t>(pkt.endpoints->size()) : 0u;
    put_le32(out, n_eps);
    if (pkt.endpoints) {
        for (auto ep : *pkt.endpoints) {
            put_le32(out, static_cast<uint32_t>(ep));
        }
    }

    put_le32(out, static_cast<uint32_t>(pkt.data_size));
    if (pkt.payload) {
        out.insert(out.end(), pkt.payload->begin(), pkt.payload->end());
    }
    return Result<std::vector<uint8_t>, ErrorInfo>(out);
}

Result<TelemetryPacket, ErrorInfo>
Serializer::deserialize(const std::vector<uint8_t>& bytes) noexcept {
    size_t p = 0;
    uint32_t ty_u32 = 0, n_eps = 0, data_sz = 0;
    uint64_t ts = 0;

    auto fail = [](const char* msg) {
        return Result<TelemetryPacket, ErrorInfo>::err(
            ErrorInfo(TelemetryError::Deserialize, msg));
    };

    if (!get_le32(bytes, p, ty_u32)) return fail("truncated: type");
    if (!get_le64(bytes, p, ts))     return fail("truncated: timestamp");
    if (!get_le32(bytes, p, n_eps))  return fail("truncated: n_endpoints");

    std::vector<DataEndpoint> eps;
    eps.reserve(n_eps);
    for (uint32_t i = 0; i < n_eps; ++i) {
        uint32_t ep_u32 = 0;
        if (!get_le32(bytes, p, ep_u32)) return fail("truncated: endpoints");
        eps.push_back(static_cast<DataEndpoint>(ep_u32));
    }

    if (!get_le32(bytes, p, data_sz)) return fail("truncated: data_size");
    if (p + data_sz > bytes.size())   return fail("truncated: payload");

    std::vector<uint8_t> payload(bytes.begin() + p, bytes.begin() + p + data_sz);

    TelemetryPacket pkt(static_cast<DataType>(ty_u32),
                        eps,
                        DEVICE_IDENTIFIER,   // or whatever your default sender is
                        ts,
                        payload);
    return Result<TelemetryPacket, ErrorInfo>(std::move(pkt));
}

} // namespace seds