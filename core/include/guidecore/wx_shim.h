// Minimal stand-ins for the wxWidgets types used by PHD2's star finder.
// Geometry semantics match wxWidgets (GetRight/Bottom are inclusive).
#pragma once

namespace gc {

struct Point
{
    int x = 0;
    int y = 0;
    Point() = default;
    Point(int x_, int y_) : x(x_), y(y_) { }
};

struct Size
{
    int x = 0;
    int y = 0;
    Size() = default;
    Size(int w, int h) : x(w), y(h) { }
    int GetWidth() const { return x; }
    int GetHeight() const { return y; }
};

struct Rect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    Rect() = default;
    Rect(int x_, int y_, int w_, int h_) : x(x_), y(y_), width(w_), height(h_) { }
    explicit Rect(const Size& s) : x(0), y(0), width(s.x), height(s.y) { }

    bool IsEmpty() const { return width <= 0 || height <= 0; }
    int GetLeft() const { return x; }
    int GetTop() const { return y; }
    int GetRight() const { return x + width - 1; }
    int GetBottom() const { return y + height - 1; }
    int GetX() const { return x; }
    int GetY() const { return y; }
    int GetWidth() const { return width; }
    int GetHeight() const { return height; }
    Size GetSize() const { return Size(width, height); }

    void Intersect(const Rect& other)
    {
        const int l = x > other.x ? x : other.x;
        const int t = y > other.y ? y : other.y;
        const int r = GetRight() < other.GetRight() ? GetRight() : other.GetRight();
        const int b = GetBottom() < other.GetBottom() ? GetBottom() : other.GetBottom();
        if (r < l || b < t)
        {
            width = height = 0;
            return;
        }
        x = l;
        y = t;
        width = r - l + 1;
        height = b - t + 1;
    }
};

template <typename T>
constexpr T wxMax(T a, T b)
{
    return a < b ? b : a;
}

template <typename T>
constexpr T wxMin(T a, T b)
{
    return b < a ? b : a;
}

} // namespace gc
