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

// scope.cpp MountCoords(): used while nudging South after the North run.
static PHD_Point mountCoords(const PHD_Point& cameraVector, double xAngle, double yAngle)
{
    const double hyp = cameraVector.Distance();
    const double cameraTheta = cameraVector.Angle();
    const double yAngleError = normAngle((xAngle - yAngle) + kPi / 2.0);
    const double xa = cameraTheta - xAngle;
    const double ya = cameraTheta - (xAngle + yAngleError);
    return PHD_Point(hyp * std::cos(xa), hyp * std::sin(ya));
}

CalibrationSession::CalibrationSession(const CalibrationParams& params)
    : m_params(params)
{
}

void CalibrationSession::begin(const PHD_Point& starPosition, double declination, int pierSide)
{
    m_cal = Calibration();
    m_state = CalibrationState::GoWest;
    m_error = CalibrationError::None;
    m_begun = true;
    m_failed = false;
    m_declination = declination;
    m_pierSide = pierSide;

    // Scope::BeginCalibration
    m_initial = starPosition;
    m_start.Invalidate();
    m_marker.Invalidate();
    m_southStart.Invalidate();
    m_steps = 0;
    m_recenterRemaining = 0;
    m_recenterDuration = 0;
    m_blExpectedBacklashStep = 0.;
    m_blLastCumDistance = 0.;
    m_blDistanceMoved = 0.;
    m_blAcceptedMoves = 0;
    m_blMaxClearingPulses = 0;
    m_northDirCosX = m_northDirCosY = 0.;
    m_totalSouthAmt = 0.;
}

CalibrationAction CalibrationSession::pulse(CalibrationDirection dir, int ms)
{
    CalibrationAction a;
    a.state = m_state;
    a.direction = dir;
    a.durationMs = ms;
    a.step = m_steps;
    return a;
}

CalibrationAction CalibrationSession::fail(CalibrationError error)
{
    m_failed = true;
    m_error = error;
    m_cal = Calibration();
    CalibrationAction a;
    a.state = m_state;
    a.failed = true;
    a.error = error;
    return a;
}

CalibrationAction CalibrationSession::finish()
{
    m_cal.declination = m_declination;
    m_cal.pierSide = m_pierSide;
    m_cal.isValid = true;
    m_state = CalibrationState::Complete;

    CalibrationAction a;
    a.state = m_state;
    a.complete = true;
    return a;
}

CalibrationAction CalibrationSession::update(const PHD_Point& pos)
{
    CalibrationAction a;
    if (!m_begun)
    {
        a.failed = true;
        a.error = CalibrationError::NotStarted;
        return a;
    }
    if (m_failed)
    {
        a.state = m_state;
        a.failed = true;
        a.error = m_error;
        return a;
    }
    if (m_state == CalibrationState::Complete)
    {
        a.state = m_state;
        a.complete = true;
        return a;
    }
    if (!pos.IsValid())
        return fail(CalibrationError::RaStarDidNotMove);

    if (!m_start.IsValid())
        m_start = pos;

    const int stepMs = m_params.stepMs;
    if (stepMs <= 0)
        return fail(CalibrationError::RaStarDidNotMove);

    for (;;)
    {
        const double dx = m_start.dX(pos);
        const double dy = m_start.dY(pos);
        const double dist = m_start.Distance(pos);

        auto scheduled = [&](CalibrationDirection dir, int ms) {
            CalibrationAction r = pulse(dir, ms);
            r.dx = dx;
            r.dy = dy;
            r.dist = dist;
            return r;
        };

        switch (m_state)
        {
        case CalibrationState::GoWest:
        {
            if (dist < m_params.distancePx)
            {
                if (m_steps++ > m_params.maxSteps)
                    return fail(CalibrationError::RaStarDidNotMove);
                return scheduled(CalibrationDirection::West, stepMs);
            }

            // Scope::UpdateCalibrationState WEST complete
            m_cal.xAngle = m_start.Angle(pos);
            m_cal.xRate = dist / (m_steps * stepMs);
            // RA parity needs mount RA/Dec readback; left unknown here.
            m_cal.raParity = GuideParity::Unknown;

            m_recenterRemaining = m_steps * stepMs;
            if (m_params.fastRecenter && m_cal.xRate > 0.0)
            {
                m_recenterDuration = (int) std::floor(m_params.maxMovePixels / m_cal.xRate);
                if (m_recenterDuration > m_params.maxRaDurationMs)
                    m_recenterDuration = m_params.maxRaDurationMs;
                if (m_recenterDuration < stepMs)
                    m_recenterDuration = stepMs;
            }
            else
                m_recenterDuration = stepMs;

            m_steps = (m_recenterRemaining + m_recenterDuration - 1) / m_recenterDuration;
            m_state = CalibrationState::GoEast;
            continue;
        }

        case CalibrationState::GoEast:
        {
            if (m_recenterRemaining > 0)
            {
                int duration = m_recenterDuration;
                if (duration > m_recenterRemaining)
                    duration = m_recenterRemaining;
                m_recenterRemaining -= duration;
                --m_steps;
                return scheduled(CalibrationDirection::East, duration);
            }

            m_steps = 0;
            m_start = pos;

            if (!m_params.decGuideEnabled)
            {
                m_cal.yAngle = normAngle(m_cal.xAngle + kPi / 2.0);
                m_cal.yRate = kCalibrationRateUncalibrated;
                m_cal.decParity = GuideParity::Unknown;
                return finish();
            }

            m_state = CalibrationState::ClearBacklash;
            m_marker = pos;
            m_blExpectedBacklashStep = m_cal.xRate * stepMs * 0.6;
            if (m_params.decRateRatio > 0.0)
                m_blExpectedBacklashStep *= m_params.decRateRatio;
            m_blMaxClearingPulses = std::max(8, 60000 / stepMs);
            m_blLastCumDistance = 0.;
            m_blAcceptedMoves = 0;
            continue;
        }

        case CalibrationState::ClearBacklash:
        {
            const double blDelta = m_marker.Distance(pos);
            const double blCumDelta = dist;

            if (m_steps == 0)
            {
                m_steps = 1;
                return scheduled(CalibrationDirection::North, stepMs);
            }

            if (blDelta >= m_blExpectedBacklashStep)
            {
                if (m_blAcceptedMoves == 0 || blCumDelta > m_blLastCumDistance)
                    ++m_blAcceptedMoves;
                else
                    m_blAcceptedMoves = 0;
            }
            else if (blCumDelta < m_blLastCumDistance)
                m_blAcceptedMoves = 0;

            if (m_blAcceptedMoves < 3) // BL_BACKLASH_MIN_COUNT
            {
                if (m_steps < m_blMaxClearingPulses && blCumDelta < m_params.distancePx)
                {
                    ++m_steps;
                    m_marker = pos;
                    m_blLastCumDistance = blCumDelta;
                    return scheduled(CalibrationDirection::North, stepMs);
                }
                if (blCumDelta >= 3.0) // BL_MIN_CLEARING_DISTANCE
                {
                    m_steps = 0;
                    m_start = pos;
                }
                else
                    return fail(CalibrationError::Backlash);
            }
            else
            {
                // the last accepted clearing move becomes North step 1
                m_steps = 1;
                m_start = m_marker;
            }

            m_blDistanceMoved = m_marker.Distance(m_initial);
            m_state = CalibrationState::GoNorth;
            continue;
        }

        case CalibrationState::GoNorth:
        {
            if (dist < m_params.distancePx)
            {
                if (m_steps++ > m_params.maxSteps)
                    return fail(CalibrationError::DecStarDidNotMove);
                return scheduled(CalibrationDirection::North, stepMs);
            }

            if (m_params.assumeOrthogonal)
            {
                const double a1 = normAngle(m_cal.xAngle + kPi / 2.0);
                const double a2 = normAngle(m_cal.xAngle - kPi / 2.0);
                const double measured = pos.Angle(m_start);
                m_cal.yAngle = std::fabs(normAngle(a1 - measured)) < std::fabs(normAngle(a2 - measured)) ? a1 : a2;
                const double decDist = dist * std::cos(measured - m_cal.yAngle);
                m_cal.yRate = decDist / (m_steps * stepMs);
            }
            else
            {
                m_cal.yAngle = pos.Angle(m_start);
                m_cal.yRate = dist / (m_steps * stepMs);
            }
            m_cal.decParity = GuideParity::Unknown;

            m_recenterRemaining = m_steps * stepMs;
            if (m_params.fastRecenter && m_cal.yRate > 0.0)
            {
                m_recenterDuration = (int) std::floor(0.8 * m_params.maxMovePixels / m_cal.yRate);
                if (m_recenterDuration > m_params.maxDecDurationMs)
                    m_recenterDuration = m_params.maxDecDurationMs;
                if (m_recenterDuration < stepMs)
                    m_recenterDuration = stepMs;
            }
            else
                m_recenterDuration = stepMs;

            m_steps = (m_recenterRemaining + m_recenterDuration - 1) / m_recenterDuration;
            m_state = CalibrationState::GoSouth;
            m_southStart = pos;
            continue;
        }

        case CalibrationState::GoSouth:
        {
            if (m_recenterRemaining > 0)
            {
                int duration = m_recenterDuration;
                if (duration > m_recenterRemaining)
                    duration = m_recenterRemaining;
                m_recenterRemaining -= duration;
                --m_steps;
                return scheduled(CalibrationDirection::South, duration);
            }

            // east/west and north/south cable advisories are UI-only, skipped
            m_totalSouthAmt = mountCoords(m_southStart - pos, m_cal.xAngle, m_cal.yAngle).Y;

            const double northDist = m_initial.Distance(m_southStart);
            if (northDist > 0.0)
            {
                m_northDirCosX = m_initial.dX(m_southStart) / northDist;
                m_northDirCosY = m_initial.dY(m_southStart) / northDist;
            }
            else
                m_northDirCosX = m_northDirCosY = 0.0;

            m_state = CalibrationState::NudgeSouth;
            m_steps = 0;
            continue;
        }

        case CalibrationState::NudgeSouth:
        {
            const double nudgeAmt = pos.Distance(m_initial);
            if (nudgeAmt > 0.0)
            {
                const double nx = pos.dX(m_initial) / nudgeAmt;
                const double ny = pos.dY(m_initial) / nudgeAmt;
                double cosTheta = nx * m_northDirCosX + ny * m_northDirCosY;
                cosTheta = std::max(-1.0, std::min(1.0, cosTheta));
                const double theta = std::acos(cosTheta);

                if (std::fabs(theta * 180.0 / kPi - 180.0) < 40.0)
                {
                    if (m_steps <= 3 && nudgeAmt > 2.0 &&
                        nudgeAmt < m_params.distancePx + m_blDistanceMoved)
                    {
                        double decAmt = mountCoords(pos - m_initial, m_cal.xAngle, m_cal.yAngle).Y;
                        if (decAmt * m_totalSouthAmt > 0.0)
                        {
                            decAmt = std::fabs(decAmt);
                            decAmt = std::min(decAmt, m_params.maxMovePixels);
                            int pulseAmt = (int) std::floor(decAmt / m_cal.yRate);
                            if (pulseAmt > stepMs)
                                pulseAmt = stepMs;
                            ++m_steps;
                            return scheduled(CalibrationDirection::South, pulseAmt);
                        }
                    }
                }
            }

            return finish();
        }

        case CalibrationState::Complete:
        case CalibrationState::Cleared:
            return finish();
        }
    }
}

} // namespace gc
