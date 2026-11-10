// Interactive simulator for the ported guide loop.
// One line in (command), one JSON line out (state + 121x121 preview).
// Same chain as guide_replay: calibration -> camera/mount transform ->
// guide algorithm -> 1 ms quantized pulse -> synthetic mount (1 frame delay).
// Commands: step, status, algo:<0..5>, minmove:<px>, seeing:<sigma>,
// drift:<px/frame>, dither, calibrate, reset.
#include "guidecore/guide_session.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr int kSize = 121;
constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 0.02; // px/ms
constexpr double kWestAngle = 0.4;
constexpr double kNorthAngle = 0.4 + kPi / 2.0 - 0.05;
constexpr size_t kWindow = 200;

const char* kNames[] = { "Identity", "Hysteresis", "ResistSwitch", "Lowpass", "Lowpass2", "ZFilter" };
const gc::AlgoType kTypes[] = { gc::AlgoType::Identity,  gc::AlgoType::Hysteresis, gc::AlgoType::ResistSwitch,
                                gc::AlgoType::Lowpass,   gc::AlgoType::Lowpass2,   gc::AlgoType::ZFilter };

double NormAngle(double v) { return v - std::floor((v + kPi) / (2.0 * kPi)) * (2.0 * kPi); }

struct SimMount
{
    gc::PHD_Point pos{ kSize / 2.0, kSize / 2.0 };

    void pulse(gc::CalibrationDirection dir, int ms)
    {
        double angle = 0.0, sign = 1.0;
        switch (dir)
        {
        case gc::CalibrationDirection::West: angle = kWestAngle; sign = 1.0; break;
        case gc::CalibrationDirection::East: angle = kWestAngle; sign = -1.0; break;
        case gc::CalibrationDirection::North: angle = kNorthAngle; sign = 1.0; break;
        case gc::CalibrationDirection::South: angle = kNorthAngle; sign = -1.0; break;
        case gc::CalibrationDirection::None: return;
        }
        pos.SetXY(pos.X + std::cos(angle) * kRate * ms * sign,
                  pos.Y + std::sin(angle) * kRate * ms * sign);
    }
};

bool RunCalibration(SimMount& mount, gc::Calibration& out)
{
    mount.pos.SetXY(kSize / 2.0, kSize / 2.0);
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

void Render(gc::usImage& img, double cx, double cy, double sigma, unsigned seed)
{
    img.Init(kSize, kSize);
    img.BitsPerPixel = 16;
    img.Pedestal = 0;

    std::mt19937 rng(seed);
    std::normal_distribution<double> noise(0.0, 6.0);
    const double s2 = 2.0 * sigma * sigma, amp = 12000.0;
    const double ox = kSize / 2.0, oy = kSize / 2.0;

    for (int y = 0; y < kSize; y++)
        for (int x = 0; x < kSize; x++)
        {
            const double dx = x - ox - cx, dy = y - oy - cy;
            const double v = 800.0 + noise(rng) + amp * std::exp(-(dx * dx + dy * dy) / s2);
            img.Pixel(x, y) = (unsigned short) std::max(0.0, std::min(65535.0, v));
        }
}

std::string Base64(const unsigned char* data, size_t n)
{
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3)
    {
        const unsigned v = (unsigned) data[i] << 16 | (i + 1 < n ? (unsigned) data[i + 1] << 8 : 0) |
                           (i + 2 < n ? (unsigned) data[i + 2] : 0);
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += i + 1 < n ? tbl[(v >> 6) & 63] : '=';
        out += i + 2 < n ? tbl[v & 63] : '=';
    }
    return out;
}

struct Live
{
    SimMount mount;
    gc::Calibration cal;
    gc::MountTransform transform;
    bool calibrated = false;

    gc::AlgoType algo = gc::AlgoType::Hysteresis;
    double minMove = 0.2;
    double seeing = 1.6;
    double driftRate = 0.12;

    std::unique_ptr<gc::GuideSession> session;
    gc::usImage img;

    long frame = 0;
    double appliedRA = 0.0, appliedDec = 0.0;
    double bumpX = 0.0, bumpY = 0.0;
    double jitterX = 0.0, jitterY = 0.0;
    int ditherStep = 0;

    bool hasLock = false, found = false;
    double lastRA = 0.0, lastDec = 0.0, lastSnr = 0.0, lastHfd = 0.0;
    int lastPulseRA = 0, lastPulseDec = 0;

    std::deque<double> winRA, winDec, winPulse;
    std::mt19937 rng{ 424242 };

    Live() { Rebuild(); }

    void Rebuild()
    {
        gc::GuideSessionParams p;
        p.raAlgo = p.decAlgo = algo;
        p.raParams = gc::defaults(algo);
        p.decParams = gc::defaults(algo);
        p.raParams.minMove = p.decParams.minMove = minMove;
        if (calibrated)
            p.mount.setCalibration(cal);
        session = std::make_unique<gc::GuideSession>(p);
        hasLock = false;
    }

    void Calibrate()
    {
        gc::Calibration c;
        calibrated = RunCalibration(mount, c);
        if (calibrated)
        {
            cal = c;
            transform.setCalibration(cal);
        }
        appliedRA = appliedDec = bumpX = bumpY = 0.0;
        frame = 0;
        ClearStats();
        Resize();
        Rebuild();
    }

    void Resize() { winRA.clear(); winDec.clear(); winPulse.clear(); }
    void ClearStats() { Resize(); }

    void Reset()
    {
        appliedRA = appliedDec = bumpX = bumpY = 0.0;
        jitterX = jitterY = 0.0;
        ditherStep = 0;
        frame = 0;
        ClearStats();
        Rebuild();
    }

    void Step()
    {
        frame++;

        // where the mount correction so far puts the star, in camera pixels
        gc::PHD_Point camEffect;
        if (calibrated)
            transform.mountToCamera(gc::PHD_Point(appliedRA, appliedDec), camEffect);

        // AR(1) seeing jitter so RMS never reads exactly zero
        std::normal_distribution<double> g(0.0, 0.08);
        jitterX = 0.7 * jitterX + g(rng);
        jitterY = 0.7 * jitterY + g(rng);

        const double t = (double) frame;
        const double driftRA = driftRate * t + 0.9 * std::sin(2.0 * kPi * t / 70.0);
        const double driftDec = 0.04 * t + 0.45 * std::sin(2.0 * kPi * t / 95.0);
        const double cx = driftRA - camEffect.X + bumpX + jitterX;
        const double cy = driftDec - camEffect.Y + bumpY + jitterY;

        Render(img, cx, cy, seeing, (unsigned) (frame * 7919));

        gc::GuideStep step;
        bool ok;
        if (!hasLock)
        {
            ok = session->acquire(img);
            hasLock = ok;
            if (ok)
            {
                step.x = session->primaryStar().X;
                step.y = session->primaryStar().Y;
                lastSnr = step.snr = session->primaryStar().SNR;
                lastHfd = step.hfd = session->primaryStar().HFD;
                lastRA = lastDec = 0.0;
                lastPulseRA = lastPulseDec = 0;
                ClearStats();
            }
        }
        else
        {
            ok = session->processFrame(img, step);
            if (!ok)
                hasLock = false; // PHD2 requeues the star; next frame re-selects
            else
            {
                lastRA = step.raOffsetPx;
                lastDec = step.decOffsetPx;
                lastSnr = step.snr;
                lastHfd = step.hfd;
                lastPulseRA = step.raPulseMs;
                lastPulseDec = step.decPulseMs;
                appliedRA += kRate * step.raPulseMs;
                appliedDec += kRate * step.decPulseMs;

                winRA.push_back(lastRA);
                winDec.push_back(lastDec);
                winPulse.push_back(std::hypot((double) lastPulseRA, (double) lastPulseDec));
                while (winRA.size() > kWindow)
                {
                    winRA.pop_front();
                    winDec.pop_front();
                    winPulse.pop_front();
                }
            }
        }
        found = ok;
    }

    void Dither()
    {
        const double a = (kPi / 2.0) * (ditherStep++ % 4);
        bumpX += 3.0 * std::cos(a);
        bumpY += 3.0 * std::sin(a);
    }

    void Json() const
    {
        double sqRA = 0, sqDec = 0, sumPulse = 0, peakRA = 0, peakDec = 0;
        for (size_t i = 0; i < winRA.size(); i++)
        {
            sqRA += winRA[i] * winRA[i];
            sqDec += winDec[i] * winDec[i];
            sumPulse += winPulse[i];
            peakRA = std::fmax(peakRA, std::fabs(winRA[i]));
            peakDec = std::fmax(peakDec, std::fabs(winDec[i]));
        }
        const double n = winRA.empty() ? 1.0 : (double) winRA.size();
        const double ortho = calibrated
                                 ? std::fabs(std::fabs(NormAngle(cal.xAngle - cal.yAngle)) - kPi / 2.0) * 180.0 / kPi
                                 : 0.0;

        std::vector<unsigned char> preview(kSize * kSize);
        for (int i = 0; i < kSize * kSize; i++)
        {
            const double v = (img.ImageData ? (double) img.ImageData[i] : 0.0) - 700.0;
            preview[i] = (unsigned char) std::max(0.0, std::min(255.0, v / 3300.0 * 255.0));
        }

        std::printf(
            "{\"frame\":%ld,\"hasLock\":%s,\"found\":%s,\"ra\":%.4f,\"dec\":%.4f,"
            "\"pulseRA\":%d,\"pulseDec\":%d,"
            "\"rmsRA\":%.4f,\"rmsDec\":%.4f,\"peakRA\":%.4f,\"peakDec\":%.4f,\"meanPulse\":%.2f,"
            "\"snr\":%.1f,\"hfd\":%.2f,"
            "\"calibrated\":%s,\"xAngle\":%.6f,\"yAngle\":%.6f,\"xRate\":%.6f,\"yRate\":%.6f,\"ortho\":%.3f,"
            "\"algo\":\"%s\",\"algoIndex\":%d,\"minMove\":%.3f,\"seeing\":%.2f,\"driftRate\":%.3f,"
            "\"appliedRA\":%.3f,\"appliedDec\":%.3f,\"bumpX\":%.3f,\"bumpY\":%.3f,"
            "\"lockX\":%.2f,\"lockY\":%.2f,\"w\":%d,\"h\":%d,\"img\":\"",
            frame, hasLock ? "true" : "false", found ? "true" : "false", lastRA, lastDec, lastPulseRA,
            lastPulseDec, std::sqrt(sqRA / n), std::sqrt(sqDec / n), peakRA, peakDec, sumPulse / n, lastSnr,
            lastHfd, calibrated ? "true" : "false", cal.xAngle, cal.yAngle, cal.xRate, cal.yRate, ortho,
            kNames[(int) algo], (int) algo, minMove, seeing, driftRate, appliedRA, appliedDec, bumpX, bumpY,
            session->lockPosition().X, session->lockPosition().Y, kSize, kSize);
        const std::string b64 = Base64(preview.data(), preview.size());
        std::fwrite(b64.data(), 1, b64.size(), stdout);
        std::printf("\"}\n");
        std::fflush(stdout);
    }
};

double Arg(const std::string& s, size_t pos) { return std::atof(s.c_str() + pos); }

} // namespace

int main()
{
    Live live;
    live.Calibrate();
    live.Step(); // acquire a star so the page has something to show while paused
    live.Json();

    std::string line;
    while (std::getline(std::cin, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line == "step")
            live.Step();
        else if (line == "dither")
            live.Dither();
        else if (line == "calibrate")
            live.Calibrate();
        else if (line == "reset")
            live.Reset();
        else if (line.rfind("algo:", 0) == 0)
        {
            int idx = (int) Arg(line, 5);
            if (idx >= 0 && idx < 6)
            {
                live.algo = kTypes[idx];
                live.Rebuild();
            }
        }
        else if (line.rfind("minmove:", 0) == 0)
        {
            live.minMove = std::max(0.0, Arg(line, 8));
            live.Rebuild();
        }
        else if (line.rfind("seeing:", 0) == 0)
            live.seeing = std::max(1.0, Arg(line, 7));
        else if (line.rfind("drift:", 0) == 0)
            live.driftRate = Arg(line, 6);
        // "status" and anything else: just report the current state
        live.Json();
    }
    return 0;
}
