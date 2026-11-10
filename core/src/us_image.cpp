// Ported from PHD2 v2.6.14 src/usImage.cpp and src/image_math.cpp (Median3).
// BSD-3-Clause.
#include "guidecore/us_image.h"

#include <cstring>

namespace gc {

bool usImage::Init(const gc::Size& size)
{
    unsigned int prev = NPixels;
    NPixels = size.GetWidth() * size.GetHeight();
    Size = size;
    Subframe = Rect(0, 0, 0, 0);
    MinADU = MaxADU = MedianADU = 0;

    if (NPixels != prev)
    {
        delete[] ImageData;
        if (NPixels)
            ImageData = new unsigned short[NPixels];
        else
            ImageData = nullptr;
    }
    return false;
}

void usImage::SwapImageData(usImage& other)
{
    unsigned short *t = ImageData;
    ImageData = other.ImageData;
    other.ImageData = t;
}

bool usImage::CopyFrom(const usImage& src)
{
    if (Init(src.Size))
        return true;
    std::memcpy(ImageData, src.ImageData, NPixels * sizeof(unsigned short));
    return false;
}

void usImage::Clear()
{
    std::memset(ImageData, 0, NPixels * sizeof(unsigned short));
}

// ------------------------------ median helpers (image_math.cpp, verbatim)
inline static void swap(unsigned short& a, unsigned short& b)
{
    unsigned short const t = a;
    a = b;
    b = t;
}

inline static unsigned short median9(const unsigned short l[9])
{
    unsigned short l0 = l[0], l1 = l[1], l2 = l[2], l3 = l[3], l4 = l[4];
    unsigned short x;

    x = l[5];
    if (x < l0)
        swap(x, l0);
    if (x < l1)
        swap(x, l1);
    if (x < l2)
        swap(x, l2);
    if (x < l3)
        swap(x, l3);
    if (x < l4)
        swap(x, l4);
    x = l[6];
    if (x < l0)
        swap(x, l0);
    if (x < l1)
        swap(x, l1);
    if (x < l2)
        swap(x, l2);
    if (x < l3)
        swap(x, l3);
    if (x < l4)
        swap(x, l4);
    x = l[7];
    if (x < l0)
        swap(x, l0);
    if (x < l1)
        swap(x, l1);
    if (x < l2)
        swap(x, l2);
    if (x < l3)
        swap(x, l3);
    if (x < l4)
        swap(x, l4);
    x = l[8];
    if (x < l0)
        swap(x, l0);
    if (x < l1)
        swap(x, l1);
    if (x < l2)
        swap(x, l2);
    if (x < l3)
        swap(x, l3);
    if (x < l4)
        swap(x, l4);

    if (l1 > l0)
        l0 = l1;
    if (l2 > l0)
        l0 = l2;
    if (l3 > l0)
        l0 = l3;
    if (l4 > l0)
        l0 = l4;

    return l0;
}

inline static unsigned short median6(const unsigned short l[6])
{
    unsigned short l0 = l[0], l1 = l[1], l2 = l[2], l3 = l[3];
    unsigned short x;

    x = l[4];
    if (x < l0)
        swap(x, l0);
    if (x < l1)
        swap(x, l1);
    if (x < l2)
        swap(x, l2);
    if (x < l3)
        swap(x, l3);
    x = l[5];
    if (x < l0)
        swap(x, l0);
    if (x < l1)
        swap(x, l1);
    if (x < l2)
        swap(x, l2);
    if (x < l3)
        swap(x, l3);

    if (l2 > l0)
        swap(l2, l0);
    if (l2 > l1)
        swap(l2, l1);

    if (l3 > l0)
        swap(l3, l0);
    if (l3 > l1)
        swap(l3, l1);

    return (unsigned short) (((unsigned int) l0 + (unsigned int) l1) / 2);
}

inline static unsigned short median4(const unsigned short l[4])
{
    unsigned short l0 = l[0], l1 = l[1], l2 = l[2];
    unsigned short x;
    x = l[3];
    if (x < l0)
        swap(x, l0);
    if (x < l1)
        swap(x, l1);
    if (x < l2)
        swap(x, l2);

    if (l2 > l0)
        swap(l2, l0);
    if (l2 > l1)
        swap(l2, l1);

    return (unsigned short) (((unsigned int) l0 + (unsigned int) l1) / 2);
}

// ------------------------------ Median3 (image_math.cpp, verbatim)
void Median3(unsigned short *dst, const unsigned short *src, const Size& size, const Rect& rect)
{
    int const W = size.GetWidth();
    int const RX = rect.GetX();
    int const RY = rect.GetY();
    int const RW = rect.GetWidth();
    int const RH = rect.GetHeight();

    unsigned short a[9];
    unsigned short *d;

#define IX(x_, y_) ((RY + (y_)) * W + RX + (x_))

    // top row
    d = &dst[IX(0, 0)];

    // top-left corner
    a[0] = src[IX(0, 0)];
    a[1] = src[IX(1, 0)];
    a[2] = src[IX(0, 1)];
    a[3] = src[IX(1, 1)];
    *d++ = median4(a);

    // top row middle pixels
    for (int x = 1; x <= RW - 2; x++)
    {
        a[0] = src[IX(x - 1, 0)];
        a[1] = src[IX(x, 0)];
        a[2] = src[IX(x + 1, 0)];
        a[3] = src[IX(x - 1, 1)];
        a[4] = src[IX(x, 1)];
        a[5] = src[IX(x + 1, 1)];
        *d++ = median6(a);
    }

    // top-right corner
    a[0] = src[IX(RW - 2, 0)];
    a[1] = src[IX(RW - 1, 0)];
    a[2] = src[IX(RW - 2, 1)];
    a[3] = src[IX(RW - 1, 1)];
    *d = median4(a);

    for (int y = 1; y <= RH - 2; y++)
    {
        d = &dst[IX(0, y)];

        // leftmost pixel
        a[0] = src[IX(0, y - 1)];
        a[1] = src[IX(1, y - 1)];
        a[2] = src[IX(0, y)];
        a[3] = src[IX(1, y)];
        a[4] = src[IX(0, y + 1)];
        a[5] = src[IX(1, y + 1)];
        *d++ = median6(a);

        for (int x = 1; x <= RW - 2; x++)
        {
            a[0] = src[IX(x - 1, y - 1)];
            a[1] = src[IX(x, y - 1)];
            a[2] = src[IX(x + 1, y - 1)];
            a[3] = src[IX(x - 1, y)];
            a[4] = src[IX(x, y)];
            a[5] = src[IX(x + 1, y)];
            a[6] = src[IX(x - 1, y + 1)];
            a[7] = src[IX(x, y + 1)];
            a[8] = src[IX(x + 1, y + 1)];
            *d++ = median9(a);
        }

        // rightmost pixel
        a[0] = src[IX(RW - 2, y - 1)];
        a[1] = src[IX(RW - 1, y - 1)];
        a[2] = src[IX(RW - 2, y)];
        a[3] = src[IX(RW - 1, y)];
        a[4] = src[IX(RW - 2, y + 1)];
        a[5] = src[IX(RW - 1, y + 1)];
        *d++ = median6(a);
    }

    // bottom row
    d = &dst[IX(0, RH - 1)];

    // bottom-left corner
    a[0] = src[IX(0, RH - 2)];
    a[1] = src[IX(1, RH - 2)];
    a[2] = src[IX(0, RH - 1)];
    a[3] = src[IX(1, RH - 1)];
    *d++ = median4(a);

    // bottom row middle pixels
    for (int x = 1; x <= RW - 2; x++)
    {
        a[0] = src[IX(x - 1, RH - 2)];
        a[1] = src[IX(x, RH - 2)];
        a[2] = src[IX(x + 1, RH - 2)];
        a[3] = src[IX(x - 1, RH - 1)];
        a[4] = src[IX(x, RH - 1)];
        a[5] = src[IX(x + 1, RH - 1)];
        *d++ = median6(a);
    }

    // bottom-right corner
    a[0] = src[IX(RW - 2, RH - 2)];
    a[1] = src[IX(RW - 1, RH - 2)];
    a[2] = src[IX(RW - 2, RH - 1)];
    a[3] = src[IX(RW - 1, RH - 1)];
    *d = median4(a);

#undef IX
}

bool Median3(usImage& img)
{
    usImage tmp;
    if (tmp.Init(img.Size))
        return true;

    if (img.Subframe.IsEmpty())
        Median3(tmp.ImageData, img.ImageData, img.Size, Rect(img.Size));
    else
    {
        tmp.Clear();
        Median3(tmp.ImageData, img.ImageData, img.Size, img.Subframe);
    }

    img.SwapImageData(tmp);
    return false;
}

} // namespace gc
