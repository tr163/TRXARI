#include "guidecore/engine.h"

#include "guidecore/algorithms.h"
#include "guidecore/calibration.h"
#include "guidecore/guide_session.h"
#include "guidecore/sim_drivers.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr size_t kStatsWindow = 300;
constexpr const char *kCoreVersion = "a1";

struct EngineConfig
{
    int exposureMs = 1000;
    uint32_t seed = 424242;
    double jitterPx = 0.08;
    double raDriftPxPerSec = 0.25;
    double decDriftPxPerSec = 0.08;
    double untrackedRaDriftPxPerSec = 1.0;
};

std::string TrimLower(std::string s)
{
    while (!s.empty() && std::isspace((unsigned char) s.front()))
        s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char) s.back()))
        s.pop_back();
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char) std::tolower(c); });
    return s;
}

bool JsonNumber(const std::string &json, const char *key, double &out)
{
    const std::string pat = std::string("\"") + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos)
        return false;
    p = json.find(':', p + pat.size());
    if (p == std::string::npos)
        return false;
    const char *s = json.c_str() + p + 1;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
        ++s;
    char *end = nullptr;
    const double v = std::strtod(s, &end);
    if (end == s)
        return false;
    out = v;
    return true;
}

double JsonNumberOr(const std::string &json, const char *key, double def)
{
    double v = def;
    return JsonNumber(json, key, v) ? v : def;
}

std::string JsonString(const std::string &json, const char *key)
{
    const std::string pat = std::string("\"") + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos)
        return {};
    p = json.find(':', p + pat.size());
    if (p == std::string::npos)
        return {};
    p = json.find('"', p + 1);
    if (p == std::string::npos)
        return {};
    const size_t e = json.find('"', p + 1);
    if (e == std::string::npos)
        return {};
    return json.substr(p + 1, e - p - 1);
}

bool AlgoFromName(const std::string &name, gc::AlgoType &out)
{
    const std::string n = TrimLower(name);
    if (n == "identity") out = gc::AlgoType::Identity;
    else if (n == "hysteresis") out = gc::AlgoType::Hysteresis;
    else if (n == "resistswitch") out = gc::AlgoType::ResistSwitch;
    else if (n == "lowpass") out = gc::AlgoType::Lowpass;
    else if (n == "lowpass2") out = gc::AlgoType::Lowpass2;
    else if (n == "zfilter") out = gc::AlgoType::ZFilter;
    else return false;
    return true;
}

const char *AlgoName(gc::AlgoType type)
{
    switch (type)
    {
    case gc::AlgoType::Identity: return "identity";
    case gc::AlgoType::Hysteresis: return "hysteresis";
    case gc::AlgoType::ResistSwitch: return "resistswitch";
    case gc::AlgoType::Lowpass: return "lowpass";
    case gc::AlgoType::Lowpass2: return "lowpass2";
    case gc::AlgoType::ZFilter: return "zfilter";
    }
    return "hysteresis";
}

const char *StateName(int state)
{
    switch (state)
    {
    case GC_IDLE: return "idle";
    case GC_LOOPING: return "looping";
    case GC_CALIBRATING: return "calibrating";
    case GC_GUIDING: return "guiding";
    case GC_STAR_LOST: return "star_lost";
    }
    return "idle";
}

const char *CalStepName(int step)
{
    switch (step)
    {
    case GC_CAL_WEST: return "west";
    case GC_CAL_EAST: return "east";
    case GC_CAL_BACKLASH: return "backlash";
    case GC_CAL_NORTH: return "north";
    case GC_CAL_SOUTH: return "south";
    case GC_CAL_RECENTER: return "recenter";
    case GC_CAL_DONE: return "done";
    case GC_CAL_FAILED: return "failed";
    default: return "idle";
    }
}

class Engine
{
public:
    Engine()
    {
        m_rng.seed(424242);
        m_sim = gc::SimMount(m_simCfg);
        m_cam = gc::SimCamera(m_simCfg);
        RebuildSession();
    }

    ~Engine() { StopWorker(); }

    int Configure(const char *configJson)
    {
        const std::string json = configJson ? configJson : "{}";
        m_cfg.exposureMs = (int) JsonNumberOr(json, "exposureMs", 1000);
        m_cfg.exposureMs = std::max(100, std::min(10000, m_cfg.exposureMs));
        m_cfg.seed = (uint32_t) JsonNumberOr(json, "seed", 424242);
        m_cfg.jitterPx = std::max(0.0, JsonNumberOr(json, "jitterPx", 0.08));
        m_cfg.raDriftPxPerSec = JsonNumberOr(json, "raDriftPxPerSec", 0.25);
        m_cfg.decDriftPxPerSec = JsonNumberOr(json, "decDriftPxPerSec", 0.08);
        m_cfg.untrackedRaDriftPxPerSec = JsonNumberOr(json, "untrackedRaDriftPxPerSec", 1.0);

        m_simCfg.seed = m_cfg.seed;
        m_simCfg.raDriftPxPerSec = m_cfg.raDriftPxPerSec;
        m_simCfg.decDriftPxPerSec = m_cfg.decDriftPxPerSec;
        m_simCfg.untrackedRaDriftPxPerSec = m_cfg.untrackedRaDriftPxPerSec;
        m_sim = gc::SimMount(m_simCfg);
        m_cam = gc::SimCamera(m_simCfg);
        m_rng.seed(m_cfg.seed);
        m_jitterX = m_jitterY = 0.0;
        m_preview.clear();
        m_hasPreview = false;
        m_img.Clear();
        ClearStats();
        m_state = GC_IDLE;
        m_calValid = false;
        m_calFailed = false;
        m_calBegun = false;
        m_transform.clear();
        RebuildSession();
        return 0;
    }

    int Tick()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state == GC_IDLE)
            return 0;

        const double dt = (double) m_cfg.exposureMs / 1000.0;
        m_sim.advance(dt);

        std::normal_distribution<double> g(0.0, m_cfg.jitterPx);
        m_jitterX = 0.7 * m_jitterX + g(m_rng);
        m_jitterY = 0.7 * m_jitterY + g(m_rng);

        const bool lowNoise = (m_state == GC_CALIBRATING);
        m_cam.render(m_sim, m_img, (uint32_t) m_frame, lowNoise ? 0.0 : m_jitterX,
                     lowNoise ? 0.0 : m_jitterY, lowNoise);
        ++m_frame;

        switch (m_state)
        {
        case GC_LOOPING: TickLooping(); break;
        case GC_CALIBRATING: TickCalibrating(); break;
        case GC_GUIDING: TickGuiding(); break;
        case GC_STAR_LOST: TickStarLost(); break;
        default: break;
        }

        m_cam.toPreview(m_img, m_preview);
        m_hasPreview = true;
        return 0;
    }

    int StartLoop()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_state = GC_LOOPING;
            m_starValid = false;
            m_lostFrames = 0;
            m_calBegun = false;
            m_calFailed = false;
            m_sim.setDriftEnabled(true);
            ClearStats();
            RebuildSession();
        }
        StartWorker();
        return 0;
    }

    int StopLoop()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_state = GC_IDLE;
            m_starValid = false;
            m_calBegun = false;
        }
        StopWorker();
        return 0;
    }

    int StartCalibration()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            gc::CalibrationParams cp;
            cp.stepMs = 250;
            cp.distancePx = 10.0;
            cp.maxMovePixels = 15.0;
            m_calSession = std::make_unique<gc::CalibrationSession>(cp);
            m_calBegun = false;
            m_calFailed = false;
            m_calFrames = 0;
            m_lostFrames = 0;
            m_state = GC_CALIBRATING;
            m_starValid = false;
            m_sim.setTracking(true);
            m_sim.setDriftEnabled(false);
            ClearStats();
            RebuildSession();
        }
        StartWorker();
        return 0;
    }

    int StartGuide()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_calValid)
                return -3;
            m_state = GC_GUIDING;
            m_lostFrames = 0;
            m_starValid = false;
            m_sim.setDriftEnabled(true);
            ClearStats();
            RebuildSession();
        }
        StartWorker();
        return 0;
    }

    int Stop()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state == GC_CALIBRATING)
        {
            m_calBegun = false;
            m_sim.setDriftEnabled(true);
        }
        if (m_state == GC_GUIDING || m_state == GC_STAR_LOST || m_state == GC_CALIBRATING)
            m_state = GC_LOOPING;
        m_lostFrames = 0;
        m_starValid = false;
        RebuildSession();
        return 0;
    }

    int ClearCalibration()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_calValid = false;
        m_calFailed = false;
        m_calBegun = false;
        m_transform.clear();
        if (m_state == GC_GUIDING || m_state == GC_STAR_LOST)
            m_state = GC_LOOPING;
        ClearStats();
        RebuildSession();
        return 0;
    }

    int SetParams(const char *json)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state == GC_CALIBRATING)
            return -2;

        const std::string in = json ? json : "{}";
        gc::AlgoType ra = m_raAlgo, dec = m_decAlgo;
        const std::string raName = JsonString(in, "raAlgo");
        const std::string decName = JsonString(in, "decAlgo");
        if (!raName.empty() && !AlgoFromName(raName, ra))
            return -4;
        if (!decName.empty() && !AlgoFromName(decName, dec))
            return -4;
        if (!raName.empty() && ra != m_raAlgo)
            m_raParams = gc::defaults(ra);
        if (!decName.empty() && dec != m_decAlgo)
            m_decParams = gc::defaults(dec);
        m_raAlgo = ra;
        m_decAlgo = dec;

        m_raParams.minMove = std::max(0.0, JsonNumberOr(in, "minMove", m_raParams.minMove));
        m_decParams.minMove = m_raParams.minMove;
        m_raParams.aggression = NormalizeFraction(JsonNumberOr(in, "aggression", m_raParams.aggression));
        m_decParams.aggression = m_raParams.aggression;
        m_raParams.hysteresis = NormalizeFraction(JsonNumberOr(in, "hysteresis", m_raParams.hysteresis));
        m_decParams.hysteresis = m_raParams.hysteresis;
        m_raParams.slopeWeight = JsonNumberOr(in, "slopeWeight", m_raParams.slopeWeight);
        m_decParams.slopeWeight = m_raParams.slopeWeight;
        m_raParams.aggressiveness = JsonNumberOr(in, "aggressiveness", m_raParams.aggressiveness);
        m_decParams.aggressiveness = m_raParams.aggressiveness;
        m_raParams.expFactor = std::max(1.0, JsonNumberOr(in, "expFactor", m_raParams.expFactor));
        m_decParams.expFactor = m_raParams.expFactor;
        m_raParams.fastSwitch = JsonNumberOr(in, "fastSwitch", m_raParams.fastSwitch ? 1.0 : 0.0) != 0.0;
        m_decParams.fastSwitch = m_raParams.fastSwitch;
        m_guideRate = std::max(0.1, std::min(1.0, JsonNumberOr(in, "guideRate", m_guideRate)));

        RebuildSession();
        return 0;
    }

    int SetTracking(int on)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sim.setTracking(on != 0);
        return 0;
    }

    int SetSlewRate(int rate)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sim.setSlewRate(rate);
        return 0;
    }

    int ManualMove(int axis, int dir, int on)
    {
        if (axis != 0 && axis != 1)
            return -4;
        if (dir != 1 && dir != -1)
            return -4;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sim.manualMove(axis, dir, on != 0);
        return 0;
    }

    int Home()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sim.reset();
        ClearStats();
        m_starValid = false;
        if (m_state == GC_GUIDING || m_state == GC_STAR_LOST)
            m_state = GC_LOOPING;
        RebuildSession();
        return 0;
    }

    int GetStats(GcEngineStats *out)
    {
        if (!out)
            return -4;
        std::lock_guard<std::mutex> lock(m_mutex);
        FillStats(*out);
        return 0;
    }

    int PreviewSize(int32_t *w, int32_t *h) const
    {
        if (!w || !h)
            return -4;
        *w = m_cam.width();
        *h = m_cam.height();
        return 0;
    }

    int GetPreview(uint8_t *out, int32_t cap)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_hasPreview)
            return 0;
        const int need = (int) m_preview.size();
        if (out && cap >= need)
            std::memcpy(out, m_preview.data(), (size_t) need);
        return need;
    }

    int GetStatus(char *out, int32_t cap)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        char body[3072];
        const double rmsTotal = TotalRms();
        const double peak = Peak();
        const double ortho = OrthoErrorDeg();
        const gc::Calibration &cal = m_cal;
        std::snprintf(
            body, sizeof body,
            "{\"state\":\"%s\",\"frame\":%d,"
            "\"star\":{\"valid\":%s,\"snr\":%.2f,\"hfd\":%.2f,\"dx\":%.3f,\"dy\":%.3f},"
            "\"guide\":{\"samples\":%d,\"rmsRa\":%.3f,\"rmsDec\":%.3f,\"rmsTotal\":%.3f,"
            "\"peak\":%.3f,\"lastRaMs\":%d,\"lastDecMs\":%d,\"meanPulseMs\":%d},"
            "\"calibration\":{\"valid\":%s,\"step\":\"%s\",\"angleDeg\":%.3f,"
            "\"raRate\":%.6f,\"decRate\":%.6f,\"orthoError\":%.3f},"
            "\"mount\":{\"tracking\":%s,\"slewRate\":%d,\"raDeg\":%.3f,\"decDeg\":%.3f},"
            "\"params\":{\"raAlgo\":\"%s\",\"decAlgo\":\"%s\",\"minMove\":%.3f,"
            "\"aggression\":%.0f,\"hysteresis\":%.0f,\"guideRate\":%.2f},"
            "\"engine\":{\"coreVersion\":\"%s\"},\"previewW\":%d,\"previewH\":%d}",
            StateName(m_state), m_frame, m_starValid ? "true" : "false", m_snr, m_hfd, m_dx, m_dy,
            (int) m_winRa.size(), Rms(m_winRa), Rms(m_winDec), rmsTotal, peak, m_lastRaMs, m_lastDecMs,
            MeanPulse(), m_calValid ? "true" : "false", CalStepName(CalStep()), cal.xAngle * 180.0 / kPi,
            cal.xRate, cal.yRate, ortho, m_sim.tracking() ? "true" : "false", m_sim.slewRate(),
            m_sim.raDeg(), m_sim.decDeg(), AlgoName(m_raAlgo), AlgoName(m_decAlgo), m_raParams.minMove,
            m_raParams.aggression * 100.0, m_raParams.hysteresis * 100.0, m_guideRate, kCoreVersion,
            m_cam.width(), m_cam.height());

        const int need = (int) std::strlen(body) + 1;
        if (out && cap >= need)
            std::memcpy(out, body, (size_t) need);
        return need;
    }

private:
    static double NormalizeFraction(double v)
    {
        if (v > 1.0)
            v /= 100.0;
        return std::max(0.0, std::min(1.0, v));
    }

    void RebuildSession()
    {
        gc::GuideSessionParams p;
        p.raAlgo = m_raAlgo;
        p.decAlgo = m_decAlgo;
        p.raParams = m_raParams;
        p.decParams = m_decParams;
        p.autoFind.searchRegion = 20;
        if (m_calValid)
            p.mount.setCalibration(m_cal);
        m_session = std::make_unique<gc::GuideSession>(p);
    }

    void ClearStats()
    {
        m_winRa.clear();
        m_winDec.clear();
        m_winPulse.clear();
        m_lastRaMs = m_lastDecMs = 0;
        m_dx = m_dy = 0.0;
        m_snr = m_hfd = 0.0;
    }

    void PushStats(double ra, double dec, int raMs, int decMs)
    {
        m_winRa.push_back(ra);
        m_winDec.push_back(dec);
        m_winPulse.push_back(std::hypot((double) raMs, (double) decMs));
        while (m_winRa.size() > kStatsWindow)
        {
            m_winRa.pop_front();
            m_winDec.pop_front();
            m_winPulse.pop_front();
        }
    }

    static double Rms(const std::deque<double> &v)
    {
        if (v.empty())
            return 0.0;
        double sum = 0.0;
        for (double x : v)
            sum += x * x;
        return std::sqrt(sum / (double) v.size());
    }

    double TotalRms() const
    {
        const double ra = Rms(m_winRa), dec = Rms(m_winDec);
        return std::sqrt((ra * ra + dec * dec) / 2.0);
    }

    double Peak() const
    {
        double peak = 0.0;
        for (double x : m_winRa)
            peak = std::max(peak, std::fabs(x));
        for (double x : m_winDec)
            peak = std::max(peak, std::fabs(x));
        return peak;
    }

    int MeanPulse() const
    {
        if (m_winPulse.empty())
            return 0;
        double sum = 0.0;
        for (double x : m_winPulse)
            sum += x;
        return (int) std::lround(sum / (double) m_winPulse.size());
    }

    int CalStep() const
    {
        if (m_calFailed)
            return GC_CAL_FAILED;
        if (!m_calSession)
            return m_calValid ? GC_CAL_DONE : GC_CAL_IDLE;
        switch (m_calSession->state())
        {
        case gc::CalibrationState::Cleared: return GC_CAL_IDLE;
        case gc::CalibrationState::GoWest: return GC_CAL_WEST;
        case gc::CalibrationState::GoEast: return GC_CAL_EAST;
        case gc::CalibrationState::ClearBacklash: return GC_CAL_BACKLASH;
        case gc::CalibrationState::GoNorth: return GC_CAL_NORTH;
        case gc::CalibrationState::GoSouth: return GC_CAL_SOUTH;
        case gc::CalibrationState::NudgeSouth: return GC_CAL_RECENTER;
        case gc::CalibrationState::Complete: return GC_CAL_DONE;
        }
        return GC_CAL_IDLE;
    }

    double OrthoErrorDeg() const
    {
        if (!m_calValid)
            return 0.0;
        const double d = m_cal.xAngle - m_cal.yAngle;
        const double norm = d - std::floor((d + kPi) / (2.0 * kPi)) * (2.0 * kPi);
        return std::fabs(std::fabs(norm) - kPi / 2.0) * 180.0 / kPi;
    }

    void FillStats(GcEngineStats &s) const
    {
        std::memset(&s, 0, sizeof s);
        s.state = m_state;
        s.frame = m_frame;
        s.starValid = m_starValid ? 1 : 0;
        s.calibrationValid = m_calValid ? 1 : 0;
        s.calibrationStep = CalStep();
        s.samples = (int) m_winRa.size();
        s.lastRaMs = m_lastRaMs;
        s.lastDecMs = m_lastDecMs;
        s.meanPulseMs = MeanPulse();
        s.snr = m_snr;
        s.hfd = m_hfd;
        s.dx = m_dx;
        s.dy = m_dy;
        s.rmsRa = Rms(m_winRa);
        s.rmsDec = Rms(m_winDec);
        s.rmsTotal = TotalRms();
        s.peak = Peak();
        s.raDeg = m_sim.raDeg();
        s.decDeg = m_sim.decDeg();
        s.tracking = m_sim.tracking() ? 1 : 0;
        s.slewRate = m_sim.slewRate();
    }

    void TickLooping()
    {
        m_lostFrames = 0;
        if (!m_session->acquire(m_img))
        {
            m_starValid = false;
            return;
        }
        m_starValid = true;
        const gc::Star &star = m_session->primaryStar();
        m_snr = star.SNR;
        m_hfd = star.HFD;
        m_dx = m_dy = 0.0;
        m_lastRaMs = m_lastDecMs = 0;
    }

    void TickCalibrating()
    {
        if (!m_calBegun)
        {
            if (!m_session->acquire(m_img))
            {
                m_starValid = false;
                return;
            }
            m_starValid = true;
            const gc::Star &star = m_session->primaryStar();
            m_snr = star.SNR;
            m_hfd = star.HFD;
            gc::PHD_Point pos;
            pos.SetXY(star.X, star.Y);
            m_calSession->begin(pos, 0.0, 0);
            m_calBegun = true;
            return;
        }

        gc::GuideStep step;
        gc::PHD_Point pos;
        if (m_session->processFrame(m_img, step))
        {
            pos.SetXY(step.x, step.y);
            m_starValid = true;
            m_snr = step.snr;
            m_hfd = step.hfd;
            m_dx = step.raOffsetPx;
            m_dy = step.decOffsetPx;
        }
        else if (m_session->acquire(m_img))
        {
            const gc::Star &star = m_session->primaryStar();
            pos.SetXY(star.X, star.Y);
            m_starValid = true;
            m_snr = star.SNR;
            m_hfd = star.HFD;
        }
        else
        {
            m_starValid = false;
            if (++m_calFrames > 400)
                FailCalibration();
            return;
        }

        const gc::CalibrationAction action = m_calSession->update(pos);
        if (action.complete)
        {
            m_cal = m_calSession->calibration();
            m_calValid = true;
            m_calFailed = false;
            m_transform.setCalibration(m_cal);
            m_state = GC_LOOPING;
            m_calBegun = false;
            m_sim.setDriftEnabled(true);
            ClearStats();
            RebuildSession();
            return;
        }
        if (action.failed)
        {
            FailCalibration();
            return;
        }
        if (action.direction != gc::CalibrationDirection::None)
            m_sim.pulse(action.direction, action.durationMs);

        if (++m_calFrames > 400)
            FailCalibration();
    }

    void TickGuiding()
    {
        gc::GuideStep step;
        if (!m_session->processFrame(m_img, step))
        {
            m_starValid = false;
            if (++m_lostFrames >= 5)
                m_state = GC_STAR_LOST;
            return;
        }

        m_lostFrames = 0;
        m_starValid = true;
        m_snr = step.snr;
        m_hfd = step.hfd;
        m_dx = step.raOffsetPx;
        m_dy = step.decOffsetPx;
        m_lastRaMs = step.raPulseMs;
        m_lastDecMs = step.decPulseMs;

        if (m_transform.isCalibrated())
        {
            gc::PHD_Point mountVec, camVec;
            mountVec.SetXY(step.raCorrectionPx, step.decCorrectionPx);
            if (m_transform.mountToCamera(mountVec, camVec))
                m_sim.applyGuideCorrection(camVec.X, camVec.Y);
        }
        PushStats(step.mountRaPx, step.mountDecPx, step.raPulseMs, step.decPulseMs);
    }

    void TickStarLost()
    {
        if (++m_lostFrames > 10)
        {
            m_state = GC_LOOPING;
            m_starValid = false;
            return;
        }
        if (m_session->acquire(m_img))
        {
            m_state = GC_GUIDING;
            m_lostFrames = 0;
            m_starValid = true;
            const gc::Star &star = m_session->primaryStar();
            m_snr = star.SNR;
            m_hfd = star.HFD;
        }
        else
        {
            m_starValid = false;
        }
    }

    void FailCalibration()
    {
        m_calFailed = true;
        m_calBegun = false;
        m_sim.setDriftEnabled(true);
        m_state = GC_LOOPING;
        ClearStats();
        RebuildSession();
    }

    void StartWorker()
    {
        if (m_worker.joinable())
        {
            if (m_running.load())
                return;
            m_worker.join();
        }
        m_running = true;
        m_worker = std::thread([this] {
            while (m_running.load())
            {
                Tick();
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait_for(lock, std::chrono::milliseconds(m_cfg.exposureMs),
                              [this] { return !m_running.load(); });
            }
        });
    }

    void StopWorker()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_running = false;
        }
        m_cv.notify_all();
        if (m_worker.joinable())
            m_worker.join();
    }

    EngineConfig m_cfg;
    gc::SimMountConfig m_simCfg;
    gc::SimMount m_sim;
    gc::SimCamera m_cam;
    gc::usImage m_img;
    std::vector<uint8_t> m_preview;
    bool m_hasPreview = false;

    std::unique_ptr<gc::GuideSession> m_session;
    std::unique_ptr<gc::CalibrationSession> m_calSession;
    gc::MountTransform m_transform;
    gc::Calibration m_cal;
    bool m_calValid = false;
    bool m_calFailed = false;
    bool m_calBegun = false;
    int m_calFrames = 0;
    int m_lostFrames = 0;

    int m_state = GC_IDLE;
    int m_frame = 0;
    bool m_starValid = false;
    double m_snr = 0.0, m_hfd = 0.0, m_dx = 0.0, m_dy = 0.0;
    int m_lastRaMs = 0, m_lastDecMs = 0;
    std::deque<double> m_winRa, m_winDec, m_winPulse;

    gc::AlgoType m_raAlgo = gc::AlgoType::Hysteresis;
    gc::AlgoType m_decAlgo = gc::AlgoType::ResistSwitch;
    gc::AlgoParams m_raParams = gc::defaults(gc::AlgoType::Hysteresis);
    gc::AlgoParams m_decParams = gc::defaults(gc::AlgoType::ResistSwitch);
    double m_guideRate = 0.5;

    std::mt19937 m_rng;
    double m_jitterX = 0.0, m_jitterY = 0.0;

    std::thread m_worker;
    std::atomic<bool> m_running{false};
    std::condition_variable m_cv;
    mutable std::mutex m_mutex;
};

std::unique_ptr<Engine> g_engine;

Engine *Get()
{
    return g_engine.get();
}

} // namespace

extern "C" {

int32_t gc_engine_create(const char *configJson)
{
    if (g_engine)
        g_engine->StopLoop();
    g_engine = std::make_unique<Engine>();
    return g_engine->Configure(configJson);
}

void gc_engine_destroy(void)
{
    if (!g_engine)
        return;
    g_engine->StopLoop();
    g_engine.reset();
}

int32_t gc_engine_step(void)
{
    Engine *e = Get();
    return e ? e->Tick() : -1;
}

int32_t gc_engine_start_loop(void) { return Get() ? Get()->StartLoop() : -1; }
int32_t gc_engine_stop_loop(void) { return Get() ? Get()->StopLoop() : -1; }
int32_t gc_engine_start_calibration(void) { return Get() ? Get()->StartCalibration() : -1; }
int32_t gc_engine_start_guide(void) { return Get() ? Get()->StartGuide() : -1; }
int32_t gc_engine_stop(void) { return Get() ? Get()->Stop() : -1; }
int32_t gc_engine_clear_calibration(void) { return Get() ? Get()->ClearCalibration() : -1; }

int32_t gc_engine_set_params(const char *json) { return Get() ? Get()->SetParams(json) : -1; }
int32_t gc_engine_set_tracking(int32_t on) { return Get() ? Get()->SetTracking(on) : -1; }
int32_t gc_engine_set_slew_rate(int32_t rate) { return Get() ? Get()->SetSlewRate(rate) : -1; }
int32_t gc_engine_manual_move(int32_t axis, int32_t dir, int32_t on)
{
    return Get() ? Get()->ManualMove(axis, dir, on) : -1;
}
int32_t gc_engine_home(void) { return Get() ? Get()->Home() : -1; }

int32_t gc_engine_get_stats(GcEngineStats *out) { return Get() ? Get()->GetStats(out) : -1; }

int32_t gc_engine_preview_size(int32_t *w, int32_t *h)
{
    return Get() ? Get()->PreviewSize(w, h) : -1;
}

int32_t gc_engine_get_preview(uint8_t *out, int32_t cap)
{
    return Get() ? Get()->GetPreview(out, cap) : 0;
}

int32_t gc_engine_get_status(char *out, int32_t cap)
{
    return Get() ? Get()->GetStatus(out, cap) : 0;
}

} // extern "C"
