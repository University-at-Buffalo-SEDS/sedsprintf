#pragma once
#include <string>

namespace seds {

    enum class TelemetryError {
        None = 0,
        BadArg,
        InvalidType,
        SizeMismatch,
        SizeMismatchError,
        Deserialize,
        HandlerError,
        Io,
        EmptyEndpoints
    };

    struct ErrorInfo {
        TelemetryError code;
        std::string message;

        ErrorInfo() : code(TelemetryError::None) {}
        ErrorInfo(TelemetryError c, std::string msg)
            : code(c), message(std::move(msg)) {}
    };

} // namespace seds
