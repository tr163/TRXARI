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

// PHD2 uses this sentinel for "no Dec calibration data" (Scope.h).
constexpr double kCalibrationRateUncalibrated = 123e4;

enum class CalibrationState
{
    Cleared,
    GoWest,
    GoEast,
    ClearBacklash,
    GoNorth,
    GoSouth,
    NudgeSouth,
    Complete,
};

enum class CalibrationDirection
{
    None,
    North,
    South,
    East,
    West,
};

enum class CalibrationError
{
    None,
    NotStarted,
    RaStarDidNotMove,
    DecStarDidNotMove,
    Backlash,
};

struct CalibrationParams
{
    int stepMs = 750;              // PHD2 DefaultCalibrationDuration
    double distancePx = 25.0;      // CalstepDialog::DEFAULT_DISTANCE
    int maxSteps = 60;             // MAX_CALIBRATION_STEPS
    bool decGuideEnabled = true;   // false = PHD2 DEC_NONE
    bool assumeOrthogonal = false;
    bool fastRecenter = true;      // PHD2 /guider/FastRecenter default
    double maxMovePixels = 15.0;   // GuiderMultiStar::GetMaxMovePixels == searchRegion
    int maxRaDurationMs = 2500;    // DefaultMaxRaDuration
    int maxDecDurationMs = 2500;   // DefaultMaxDecDuration
    double decRateRatio = 1.0;     // Dec guide speed / RA guide speed, when known
};

struct CalibrationAction
{
    CalibrationState state = CalibrationState::Cleared;
    CalibrationDirection direction = CalibrationDirection::None;
    int durationMs = 0;
    int step = 0;
    double dx = 0., dy = 0., dist = 0.;
    bool complete = false;
    bool failed = false;
    CalibrationError error = CalibrationError::None;
};

// Frame-driven port of the state machine in PHD2 Scope::UpdateCalibrationState:
// West -> East recenter -> backlash clearing -> North -> South recenter -> nudge.
// The caller pulses the mount with the returned direction/duration, measures the
// new star position, and calls update() again with it.
class CalibrationSession
{
public:
    explicit CalibrationSession(const CalibrationParams& params = CalibrationParams());

    void begin(const PHD_Point& starPosition, double declination = kUnknownDeclination, int pierSide = -1);
    CalibrationAction update(const PHD_Point& starPosition);

    CalibrationState state() const { return m_state; }
    bool isComplete() const { return m_state == CalibrationState::Complete; }
    bool hasFailed() const { return m_failed; }
    const Calibration& calibration() const { return m_cal; }

private:
    CalibrationAction pulse(CalibrationDirection dir, int ms);
    CalibrationAction fail(CalibrationError error);
    CalibrationAction finish();

    CalibrationParams m_params;
    Calibration m_cal;
    CalibrationState m_state = CalibrationState::Cleared;
    CalibrationError m_error = CalibrationError::None;
    bool m_begun = false;
    bool m_failed = false;
    double m_declination = kUnknownDeclination;
    int m_pierSide = -1;

    PHD_Point m_initial;
    PHD_Point m_start;
    PHD_Point m_marker;
    PHD_Point m_southStart;

    int m_steps = 0;
    int m_recenterRemaining = 0;
    int m_recenterDuration = 0;

    double m_blExpectedBacklashStep = 0.;
    double m_blLastCumDistance = 0.;
    double m_blDistanceMoved = 0.;
    int m_blAcceptedMoves = 0;
    int m_blMaxClearingPulses = 0;
    double m_northDirCosX = 0., m_northDirCosY = 0.;
    double m_totalSouthAmt = 0.;
};

} // namespace gc
