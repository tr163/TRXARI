// Ported from PHD2 v2.6.14 src/guiding_stats.{h,cpp} (BSD-3-Clause).
// Only the parts the guide algorithms need: median + linear fit over a sliding window.
#pragma once

#include <deque>
#include <vector>

namespace gc {

struct Sample
{
    double t;
    double y;
};

class AxisStats
{
public:
    // autoWindow <= 0: caller trims with removeOldest(); > 0: oldest auto-dropped on add.
    explicit AxisStats(int autoWindow = 0);

    void clear();
    void add(double dt, double y);
    void removeOldest();

    unsigned int count() const;
    double median() const;
    double slope() const;
    double intercept() const;
    double rSquared() const;

private:
    void recompute() const;

    std::deque<Sample> entries_;
    int window_;
    double sumX_ = 0, sumY_ = 0, sumXY_ = 0, sumXSq_ = 0, sumYSq_ = 0;

    // lazily computed fit results
    mutable bool fitValid_ = false;
    mutable double slope_ = 0, intercept_ = 0, rSquared_ = 0;
};

} // namespace gc
