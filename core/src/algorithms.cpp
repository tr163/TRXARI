// Ported from PHD2 v2.6.14:
//   guide_algorithm_{identity,hysteresis,resistswitch,lowpass,lowpass2,zfilter}.cpp
//   zfilterfactory.cpp (A.J. Fisher mkfilter, as used by PHD2)
// BSD-3-Clause. UI/config code removed.
#include "guidecore/algorithms.h"
#include "guidecore/axis_stats.h"

#include <cmath>
#include <complex>
#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace gc {

namespace {

int sign(double x)
{
    return x > 0.0 ? 1 : (x < 0.0 ? -1 : 0);
}

// ---------------------------------------------------------------- Identity
class Identity final : public GuideAlgorithm
{
public:
    double result(double input) override { return input; }
    void reset() override { }
};

// -------------------------------------------------------------- Hysteresis
class Hysteresis final : public GuideAlgorithm
{
public:
    explicit Hysteresis(const AlgoParams& p)
        : minMove_(p.minMove)
        , hysteresis_(p.hysteresis)
        , aggression_(p.aggression)
    {
    }

    void reset() override { lastMove_ = 0; }

    double result(double input) override
    {
        double dReturn = (1.0 - hysteresis_) * input + hysteresis_ * lastMove_;
        dReturn *= aggression_;
        if (std::fabs(input) < minMove_)
            dReturn = 0.0;
        lastMove_ = dReturn;
        return dReturn;
    }

private:
    double minMove_;
    double hysteresis_;
    double aggression_;
    double lastMove_ = 0;
};

// ------------------------------------------------------------ ResistSwitch
class ResistSwitch final : public GuideAlgorithm
{
public:
    explicit ResistSwitch(const AlgoParams& p)
        : minMove_(p.minMove)
        , aggression_(p.aggression)
        , fastSwitch_(p.fastSwitch)
    {
        reset();
    }

    void reset() override
    {
        history_assign();
        currentSide_ = 0;
    }

    double result(double input) override
    {
        double rslt = input;

        history_.push_back(input);
        history_.erase(history_.begin());

        bool veto = false;
        if (std::fabs(input) < minMove_)
            veto = true;

        if (!veto && fastSwitch_)
        {
            const double thresh = 3.0 * minMove_;
            if (sign(input) != currentSide_ && std::fabs(input) > thresh)
            {
                currentSide_ = 0;
                unsigned int i = 0;
                for (; i < kHistorySize - 3; i++)
                    history_[i] = 0.0;
                for (; i < kHistorySize; i++)
                    history_[i] = input;
            }
        }

        int decHistory = 0;
        if (!veto)
        {
            for (double h : history_)
                if (std::fabs(h) > minMove_)
                    decHistory += sign(h);

            if (currentSide_ == 0 || sign(currentSide_) == -sign(decHistory))
            {
                if (std::abs(decHistory) < 3)
                {
                    veto = true;
                }
                else
                {
                    double oldest = 0.0;
                    double newest = 0.0;
                    for (int i = 0; i < 3; i++)
                    {
                        oldest += history_[i];
                        newest += history_[kHistorySize - (i + 1)];
                    }
                    if (std::fabs(newest) <= std::fabs(oldest))
                        veto = true;
                    else
                        currentSide_ = sign(decHistory);
                }
            }

            if (!veto && currentSide_ != sign(input))
                veto = true;
        }

        if (veto)
            rslt = 0.0;

        return rslt * aggression_;
    }

private:
    static constexpr unsigned int kHistorySize = 10;
    double minMove_;
    double aggression_;
    bool fastSwitch_;
    std::vector<double> history_;
    int currentSide_ = 0;

    void history_assign() { history_.assign(kHistorySize, 0.0); }
};

// ----------------------------------------------------------------- Lowpass
class Lowpass final : public GuideAlgorithm
{
public:
    explicit Lowpass(const AlgoParams& p)
        : minMove_(p.minMove)
        , slopeWeight_(p.slopeWeight)
        , stats_(0) // self-managed window, exactly like PHD2
    {
        reset();
    }

    void reset() override
    {
        stats_.clear();
        timeBase_ = 0;
        while (stats_.count() < kHistorySize)
            stats_.add(timeBase_++, 0);
    }

    double result(double input) override
    {
        stats_.add(timeBase_++, input);
        const double median = stats_.median();
        stats_.removeOldest();
        double dReturn = median + slopeWeight_ * stats_.slope();

        if (std::fabs(dReturn) > std::fabs(input))
            dReturn = input;
        if (std::fabs(input) < minMove_)
            dReturn = 0.0;
        return dReturn;
    }

private:
    static constexpr int kHistorySize = 10;
    double minMove_;
    double slopeWeight_;
    AxisStats stats_;
    int timeBase_ = 0;
};

// ---------------------------------------------------------------- Lowpass2
class Lowpass2 final : public GuideAlgorithm
{
public:
    explicit Lowpass2(const AlgoParams& p)
        : minMove_(p.minMove)
        , aggressiveness_(p.aggressiveness)
        , stats_(kHistorySize) // auto-windowed
    {
        reset();
    }

    void reset() override
    {
        stats_.clear();
        timeBase_ = 0;
        rejects_ = 0;
    }

    double result(double input) override
    {
        stats_.add(timeBase_++, input);
        unsigned int numpts = stats_.count();
        const double attenuation = aggressiveness_ / 100.0;
        double dReturn = 0;

        if (numpts < 4)
        {
            dReturn = input * attenuation;
        }
        else if (std::fabs(input) > 4.0 * minMove_)
        {
            dReturn = input * attenuation;
            reset();
            numpts = 0;
        }
        else
        {
            dReturn = stats_.slope() * static_cast<double>(numpts) * attenuation;
            if (input * dReturn < 0)
                dReturn = 0;
        }

        if (std::fabs(dReturn) > std::fabs(input))
        {
            dReturn = input * attenuation;
            if (++rejects_ > 3)
                reset();
        }
        else
        {
            rejects_ = 0;
        }

        if (std::fabs(input) < minMove_)
            dReturn = 0.0;
        return dReturn;
    }

private:
    static constexpr int kHistorySize = 10;
    double minMove_;
    double aggressiveness_;
    AxisStats stats_;
    int timeBase_ = 0;
    int rejects_ = 0;
};

// ---------------------------------------------------------------- ZFilter
// Port of zfilterfactory.cpp (mkfilter by A.J. Fisher), Bessel/Butterworth only.
class ZFilterFactory
{
public:
    std::vector<double> xcoeffs, ycoeffs;

    ZFilterFactory(bool bessel, int order, double corner)
        : bessel_(bessel)
        , order_(order)
    {
        if (order <= 0)
            throw std::invalid_argument("invalid filter order");
        if (corner < 2.0)
            throw std::invalid_argument("invalid corner period multiplier");

        rawAlpha1_ = rawAlpha2_ = 1.0 / corner;
        splane();
        prewarp();
        normalize();
        zplane();
        expandpoly();
    }

    double gain() const { return std::hypot(dcGain_.imag(), dcGain_.real()); }

private:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kTwoPi = 2.0 * kPi;
    static constexpr double kEps = 1e-10;

    bool bessel_;
    int order_;
    double rawAlpha1_ = 0, rawAlpha2_ = 0;
    double warpedAlpha1_ = 0;
    std::complex<double> dcGain_;
    std::vector<std::complex<double>> spoles_, szeros_, zpoles_, zzeros_;

    static const std::vector<std::complex<double>>& besselPoles()
    {
        // First 12 entries of Fisher's table (one of each conjugate pair).
        static const std::vector<std::complex<double>> table = {
            {-1.00000000000e+00, 0.00000000000e+00},
            {-1.10160133059e+00, 6.36009824757e-01},
            {-1.32267579991e+00, 0.00000000000e+00},
            {-1.04740916101e+00, 9.99264436281e-01},
            {-1.37006783055e+00, 4.10249717494e-01},
            {-9.95208764350e-01, 1.25710573945e+00},
            {-1.50231627145e+00, 0.00000000000e+00},
            {-1.38087732586e+00, 7.17909587627e-01},
            {-9.57676548563e-01, 1.47112432073e+00},
            {-1.57149040362e+00, 3.20896374221e-01},
            {-1.38185809760e+00, 9.71471890712e-01},
            {-9.30656522947e-01, 1.66186326894e+00},
        };
        return table;
    }

    void setpole(const std::complex<double>& z)
    {
        if (z.real() < 0.0)
            spoles_.push_back(z);
    }

    void splane()
    {
        if (bessel_)
        {
            const auto& t = besselPoles();
            int p = (order_ * order_) / 4;
            if (order_ & 1)
                setpole(t[p++]);
            for (int i = 0; i < order_ / 2; i++)
            {
                setpole(t[p]);
                setpole(std::conj(t[p]));
                p++;
            }
        }
        else
        {
            for (int i = 0; i < 2 * order_; i++)
            {
                const double theta =
                    (order_ & 1) ? (i * kPi) / order_ : ((i + 0.5) * kPi) / order_;
                setpole(std::polar(1.0, theta));
            }
        }
    }

    void prewarp()
    {
        // PHD2 uses matched z-transform for Bessel (isMzt), so no prewarp there.
        warpedAlpha1_ = bessel_ ? rawAlpha1_ : std::tan(kPi * rawAlpha1_) / kPi;
    }

    void normalize()
    {
        const double w1 = kTwoPi * warpedAlpha1_;
        for (auto& p : spoles_)
            p *= w1;
        szeros_.clear();
    }

    void zplane()
    {
        zpoles_.clear();
        zzeros_.clear();
        if (!bessel_)
        {
            for (const auto& p : spoles_)
                zpoles_.push_back((2.0 + p) / (2.0 - p));
            for (const auto& z : szeros_)
                zzeros_.push_back((2.0 + z) / (2.0 - z));
            while (zzeros_.size() < zpoles_.size())
                zzeros_.push_back(-1.0);
        }
        else
        {
            for (const auto& p : spoles_)
                zpoles_.push_back(std::exp(p));
            for (const auto& z : szeros_)
                zzeros_.push_back(std::exp(z));
        }
    }

    static void multin(const std::complex<double>& w, std::vector<std::complex<double>>& coeffs)
    {
        const std::complex<double> nw = -w;
        for (int i = static_cast<int>(coeffs.size()) - 1; i >= 1; i--)
            coeffs[i] = (nw * coeffs[i]) + coeffs[i - 1];
        coeffs[0] = nw * coeffs[0];
    }

    static void expand(const std::vector<std::complex<double>>& pz,
                       std::vector<std::complex<double>>& coeffs)
    {
        coeffs.clear();
        coeffs.push_back(1.0);
        for (size_t i = 0; i < pz.size(); i++)
            coeffs.push_back(0.0);
        for (const auto& p : pz)
            multin(p, coeffs);
        for (const auto& c : coeffs)
            if (std::fabs(c.imag()) > kEps)
                throw std::runtime_error("filter coefficients are not real");
    }

    static std::complex<double> eval(const std::vector<std::complex<double>>& coeffs,
                                     const std::complex<double>& z)
    {
        std::complex<double> sum(0.0, 0.0);
        for (int i = static_cast<int>(coeffs.size()) - 1; i >= 0; i--)
            sum = (sum * z) + coeffs[i];
        return sum;
    }

    void expandpoly()
    {
        std::vector<std::complex<double>> top, bot;
        expand(zzeros_, top);
        expand(zpoles_, bot);

        const std::complex<double> zOne(1.0, 0.0);
        dcGain_ = eval(top, zOne) / eval(bot, zOne);

        xcoeffs.clear();
        ycoeffs.clear();
        for (int i = static_cast<int>(top.size()) - 1; i >= 0; i--)
            xcoeffs.push_back(top[i].real() / bot.back().real());
        for (int i = static_cast<int>(bot.size()) - 1; i >= 0; i--)
            ycoeffs.push_back(-(bot[i].real() / bot.back().real()));
    }
};

class ZFilter final : public GuideAlgorithm
{
public:
    explicit ZFilter(const AlgoParams& p)
        : minMove_(p.minMove)
        , expFactor_(p.expFactor)
    {
        buildFilter();
        reset();
    }

    void reset() override
    {
        xv_.assign(xcoeff_.size(), 0.0);
        yv_.assign(ycoeff_.size(), 0.0);
        sumCorr_ = 0.0;
    }

    double result(double input) override
    {
        // Uncorrected waveform = star deflection + corrections already issued.
        xv_.insert(xv_.begin(), (input + sumCorr_) / gain_);
        xv_.pop_back();
        yv_.insert(yv_.begin(), 0.0);
        yv_.pop_back();

        for (size_t i = 0; i < xcoeff_.size(); i++)
            yv_[0] += xv_[i] * xcoeff_[i];
        for (size_t i = 1; i < ycoeff_.size(); i++)
            yv_[0] += yv_[i] * ycoeff_[i];

        double dReturn = yv_[0] - sumCorr_;
        if (std::fabs(dReturn) < minMove_)
            dReturn = 0.0;
        sumCorr_ += dReturn;
        return dReturn;
    }

private:
    double minMove_;
    double expFactor_;
    double gain_ = 1.0;
    std::vector<double> xcoeff_, ycoeff_;
    std::vector<double> xv_, yv_;
    double sumCorr_ = 0.0;

    void buildFilter()
    {
        if (expFactor_ < 1.0)
            throw std::invalid_argument("invalid expFactor");
        const double corner = expFactor_ * 4.0;
        const bool bessel = corner >= 6.0;
        ZFilterFactory f(bessel, 4, corner);
        gain_ = f.gain();
        xcoeff_ = f.xcoeffs;
        ycoeff_ = f.ycoeffs;
    }
};

} // namespace

AlgoParams defaults(AlgoType type)
{
    AlgoParams p;
    switch (type)
    {
    case AlgoType::ResistSwitch:
        p.aggression = 1.0;
        break;
    case AlgoType::ZFilter:
        p.minMove = 0.1;
        break;
    default:
        break;
    }
    return p;
}

std::unique_ptr<GuideAlgorithm> makeAlgorithm(AlgoType type, const AlgoParams& params)
{
    switch (type)
    {
    case AlgoType::Identity:
        return std::make_unique<Identity>();
    case AlgoType::Hysteresis:
        return std::make_unique<Hysteresis>(params);
    case AlgoType::ResistSwitch:
        return std::make_unique<ResistSwitch>(params);
    case AlgoType::Lowpass:
        return std::make_unique<Lowpass>(params);
    case AlgoType::Lowpass2:
        return std::make_unique<Lowpass2>(params);
    case AlgoType::ZFilter:
        return std::make_unique<ZFilter>(params);
    }
    throw std::invalid_argument("unknown algorithm");
}

} // namespace gc
