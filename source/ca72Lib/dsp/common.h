#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace ca72
{
// Rust's Result<T, E>. Result<void, E> holds only the error.
template <typename T, typename E>
struct Result
{
    std::variant<T, E> v;

    static Result Ok (T t) { return Result { std::variant<T, E> (std::in_place_index<0>, std::move (t)) }; }
    static Result Err (E e) { return Result { std::variant<T, E> (std::in_place_index<1>, std::move (e)) }; }

    bool is_ok() const { return v.index() == 0; }
    bool is_err() const { return v.index() == 1; }
    T& value() { return std::get<0> (v); }
    const T& value() const { return std::get<0> (v); }
    E& error() { return std::get<1> (v); }
    const E& error() const { return std::get<1> (v); }
    T unwrap() const { assert (is_ok()); return std::get<0> (v); }
};

template <typename E>
struct Result<void, E>
{
    std::optional<E> err;

    static Result Ok() { return {}; }
    static Result Err (E e) { return Result { std::move (e) }; }

    bool is_ok() const { return ! err.has_value(); }
    bool is_err() const { return err.has_value(); }
    E& error() { return *err; }
    const E& error() const { return *err; }
    void unwrap() const { assert (is_ok()); }
};

inline double signum (double x) { return std::isnan (x) ? x : (std::signbit (x) ? -1.0 : 1.0); }
inline float signum (float x) { return std::isnan (x) ? x : (std::signbit (x) ? -1.0f : 1.0f); }

// Rust's f64::clamp, f64::max/min (NaN-ignoring, as Rust's).
inline double clamp (double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float clamp (float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline double fmax (double a, double b) { return std::fmax (a, b); }
inline double fmin (double a, double b) { return std::fmin (a, b); }

// Rust's rem_euclid for floats.
inline double rem_euclid (double a, double b)
{
    double r = std::fmod (a, b);
    return r < 0.0 ? r + std::fabs (b) : r;
}
} // namespace ca72
