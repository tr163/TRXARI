#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum GcEngineState
{
    GC_IDLE = 0,
    GC_LOOPING = 1,
    GC_CALIBRATING = 2,
    GC_GUIDING = 3,
    GC_STAR_LOST = 4
};

enum GcCalibrationStep
{
    GC_CAL_IDLE = 0,
    GC_CAL_WEST = 1,
    GC_CAL_EAST = 2,
    GC_CAL_BACKLASH = 3,
    GC_CAL_NORTH = 4,
    GC_CAL_SOUTH = 5,
    GC_CAL_RECENTER = 6,
    GC_CAL_DONE = 7,
    GC_CAL_FAILED = 8
};

typedef struct GcEngineStats
{
    int32_t state;
    int32_t frame;
    int32_t starValid;
    int32_t calibrationValid;
    int32_t calibrationStep;
    int32_t samples;
    int32_t lastRaMs;
    int32_t lastDecMs;
    int32_t meanPulseMs;
    double snr;
    double hfd;
    double dx;
    double dy;
    double rmsRa;
    double rmsDec;
    double rmsTotal;
    double peak;
    double raDeg;
    double decDeg;
    int32_t tracking;
    int32_t slewRate;
} GcEngineStats;

int32_t gc_engine_create(const char *configJson); // 0 ok, <0 error
void gc_engine_destroy(void);
int32_t gc_engine_step(void); // advance one frame; host tests use this

int32_t gc_engine_start_loop(void);
int32_t gc_engine_stop_loop(void);
int32_t gc_engine_start_calibration(void);
int32_t gc_engine_start_guide(void); // -3 when not calibrated
int32_t gc_engine_stop(void);        // stop guiding/calibration, back to loop
int32_t gc_engine_clear_calibration(void);

int32_t gc_engine_set_params(const char *json);
int32_t gc_engine_set_tracking(int32_t on);
int32_t gc_engine_set_slew_rate(int32_t rate); // 2..2048
int32_t gc_engine_manual_move(int32_t axis, int32_t dir, int32_t on); // axis 0=RA 1=DEC
int32_t gc_engine_home(void);

int32_t gc_engine_get_stats(GcEngineStats *out);
int32_t gc_engine_preview_size(int32_t *w, int32_t *h);
int32_t gc_engine_get_preview(uint8_t *out, int32_t cap); // 8-bit gray, w*h; 0 when no frame
int32_t gc_engine_get_status(char *out, int32_t cap);     // required length incl. '\0'

#ifdef __cplusplus
}
#endif
