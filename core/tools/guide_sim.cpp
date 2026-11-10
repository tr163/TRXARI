// Host-side acceptance harness for the GuideCore algorithms.
// Simulates a guide star with periodic error + slow drift + seeing noise,
// runs each algorithm in a closed loop with a 1-frame mount response delay,
// and prints the traces as JSON (consumed by tools/make_report.py).
#include "guidecore/algorithms.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr int kFrames = 600;
constexpr int kSettleFrames = 30;

struct RunResult
{
    std::vector<double> corr, residual;
    double rms = 0, peak = 0;
};

RunResult runClosedLoop(gc::AlgoType type, const std::vector<double>& raw, bool decAxis)
{
    gc::AlgoParams p = gc::defaults(type);
    auto algo = gc::makeAlgorithm(type, p);

    RunResult r;
    r.corr.resize(raw.size());
    r.residual.resize(raw.size());

    double applied = 0.0;
    for (size_t i = 0; i < raw.size(); i++)
    {
        const double measured = raw[i] - applied;
        const double c = algo->result(measured);
        r.corr[i] = c;
        r.residual[i] = measured;
        applied += c;
    }

    double sumSq = 0;
    int n = 0;
    for (size_t i = kSettleFrames; i < r.residual.size(); i++)
    {
        sumSq += r.residual[i] * r.residual[i];
        r.peak = std::fmax(r.peak, std::fabs(r.residual[i]));
        n++;
    }
    r.rms = std::sqrt(sumSq / n);
    (void) decAxis;
    return r;
}

void printArray(const std::vector<double>& v)
{
    std::printf("[");
    for (size_t i = 0; i < v.size(); i++)
        std::printf("%s%.4f", i ? "," : "", v[i]);
    std::printf("]");
}

} // namespace

int main()
{
    std::mt19937 rng(42);
    std::normal_distribution<double> seeing(0.0, 0.12);

    std::vector<double> rawRA(kFrames), rawDec(kFrames);
    for (int i = 0; i < kFrames; i++)
    {
        // ~1.2 px periodic error (150-frame period) + slow drift + seeing
        rawRA[i] = 1.2 * std::sin(2.0 * 3.14159265358979323846 * i / 150.0) + 0.008 * i + seeing(rng);
        rawDec[i] = 0.004 * i + seeing(rng);
    }

    const gc::AlgoType types[] = {gc::AlgoType::Identity, gc::AlgoType::Hysteresis,
                                  gc::AlgoType::ResistSwitch, gc::AlgoType::Lowpass,
                                  gc::AlgoType::Lowpass2, gc::AlgoType::ZFilter};
    const char *names[] = {"Identity", "Hysteresis", "ResistSwitch", "Lowpass", "Lowpass2", "ZFilter"};

    double baseRA = 0, baseDec = 0;
    for (int i = kSettleFrames; i < kFrames; i++)
    {
        baseRA += rawRA[i] * rawRA[i];
        baseDec += rawDec[i] * rawDec[i];
    }
    baseRA = std::sqrt(baseRA / (kFrames - kSettleFrames));
    baseDec = std::sqrt(baseDec / (kFrames - kSettleFrames));

    std::printf("{\"frames\":%d,\"settle\":%d,", kFrames, kSettleFrames);
    std::printf("\"raw\":{\"ra\":");
    printArray(rawRA);
    std::printf(",\"dec\":");
    printArray(rawDec);
    std::printf("},\"baseline\":{\"rmsRA\":%.4f,\"rmsDec\":%.4f},", baseRA, baseDec);
    std::printf("\"algorithms\":[");

    for (size_t a = 0; a < sizeof(types) / sizeof(types[0]); a++)
    {
        RunResult ra = runClosedLoop(types[a], rawRA, false);
        RunResult dec = runClosedLoop(types[a], rawDec, true);

        std::printf("%s{\"name\":\"%s\",\"rmsRA\":%.4f,\"peakRA\":%.4f,", a ? "," : "", names[a], ra.rms, ra.peak);
        std::printf("\"rmsDec\":%.4f,\"peakDec\":%.4f,", dec.rms, dec.peak);
        std::printf("\"corrRA\":");
        printArray(ra.corr);
        std::printf(",\"resRA\":");
        printArray(ra.residual);
        std::printf(",\"corrDec\":");
        printArray(dec.corr);
        std::printf(",\"resDec\":");
        printArray(dec.residual);
        std::printf("}");
    }

    std::printf("]}\n");
    return 0;
}
