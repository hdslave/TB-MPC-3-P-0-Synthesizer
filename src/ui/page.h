// Native-pixel drawing and Melodee controls for the instrument page.
#pragma once

#include "font.h"
#include "identity.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace hdui
{
constexpr int width = 320, height = 218;
constexpr uint32_t noParam = 0xffffffffu;
using Ink = uint32_t;
namespace ink
{
constexpr Ink bg = 0xff130c1a, field = 0xff1a1022, card = 0xff28192f, selected = 0xffff78ba;
constexpr Ink rule = 0xff795477, grid = 0xff4a2b46, muted = 0xffc7adc8, soft = 0xffe5d5e6;
constexpr Ink pink = 0xffff479e, orange = 0xffffa52b, mint = 0xff85f6d1, alternate = 0xff241329;
constexpr Ink paper = 0xfffff3da, live = 0xffffee32, cursor = 0xff534022;
}

struct Field { uint32_t id; const char* label; };
struct Group { const char* name; const Field* fields; int count; };

struct Host
{
    uint64_t nowMs = 0;
    Ink accent = 0;     // the track's colour (0: none given)
    int16_t pot[3] = { -1, -1, -1 };
    bool engaged[3] = { true, true, true };
    bool shift = false, playing = false;
};

enum class Key : uint8_t { wheel = 0, push = 1, nudge = 2 }; // as MELODEE_SCREEN_WHEEL, _PUSH, _NUDGE

// What an input did: parameters set (plain values) and buttons pressed.
struct Edits
{
    struct Edit { uint32_t id; float value; bool press; };
    Edit list[4];
    int count = 0;
    void set (uint32_t id, float v) { if (count < 4) list[count++] = { id, v, false }; }
    void press (uint32_t id) { if (count < 4) list[count++] = { id, 0.0f, true }; }
};

struct Raster
{
    Ink* px;
    int w, h;

    void box (int x, int y, int wide, int high, Ink c) const
    {
        const int left = std::max (0, x), right = std::min (w, x + wide);
        if (right <= left) return;
        for (int j = std::max (0, y); j < std::min (h, y + high); ++j)
            std::fill (px + j * w + left, px + j * w + right, c);
    }

    void dot (int x, int y, Ink c) const
    {
        if (x >= 0 && x < w && y >= 0 && y < h)
            px[y * w + x] = c;
    }

    void frame (int x, int y, int wide, int high, Ink c) const
    {
        box (x, y, wide, 1, c);
        box (x, y + high - 1, wide, 1, c);
        box (x, y, 1, high, c);
        box (x + wide - 1, y, 1, high, c);
    }

    // Short diagonal bands for the printed metal edge and control tabs.
    void caution (int x, int y, int wide, int high, Ink c) const
    {
        for (int j = 0; j < high; ++j)
            for (int i = 0; i < wide; ++i)
                if (((i + j) & 7) < 4) dot (x + i, y + j, c);
    }

    template<size_t Rows, size_t Bytes>
    void print (int x, int y, const uint8_t (&plate)[Rows][Bytes], const Ink (&palette)[16]) const
    {
        // These opaque plates are compiled at their final panel size.
        if (x < 0 || y < 0 || x + int (Bytes * 2) > w || y + int (Rows) > h) return;
        for (size_t j = 0; j < Rows; ++j)
        {
            Ink* dest = px + (y + int (j)) * w + x;
            for (uint8_t pair : plate[j])
            {
                *dest++ = palette[pair >> 4];
                *dest++ = palette[pair & 15];
            }
        }
    }

    int text (int x, int y, const char* s, Ink color) const
    {
        for (; *s; ++s, x += font::glyphWidth)
        {
            const unsigned ch = static_cast<unsigned char> (*s);
            const auto& glyph = font::glyphs[(ch >= 32 && ch <= 127 ? ch : '?') - 32];
            for (int j = 0; j < font::glyphHeight; ++j)
                for (int bits = glyph[j], k = 0; bits; bits >>= 1, ++k)
                    if (bits & 1) dot (x + k, y + j, color);
        }
        return x;
    }

    void right (int end, int y, const char* s, Ink c) const
    {
        text (end - font::glyphWidth * int (std::strlen (s)), y, s, c);
    }

    template<size_t Bytes>
    void mask (int x, int y, const uint8_t (*data)[Bytes], int rows, Ink color) const
    {
        for (int j = 0; j < rows; ++j)
            for (size_t i = 0; i < Bytes; ++i)
                for (int bits = data[j][i], k = 0; bits; bits >>= 1, ++k)
                    if (bits & 1) dot (x + int (i) * 8 + k, y + j, color);
    }

    void hex (int x, int y, unsigned seed, Ink color) const
    {
        for (int shift = 12; shift >= 0; shift -= 4, x += 24)
            mask (x, y, art::digits + 25 * ((seed >> shift) & 15), 25, color);
    }
};

// Retain the last plug-in pixels independently of any host overlays. Compare only
// invalidated regions. Scratch space is shared by all regions and bounded at compile time.
class Picture
{
public:
    template<int W, int H, typename Draw>
    bool region (Ink* dest, int stride, bool whole, int x, int y, Draw draw, int first = 0, int end = W)
    {
        static_assert (W * H <= capacity);
        Raster r { scratch, W, H };
        std::fill_n (scratch, W * H, ink::bg);
        draw (r);
        bool touched = false;
        for (int j = 0; j < H; ++j)
        {
            auto* old = prior + (y + j) * width + x;
            const auto* row = scratch + j * W;
            int left = whole ? 0 : first, right = whole ? W : end;
            if (!whole && std::memcmp (old + left, row + left, (right - left) * sizeof (Ink)) == 0) continue;
            if (! whole)
            {
                while (old[left] == row[left]) ++left;
                while (old[right - 1] == row[right - 1]) --right;
            }
            std::copy (row + left, row + right, old + left);
            std::copy (row + left, row + right, dest + (y + j) * stride + x + left);
            touched = true;
        }
        return touched;
    }

private:
    static constexpr int capacity = 320 * 105;
    Ink prior[width * height], scratch[capacity];
};
} // namespace hdui
