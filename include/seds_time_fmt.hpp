#pragma once
#include <ctime>
#include <cstddef>
#include <cstdio>

namespace seds {

    // Format: YYYY-MM-DD HH:MM:SS (UTC or local—whatever `tm` you pass)
    // Returns number of chars written (excluding '\0'). Never calls wcsftime/strftime.
    inline std::size_t format_datetime_iso8601(char* buf, std::size_t cap, const std::tm& t)
    {
        // Keep it simple and narrow. newlib-nano has snprintf; wide I/O is the problem.
        // Example: 2025-01-31 23:59:59  -> needs 19 + 1 NUL.
        if (cap == 0) return 0;
        int n = std::snprintf(buf, cap, "%04d-%02d-%02d %02d:%02d:%02d",
                              1900 + t.tm_year, 1 + t.tm_mon, t.tm_mday,
                              t.tm_hour, t.tm_min, t.tm_sec);
        if (n < 0) return 0;
        return static_cast<std::size_t>(n);
    }

    // If you want a date-only (YYYYMMDD) for filenames/logs:
    inline std::size_t format_date_compact(char* buf, std::size_t cap, const std::tm& t)
    {
        if (cap == 0) return 0;
        int n = std::snprintf(buf, cap, "%04d%02d%02d",
                              1900 + t.tm_year, 1 + t.tm_mon, t.tm_mday);
        if (n < 0) return 0;
        return static_cast<std::size_t>(n);
    }

} // namespace seds
