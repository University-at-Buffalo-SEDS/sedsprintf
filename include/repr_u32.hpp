#pragma once
#include <cstdint>
#include <type_traits>

namespace seds {

    // Trait to mark enums that can be represented as uint32_t
    template<typename Enum>
    struct ReprU32Enum {
        static constexpr bool value =
            std::is_enum<Enum>::value &&
            sizeof(Enum) == sizeof(uint32_t);
    };

    template<typename Enum>
    constexpr bool is_repr_u32_v = ReprU32Enum<Enum>::value;

    // Helpers for conversion
    template<typename Enum,
             typename std::enable_if<is_repr_u32_v<Enum>, int>::type = 0>
    constexpr uint32_t to_u32(Enum e) noexcept {
        return static_cast<uint32_t>(e);
    }

    template<typename Enum,
             typename std::enable_if<is_repr_u32_v<Enum>, int>::type = 0>
    constexpr Enum from_u32(uint32_t v) noexcept {
        return static_cast<Enum>(v);
    }

} // namespace seds
