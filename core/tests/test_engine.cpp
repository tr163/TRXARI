// End-to-end test of the simulated engine: calibration followed by guiding.
#include "guidecore/engine.h"

#include <cmath>
#include <cstdio>

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

int main()
{
    CHECK(gc_engine_create("{\"exposureMs\":1000,\"seed\":7,\"jitterPx\":0.0}") == 0);
    CHECK(gc_engine_set_tracking(1) == 0);
    CHECK(gc_engine_start_calibration() == 0);

    GcEngineStats s{};
    for (int i = 0; i < 400; ++i)
    {
        CHECK(gc_engine_step() == 0);
        CHECK(gc_engine_get_stats(&s) == 0);
        if (s.state != GC_CALIBRATING)
            break;
    }
    CHECK(s.calibrationValid == 1);
    CHECK(s.calibrationStep == GC_CAL_DONE);

    CHECK(gc_engine_start_guide() == 0);
    for (int i = 0; i < 60; ++i)
        CHECK(gc_engine_step() == 0);

    CHECK(gc_engine_get_stats(&s) == 0);
    CHECK(s.starValid == 1);
    CHECK(s.rmsTotal < 0.5);
    CHECK(s.meanPulseMs > 0);

    int32_t w = 0, h = 0;
    CHECK(gc_engine_preview_size(&w, &h) == 0);
    CHECK(w == 640 && h == 480);
    CHECK(gc_engine_get_preview(nullptr, 0) == w * h);

    char small[8];
    const int need = gc_engine_get_status(small, sizeof small);
    CHECK(need > (int) sizeof small);

    char status[4096];
    CHECK(gc_engine_get_status(status, sizeof status) == need);
    CHECK(std::fabs(s.rmsTotal) >= 0.0);
    std::printf("engine: rmsTotal=%.3f pulse=%dms\n", s.rmsTotal, s.meanPulseMs);

    gc_engine_destroy();
    std::printf("engine: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
