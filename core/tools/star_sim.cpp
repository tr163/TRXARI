// Acceptance harness for the ported PHD2 star finder.
// Renders a synthetic star field (Gaussian PSFs + noise + hot pixels), runs
// GuideStar::AutoFind exactly as ported, and prints image + detections as JSON.
#include "guidecore/star.h"
#include "guidecore/us_image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace {

constexpr int W = 640;
constexpr int H = 480;
constexpr int MARGIN = 40;
constexpr int CROP = 61;

struct Truth
{
    double x, y, sigma, amp;
};

struct Detection
{
    double x, y, mass, snr, hfd;
    unsigned short peak;
    int truth;
    double dx, dy, err;
};

void renderStar(gc::usImage& img, double cx, double cy, double sigma, double amp)
{
    const double s2 = 2.0 * sigma * sigma;
    const int x0 = std::max(0, (int) std::floor(cx - 5 * sigma));
    const int x1 = std::min(W - 1, (int) std::ceil(cx + 5 * sigma));
    const int y0 = std::max(0, (int) std::floor(cy - 5 * sigma));
    const int y1 = std::min(H - 1, (int) std::ceil(cy + 5 * sigma));
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
        {
            const double dx = x - cx, dy = y - cy;
            const double v = img.Pixel(x, y) + amp * std::exp(-(dx * dx + dy * dy) / s2);
            img.Pixel(x, y) = (unsigned short) std::min(65535.0, v);
        }
}

void printInts(const std::vector<int>& v)
{
    std::printf("[");
    for (size_t i = 0; i < v.size(); i++)
        std::printf("%s%d", i ? "," : "", v[i]);
    std::printf("]");
}

} // namespace

int main()
{
    gc::usImage img;
    img.Init(W, H);
    img.BitsPerPixel = 16;
    img.Pedestal = 0;

    std::mt19937 rng(20261111);
    std::normal_distribution<double> noise(0.0, 8.0);
    std::uniform_real_distribution<double> u01(0.0, 1.0);

    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            img.Pixel(x, y) = (unsigned short) std::max(0.0, 800.0 + noise(rng));

    // 24 stars, at least 32 px apart, brightness/size varied.
    std::vector<Truth> truth;
    for (int attempt = 0; attempt < 5000 && (int) truth.size() < 24; attempt++)
    {
        const double x = MARGIN + u01(rng) * (W - 2 * MARGIN);
        const double y = MARGIN + u01(rng) * (H - 2 * MARGIN);
        bool tooClose = false;
        for (const auto& t : truth)
            if (std::hypot(t.x - x, t.y - y) < 32.0)
            {
                tooClose = true;
                break;
            }
        if (tooClose)
            continue;

        const double sigma = 1.2 + u01(rng) * 1.0;
        const double amp = 4000.0 + u01(rng) * 41000.0;
        truth.push_back({ x, y, sigma, amp });
        renderStar(img, x, y, sigma, amp);
    }

    // hot pixels, which AutoFind's 3x3 median is supposed to remove.
    // keep them away from real stars so PeakVal stays meaningful.
    const int hx[] = { 37, 500, 300, 580, 90 };
    const int hy[] = { 41, 100, 400, 430, 380 };
    int placed = 0;
    for (int i = 0; i < 5 && placed < 3; i++)
    {
        bool clear = true;
        for (const auto& t : truth)
            if (std::hypot(t.x - hx[i], t.y - hy[i]) < 30.0)
            {
                clear = false;
                break;
            }
        if (clear)
        {
            img.Pixel(hx[i], hy[i]) = 60000;
            placed++;
        }
    }

    gc::AutoFindParams params;
    params.maxStars = 9;
    params.downsample = 1;
    params.minHFD = 1.5;
    params.maxHFD = 20.0;
    params.minSNR = 6.0;
    params.searchRegion = 15;
    params.extraEdgeAllowance = 10;

    std::vector<gc::GuideStar> found;
    const bool ok = gc::GuideStar().AutoFind(img, params, found);

    std::vector<Detection> det;
    double sumErr = 0, maxErr = 0;
    for (const auto& s : found)
    {
        int best = -1;
        double bestD = 1e9;
        for (size_t i = 0; i < truth.size(); i++)
        {
            const double d = std::hypot(truth[i].x - s.X, truth[i].y - s.Y);
            if (d < bestD)
            {
                bestD = d;
                best = (int) i;
            }
        }
        Detection d;
        d.x = s.X;
        d.y = s.Y;
        d.mass = s.Mass;
        d.snr = s.SNR;
        d.hfd = s.HFD;
        d.peak = s.PeakVal;
        d.truth = best;
        d.dx = best >= 0 ? s.X - truth[best].x : 0;
        d.dy = best >= 0 ? s.Y - truth[best].y : 0;
        d.err = best >= 0 ? bestD : 0;
        sumErr += d.err;
        maxErr = std::max(maxErr, d.err);
        det.push_back(d);
    }

    // 8-bit gamma preview (2x2 max-pool) for the browser
    unsigned short maxVal = 0;
    for (unsigned int i = 0; i < img.NPixels; i++)
        maxVal = std::max(maxVal, img.ImageData[i]);
    const double scale = 255.0 / std::sqrt((double) maxVal);

    std::vector<int> preview;
    preview.reserve((W / 2) * (H / 2));
    for (int y = 0; y < H; y += 2)
        for (int x = 0; x < W; x += 2)
        {
            unsigned short v = 0;
            for (int j = 0; j < 2; j++)
                for (int i = 0; i < 2; i++)
                    v = std::max(v, img.Pixel(x + i, y + j));
            preview.push_back((int) std::lround(scale * std::sqrt((double) v)));
        }

    // zoomed crop around the primary star
    const double pcx = det.empty() ? W / 2.0 : det[0].x;
    const double pcy = det.empty() ? H / 2.0 : det[0].y;
    const int cx0 = std::max(0, std::min(W - CROP, (int) std::lround(pcx) - CROP / 2));
    const int cy0 = std::max(0, std::min(H - CROP, (int) std::lround(pcy) - CROP / 2));
    std::vector<int> crop;
    crop.reserve(CROP * CROP);
    for (int y = 0; y < CROP; y++)
        for (int x = 0; x < CROP; x++)
        {
            const unsigned short v = img.Pixel(cx0 + x, cy0 + y);
            crop.push_back((int) std::lround(scale * std::sqrt((double) v)));
        }

    std::printf("{\"width\":%d,\"height\":%d,\"ok\":%s,", W, H, ok ? "true" : "false");
    std::printf("\"truthCount\":%d,\"detectedCount\":%d,", (int) truth.size(), (int) det.size());
    std::printf("\"meanError\":%.4f,\"maxError\":%.4f,",
                det.empty() ? 0.0 : sumErr / det.size(), maxErr);
    std::printf("\"previewW\":%d,\"previewH\":%d,\"preview\":", W / 2, H / 2);
    printInts(preview);
    std::printf(",\"cropX\":%d,\"cropY\":%d,\"cropSize\":%d,\"crop\":", cx0, cy0, CROP);
    printInts(crop);

    std::printf(",\"truth\":[");
    for (size_t i = 0; i < truth.size(); i++)
        std::printf("%s{\"x\":%.2f,\"y\":%.2f,\"sigma\":%.2f,\"amp\":%.0f}", i ? "," : "", truth[i].x,
                    truth[i].y, truth[i].sigma, truth[i].amp);
    std::printf("],\"detections\":[");
    for (size_t i = 0; i < det.size(); i++)
    {
        const Detection& d = det[i];
        std::printf("%s{\"x\":%.3f,\"y\":%.3f,\"mass\":%.1f,\"snr\":%.1f,\"hfd\":%.2f,\"peak\":%u,"
                    "\"truth\":%d,\"dx\":%.3f,\"dy\":%.3f,\"err\":%.3f}",
                    i ? "," : "", d.x, d.y, d.mass, d.snr, d.hfd, d.peak, d.truth, d.dx, d.dy, d.err);
    }
    std::printf("],\"params\":{\"searchRegion\":%d,\"minHFD\":%.1f,\"maxHFD\":%.1f,\"minSNR\":%.1f,"
                "\"maxStars\":%d,\"downsample\":%d}}\n",
                params.searchRegion, params.minHFD, params.maxHFD, params.minSNR, params.maxStars,
                params.downsample);
    return 0;
}
