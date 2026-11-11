#pragma once
#include "guidecore/calibration.h"
#include "guidecore/us_image.h"

#include <cstdint>
#include <vector>

namespace gc {

struct SimMountConfig
{
    int width = 640;
    int height = 480;
    double raDriftPxPerSec = 0.25;
    double decDriftPxPerSec = 0.08;
    double untrackedRaDriftPxPerSec = 1.0;
    double slewPxPerSecAt512 = 2.0;
    uint32_t seed = 424242;
};

// Synthetic mount: the "star" is a displacement in camera pixels. Calibration
// pulses and guide corrections move that displacement; drift/manual moves add
// to it. Angles are a monotonic visual mapping, not real sidereal coordinates.
class SimMount
{
public:
    explicit SimMount(const SimMountConfig &cfg = SimMountConfig());

    void reset();
    void advance(double dtSec);
    void pulse(CalibrationDirection dir, int durationMs);
    void applyGuideCorrection(double cameraDx, double cameraDy);

    void setTracking(bool on) { m_tracking = on; }
    bool tracking() const { return m_tracking; }
    void setSlewRate(int rate);
    int slewRate() const { return m_slewRate; }
    void manualMove(int axis, int dir, bool on);
    void setDriftEnabled(bool on) { m_driftEnabled = on; }

    double starX() const { return m_x; }
    double starY() const { return m_y; }
    double raDeg() const { return 123.0 + m_x * 0.01; }
    double decDeg() const { return 45.0 + m_y * 0.01; }

private:
    SimMountConfig m_cfg;
    double m_x = 0.0;
    double m_y = 0.0;
    bool m_tracking = false;
    bool m_driftEnabled = true;
    int m_slewRate = 512;
    int m_manualAxis = -1;
    int m_manualDir = 0;
};

class SimCamera
{
public:
    explicit SimCamera(const SimMountConfig &cfg = SimMountConfig());

    // jitterX/Y are added to the star centre; pass 0 during calibration.
    void render(const SimMount &mount, usImage &img, uint32_t frame, double jitterX, double jitterY,
                bool lowNoise) const;
    void toPreview(const usImage &img, std::vector<uint8_t> &out) const;

    int width() const { return m_cfg.width; }
    int height() const { return m_cfg.height; }

private:
    SimMountConfig m_cfg;
};

} // namespace gc
