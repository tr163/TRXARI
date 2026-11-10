// Regression tests for the ported PHD2 star finder.
#include "guidecore/star.h"
#include "guidecore/us_image.h"

#include <cmath>
#include <cstdio>
#include <random>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        ++g_checks;                                                                                                    \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            ++g_failures;                                                                                              \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                \
        }                                                                                                              \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        ++g_checks;                                                                                                    \
        const double va = (a), vb = (b);                                                                               \
        if (std::fabs(va - vb) > (eps))                                                                                \
        {                                                                                                              \
            ++g_failures;                                                                                              \
            std::printf("FAIL %s:%d: %s = %.12g, expected %.12g\n", __FILE__, __LINE__, #a, va, vb);                  \
        }                                                                                                              \
    } while (0)

static void AddGaussian(gc::usImage& img, double cx, double cy, double sigma, double amp)
{
    const double s2 = 2.0 * sigma * sigma;
    for (int y = 0; y < img.Size.GetHeight(); y++)
    {
        for (int x = 0; x < img.Size.GetWidth(); x++)
        {
            const double dx = x - cx, dy = y - cy;
            const double v = img.Pixel(x, y) + amp * std::exp(-(dx * dx + dy * dy) / s2);
            img.Pixel(x, y) = (unsigned short) std::min(65535.0, v);
        }
    }
}

static void MakeStarField(gc::usImage& img)
{
    img.Init(200, 150);
    img.BitsPerPixel = 16;
    img.Pedestal = 0;

    std::mt19937 rng(7);
    std::normal_distribution<double> noise(0.0, 5.0);
    for (int y = 0; y < img.Size.GetHeight(); y++)
        for (int x = 0; x < img.Size.GetWidth(); x++)
            img.Pixel(x, y) = (unsigned short) std::max(0.0, 800.0 + noise(rng));

    AddGaussian(img, 100.0, 75.0, 1.5, 20000.0);
}

static void testMedian3()
{
    gc::usImage img;
    img.Init(5, 5);
    img.Clear();
    img.Pixel(2, 2) = 60000; // hot pixel
    img.Pixel(1, 2) = 100;
    img.Pixel(3, 2) = 110;

    CHECK(!gc::Median3(img));
    CHECK(img.Pixel(2, 2) < 200);
}

static void testFindCentroid()
{
    gc::usImage img;
    MakeStarField(img);
    gc::Star star;
    const bool found = star.Find(&img, 15, 100, 75, gc::Star::FIND_CENTROID, 1.5, 20.0, 0);
    CHECK(found);
    CHECK(star.GetError() == gc::Star::STAR_OK);
    CHECK_NEAR(star.X, 100.0, 0.1);
    CHECK_NEAR(star.Y, 75.0, 0.1);
    CHECK(star.Mass > 10000.0);
    CHECK(star.SNR > 6.0);
    // Gaussian HFD = 2 * HFR = 2 * 1.1774 * sigma
    CHECK_NEAR(star.HFD, 2.3548 * 1.5, 0.3);
}

static void testSaturation()
{
    gc::usImage img;
    MakeStarField(img);
    gc::Star star;
    const bool found = star.Find(&img, 15, 100, 75, gc::Star::FIND_CENTROID, 1.5, 20.0, 10000);
    CHECK(found);
    CHECK(star.GetError() == gc::Star::STAR_SATURATED);
}

static void testAutoFind()
{
    gc::usImage img;
    MakeStarField(img);
    gc::AutoFindParams p;
    std::vector<gc::GuideStar> stars;

    CHECK(gc::GuideStar().AutoFind(img, p, stars));
    CHECK(stars.size() == 1);
    if (stars.size() == 1)
    {
        CHECK_NEAR(stars[0].X, 100.0, 0.1);
        CHECK_NEAR(stars[0].Y, 75.0, 0.1);
        CHECK(stars[0].HFD > 2.5 && stars[0].HFD < 5.0);
        CHECK(stars[0].SNR >= 6.0);
    }
}

static void testAutoFindMulti()
{
    gc::usImage img;
    img.Init(240, 160);
    img.BitsPerPixel = 16;
    for (int y = 0; y < img.Size.GetHeight(); y++)
        for (int x = 0; x < img.Size.GetWidth(); x++)
            img.Pixel(x, y) = 800;

    AddGaussian(img, 60.0, 80.0, 1.4, 30000.0);
    AddGaussian(img, 130.0, 80.0, 1.6, 20000.0);
    AddGaussian(img, 190.0, 80.0, 1.5, 15000.0);

    gc::AutoFindParams p;
    p.maxStars = 3;
    std::vector<gc::GuideStar> stars;
    CHECK(gc::GuideStar().AutoFind(img, p, stars));
    CHECK(stars.size() == 3);
    if (stars.size() >= 1)
        CHECK_NEAR(stars[0].X, 60.0, 0.2); // brightest becomes primary
}

int main()
{
    testMedian3();
    testFindCentroid();
    testSaturation();
    testAutoFind();
    testAutoFindMulti();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
