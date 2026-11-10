// Regression tests for the PHD2 algorithm port.
// Values are hand-derived from PHD2 v2.6.14 source; the closed-loop tests
// assert the control property (residual deflection converges to zero).
#include "guidecore/algorithms.h"
#include "guidecore/axis_stats.h"

#include <cmath>
#include <cstdio>
#include <memory>

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

static void testAxisStats()
{
    gc::AxisStats s;
    for (int i = 0; i < 5; i++)
        s.add(i, 2.0 * i + 3.0);
    CHECK(s.count() == 5);
    CHECK_NEAR(s.slope(), 2.0, 1e-12);
    CHECK_NEAR(s.intercept(), 3.0, 1e-12);
    CHECK_NEAR(s.rSquared(), 1.0, 1e-12);

    gc::AxisStats m;
    const double odd[] = {5, 1, 3, 2, 4};
    for (double y : odd)
        m.add(0, y);
    CHECK_NEAR(m.median(), 3.0, 1e-12);

    gc::AxisStats e;
    const double even[] = {4, 1, 3, 2};
    for (double y : even)
        e.add(0, y);
    CHECK_NEAR(e.median(), 2.5, 1e-12);

    // auto-windowing keeps only the newest 3 entries
    gc::AxisStats w(3);
    for (int i = 0; i < 10; i++)
        w.add(i, i);
    CHECK(w.count() == 3);
    CHECK_NEAR(w.median(), 8.0, 1e-12);
    CHECK_NEAR(w.slope(), 1.0, 1e-12);
}

static void testIdentity()
{
    auto a = gc::makeAlgorithm(gc::AlgoType::Identity, gc::defaults(gc::AlgoType::Identity));
    CHECK_NEAR(a->result(0.37), 0.37, 1e-15);
    CHECK_NEAR(a->result(-1.2), -1.2, 1e-15);
}

static void testHysteresis()
{
    const auto p = gc::defaults(gc::AlgoType::Hysteresis); // 0.2 / 0.1 / 0.7
    auto a = gc::makeAlgorithm(gc::AlgoType::Hysteresis, p);

    // (0.9*1 + 0.1*0) * 0.7
    CHECK_NEAR(a->result(1.0), 0.63, 1e-12);
    // (0.9*1 + 0.1*0.63) * 0.7
    CHECK_NEAR(a->result(1.0), 0.6741, 1e-12);
    // below minMove -> no move, and lastMove becomes 0
    CHECK_NEAR(a->result(0.1), 0.0, 1e-15);
    CHECK_NEAR(a->result(1.0), 0.63, 1e-12);

    a->reset();
    CHECK_NEAR(a->result(0.5), 0.315, 1e-12); // (0.9*0.5)*0.7
}

static void testResistSwitch()
{
    auto p = gc::defaults(gc::AlgoType::ResistSwitch); // aggression 1.0, fastSwitch true
    auto a = gc::makeAlgorithm(gc::AlgoType::ResistSwitch, p);

    // large excursion switches direction immediately
    CHECK_NEAR(a->result(1.0), 1.0, 1e-12);
    CHECK_NEAR(a->result(-1.0), -1.0, 1e-12);

    // slow build: 3 consistent moves needed before switching, then aggression applies
    p.aggression = 0.5;
    auto b = gc::makeAlgorithm(gc::AlgoType::ResistSwitch, p);
    CHECK_NEAR(b->result(0.5), 0.0, 1e-15);
    CHECK_NEAR(b->result(0.5), 0.0, 1e-15);
    CHECK_NEAR(b->result(0.5), 0.25, 1e-12);
    CHECK_NEAR(b->result(0.5), 0.25, 1e-12);

    // overshoot in the opposite direction is vetoed
    CHECK_NEAR(b->result(-0.5), 0.0, 1e-15);
}

static void testLowpass()
{
    auto p = gc::defaults(gc::AlgoType::Lowpass); // minMove 0.2, slopeWeight 5
    auto a = gc::makeAlgorithm(gc::AlgoType::Lowpass, p);

    // median 0, slope 45/825 -> 0 + 5 * 45/825 = 3/11
    CHECK_NEAR(a->result(1.0), 3.0 / 11.0, 1e-12);
    CHECK_NEAR(a->result(0.1), 0.0, 1e-15); // below minMove
}

static void testLowpass2()
{
    auto p = gc::defaults(gc::AlgoType::Lowpass2); // minMove 0.2, aggressiveness 80%
    auto a = gc::makeAlgorithm(gc::AlgoType::Lowpass2, p);

    CHECK_NEAR(a->result(1.0), 0.8, 1e-12); // fewer than 4 points: passthrough
    CHECK_NEAR(a->result(1.0), 0.8, 1e-12);
    CHECK_NEAR(a->result(1.0), 0.8, 1e-12);
    // 4th point, > 4*minMove -> outlier resets history
    CHECK_NEAR(a->result(1.0), 0.8, 1e-12);

    auto b = gc::makeAlgorithm(gc::AlgoType::Lowpass2, p);
    CHECK_NEAR(b->result(0.3), 0.24, 1e-12);
    CHECK_NEAR(b->result(0.4), 0.32, 1e-12);
    CHECK_NEAR(b->result(0.5), 0.40, 1e-12);
    // 4th point: slope 0.1, n=4 -> 0.1*4*0.8 = 0.32
    CHECK_NEAR(b->result(0.6), 0.32, 1e-12);
}

static void testZFilter()
{
    auto p = gc::defaults(gc::AlgoType::ZFilter); // minMove 0.1, expFactor 2 (Bessel)
    auto a = gc::makeAlgorithm(gc::AlgoType::ZFilter, p);

    // The Bessel filter starts from zero history, so its first output is the
    // first impulse-response coefficient (1/dcGain), not the steady-state value.
    const double first = a->result(1.0);
    CHECK(std::isfinite(first));
    CHECK(first > 0.0 && first < 1.0);

    a->reset();
    CHECK_NEAR(a->result(1.0), first, 1e-12); // reset is deterministic

    // expFactor < 1.5 selects Butterworth; it may fall under minMove at first
    p.expFactor = 1.0;
    auto b = gc::makeAlgorithm(gc::AlgoType::ZFilter, p);
    CHECK(std::isfinite(b->result(1.0)));
}

// Simulate a perfectly responsive mount: each correction removes exactly that
// much deflection. A working guide algorithm must drive the residual to zero.
static void testClosedLoop(gc::AlgoType type)
{
    auto p = gc::defaults(type);
    p.minMove = 0.001; // remove the deadband so convergence is observable
    auto a = gc::makeAlgorithm(type, p);
    double residual = 1.0;
    for (int i = 0; i < 300; i++)
        residual -= a->result(residual);
    CHECK_NEAR(residual, 0.0, 1e-2);
}

int main()
{
    testAxisStats();
    testIdentity();
    testHysteresis();
    testResistSwitch();
    testLowpass();
    testLowpass2();
    testZFilter();

    testClosedLoop(gc::AlgoType::Lowpass);
    testClosedLoop(gc::AlgoType::Lowpass2);
    testClosedLoop(gc::AlgoType::ZFilter);

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
