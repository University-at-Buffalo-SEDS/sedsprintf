#pragma once
#include <utility>

namespace seds {

template <typename T, typename E>
class Result {
    bool ok_;
    T value_;
    E error_;

public:
    Result(const Result&) = default;
    Result(Result&&) noexcept = default;
    Result& operator=(const Result&) = default;
    Result& operator=(Result&&) noexcept = default;

    explicit Result(const T& v) noexcept : ok_(true), value_(v) {}
    explicit Result(T&& v) noexcept : ok_(true), value_(std::move(v)) {}
    explicit Result(const E& e) noexcept : ok_(false), error_(e) {}
    explicit Result(E&& e) noexcept : ok_(false), error_(std::move(e)) {}

    static Result ok(const T& v) noexcept { return Result(v); }
    static Result ok(T&& v) noexcept { return Result(std::move(v)); }
    static Result err(const E& e) noexcept { return Result(e); }
    static Result err(E&& e) noexcept { return Result(std::move(e)); }

    [[nodiscard]] bool is_ok() const noexcept { return ok_; }
    [[nodiscard]] bool is_err() const noexcept { return !ok_; }

    [[nodiscard]] const T& value() const noexcept { return value_; }
    [[nodiscard]] const E& error() const noexcept { return error_; }
    [[nodiscard]] T& value() noexcept { return value_; }
    [[nodiscard]] E& error() noexcept { return error_; }
};

// --- specialization for void
template <typename E>
class Result<void, E> {
    bool ok_;
    E error_;

public:
    Result() noexcept : ok_(true) {}
    explicit Result(const E& e) noexcept : ok_(false), error_(e) {}
    explicit Result(E&& e) noexcept : ok_(false), error_(std::move(e)) {}

    static Result ok() noexcept { return Result(); }
    static Result err(const E& e) noexcept { return Result(e); }
    static Result err(E&& e) noexcept { return Result(std::move(e)); }

    [[nodiscard]] bool is_ok() const noexcept { return ok_; }
    [[nodiscard]] bool is_err() const noexcept { return !ok_; }

    [[nodiscard]] const E& error() const noexcept { return error_; }
    [[nodiscard]] E& error() noexcept { return error_; }
};

} // namespace seds
