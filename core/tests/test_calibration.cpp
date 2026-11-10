// Regression tests for the ported PHD2 calibration math: transforms, pulse
// sizing, declination compensation, meridian flip, and axis solving.
#include "guidecore/calibration.h"
#include "guidecore/guide_session.h"
#include "guidecore/us_image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        ++g_checks;                                                                                                    \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            ++g_failures;                                                                                              \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                \
        }                                                                                                              \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        ++g_checks;                                                                                                    \
        const double va = (a), vb = (b);                                                                               \
        if (std::fabs(va - vb) > (eps))                                                                                \
        {                                                                                                              \
            ++g_failures;                                                                                              \
            std::printf("FAIL %s:%d: %s = %.12g, expected %.12g\n", __FILE__, __LINE__, #a, va, vb);                  \
        }                                                                                                              \
    } while (0)

static constexpr double PI = 3.14159265358979323846;

static double normAngle(double v)
{
    return v - std::floor((v + PI) / (2.0 * PI)) * (2.0 * PI);
}

// Port of PHD2 Mount::TestTransforms (mount.cpp, TEST_TRANSFORMS): every 15
// degrees, both normal and inverted Y axis, forward and reverse transform.
static void testTransforms()
{
    for (int inverted = 0; inverted < 2; inverted++)
    {
        for (int i = -13; i < 14; i++)
        {
            double xAngle = i * PI / 12.0;
            double yAngle = xAngle + PI / 2.0;
            if (inverted)
                yAngle -= PI;
            xAngle = std::atan2(std::sin(xAngle), std::cos(xAngle));
            yAngle = std::atan2(std::sin(yAngle), std::cos(yAngle));

            gc::Calibration cal;
            cal.xAngle = xAngle;
            cal.yAngle = yAngle;
            cal.xRate = cal.yRate = 1.0;
            gc::MountTransform t;
            t.setCalibration(cal);

            for (int j = -13; j < 14; j++)
            {
                const double cameraAngle =
                    std::atan2(std::sin(j * PI / 12.0), std::cos(j * PI / 12.0));
                const gc::PHD_Point p0(std::cos(cameraAngle), std::sin(cameraAngle));
                gc::PHD_Point p1, p2;

                CHECK(t.cameraToMount(p0, p1));
                CHECK_NEAR(std::hypot(p1.X, p1.Y), 1.0, 0.01);

                const double adjusted = cameraAngle - xAngle;
                CHECK_NEAR(p1.X, std::cos(adjusted), 0.01);
                const double expectedY = inverted ? -std::sin(adjusted) : std::sin(adjusted);
                CHECK_NEAR(p1.Y, expectedY, 0.01);

                CHECK(t.mountToCamera(p1, p2));
                CHECK_NEAR(p2.X, p0.X, 0.01);
                CHECK_NEAR(p2.Y, p0.Y, 0.01);
            }
        }
    }
}

static void testPulseMs()
{
    gc::Calibration cal;
    cal.xRate = 0.02; // px/ms
    cal.yRate = 0.05;
    gc::MountTransform t;
    t.setCalibration(cal);

    CHECK(t.pulseMs(0.5, true) == 25);
    CHECK(t.pulseMs(-0.5, false) == 10);
    CHECK(t.pulseMs(0.0, true) == 0);
}

static void testDeclinationCompensation()
{
    gc::Calibration cal;
    cal.xRate = 0.01;
    cal.declination = 0.0; // calibrated at the celestial equator
    gc::MountTransform t;
    t.setCalibration(cal);

    const double dec60 = 60.0 * PI / 180.0;
    t.adjustForDeclination(dec60);
    CHECK_NEAR(t.xRate(), 0.005, 1e-12); // cos(60 deg)

    t.adjustForDeclination(0.0);
    CHECK_NEAR(t.xRate(), 0.01, 1e-12);

    // calibration too far from the equator is not compensated
    cal.declination = 80.0 * PI / 180.0;
    t.setCalibration(cal);
    t.adjustForDeclination(dec60);
    CHECK_NEAR(t.xRate(), 0.01, 1e-12);

    // unknown declination leaves the rate alone
    cal.declination = gc::kUnknownDeclination;
    t.setCalibration(cal);
    t.adjustForDeclination(dec60);
    CHECK_NEAR(t.xRate(), 0.01, 1e-12);
}

static void testFlip()
{
    gc::Calibration cal;
    cal.xAngle = 0.3;
    cal.yAngle = 0.3 + PI / 2.0;
    cal.pierSide = 0; // east
    cal.decParity = gc::GuideParity::Even;
    gc::MountTransform t;
    t.setCalibration(cal);

    gc::PHD_Point before, afterV, cam;
    CHECK(t.cameraToMount(gc::PHD_Point(1.0, 0.0), before));

    t.flip();
    CHECK(t.calibration().pierSide == 1);
    CHECK(t.calibration().decParity == gc::GuideParity::Odd);
    CHECK(t.cameraToMount(gc::PHD_Point(1.0, 0.0), afterV));

    // RA axis is now flipped 180 degrees: mount X negates, |Y| unchanged
    CHECK_NEAR(afterV.X, -before.X, 1e-9);
    CHECK_NEAR(afterV.Y, before.Y, 1e-9);

    // transforms still round-trip
    CHECK(t.mountToCamera(afterV, cam));
    CHECK_NEAR(cam.X, 1.0, 1e-9);
    CHECK_NEAR(cam.Y, 0.0, 1e-9);

    // decFlipRequired keeps the Dec parity and flips Y
    cal.decParity = gc::GuideParity::Even;
    t.setCalibration(cal);
    t.flip(true);
    CHECK(t.calibration().decParity == gc::GuideParity::Even);
}

static void testAxisSolvers()
{
    // WEST: PHD2 uses start.Angle(end), which with PHD_Point's (this - p)
    // convention points from the end location back to the start.
    gc::AxisSolveResult ra = gc::solveRaAxis(gc::PHD_Point(100.0, 100.0), gc::PHD_Point(103.0, 104.0), 5, 100);
    CHECK(ra.ok);
    CHECK_NEAR(ra.angle, std::atan2(-4.0, -3.0), 1e-12);
    CHECK_NEAR(ra.rate, 5.0 / 500.0, 1e-12);

    // NORTH: PHD2 uses end.Angle(start), i.e. start -> end (reversed from RA)
    gc::AxisSolveResult dec =
        gc::solveDecAxis(gc::PHD_Point(100.0, 100.0), gc::PHD_Point(100.0, 104.0), 4, 100, 0.0, false);
    CHECK(dec.ok);
    CHECK_NEAR(dec.angle, PI / 2.0, 1e-12);
    CHECK_NEAR(dec.rate, 4.0 / 400.0, 1e-12);

    // orthogonal assumption snaps to +/-90 deg and projects the distance
    gc::AxisSolveResult orth =
        gc::solveDecAxis(gc::PHD_Point(100.0, 100.0), gc::PHD_Point(100.0, 104.0), 4, 100, 0.0, true);
    CHECK(orth.ok);
    CHECK_NEAR(orth.angle, PI / 2.0, 1e-12);
    CHECK_NEAR(orth.rate, 4.0 / 400.0, 1e-12);

    // invalid input is rejected
    CHECK(!gc::solveRaAxis(gc::PHD_Point(1.0, 1.0), gc::PHD_Point(2.0, 2.0), 0, 100).ok);
}

static void RenderStar(gc::usImage& img, double cx, double cy)
{
    const int W = 200, H = 160;
    img.Init(W, H);
    img.BitsPerPixel = 16;
    img.Pedestal = 0;

    std::mt19937 rng(99);
    std::normal_distribution<double> noise(0.0, 5.0);
    const double sigma = 1.6, s2 = 2.0 * sigma * sigma, amp = 12000.0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
        {
            const double dx = x - cx, dy = y - cy;
            const double v = 800.0 + noise(rng) + amp * std::exp(-(dx * dx + dy * dy) / s2);
            img.Pixel(x, y) = (unsigned short) std::max(0.0, std::min(65535.0, v));
        }
}

struct SimMount
{
    double ratePxPerMs = 0.02;
    double westAngle = 0.4;             // camera direction the star moves on a WEST pulse
    double northAngle = 0.4 + PI / 2.0 - 0.05; // and on a NORTH pulse
    gc::PHD_Point pos{ 100.0, 80.0 };

    void pulse(gc::CalibrationDirection dir, int ms)
    {
        double angle = 0.0, sign = 1.0;
        switch (dir)
        {
        case gc::CalibrationDirection::West: angle = westAngle; sign = 1.0; break;
        case gc::CalibrationDirection::East: angle = westAngle; sign = -1.0; break;
        case gc::CalibrationDirection::North: angle = northAngle; sign = 1.0; break;
        case gc::CalibrationDirection::South: angle = northAngle; sign = -1.0; break;
        case gc::CalibrationDirection::None: return;
        }
        pos.SetXY(pos.X + std::cos(angle) * ratePxPerMs * ms * sign,
                  pos.Y + std::sin(angle) * ratePxPerMs * ms * sign);
    }
};

static void testCalibrationStateMachine()
{
    SimMount mount;
    const gc::PHD_Point start = mount.pos;

    gc::CalibrationParams p;
    gc::CalibrationSession session(p);
    session.begin(start);

    int guard = 0;
    gc::CalibrationAction a;
    bool sawWest = false, sawEast = false, sawNorth = false, sawSouth = false;
    do
    {
        a = session.update(mount.pos);
        if (a.complete || a.failed)
            break;
        CHECK(a.direction != gc::CalibrationDirection::None);
        if (a.direction == gc::CalibrationDirection::West) sawWest = true;
        if (a.direction == gc::CalibrationDirection::East) sawEast = true;
        if (a.direction == gc::CalibrationDirection::North) sawNorth = true;
        if (a.direction == gc::CalibrationDirection::South) sawSouth = true;
        mount.pulse(a.direction, a.durationMs);
    } while (++guard < 500);

    CHECK(a.complete);
    CHECK(!a.failed);
    CHECK(session.isComplete());
    CHECK(sawWest && sawEast && sawNorth && sawSouth);

    const gc::Calibration& cal = session.calibration();
    CHECK(cal.isValid);
    CHECK_NEAR(cal.xAngle, normAngle(mount.westAngle + PI), 1e-9);
    CHECK_NEAR(cal.yAngle, normAngle(mount.northAngle), 1e-9);
    CHECK_NEAR(cal.xRate, mount.ratePxPerMs, 1e-9);
    CHECK_NEAR(cal.yRate, mount.ratePxPerMs, 1e-9);
    CHECK_NEAR(mount.pos.Distance(start), 0.0, 1.0); // recentered / nudged home

    // the resulting calibration must drive the transforms
    gc::MountTransform t;
    t.setCalibration(cal);
    gc::PHD_Point mountVec;
    CHECK(t.cameraToMount(gc::PHD_Point(std::cos(mount.westAngle + PI) * 3.0,
                                        std::sin(mount.westAngle + PI) * 3.0),
                          mountVec));
    CHECK_NEAR(mountVec.X, 3.0, 1e-6);
}

static void testCalibrationDecDisabled()
{
    SimMount mount;
    gc::CalibrationParams p;
    p.decGuideEnabled = false;
    gc::CalibrationSession session(p);
    session.begin(mount.pos);

    int guard = 0;
    gc::CalibrationAction a;
    do
    {
        a = session.update(mount.pos);
        if (a.complete || a.failed)
            break;
        mount.pulse(a.direction, a.durationMs);
    } while (++guard < 500);

    CHECK(a.complete);
    const gc::Calibration& cal = session.calibration();
    CHECK_NEAR(cal.xAngle, normAngle(mount.westAngle + PI), 1e-9);
    CHECK_NEAR(cal.yRate, gc::kCalibrationRateUncalibrated, 1e-9);
    CHECK_NEAR(cal.yAngle, normAngle(cal.xAngle + PI / 2.0), 1e-9);
}

static void testCalibrationStarDoesNotMove()
{
    SimMount mount;
    gc::CalibrationSession session;
    session.begin(mount.pos);

    int guard = 0;
    gc::CalibrationAction a;
    do
    {
        a = session.update(mount.pos); // star never moves
    } while (!a.failed && !a.complete && ++guard < 500);

    CHECK(a.failed);
    CHECK(a.error == gc::CalibrationError::RaStarDidNotMove);
    CHECK(session.hasFailed());
}

// A 90-degree camera rotation must move the correction into the mount Y axis,
// and the pulse width must come from the calibrated rate.
static void testSessionUsesCalibration()
{
    gc::Calibration cal;
    cal.xAngle = PI / 2.0;
    cal.yAngle = PI; // xAngle + PI/2
    cal.xRate = 0.01;
    cal.yRate = 0.01;

    gc::GuideSessionParams p;
    p.raAlgo = gc::AlgoType::Identity;
    p.decAlgo = gc::AlgoType::Identity;
    p.raParams.minMove = 0.01;
    p.decParams.minMove = 0.01;
    p.mount.setCalibration(cal);

    gc::usImage img;
    RenderStar(img, 100.0, 80.0);
    gc::GuideSession session(p);
    CHECK(session.acquire(img));

    // star 2 px to camera +X => mount -Y after the 90-degree rotation
    RenderStar(img, 102.0, 80.0);
    gc::GuideStep step;
    CHECK(session.processFrame(img, step));
    CHECK_NEAR(step.raOffsetPx, 2.0, 0.05);
    CHECK_NEAR(step.decOffsetPx, 0.0, 0.05);
    CHECK_NEAR(step.mountRaPx, 0.0, 0.05);
    CHECK_NEAR(step.mountDecPx, -2.0, 0.05);
    CHECK_NEAR(step.raCorrectionPx, 0.0, 0.05);
    CHECK_NEAR(step.decCorrectionPx, -2.0, 0.05);
    CHECK(step.decPulseMs == -200); // 2 px / 0.01 px per ms
}

int main()
{
    testTransforms();
    testPulseMs();
    testDeclinationCompensation();
    testFlip();
    testAxisSolvers();
    testCalibrationStateMachine();
    testCalibrationDecDisabled();
    testCalibrationStarDoesNotMove();
    testSessionUsesCalibration();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
