// Ported from PHD2 v2.6.14 src/mount.{h,cpp} and src/scope.cpp (BSD-3-Clause).
#include "guidecore/calibration.h"

#include <algorithm>
#include <cmath>

namespace gc {

namespace {

constexpr double kPi = 3.14159265358979323846;
// Scope::DEC_COMP_LIMIT: 2/3 of 90 degrees
constexpr double kDecCompLimit = kPi / 2.0 * 2.0 / 3.0;
constexpr double kDecCompClamp = 89.0 * kPi / 180.0;

// image_math.h norm_angle(): radians wrapped into [-PI, PI)
double normAngle(double val)
{
    return val - std::floor((val + kPi) / (2.0 * kPi)) * (2.0 * kPi);
}

GuideParity oppositeParity(GuideParity p)
{
    switch (p)
    {
    case GuideParity::Even: return GuideParity::Odd;
    case GuideParity::Odd: return GuideParity::Even;
    default: return GuideParity::Unknown;
    }
}

int oppositeSide(int side)
{
    if (side == 0) return 1; // east -> west
    if (side == 1) return 0; // west -> east
    return -1;
}

} // namespace

void MountTransform::setCalibration(const Calibration& cal)
{
    m_cal = cal;
    m_cal.isValid = true;
    m_yAngleError = normAngle(cal.xAngle - cal.yAngle + kPi / 2.0);
    m_xRate = cal.xRate;
}

void MountTransform::clear()
{
    m_cal = Calibration();
    m_cal.isValid = false;
    m_yAngleError = 0.0;
    m_xRate = 1.0;
}

bool MountTransform::cameraToMount(const PHD_Point& cameraVector, PHD_Point& mountVector) const
{
    if (!cameraVector.IsValid())
        return false;

    if (!m_cal.isValid)
    {
        mountVector = cameraVector;
        return true;
    }

    const double hyp = cameraVector.Distance();
    const double cameraTheta = cameraVector.Angle();
    const double xAngle = cameraTheta - m_cal.xAngle;
    const double yAngle = cameraTheta - (m_cal.xAngle + m_yAngleError);
    mountVector.SetXY(std::cos(xAngle) * hyp, std::sin(yAngle) * hyp);
    return true;
}

bool MountTransform::mountToCamera(const PHD_Point& mountVector, PHD_Point& cameraVector) const
{
    if (!mountVector.IsValid())
        return false;

    if (!m_cal.isValid)
    {
        cameraVector = mountVector;
        return true;
    }

    const double hyp = mountVector.Distance();
    double mountTheta = mountVector.Angle();
    if (std::fabs(m_yAngleError) > kPi / 2.0)
        mountTheta = -mountTheta;

    const double xAngle = mountTheta + m_cal.xAngle;
    cameraVector.SetXY(std::cos(xAngle) * hyp, std::sin(xAngle) * hyp);
    return true;
}

int MountTransform::pulseMs(double mountDistancePx, bool raAxis) const
{
    const double rate = raAxis ? m_xRate : m_cal.yRate;
    if (rate <= 0.0)
        return 0;
    return (int) std::lround(std::fabs(mountDistancePx / rate));
}

void MountTransform::adjustForDeclination(double newDeclinationRad)
{
    const double calDecl = m_cal.declination;

    if (newDeclinationRad == kUnknownDeclination || calDecl == kUnknownDeclination)
    {
        m_xRate = m_cal.xRate;
        return;
    }

    if (newDeclinationRad != calDecl && std::fabs(calDecl) <= kDecCompLimit)
    {
        newDeclinationRad = std::max(-kDecCompClamp, std::min(kDecCompClamp, newDeclinationRad));
        m_xRate = (m_cal.xRate / std::cos(calDecl)) * std::cos(newDeclinationRad);
    }
    else
    {
        m_xRate = m_cal.xRate;
    }
}

void MountTransform::flip(bool decFlipRequired)
{
    if (!m_cal.isValid)
        return;

    Calibration cal(m_cal);
    cal.xAngle = normAngle(cal.xAngle + kPi);
    cal.yAngle = normAngle(decFlipRequired ? cal.yAngle + kPi : cal.yAngle);
    cal.pierSide = oppositeSide(cal.pierSide);
    if (!decFlipRequired)
        cal.decParity = oppositeParity(cal.decParity);

    setCalibration(cal);
}

AxisSolveResult solveRaAxis(const PHD_Point& start, const PHD_Point& end, int steps, int stepMs)
{
    AxisSolveResult r;
    if (!start.IsValid() || !end.IsValid() || steps <= 0 || stepMs <= 0)
        return r;

    r.angle = start.Angle(end);
    r.rate = start.Distance(end) / (steps * stepMs);
    r.ok = true;
    return r;
}

AxisSolveResult solveDecAxis(const PHD_Point& start, const PHD_Point& end, int steps, int stepMs, double raAngle,
                             bool assumeOrthogonal)
{
    AxisSolveResult r;
    if (!start.IsValid() || !end.IsValid() || steps <= 0 || stepMs <= 0)
        return r;

    const double dist = start.Distance(end);

    if (assumeOrthogonal)
    {
        const double a1 = normAngle(raAngle + kPi / 2.0);
        const double a2 = normAngle(raAngle - kPi / 2.0);
        const double measured = end.Angle(start);
        r.angle = std::fabs(normAngle(a1 - measured)) < std::fabs(normAngle(a2 - measured)) ? a1 : a2;
        const double decDist = dist * std::cos(measured - r.angle);
        r.rate = decDist / (steps * stepMs);
    }
    else
    {
        r.angle = end.Angle(start);
        r.rate = dist / (steps * stepMs);
    }

    r.ok = true;
    return r;
}

} // namespace gc
