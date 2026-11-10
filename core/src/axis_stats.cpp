// Ported from PHD2 v2.6.14 src/guiding_stats.cpp (BSD-3-Clause).
#include "guidecore/axis_stats.h"

#include <algorithm>
#include <cmath>

namespace gc {

AxisStats::AxisStats(int autoWindow)
    : window_(autoWindow > 0 ? autoWindow : 0)
{
}

void AxisStats::clear()
{
    entries_.clear();
    sumX_ = sumY_ = sumXY_ = sumXSq_ = sumYSq_ = 0;
    fitValid_ = false;
}

void AxisStats::add(double dt, double y)
{
    entries_.push_back({dt, y});
    sumX_ += dt;
    sumY_ += y;
    sumXY_ += dt * y;
    sumXSq_ += dt * dt;
    sumYSq_ += y * y;
    fitValid_ = false;

    if (window_ > 0 && static_cast<int>(entries_.size()) > window_)
        removeOldest();
}

void AxisStats::removeOldest()
{
    if (entries_.empty())
        return;
    const Sample s = entries_.front();
    entries_.pop_front();
    sumX_ -= s.t;
    sumY_ -= s.y;
    sumXY_ -= s.t * s.y;
    sumXSq_ -= s.t * s.t;
    sumYSq_ -= s.y * s.y;
    fitValid_ = false;
}

unsigned int AxisStats::count() const
{
    return static_cast<unsigned int>(entries_.size());
}

double AxisStats::median() const
{
    if (entries_.empty())
        return 0;
    if (entries_.size() == 1)
        return entries_.front().y;

    std::vector<double> v;
    v.reserve(entries_.size());
    for (const auto& e : entries_)
        v.push_back(e.y);
    std::sort(v.begin(), v.end());

    const size_t c = v.size() / 2;
    if (v.size() % 2 == 1)
        return v[c];
    return (v[c] + v[c - 1]) / 2.0;
}

void AxisStats::recompute() const
{
    const size_t n = entries_.size();
    if (n <= 1)
    {
        slope_ = intercept_ = rSquared_ = 0;
        fitValid_ = true;
        return;
    }

    const double denom = static_cast<double>(n) * sumXSq_ - sumX_ * sumX_;
    slope_ = ((static_cast<double>(n) * sumXY_) - (sumX_ * sumY_)) / denom;
    intercept_ = (sumY_ - (slope_ * sumX_)) / static_cast<double>(n);

    const double Syy = sumYSq_ - (sumY_ * sumY_) / n;
    const double Sxy = sumXY_ - (sumX_ * sumY_) / n;
    const double Sxx = sumXSq_ - (sumX_ * sumX_) / n;
    const double SSE = Syy - (Sxy * Sxy) / Sxx;
    rSquared_ = Syy != 0 ? (Syy - SSE) / Syy : 0;
    fitValid_ = true;
}

double AxisStats::slope() const
{
    if (!fitValid_)
        recompute();
    return slope_;
}

double AxisStats::intercept() const
{
    if (!fitValid_)
        recompute();
    return intercept_;
}

double AxisStats::rSquared() const
{
    if (!fitValid_)
        recompute();
    return rSquared_;
}

} // namespace gc
