// Ported from PHD2 v2.6.14 src/star.{h,cpp} (BSD-3-Clause).
// AutoFind is detached from PHD2 globals (pFrame/pCamera/pGuider); its
// parameters come from AutoFindParams with the same defaults PHD2 uses.
#pragma once

#include "guidecore/point.h"
#include "guidecore/us_image.h"

#include <vector>

namespace gc {

class Star : public PHD_Point
{
public:
    enum FindMode
    {
        FIND_CENTROID,
        FIND_PEAK,
    };

    enum FindResult
    {
        STAR_OK = 0,
        STAR_SATURATED,
        STAR_LOWSNR,
        STAR_LOWMASS,
        STAR_LOWHFD,
        STAR_HIHFD,
        STAR_TOO_NEAR_EDGE,
        STAR_MASSCHANGE,
        STAR_ERROR,
    };

    double Mass = 0.0;
    double SNR = 0.0;
    double HFD = 0.0;
    unsigned short PeakVal = 0;

    Star();

    // Returns true if the star was found (same inverted convention as PHD2).
    bool Find(const usImage *pImg, int searchRegion, int base_x, int base_y, FindMode mode, double minHFD,
              double maxHFD, unsigned short maxADU);
    bool Find(const usImage *pImg, int searchRegion, FindMode mode, double minHFD, double maxHFD,
              unsigned short maxADU)
    {
        return Find(pImg, searchRegion, (int) X, (int) Y, mode, minHFD, maxHFD, maxADU);
    }

    static bool WasFound(FindResult result);
    bool WasFound() const;
    void Invalidate();
    void SetError(FindResult error);
    FindResult GetError() const { return m_lastFindResult; }

private:
    FindResult m_lastFindResult = STAR_ERROR;
};

struct AutoFindParams
{
    int extraEdgeAllowance = 10;
    int searchRegion = 15; // PHD2 DEFAULT_SEARCH_REGION
    Rect roi;              // empty = full frame
    int maxStars = 1;      // >1 enables PHD2 multi-star selection
    int downsample = 1;    // 0 = auto from pixelScale (PHD2 default)
    double pixelScale = 2.0;
    double minHFD = 1.5;  // PHD2 GetMinStarHFDDefault()
    double maxHFD = 20.0; // PHD2 /guider/StarMaxHFD default
    double minSNR = 6.0;  // PHD2 /guider/StarMinSNR default
    bool saturationByADU = false;
    unsigned short saturationADU = 0;
};

class GuideStar : public Star
{
public:
    PHD_Point referencePoint;
    unsigned int missCount = 0;
    unsigned int zeroCount = 0;
    PHD_Point offsetFromPrimary;
    bool wasLost = false;

    GuideStar()
    {
        referencePoint.SetXY(0., 0.);
        offsetFromPrimary.SetXY(0., 0.);
    }

    explicit GuideStar(const Star& star)
        : Star(star)
    {
        referencePoint.SetXY(star.X, star.Y);
        offsetFromPrimary.SetXY(0., 0.);
    }

    bool AutoFind(const usImage& image, const AutoFindParams& params, std::vector<GuideStar>& foundStars);
};

} // namespace gc
