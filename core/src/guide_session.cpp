// Ported from PHD2 v2.6.14 guider_multistar.cpp UpdateCurrentPosition()
// (BSD-3-Clause). Multi-star refinement, mass-change checks and the frame
// drop bookkeeping are left out; the single-star math is kept verbatim.
#include "guidecore/guide_session.h"

namespace gc {

GuideSessionParams::GuideSessionParams()
{
    raParams = defaults(raAlgo);
    decParams = defaults(decAlgo);
}

GuideSession::GuideSession(const GuideSessionParams& params)
    : m_params(params)
{
    m_ra = makeAlgorithm(params.raAlgo, params.raParams);
    m_dec = makeAlgorithm(params.decAlgo, params.decParams);
}

void GuideSession::reset()
{
    m_primary.Invalidate();
    m_lock.Invalidate();
    m_acquired = false;
    m_frame = 0;
    m_ra->reset();
    m_dec->reset();
}

bool GuideSession::acquire(const usImage& image)
{
    std::vector<GuideStar> stars;
    reset();

    // PHD2 STATE_SELECTING: AutoFind picks the star; the caller's
    // maxStars decides whether the list is single- or multi-star.
    if (!GuideStar().AutoFind(image, m_params.autoFind, stars) || stars.empty())
        return false;

    m_primary = stars[0];
    m_lock.SetXY(stars[0].X, stars[0].Y);
    m_acquired = true;
    return true;
}

bool GuideSession::processFrame(const usImage& image, GuideStep& step)
{
    step = GuideStep();
    step.frame = ++m_frame;

    if (!m_acquired)
    {
        step.error = Star::STAR_ERROR;
        return false;
    }

    const AutoFindParams& p = m_params.autoFind;
    const unsigned short maxADU = p.saturationByADU ? p.saturationADU : 0;

    // PHD2: Star newStar(m_primaryStar); newStar.Find(pImage, searchRegion, ...)
    Star newStar(m_primary);
    const bool found = newStar.Find(&image, p.searchRegion, (int) m_primary.X, (int) m_primary.Y,
                                    m_params.findMode, p.minHFD, p.maxHFD, maxADU);

    step.found = found;
    step.error = newStar.GetError();
    step.x = newStar.X;
    step.y = newStar.Y;
    step.mass = newStar.Mass;
    step.snr = newStar.SNR;
    step.hfd = newStar.HFD;

    if (!found)
    {
        // PHD2 keeps the previous position so the next frame searches there.
        m_primary.SetError(newStar.GetError());
        return false;
    }

    m_primary = newStar;

    // PHD2: ofs->cameraOfs = m_primaryStar - lockPos;
    const PHD_Point offset = m_primary - m_lock;
    step.raOffsetPx = offset.X;
    step.decOffsetPx = m_params.raOnly ? 0.0 : offset.Y;

    // PHD2 Mount::MoveOffset: TransformCameraCoordinatesToMountCoordinates()
    // before the guide algorithms see the distance.
    PHD_Point mountOffset;
    const PHD_Point cameraOffset(step.raOffsetPx, step.decOffsetPx);
    m_params.mount.cameraToMount(cameraOffset, mountOffset);
    step.mountRaPx = mountOffset.X;
    step.mountDecPx = m_params.raOnly ? 0.0 : mountOffset.Y;

    step.raCorrectionPx = m_ra->result(step.mountRaPx);
    step.decCorrectionPx = m_params.raOnly ? 0.0 : m_dec->result(step.mountDecPx);

    if (m_params.mount.isCalibrated())
    {
        auto signedPulse = [](int ms, double distance) { return distance >= 0.0 ? ms : -ms; };
        step.raPulseMs = signedPulse(m_params.mount.pulseMs(step.raCorrectionPx, true), step.raCorrectionPx);
        step.decPulseMs = signedPulse(m_params.mount.pulseMs(step.decCorrectionPx, false), step.decCorrectionPx);
    }
    return true;
}

} // namespace gc
