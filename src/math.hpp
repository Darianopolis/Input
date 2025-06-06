#pragma once

#include <cmath>

namespace input
{
    struct vec2
    {
        double x = {}, y = {};

        constexpr vec2() = default;
        constexpr vec2(double x, double y): x(x), y(y) {}
        constexpr vec2(double v): x(v), y(v) {}

        friend constexpr auto operator+(vec2 l, vec2 r) -> vec2 { return vec2{ l.x + r.x, l.y + r.y }; }
        friend constexpr auto operator-(vec2 l, vec2 r) -> vec2 { return vec2{ l.x - r.x, l.y - r.y }; }
        friend constexpr auto operator*(vec2 l, vec2 r) -> vec2 { return vec2{ l.x * r.x, l.y * r.y }; };
        friend constexpr auto operator*(vec2 v, double s) -> vec2 { return vec2{ v.x * s, v.y * s }; };

        constexpr auto operator+=(vec2 r) -> decltype(auto) { return *this = *this + r; }
        constexpr auto operator-=(vec2 r) -> decltype(auto) { return *this = *this - r; }
        constexpr auto operator*=(vec2 r) -> decltype(auto) { return *this = *this * r; };
        constexpr auto operator*=(double s) -> decltype(auto) { return *this = *this * s; };

        friend constexpr bool operator==(const vec2&, const vec2&) = default;

        constexpr operator bool() const { return x || y; }
    };

    constexpr auto mag(vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); };

    constexpr auto abs(double v) { return v >= 0 ? v : -v; };
    constexpr auto abs(vec2   v) { return vec2(abs(v.x), abs(v.y)); }

    constexpr auto min(double a, double b) { return a <= b ? a : b; }
    constexpr auto min(vec2   a, vec2   b) { return vec2{min(a.x, b.x), min(a.y, b.y)}; }

    constexpr auto max(double a, double b) { return a >= b ? a : b; }
    constexpr auto max(vec2   a, vec2   b) { return vec2{max(a.x, b.x), max(a.y, b.y)}; }

    constexpr auto copysign(double v, double s) { return s >= 0 ? abs(v) : -abs(v); }
    constexpr vec2 copysign(vec2   v, vec2   s) { return vec2(std::copysign(v.x, s.x), std::copysign(v.y, s.y)); }

    constexpr auto floor(double v) { return std::floor(v); }
    constexpr auto floor(vec2   v) { return vec2(floor(v.x), floor(v.y)); }

    constexpr auto round_to_zero(double v) { return copysign(floor(abs(v)), v); }
    constexpr auto round_to_zero(vec2   v) { return copysign(floor(abs(v)), v); }

    constexpr auto clamp(double v, double l, double h) { return v <= l ? l : v >= h ? h : v; }
    constexpr auto clamp(vec2   v, vec2   l, vec2   h) { return vec2(clamp(v.x, l.x, h.x), clamp(v.y, l.y, h.y)); }
}
