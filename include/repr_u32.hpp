#pragma once

#include <cstdint>
#include <type_traits>

// The ReprU32Enum “trait” for enums represented as u32 in serialized form.
template <typename E, typename Enable = void>
struct ReprU32Enum; // no default; must be specialized by the macro below

// Helper macro to specialize ReprU32Enum for a concrete enum type E,
// and enforce at compile-time that sizeof(E) == sizeof(uint32_t).
//
// Usage:
//   enum class MyEnum : std::uint32_t { A = 0, B = 1 };
//   SEDS_IMPL_REPR_U32_ENUM(MyEnum, /*max=*/1);
//
// After that, you can access: ReprU32Enum<MyEnum>::MAX
#define SEDS_IMPL_REPR_U32_ENUM(E, MAXVAL)                                       \
static_assert(sizeof(E) == sizeof(std::uint32_t),                             \
"Enum " #E " must be 32-bit (repr(u32))");                      \
template <>                                                                   \
struct ReprU32Enum<E, void> {                                                \
static constexpr std::uint32_t MAX = static_cast<std::uint32_t>(MAXVAL);  \
}
