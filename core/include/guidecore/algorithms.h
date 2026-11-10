// Guide algorithms ported from PHD2 v2.6.14 (BSD-3-Clause).
// Configuration/UI removed: parameters are plain structs.
#pragma once

#include <memory>

namespace gc {

enum class AlgoType
{
    Identity,
    Hysteresis,
    ResistSwitch,
    Lowpass,
    Lowpass2,
    ZFilter,
};

struct AlgoParams
{
    double minMove = 0.2;         // pixels; ZFilter default is 0.1, see defaults()
    double hysteresis = 0.1;      // Hysteresis
    double aggression = 0.7;      // Hysteresis; ResistSwitch default is 1.0
    double slopeWeight = 5.0;     // Lowpass
    double aggressiveness = 80.0; // Lowpass2, percent
    double expFactor = 2.0;       // ZFilter, must be >= 1.0
    bool fastSwitch = true;       // ResistSwitch
};

// PHD2 defaults per algorithm (differ for minMove/aggression).
AlgoParams defaults(AlgoType type);

class GuideAlgorithm
{
public:
    virtual ~GuideAlgorithm() = default;
    virtual double result(double input) = 0;
    virtual void reset() = 0;
};

std::unique_ptr<GuideAlgorithm> makeAlgorithm(AlgoType type, const AlgoParams& params);

} // namespace gc
