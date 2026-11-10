// Ported from PHD2 v2.6.14 src/point.h (BSD-3-Clause), trimmed to what the
// star finder uses.
#pragma once

#include <cassert>
#include <cmath>

namespace gc {

class PHD_Point
{
    bool m_valid = false;

public:
    double X = 0.0;
    double Y = 0.0;

    PHD_Point() = default;
    PHD_Point(double x, double y) { SetXY(x, y); }

    bool IsValid() const { return m_valid; }
    void Invalidate() { m_valid = false; }

    void SetXY(double x, double y)
    {
        X = x;
        Y = y;
        m_valid = true;
    }

    double dX(const PHD_Point& p) const
    {
        assert(m_valid && p.m_valid);
        return X - p.X;
    }

    double dY(const PHD_Point& p) const
    {
        assert(m_valid && p.m_valid);
        return Y - p.Y;
    }

    double Distance(const PHD_Point& p) const { return std::hypot(dX(p), dY(p)); }
    double Distance() const { return std::hypot(X, Y); }

    double Angle(const PHD_Point& p) const
    {
        const double dx = dX(p);
        const double dy = dY(p);
        // same explicit zero check as PHD2 point.h (atan2(0,0) edge case)
        return (dx != 0 || dy != 0) ? std::atan2(dy, dx) : 0.0;
    }

    double Angle() const { return Angle(PHD_Point(0, 0)); }

    PHD_Point operator+(const PHD_Point& other) const
    {
        assert(m_valid && other.m_valid);
        return PHD_Point(X + other.X, Y + other.Y);
    }

    PHD_Point operator-(const PHD_Point& other) const
    {
        assert(m_valid && other.m_valid);
        return PHD_Point(X - other.X, Y - other.Y);
    }
};

} // namespace gc
