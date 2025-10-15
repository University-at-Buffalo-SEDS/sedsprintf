#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#  define RC_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#  define RC_ALWAYS_INLINE inline
#endif

#ifndef RC_NODISCARD
#  if __has_cpp_attribute(nodiscard)
#    define RC_NODISCARD [[nodiscard]]
#  else
#    define RC_NODISCARD
#  endif
#endif

namespace rc {

// ---------- Option ----------
template <typename T>
using Option = std::optional<T>;

// ---------- Result<T, E> ----------
template <typename T, typename E>
class Result {
public:
    using ok_t = T;
    using err_t = E;

    RC_NODISCARD static Result Ok(T v) { return Result(std::in_place_index<0>, std::move(v)); }
    RC_NODISCARD static Result Err(E e) { return Result(std::in_place_index<1>, std::move(e)); }

    RC_NODISCARD bool is_ok() const { return std::holds_alternative<T>(inner_); }
    RC_NODISCARD bool is_err() const { return std::holds_alternative<E>(inner_); }

    RC_NODISCARD const T& unwrap() const { return std::get<T>(inner_); }
    RC_NODISCARD T& unwrap() { return std::get<T>(inner_); }

    RC_NODISCARD const E& unwrap_err() const { return std::get<E>(inner_); }
    RC_NODISCARD E& unwrap_err() { return std::get<E>(inner_); }

    template <typename U>
    RC_NODISCARD T unwrap_or(U&& fallback) const {
        if (is_ok()) return std::get<T>(inner_);
        return static_cast<T>(std::forward<U>(fallback));
    }

private:
    std::variant<T, E> inner_;
    template <typename... Args>
    explicit Result(std::in_place_index_t<0>, Args&&... args)
        : inner_(std::in_place_index<0>, std::forward<Args>(args)...) {}
    template <typename... Args>
    explicit Result(std::in_place_index_t<1>, Args&&... args)
        : inner_(std::in_place_index<1>, std::forward<Args>(args)...) {}
};

// ---------- Span (C++17, minimal) ----------
template <typename T>
struct Span {
    using element_type = T;
    using pointer = T*;
    using reference = T&;

    pointer data = nullptr;
    std::size_t size = 0;

    RC_NODISCARD pointer begin() const { return data; }
    RC_NODISCARD pointer end() const { return data + size; }
    RC_NODISCARD bool empty() const { return size == 0; }
    RC_NODISCARD reference operator[](std::size_t i) const { return data[i]; }
};

template <typename T>
RC_NODISCARD  Span<T> make_span(T* ptr, std::size_t n) {
    return Span<T>{ptr, n};
}

template <typename T>
RC_NODISCARD  Span<const T> make_cspan(const T* ptr, std::size_t n) {
    return Span<const T>{ptr, n};
}

template <typename T>
RC_NODISCARD  Span<T> make_span(std::vector<T>& v) {
    return Span<T>{v.data(), v.size()};
}

template <typename T>
RC_NODISCARD  Span<const T> make_cspan(const std::vector<T>& v) {
    return Span<const T>{v.data(), v.size()};
}

// ---------- Common aliases ----------
using Byte = std::uint8_t;
using Bytes = std::vector<Byte>;

// ---------- Smart pointers (Arc/Box) ----------
template <typename T, typename... Args>
RC_NODISCARD  std::shared_ptr<T> make_arc(Args&&... args) {
    return std::make_shared<T>(std::forward<Args>(args)...);
}

template <typename T, typename... Args>
RC_NODISCARD  std::unique_ptr<T> make_box(Args&&... args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
}

// ---------- Enum helpers (u32 conversions like Rust TryFrom/From) ----------
template <typename E>
RC_NODISCARD std::underlying_type_t<E> to_u32(E e) {
    static_assert(std::is_enum_v<E>, "to_u32 requires enum type");
    return static_cast<std::underlying_type_t<E>>(e);
}

template <typename E>
RC_NODISCARD std::optional<E> from_u32(std::uint32_t v) {
    static_assert(std::is_enum_v<E>, "from_u32 requires enum type");
    // NOTE: We won’t validate ranges here; callers can layer checks as needed.
    return static_cast<E>(v);
}

// ---------- Small utilities ----------
template <typename T>
RC_NODISCARD T clamp(T x, T lo, T hi) {
    return (x < lo) ? lo : (x > hi) ? hi : x;
}

} // namespace rc
