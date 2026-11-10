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

    double Distance(const PHD_Point& p) const { return std::hypot(X - p.X, Y - p.Y); }

    PHD_Point operator-(const PHD_Point& other) const
    {
        assert(m_valid && other.m_valid);
        return PHD_Point(X - other.X, Y - other.Y);
    }
};

} // namespace gc
