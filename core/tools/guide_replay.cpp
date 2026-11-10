// Image-level acceptance harness for the frame guide loop.
// Runs the ported calibration state machine against a synthetic mount, then
// generates a moving star frame by frame and feeds each frame through
// GuideSession (PHD2 single-star UpdateCurrentPosition): camera -> mount
// transform -> guide algorithm -> pulse milliseconds -> mount. The mount has a
// one-frame response delay and quantizes corrections to 1 ms.
// Prints traces as JSON for tools/make_replay_report.py.
#include "guidecore/guide_session.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace {

constexpr int kSize = 121; // patch around the lock position
constexpr int kFrames = 300;
constexpr int kSettleFrames = 20;
constexpr double kPi = 3.14159265358979323846;

const char* kNames[] = { "Identity", "Hysteresis", "ResistSwitch", "Lowpass", "Lowpass2", "ZFilter" };
const gc::AlgoType kTypes[] = { gc::AlgoType::Identity,  gc::AlgoType::Hysteresis, gc::AlgoType::ResistSwitch,
                                gc::AlgoType::Lowpass,   gc::AlgoType::Lowpass2,   gc::AlgoType::ZFilter };

void Drift(int i, double& ra, double& dec)
{
    // periodic error + slow drift, relative to the lock position
    ra = 0.12 * i + 0.9 * std::sin(2.0 * kPi * i / 70.0);
    dec = 0.04 * i + 0.45 * std::sin(2.0 * kPi * i / 95.0);
}

struct SimMount
{
    double ratePxPerMs = 0.02;
    double westAngle = 0.4;
    double northAngle = 0.4 + kPi / 2.0 - 0.05;
    gc::PHD_Point pos{ 60.0, 60.0 };

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

double NormAngle(double v) { return v - std::floor((v + kPi) / (2.0 * kPi)) * (2.0 * kPi); }

bool RunCalibration(SimMount& mount, gc::Calibration& out)
{
    gc::CalibrationSession session;
    session.begin(mount.pos);

    int guard = 0;
    gc::CalibrationAction a;
    do
    {
        a = session.update(mount.pos);
        if (a.complete)
        {
            out = session.calibration();
            return true;
        }
        if (a.failed)
            return false;
        mount.pulse(a.direction, a.durationMs);
    } while (++guard < 500);

    return false;
}

void Render(gc::usImage& img, double cx, double cy, unsigned seed)
{
    img.Init(kSize, kSize);
    img.BitsPerPixel = 16;
    img.Pedestal = 0;

    std::mt19937 rng(seed);
    std::normal_distribution<double> noise(0.0, 6.0);
    const double sigma = 1.6, s2 = 2.0 * sigma * sigma, amp = 12000.0;
    const double ox = kSize / 2.0, oy = kSize / 2.0;

    for (int y = 0; y < kSize; y++)
    {
        for (int x = 0; x < kSize; x++)
        {
            const double dx = x - ox - cx, dy = y - oy - cy;
            const double v = 800.0 + noise(rng) + amp * std::exp(-(dx * dx + dy * dy) / s2);
            img.Pixel(x, y) = (unsigned short) std::max(0.0, std::min(65535.0, v));
        }
    }
}

struct Run
{
    std::vector<double> resRA, resDec, corrRA, corrDec, snr, hfd, err;
    std::vector<double> pulseRA, pulseDec, appCamRA, appCamDec;
    std::vector<int> found;
    double rmsRA = 0, rmsDec = 0, peakRA = 0, peakDec = 0, meanErr = 0;
    double meanPulseMs = 0, maxPulseMs = 0;
    int lost = 0;
};

Run RunOne(gc::AlgoType type, const std::vector<double>& driftRA, const std::vector<double>& driftDec,
           const gc::Calibration& cal, const SimMount& mountCfg)
{
    gc::GuideSessionParams p;
    p.raAlgo = type;
    p.decAlgo = type;
    p.raParams = gc::defaults(type);
    p.decParams = gc::defaults(type);
    p.mount.setCalibration(cal);

    gc::GuideSession session(p);

    Run run;
    const size_t n = driftRA.size();
    run.resRA.assign(n, 0.0);
    run.resDec.assign(n, 0.0);
    run.corrRA.assign(n, 0.0);
    run.corrDec.assign(n, 0.0);
    run.snr.assign(n, 0.0);
    run.hfd.assign(n, 0.0);
    run.err.assign(n, 0.0);
    run.pulseRA.assign(n, 0.0);
    run.pulseDec.assign(n, 0.0);
    run.appCamRA.assign(n, 0.0);
    run.appCamDec.assign(n, 0.0);
    run.found.assign(n, 0);

    gc::usImage img;
    double appliedRA = 0.0, appliedDec = 0.0; // mount correction already applied, mount px
    double sumSqRA = 0.0, sumSqDec = 0.0, sumErr = 0.0;
    double sumPulse = 0.0;
    int used = 0;

    for (size_t i = 0; i < n; i++)
    {
        // camera-space effect of the mount corrections applied so far
        gc::PHD_Point camEffect;
        p.mount.mountToCamera(gc::PHD_Point(appliedRA, appliedDec), camEffect);
        const double trueRA = driftRA[i] - camEffect.X;
        const double trueDec = driftDec[i] - camEffect.Y;
        Render(img, trueRA, trueDec, (unsigned) (5000 + i));

        gc::GuideStep step;
        bool ok;
        if (i == 0)
        {
            ok = session.acquire(img);
            step.found = ok;
            step.x = session.primaryStar().X;
            step.y = session.primaryStar().Y;
            step.snr = session.primaryStar().SNR;
            step.hfd = session.primaryStar().HFD;
        }
        else
        {
            ok = session.processFrame(img, step);
        }

        run.found[i] = ok ? 1 : 0;
        if (!ok)
        {
            run.lost++;
            continue;
        }

        run.resRA[i] = step.raOffsetPx;
        run.resDec[i] = step.decOffsetPx;
        run.corrRA[i] = step.raCorrectionPx;
        run.corrDec[i] = step.decCorrectionPx;
        run.snr[i] = step.snr;
        run.hfd[i] = step.hfd;

        // centroid error against the rendered position (relative to patch center)
        run.err[i] = std::hypot(step.x - (kSize / 2.0 + trueRA), step.y - (kSize / 2.0 + trueDec));

        // PHD2 Mount::MoveOffset -> MoveAxis: integer milliseconds, signed here.
        // Direction-to-mount-sign mapping belongs to the mount driver; the
        // simulation assumes +mountX cancels +mountX offset.
        run.pulseRA[i] = step.raPulseMs;
        run.pulseDec[i] = step.decPulseMs;
        appliedRA += mountCfg.ratePxPerMs * step.raPulseMs;
        appliedDec += mountCfg.ratePxPerMs * step.decPulseMs;

        // what the mount correction looks like back in camera pixels
        p.mount.mountToCamera(gc::PHD_Point(appliedRA, appliedDec), camEffect);
        run.appCamRA[i] = camEffect.X;
        run.appCamDec[i] = camEffect.Y;

        if ((int) i >= kSettleFrames)
        {
            sumSqRA += step.raOffsetPx * step.raOffsetPx;
            sumSqDec += step.decOffsetPx * step.decOffsetPx;
            run.peakRA = std::fmax(run.peakRA, std::fabs(step.raOffsetPx));
            run.peakDec = std::fmax(run.peakDec, std::fabs(step.decOffsetPx));
            sumErr += run.err[i];
            const double pulse = std::hypot(step.raPulseMs, step.decPulseMs);
            sumPulse += pulse;
            run.maxPulseMs = std::fmax(run.maxPulseMs, pulse);
            used++;
        }
    }

    run.rmsRA = used ? std::sqrt(sumSqRA / used) : 0.0;
    run.rmsDec = used ? std::sqrt(sumSqDec / used) : 0.0;
    run.meanErr = used ? sumErr / used : 0.0;
    run.meanPulseMs = used ? sumPulse / used : 0.0;
    return run;
}

void PrintArray(const std::vector<double>& v)
{
    std::printf("[");
    for (size_t i = 0; i < v.size(); i++)
        std::printf("%s%.4f", i ? "," : "", v[i]);
    std::printf("]");
}

void PrintInts(const std::vector<int>& v)
{
    std::printf("[");
    for (size_t i = 0; i < v.size(); i++)
        std::printf("%s%d", i ? "," : "", v[i]);
    std::printf("]");
}

} // namespace

int main()
{
    std::vector<double> driftRA(kFrames), driftDec(kFrames);
    for (int i = 0; i < kFrames; i++)
        Drift(i, driftRA[i], driftDec[i]);

    // calibrate once on the simulated mount, then guide with that calibration
    SimMount mountCfg;
    gc::Calibration cal;
    const bool calibrated = RunCalibration(mountCfg, cal);

    const double ortho =
        std::fabs(std::fabs(NormAngle(cal.xAngle - cal.yAngle)) - kPi / 2.0) * 180.0 / kPi;

    std::printf("{\"width\":%d,\"height\":%d,\"frames\":%d,\"settle\":%d,", kSize, kSize, kFrames, kSettleFrames);
    std::printf("\"calibrated\":%s,", calibrated ? "true" : "false");
    std::printf("\"calibration\":{\"xAngle\":%.6f,\"yAngle\":%.6f,\"xRate\":%.6f,\"yRate\":%.6f,"
                "\"orthoErrorDeg\":%.4f,\"westAngle\":%.6f,\"northAngle\":%.6f,\"rate\":%.6f},",
                cal.xAngle, cal.yAngle, cal.xRate, cal.yRate, ortho, mountCfg.westAngle, mountCfg.northAngle,
                mountCfg.ratePxPerMs);
    std::printf("\"drift\":{\"ra\":");
    PrintArray(driftRA);
    std::printf(",\"dec\":");
    PrintArray(driftDec);
    std::printf("},\"algorithms\":[");

    for (size_t a = 0; a < sizeof(kTypes) / sizeof(kTypes[0]); a++)
    {
        Run r = RunOne(kTypes[a], driftRA, driftDec, cal, mountCfg);
        std::printf("%s{\"name\":\"%s\",\"rmsRA\":%.4f,\"rmsDec\":%.4f,\"peakRA\":%.4f,\"peakDec\":%.4f,"
                    "\"meanErr\":%.4f,\"lost\":%d,\"meanPulseMs\":%.2f,\"maxPulseMs\":%.2f,",
                    a ? "," : "", kNames[a], r.rmsRA, r.rmsDec, r.peakRA, r.peakDec, r.meanErr, r.lost,
                    r.meanPulseMs, r.maxPulseMs);
        std::printf("\"resRA\":");
        PrintArray(r.resRA);
        std::printf(",\"resDec\":");
        PrintArray(r.resDec);
        std::printf(",\"corrRA\":");
        PrintArray(r.corrRA);
        std::printf(",\"corrDec\":");
        PrintArray(r.corrDec);
        std::printf(",\"pulseRA\":");
        PrintArray(r.pulseRA);
        std::printf(",\"pulseDec\":");
        PrintArray(r.pulseDec);
        std::printf(",\"appCamRA\":");
        PrintArray(r.appCamRA);
        std::printf(",\"appCamDec\":");
        PrintArray(r.appCamDec);
        std::printf(",\"snr\":");
        PrintArray(r.snr);
        std::printf(",\"hfd\":");
        PrintArray(r.hfd);
        std::printf(",\"found\":");
        PrintInts(r.found);
        std::printf("}");
    }
    std::printf("]}\n");
    return calibrated ? 0 : 1;
}
