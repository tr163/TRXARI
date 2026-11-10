// Regression tests for the frame-level guide loop (PHD2
// GuiderMultiStar::UpdateCurrentPosition, single-star path).
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

static constexpr int W = 200;
static constexpr int H = 160;

// Renders one frame with a Gaussian star at (cx, cy) over fixed background +
// fresh gaussian noise. Noise seed varies per frame so it is not a constant.
static void RenderFrame(gc::usImage& img, double cx, double cy, unsigned frame)
{
    img.Init(W, H);
    img.BitsPerPixel = 16;
    img.Pedestal = 0;

    std::mt19937 rng(1000 + frame);
    std::normal_distribution<double> noise(0.0, 5.0);
    const double sigma = 1.6, s2 = 2.0 * sigma * sigma, amp = 12000.0;

    for (int y = 0; y < H; y++)
    {
        for (int x = 0; x < W; x++)
        {
            const double dx = x - cx, dy = y - cy;
            const double v = 800.0 + noise(rng) + amp * std::exp(-(dx * dx + dy * dy) / s2);
            img.Pixel(x, y) = (unsigned short) std::max(0.0, std::min(65535.0, v));
        }
    }
}

static gc::GuideSessionParams Params(gc::AlgoType ra, gc::AlgoType dec, double minMove)
{
    gc::GuideSessionParams p;
    p.raAlgo = ra;
    p.decAlgo = dec;
    p.raParams = gc::defaults(ra);
    p.decParams = gc::defaults(dec);
    p.raParams.minMove = minMove;
    p.decParams.minMove = minMove;
    return p;
}

static void testAcquire()
{
    gc::usImage img;
    RenderFrame(img, 100.0, 80.0, 0);

    gc::GuideSession session;
    CHECK(session.acquire(img));
    CHECK(session.hasLock());
    CHECK_NEAR(session.lockPosition().X, 100.0, 0.1);
    CHECK_NEAR(session.lockPosition().Y, 80.0, 0.1);
}

static void testStaticStar()
{
    gc::usImage img;
    RenderFrame(img, 100.0, 80.0, 0);

    gc::GuideSession session;
    CHECK(session.acquire(img));

    for (int i = 1; i <= 10; i++)
    {
        gc::GuideStep step;
        RenderFrame(img, 100.0, 80.0, (unsigned) i);
        CHECK(session.processFrame(img, step));
        CHECK(step.frame == i);
        CHECK(std::fabs(step.raOffsetPx) < 0.05);
        CHECK(std::fabs(step.decOffsetPx) < 0.05);
        // inside the default 0.2 px minMove deadband: no correction
        CHECK(step.raCorrectionPx == 0.0);
        CHECK(step.decCorrectionPx == 0.0);
    }
}

static void testTracksDriftNoMount()
{
    // Star walks away from the lock position; no mount correction is applied,
    // so the reported offset must equal the true displacement.
    gc::GuideSessionParams p = Params(gc::AlgoType::Identity, gc::AlgoType::Identity, 0.01);
    gc::GuideSession session(p);

    gc::usImage img;
    RenderFrame(img, 100.0, 80.0, 0);
    CHECK(session.acquire(img));

    for (int i = 1; i <= 10; i++)
    {
        const double tx = 100.0 + 0.37 * i;
        const double ty = 80.0 + 0.21 * i;
        RenderFrame(img, tx, ty, (unsigned) i);

        gc::GuideStep step;
        CHECK(session.processFrame(img, step));
        CHECK(step.error == gc::Star::STAR_OK);
        CHECK_NEAR(step.x, tx, 0.1);
        CHECK_NEAR(step.y, ty, 0.1);
        CHECK_NEAR(step.raOffsetPx, tx - 100.0, 0.1);
        CHECK_NEAR(step.decOffsetPx, ty - 80.0, 0.1);
        CHECK(step.raCorrectionPx != 0.0 || std::fabs(step.raOffsetPx) < 0.01);
    }
}

static void testRaOnly()
{
    gc::GuideSessionParams p;
    p.raOnly = true;
    gc::GuideSession session(p);

    gc::usImage img;
    RenderFrame(img, 100.0, 80.0, 0);
    CHECK(session.acquire(img));

    RenderFrame(img, 100.0, 90.0, 1);
    gc::GuideStep step;
    CHECK(session.processFrame(img, step));
    CHECK(step.decOffsetPx == 0.0);
    CHECK(step.decCorrectionPx == 0.0);
}

static const char* AlgoName(gc::AlgoType t)
{
    switch (t)
    {
    case gc::AlgoType::Identity: return "Identity";
    case gc::AlgoType::Hysteresis: return "Hysteresis";
    case gc::AlgoType::ResistSwitch: return "ResistSwitch";
    case gc::AlgoType::Lowpass: return "Lowpass";
    case gc::AlgoType::Lowpass2: return "Lowpass2";
    case gc::AlgoType::ZFilter: return "ZFilter";
    }
    return "?";
}

static void testClosedLoop(gc::AlgoType ra, gc::AlgoType dec, double maxResidualRms)
{
    gc::GuideSession session(Params(ra, dec, 0.01));

    gc::usImage img;
    RenderFrame(img, 100.0, 80.0, 0);
    CHECK(session.acquire(img));

    double appliedX = 0.0, appliedY = 0.0; // correction already on the mount
    double sumSq = 0.0, rawSumSq = 0.0;
    int n = 0;

    for (int i = 1; i <= 40; i++)
    {
        // raw drift: 0.5 px/frame in RA, 0.2 px/frame in DEC
        const double rawX = 100.0 + 0.5 * i;
        const double rawY = 80.0 + 0.2 * i;
        // perfect mount: star position = drift - applied correction (1 frame delay)
        RenderFrame(img, rawX - appliedX, rawY - appliedY, (unsigned) i);

        gc::GuideStep step;
        CHECK(session.processFrame(img, step));

        appliedX += step.raCorrectionPx;
        appliedY += step.decCorrectionPx;

        const double resX = rawX - appliedX - 100.0;
        const double resY = rawY - appliedY - 80.0;
        if (i > 10)
        {
            sumSq += resX * resX + resY * resY;
            rawSumSq += (0.5 * i) * (0.5 * i) + (0.2 * i) * (0.2 * i);
            n++;
        }
    }

    const double rms = std::sqrt(sumSq / (2.0 * n));
    const double rawRms = std::sqrt(rawSumSq / (2.0 * n));
    std::printf("  closed loop %s/%s: residual rms %.4f px (raw %.2f px)\n", AlgoName(ra), AlgoName(dec), rms,
                rawRms);
    CHECK(n == 30);
    CHECK(rms < maxResidualRms);
    CHECK(rms < rawRms / 5.0);
}

static void testLostStar()
{
    gc::GuideSessionParams p;
    gc::GuideSession session(p);

    gc::usImage img;
    RenderFrame(img, 100.0, 80.0, 0);
    CHECK(session.acquire(img));

    const double px = session.primaryStar().X, py = session.primaryStar().Y;

    // background only
    RenderFrame(img, -1000.0, -1000.0, 1);
    gc::GuideStep step;
    CHECK(!session.processFrame(img, step));
    CHECK(!step.found);
    CHECK(step.error != gc::Star::STAR_OK);
    // PHD2 keeps the last star position for the next search
    CHECK_NEAR(session.primaryStar().X, px, 0.0);
    CHECK_NEAR(session.primaryStar().Y, py, 0.0);

    // star comes back
    RenderFrame(img, 100.0, 80.0, 2);
    CHECK(session.processFrame(img, step));
    CHECK(step.found);
}

int main()
{
    testAcquire();
    testStaticStar();
    testTracksDriftNoMount();
    testRaOnly();
    testClosedLoop(gc::AlgoType::Identity, gc::AlgoType::Identity, 0.15);
    testClosedLoop(gc::AlgoType::Hysteresis, gc::AlgoType::ResistSwitch, 0.30);
    testLostStar();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
