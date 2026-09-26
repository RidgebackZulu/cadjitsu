#pragma once

#include <cmath>

namespace cad {

struct Vec2 {
    double x = 0.0, y = 0.0;

    constexpr Vec2() = default;
    constexpr Vec2(double x_, double y_) : x(x_), y(y_) {}

    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
    Vec2 operator/(double s) const { return {x / s, y / s}; }
    Vec2 operator-() const { return {-x, -y}; }
    Vec2 &operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2 &operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }

    double dot(Vec2 o) const { return x * o.x + y * o.y; }
    double cross(Vec2 o) const { return x * o.y - y * o.x; }
    double length() const { return std::hypot(x, y); }
    double length2() const { return x * x + y * y; }
    Vec2 normalized() const {
        const double l = length();
        return l > 0 ? Vec2{x / l, y / l} : Vec2{};
    }
    Vec2 perp() const { return {-y, x}; } // rotated +90 degrees
    double angle() const { return std::atan2(y, x); }
};

inline double distance(Vec2 a, Vec2 b) { return (a - b).length(); }

constexpr double kPi = 3.14159265358979323846;

// Normalizes an angle into [0, 2*pi).
inline double normAngle(double a) {
    a = std::fmod(a, 2.0 * kPi);
    if(a < 0) a += 2.0 * kPi;
    return a;
}

} // namespace cad
