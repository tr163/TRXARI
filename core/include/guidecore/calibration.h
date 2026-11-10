// Ported from PHD2 v2.6.14 src/mount.{h,cpp} and src/scope.cpp (BSD-3-Clause):
//   Calibration struct, Mount::SetCalibration, TransformCameraCoordinatesToMountCoordinates,
//   TransformMountCoordinatesToCameraCoordinates, MoveOffset pulse sizing,
//   AdjustCalibrationForScopePointing (declination term), FlipCalibration,
//   and the West/North axis math of Scope::UpdateCalibrationState.
// wxWidgets config/profile persistence and the interactive calibration state machine
// itself are not part of this layer.
#pragma once

#include "guidecore/point.h"

namespace gc {

enum class GuideParity
{
    Even = 1,     // Guide(NORTH) moves scope north
    Odd = -1,     // Guide(NORTH) moves scope south
    Unknown = 0,
};

constexpr double kUnknownDeclination = 997.0;

struct Calibration
{
    double xRate = 1.0;  // pixels per millisecond (distance / (steps * stepMs))
    double yRate = 1.0;  // pixels per millisecond
    double xAngle = 0.0; // radians, RA axis rotation vs camera X axis
    double yAngle = 1.57079632679489661923; // radians
    double declination = kUnknownDeclination;
    double rotatorAngle = 0.0;
    unsigned short binning = 1;
    int pierSide = -1; // -1 unknown, 0 east, 1 west (PHD2 PierSide)
    GuideParity raParity = GuideParity::Unknown;
    GuideParity decParity = GuideParity::Unknown;
    bool isValid = false;
};

// Mount-side math detached from wx. With no calibration set, transforms are the
// identity so the guide loop can run without one.
class MountTransform
{
public:
    // PHD2 Mount::SetCalibration: yAngleError = norm_angle(xAngle - yAngle + PI/2)
    void setCalibration(const Calibration& cal);
    void clear();
    bool isCalibrated() const { return m_cal.isValid; }

    const Calibration& calibration() const { return m_cal; }
    double yAngleError() const { return m_yAngleError; }
    double xRate() const { return m_xRate; } // declination-adjusted RA rate

    // Return true on success (note: PHD2's methods use the opposite convention).
    bool cameraToMount(const PHD_Point& cameraVector, PHD_Point& mountVector) const;
    bool mountToCamera(const PHD_Point& mountVector, PHD_Point& cameraVector) const;

    // PHD2 Mount::MoveOffset: requested = ROUND(fabs(distance / rate)).
    // RA uses the declination-adjusted xRate; DEC uses the calibrated yRate.
    int pulseMs(double mountDistancePx, bool raAxis) const;

    // PHD2 Scope::AdjustCalibrationForScopePointing declination term.
    void adjustForDeclination(double newDeclinationRad);

    // PHD2 Mount::FlipCalibration (meridian flip).
    void flip(bool decFlipRequired = false);

private:
    Calibration m_cal;
    double m_yAngleError = 0.0;
    double m_xRate = 1.0;
};

struct AxisSolveResult
{
    double angle = 0.0;
    double rate = 0.0; // px/ms
    bool ok = false;
};

// End of the PHD2 WEST measurement: xAngle = start.Angle(end),
// xRate = distance / (steps * stepMs).
AxisSolveResult solveRaAxis(const PHD_Point& start, const PHD_Point& end, int steps, int stepMs);

// End of the PHD2 NORTH measurement: yAngle = end.Angle(start) (reversed),
// yRate = distance / (steps * stepMs). With assumeOrthogonal, yAngle snaps to
// +/-90 deg from raAngle and the rate uses the projected offset.
AxisSolveResult solveDecAxis(const PHD_Point& start, const PHD_Point& end, int steps, int stepMs,
                             double raAngle, bool assumeOrthogonal);

} // namespace gc
