// serialize.cpp — leak-free, Arc-like ownership for sender/endpoints/payload

#include "serialize.hpp"
#include <cstring>
#include <memory>
#include <vector>
#include <algorithm>

namespace seds
{
    // ---- UTF-8 validation (simple, enough for sender check like Rust does) ----
    static bool is_valid_utf8(const std::uint8_t * s, std::size_t n)
    {
        std::size_t i = 0;
        while (i < n)
        {
            std::uint8_t c = s[i];
            if ((c & 0x80u) == 0x00u)
            {
                // 1-byte
                ++i;
            }
            else if ((c & 0xE0u) == 0xC0u)
            {
                // 2-byte
                if (i + 1 >= n) return false;
                if ((s[i + 1] & 0xC0u) != 0x80u) return false;
                // overlong check
                std::uint8_t c0 = c & 0x1Fu;
                if (c0 == 0) return false;
                i += 2;
            }
            else if ((c & 0xF0u) == 0xE0u)
            {
                // 3-byte
                if (i + 2 >= n) return false;
                if ((s[i + 1] & 0xC0u) != 0x80u) return false;
                if ((s[i + 2] & 0xC0u) != 0x80u) return false;
                i += 3;
            }
            else if ((c & 0xF8u) == 0xF0u)
            {
                // 4-byte
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

    // ---- serialize_packet (no borrows to temporaries; works with shared_ptr fields) ----
    std::vector<std::uint8_t> serialize_packet(const TelemetryPacket & pkt)
    {
        const std::size_t cap = packet_wire_size(pkt);
        std::vector<std::uint8_t> out;
        out.reserve(cap);

        // type (u32 LE)
        const auto ty = static_cast<std::uint32_t>(pkt.ty);
        out.push_back(static_cast<std::uint8_t>(ty & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((ty >> 8) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((ty >> 16) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((ty >> 24) & 0xFFu));

        // data_size (u32 LE)
        const auto dsz = static_cast<std::uint32_t>(pkt.data_size);
        out.push_back(static_cast<std::uint8_t>(dsz & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((dsz >> 8) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((dsz >> 16) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((dsz >> 24) & 0xFFu));

        // sender_len (u32 LE)
        const std::size_t sender_len_sz =
            (pkt.sender ? pkt.sender->size() : 0u);
        const auto sender_len = static_cast<std::uint32_t>(sender_len_sz);
        out.push_back(static_cast<std::uint8_t>(sender_len & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((sender_len >> 8) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((sender_len >> 16) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((sender_len >> 24) & 0xFFu));

        // timestamp (u64 LE)
        const std::uint64_t ts = pkt.timestamp;
        for (int i = 0; i < 8; ++i)
            out.push_back(static_cast<std::uint8_t>((ts >> (8 * i)) & 0xFFu));

        // num_endpoints (u32 LE)
        const auto nep = static_cast<std::uint32_t>(pkt.endpoints ? pkt.endpoints->size() : 0u);
        out.push_back(static_cast<std::uint8_t>(nep & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((nep >> 8) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((nep >> 16) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((nep >> 24) & 0xFFu));

        // endpoints (u32 LE each)
        if (pkt.endpoints)
        {
            for (DataEndpoint ep : *pkt.endpoints)
            {
                const auto v = static_cast<std::uint32_t>(ep);
                out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
                out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
                out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
                out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
            }
        }

        // sender bytes
        if (sender_len_sz)
            out.insert(out.end(), pkt.sender->data(), pkt.sender->data() + sender_len_sz);

        // payload
        if (pkt.payload)
            out.insert(out.end(), pkt.payload->begin(), pkt.payload->end());

        return out;
    }

    // ---- deserialize_packet (NO LEAKS; sender becomes shared_ptr<string>) ----
    TelemetryResult<TelemetryPacket> deserialize_packet(const std::vector<std::uint8_t> & buf)
    {
        ByteReader r(buf);

        if (r.remaining() < header_size_bytes())
        {
            return TelemetryResult<TelemetryPacket>::Err(TelemetryError::Deserialize("short header"));
        }

        const char * err = nullptr;

        // type (u32)
        auto ty_raw_opt = r.read_u32(&err);
        if (!ty_raw_opt) return TelemetryResult<TelemetryPacket>::Err(TelemetryError::Deserialize(err));
        const std::uint32_t ty_raw = *ty_raw_opt;

        // convert type
        auto ty_opt = try_enum_from_u32(ty_raw);
        if (!ty_opt) return TelemetryResult<TelemetryPacket>::Err(TelemetryError::InvalidType());
        const DataType ty = *ty_opt;

        // data_size (u32) -> size_t
        auto dsz_u32 = r.read_u32(&err);
        if (!dsz_u32) return TelemetryResult<TelemetryPacket>::Err(TelemetryError::Deserialize(err));
        const auto dsz = static_cast<std::size_t>(*dsz_u32);

        // sender_len (u32) -> size_t
        auto sender_len_u32 = r.read_u32(&err);
        if (!sender_len_u32) return TelemetryResult<TelemetryPacket>::Err(TelemetryError::Deserialize(err));
        const auto sender_len = static_cast<std::size_t>(*sender_len_u32);

        // timestamp (u64)
        auto ts_u64 = r.read_u64(&err);
        if (!ts_u64) return TelemetryResult<TelemetryPacket>::Err(TelemetryError::Deserialize(err));
        const std::uint64_t ts = *ts_u64;

        // num_endpoints (u32)
        auto nep_u32 = r.read_u32(&err);
        if (!nep_u32) return TelemetryResult<TelemetryPacket>::Err(TelemetryError::Deserialize(err));
        const auto nep = static_cast<std::size_t>(*nep_u32);

        // total needed bytes = header + endpoints + sender + payload
        const std::size_t need = header_size_bytes() + nep * ENDPOINT_ELEM_SIZE + sender_len + dsz;
        if (buf.size() < need)
        {
            return TelemetryResult<TelemetryPacket>::Err(TelemetryError::Deserialize("short buffer"));
        }

        // endpoints
        std::vector<DataEndpoint> eps;
        eps.reserve(nep);
        for (std::size_t i = 0; i < nep; ++i)
        {
            auto e_u32 = r.read_u32(&err);
            if (!e_u32) return TelemetryResult<TelemetryPacket>::Err(TelemetryError::Deserialize(err));
            auto ep_opt = try_enum_from_u32_endpoint(*e_u32);
            if (!ep_opt)
            {
                return TelemetryResult<TelemetryPacket>::Err(TelemetryError::Deserialize("bad endpoint"));
            }
            eps.push_back(*ep_opt);
        }

        // sender (OWNED, UTF-8 validated) — std::shared_ptr<std::string>
        auto sender_ptr_opt = r.read_bytes(sender_len, &err);
        if (!sender_ptr_opt) return TelemetryResult<TelemetryPacket>::Err(TelemetryError::Deserialize(err));
        const std::uint8_t * sender_ptr = *sender_ptr_opt;
        if (!is_valid_utf8(sender_ptr, sender_len))
        {
            return TelemetryResult<TelemetryPacket>::Err(TelemetryError::Deserialize("sender not UTF-8"));
        }
        auto sender_arc = std::make_shared<std::string>(
            reinterpret_cast<const char*>(sender_ptr),
            sender_len
        );

        // payload bytes (OWNED)
        auto payload_ptr_opt = r.read_bytes(dsz, &err);
        if (!payload_ptr_opt) return TelemetryResult<TelemetryPacket>::Err(TelemetryError::Deserialize(err));
        const std::uint8_t * payload_ptr = *payload_ptr_opt;
        auto payload_vec = std::make_shared<const std::vector<std::uint8_t>>(
            payload_ptr, payload_ptr + dsz
        );

        // endpoints arc
        auto endpoints_arc = std::make_shared<const std::vector<DataEndpoint>>(std::move(eps));

        // Construct packet (owned fields; no leaks)
        TelemetryPacket pkt;
        pkt.ty        = ty;
        pkt.data_size = dsz;
        pkt.sender    = std::move(sender_arc);
        pkt.endpoints = std::move(endpoints_arc);
        pkt.timestamp = ts;
        pkt.payload   = std::move(payload_vec);

        // Validate invariants similar to Rust new()/validate()
        if (auto v = pkt.Validate(); v.is_err())
        {
            return TelemetryResult<TelemetryPacket>::Err(v.unwrap_err());
        }
        return TelemetryResult<TelemetryPacket>::Ok(std::move(pkt));
    }
} // namespace seds
