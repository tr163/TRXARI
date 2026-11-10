// Frame-by-frame guiding session, mirroring PHD2 v2.6.14
// GuiderMultiStar::UpdateCurrentPosition() (single-star path) from
// guider_multistar.cpp (BSD-3-Clause):
//
//   Star newStar(m_primaryStar);
//   newStar.Find(pImage, searchRegion, findMode, minHFD, maxHFD, saturationADU);
//   ofs->cameraOfs = m_primaryStar - lockPos;
//
// The mount is not part of this layer: the caller applies the returned
// correction (pixels) and tells the session nothing. Calibration (camera ->
// mount coordinates) and pulse conversion stay in the caller, as in PHD2.
#pragma once

#include "guidecore/algorithms.h"
#include "guidecore/star.h"
#include "guidecore/us_image.h"

#include <memory>

namespace gc {

struct GuideSessionParams
{
    AutoFindParams autoFind; // searchRegion/minHFD/maxHFD/saturation also used per frame
    AlgoType raAlgo = AlgoType::Hysteresis;   // PHD2 defaults
    AlgoType decAlgo = AlgoType::ResistSwitch;
    AlgoParams raParams;
    AlgoParams decParams;
    Star::FindMode findMode = Star::FIND_CENTROID;
    bool raOnly = false; // PHD2 MyFrame::GuidingRAOnly()

    GuideSessionParams();
};

struct GuideStep
{
    int frame = 0; // 1-based, counts frames since acquire()
    bool found = false;
    Star::FindResult error = Star::STAR_ERROR;
    double x = 0., y = 0.; // measured star position
    double mass = 0., snr = 0., hfd = 0.;
    double raOffsetPx = 0., decOffsetPx = 0.;      // star - lock position (camera coords)
    double raCorrectionPx = 0., decCorrectionPx = 0.; // guide algorithm output
};

class GuideSession
{
public:
    explicit GuideSession(const GuideSessionParams& params = GuideSessionParams());

    void reset();

    // PHD2 STATE_SELECTING: auto-select the primary star and set the lock
    // position to it. Returns false if no star passed AutoFind.
    bool acquire(const usImage& image);

    // PHD2 STATE_GUIDING: re-find the primary star from its last position,
    // report the offset from the lock position and run both guide algorithms.
    // Returns false (and leaves the previous star position intact) when the
    // star is lost, matching PHD2.
    bool processFrame(const usImage& image, GuideStep& step);

    bool hasLock() const { return m_acquired; }
    const Star& primaryStar() const { return m_primary; }
    const PHD_Point& lockPosition() const { return m_lock; }

private:
    GuideSessionParams m_params;
    std::unique_ptr<GuideAlgorithm> m_ra;
    std::unique_ptr<GuideAlgorithm> m_dec;

    Star m_primary;
    PHD_Point m_lock;
    bool m_acquired = false;
    int m_frame = 0;
};

} // namespace gc
