#include "guidecore/sim_drivers.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace gc {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 0.02; // px/ms, same as guide_live
constexpr double kWestAngle = 0.4;
constexpr double kNorthAngle = 0.4 + kPi / 2.0 - 0.05;
constexpr int kStarHalfBox = 24;
} // namespace

SimMount::SimMount(const SimMountConfig &cfg) : m_cfg(cfg) {}

void SimMount::reset()
{
    m_x = m_y = 0.0;
    m_manualAxis = -1;
    m_manualDir = 0;
}

void SimMount::advance(double dtSec)
{
    if (m_driftEnabled)
    {
        const double ra = m_tracking ? m_cfg.raDriftPxPerSec : m_cfg.untrackedRaDriftPxPerSec;
        m_x += ra * dtSec;
        m_y += m_cfg.decDriftPxPerSec * dtSec;
    }

    if (m_manualAxis >= 0 && m_manualDir != 0)
    {
        const double speed = m_cfg.slewPxPerSecAt512 * (double) m_slewRate / 512.0;
        if (m_manualAxis == 0)
            m_x += m_manualDir * speed * dtSec;
        else
            m_y += m_manualDir * speed * dtSec;
    }
}

void SimMount::pulse(CalibrationDirection dir, int durationMs)
{
    double angle = 0.0, sign = 1.0;
    switch (dir)
    {
    case CalibrationDirection::West: angle = kWestAngle; sign = 1.0; break;
    case CalibrationDirection::East: angle = kWestAngle; sign = -1.0; break;
    case CalibrationDirection::North: angle = kNorthAngle; sign = 1.0; break;
    case CalibrationDirection::South: angle = kNorthAngle; sign = -1.0; break;
    case CalibrationDirection::None: return;
    }
    m_x += std::cos(angle) * kRate * durationMs * sign;
    m_y += std::sin(angle) * kRate * durationMs * sign;
}

void SimMount::applyGuideCorrection(double cameraDx, double cameraDy)
{
    m_x -= cameraDx;
    m_y -= cameraDy;
}

void SimMount::setSlewRate(int rate)
{
    if (rate < 2 || rate > 2048 || (rate & (rate - 1)) != 0)
        return;
    m_slewRate = rate;
}

void SimMount::manualMove(int axis, int dir, bool on)
{
    if (axis != 0 && axis != 1)
        return;
    if (on && (dir == 1 || dir == -1))
    {
        m_manualAxis = axis;
        m_manualDir = dir;
    }
    else if (m_manualAxis == axis)
    {
        m_manualAxis = -1;
        m_manualDir = 0;
    }
}

SimCamera::SimCamera(const SimMountConfig &cfg) : m_cfg(cfg) {}

void SimCamera::render(const SimMount &mount, usImage &img, uint32_t frame, double jitterX,
                       double jitterY, bool lowNoise) const
{
    img.Init(m_cfg.width, m_cfg.height);
    img.BitsPerPixel = 16;
    img.Pedestal = 0;

    std::mt19937 rng(m_cfg.seed + frame * 7919u);
    std::normal_distribution<double> noise(0.0, lowNoise ? 2.0 : 6.0);

    for (int y = 0; y < m_cfg.height; ++y)
        for (int x = 0; x < m_cfg.width; ++x)
            img.Pixel(x, y) = (unsigned short) std::max(0.0, std::min(65535.0, 800.0 + noise(rng)));

    const double cx = m_cfg.width / 2.0 + mount.starX() + jitterX;
    const double cy = m_cfg.height / 2.0 + mount.starY() + jitterY;
    const double sigma = 1.8;
    const double s2 = 2.0 * sigma * sigma;
    const double amp = 12000.0;

    const int x0 = std::max(0, (int) std::floor(cx) - kStarHalfBox);
    const int x1 = std::min(m_cfg.width - 1, (int) std::ceil(cx) + kStarHalfBox);
    const int y0 = std::max(0, (int) std::floor(cy) - kStarHalfBox);
    const int y1 = std::min(m_cfg.height - 1, (int) std::ceil(cy) + kStarHalfBox);

    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
        {
            const double dx = x - cx, dy = y - cy;
            const double v = (double) img.Pixel(x, y) + amp * std::exp(-(dx * dx + dy * dy) / s2);
            img.Pixel(x, y) = (unsigned short) std::max(0.0, std::min(65535.0, v));
        }
}

void SimCamera::toPreview(const usImage &img, std::vector<uint8_t> &out) const
{
    const size_t n = (size_t) m_cfg.width * m_cfg.height;
    out.resize(n);
    if (!img.ImageData)
    {
        std::fill(out.begin(), out.end(), 0);
        return;
    }
    for (size_t i = 0; i < n; ++i)
    {
        const double v = ((double) img.ImageData[i] - 700.0) / 3300.0 * 255.0;
        out[i] = (uint8_t) std::max(0.0, std::min(255.0, v));
    }
}

} // namespace gc
