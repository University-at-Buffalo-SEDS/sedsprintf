#include "serialize.hpp"
#include <cstring>

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

    // ---- serialize_packet ----
    std::vector<std::uint8_t> serialize_packet(const TelemetryPacket & pkt)
    {
        const std::size_t cap = packet_wire_size(pkt);
        std::vector<std::uint8_t> out;
        out.reserve(cap);

        // type
        const std::uint32_t ty = static_cast<std::uint32_t>(pkt.ty);
        out.push_back(static_cast<std::uint8_t>(ty & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((ty >> 8) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((ty >> 16) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((ty >> 24) & 0xFFu));

        // data_size (u32)
        const std::uint32_t dsz = static_cast<std::uint32_t>(pkt.data_size);
        out.push_back(static_cast<std::uint8_t>(dsz & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((dsz >> 8) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((dsz >> 16) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((dsz >> 24) & 0xFFu));

        // sender_len (u32) and later append sender bytes
        const char * sender_c = pkt.sender ? pkt.sender : "";
        const std::size_t sender_len_sz = std::strlen(sender_c);
        const std::uint32_t sender_len = static_cast<std::uint32_t>(sender_len_sz);
        out.push_back(static_cast<std::uint8_t>(sender_len & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((sender_len >> 8) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((sender_len >> 16) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((sender_len >> 24) & 0xFFu));

        // timestamp (u64 LE)
        const std::uint64_t ts = pkt.timestamp;
        for (int i = 0; i < 8; ++i)
        {
            out.push_back(static_cast<std::uint8_t>((ts >> (8 * i)) & 0xFFu));
        }

        // num_endpoints (u32)
        const std::uint32_t nep = static_cast<std::uint32_t>(pkt.endpoints ? pkt.endpoints->size() : 0);
        out.push_back(static_cast<std::uint8_t>(nep & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((nep >> 8) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((nep >> 16) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((nep >> 24) & 0xFFu));

        // endpoints (u32 each)
        if (pkt.endpoints)
        {
            for (DataEndpoint ep: *pkt.endpoints)
            {
                const std::uint32_t v = static_cast<std::uint32_t>(ep);
                out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
                out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
                out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
                out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
            }
        }

        // sender bytes
        out.insert(out.end(), sender_c, sender_c + sender_len_sz);

        // payload
        if (pkt.payload)
        {
            out.insert(out.end(), pkt.payload->begin(), pkt.payload->end());
        }
        return out;
    }

    // ---- deserialize_packet ----
    TelemetryResult<TelemetryPacket> deserialize_packet(const std::vector<std::uint8_t> & buf)
    {
        ByteReader r(buf);

        if (r.remaining() < header_size_bytes())
        {
            return TelemetryResult<TelemetryPacket>::Err(TelemetryError::Deserialize("short header"));
        }

        const char * err = nullptr;

        auto ty_raw_opt = r.read_u32(&err);
        if (!ty_raw_opt) return TelemetryResult<TelemetryPacket>::Err(TelemetryError::Deserialize(err));
        const std::uint32_t ty_raw = *ty_raw_opt;

        // NOTE: uses the non-template overload from serialize.hpp
        auto ty_opt = try_enum_from_u32(ty_raw);
        if (!ty_opt)
        {
            return TelemetryResult<TelemetryPacket>::Err(TelemetryError::InvalidType());
        }
        const DataType ty = *ty_opt;

        auto dsz_u32 = r.read_u32(&err);
        if (!dsz_u32) return TelemetryResult<TelemetryPacket>::Err(TelemetryError::Deserialize(err));
        const std::size_t dsz = static_cast<std::size_t>(*dsz_u32);

        auto sender_len_u32 = r.read_u32(&err);
        if (!sender_len_u32) return TelemetryResult<TelemetryPacket>::Err(TelemetryError::Deserialize(err));
        const std::size_t sender_len = static_cast<std::size_t>(*sender_len_u32);

        auto ts_u64 = r.read_u64(&err);
        if (!ts_u64) return TelemetryResult<TelemetryPacket>::Err(TelemetryError::Deserialize(err));
        const std::uint64_t ts = *ts_u64;

        auto nep_u32 = r.read_u32(&err);
        if (!nep_u32) return TelemetryResult<TelemetryPacket>::Err(TelemetryError::Deserialize(err));
        const std::size_t nep = static_cast<std::size_t>(*nep_u32);

        const std::size_t need =
                header_size_bytes() + nep * ENDPOINT_ELEM_SIZE + dsz;
        if (buf.size() < need)
        {
            return TelemetryResult<TelemetryPacket>::Err(TelemetryError::Deserialize("short buffer"));
        }

        std::vector<DataEndpoint> eps;
        eps.reserve(nep);
        for (std::size_t i = 0; i < nep; ++i)
        {
            auto e_u32 = r.read_u32(&err);
            if (!e_u32) return TelemetryResult<TelemetryPacket>::Err(TelemetryError::Deserialize(err));
            // NOTE: endpoint conversion uses the dedicated overload
            auto ep_opt = try_enum_from_u32_endpoint(*e_u32);
            if (!ep_opt)
            {
                return TelemetryResult<TelemetryPacket>::Err(TelemetryError::Deserialize("bad endpoint"));
            }
            eps.push_back(*ep_opt);
        }

        // sender bytes -> validate UTF-8 -> leak to persistent const char* (match Rust &'static str)
        auto sender_ptr_opt = r.read_bytes(sender_len, &err);
        if (!sender_ptr_opt) return TelemetryResult<TelemetryPacket>::Err(TelemetryError::Deserialize(err));
        const std::uint8_t * sender_ptr = *sender_ptr_opt;
        if (!is_valid_utf8(sender_ptr, sender_len))
        {
            return TelemetryResult<TelemetryPacket>::Err(TelemetryError::Deserialize("sender not UTF-8"));
        }
        char * sender_c = new char[sender_len + 1];
        std::memcpy(sender_c, sender_ptr, sender_len);
        sender_c[sender_len] = '\0';

        // payload bytes
        auto payload_ptr_opt = r.read_bytes(dsz, &err);
        if (!payload_ptr_opt) return TelemetryResult<TelemetryPacket>::Err(TelemetryError::Deserialize(err));
        const std::uint8_t * payload_ptr = *payload_ptr_opt;
        std::vector<std::uint8_t> payload(payload_ptr, payload_ptr + dsz);

        // Construct packet (mirrors Rust)
        auto payload_arc = std::make_shared<const std::vector<std::uint8_t>>(std::move(payload));
        auto endpoints_arc = std::make_shared<const std::vector<DataEndpoint>>(std::move(eps));

        TelemetryPacket pkt;
        pkt.ty = ty;
        pkt.data_size = dsz;
        pkt.sender = sender_c; // leaked, as in Rust Box::leak
        pkt.endpoints = std::move(endpoints_arc);
        pkt.timestamp = ts;
        pkt.payload = std::move(payload_arc);

        // Validate invariants similar to Rust new()/validate()
        auto v = pkt.Validate();
        if (v.is_err())
        {
            return TelemetryResult<TelemetryPacket>::Err(v.unwrap_err());
        }
        return TelemetryResult<TelemetryPacket>::Ok(std::move(pkt));
    }
} // namespace seds
