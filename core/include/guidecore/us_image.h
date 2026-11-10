// Minimal port of PHD2 v2.6.14 src/usImage.{h,cpp}: the image container used
// by the star finder. FITS/wxImage I/O and stats unrelated to finding are out.
#pragma once

#include "guidecore/wx_shim.h"

namespace gc {

class usImage
{
public:
    unsigned short *ImageData = nullptr;
    gc::Size Size;
    Rect Subframe;
    unsigned int NPixels = 0;
    unsigned short MinADU = 0;
    unsigned short MaxADU = 0;
    unsigned short MedianADU = 0;
    unsigned short FiltMin = 0;
    unsigned short FiltMax = 0;
    int ImgExpDur = 0;
    int ImgStackCnt = 1;
    unsigned char BitsPerPixel = 0;
    unsigned short Pedestal = 0;
    unsigned int FrameNum = 0;

    usImage() = default;
    ~usImage() { delete[] ImageData; }
    usImage(const usImage&) = delete;
    usImage& operator=(const usImage&) = delete;

    bool Init(const gc::Size& size);
    bool Init(int width, int height) { return Init(gc::Size(width, height)); }
    void SwapImageData(usImage& other);
    bool CopyFrom(const usImage& src);
    void Clear();

    unsigned short& Pixel(int x, int y) { return ImageData[y * Size.x + x]; }
    const unsigned short& Pixel(int x, int y) const { return ImageData[y * Size.x + x]; }
};

// 3x3 median; pixels outside rect are left untouched.
void Median3(unsigned short *dst, const unsigned short *src, const Size& size, const Rect& rect);
// Returns true on error (allocation failure), matching PHD2.
bool Median3(usImage& img);

} // namespace gc
