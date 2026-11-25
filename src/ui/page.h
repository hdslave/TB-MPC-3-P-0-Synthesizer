// Native-pixel drawing and Melodee controls for the instrument page.
#pragma once

#include "font.h"
#include "identity.h"
#include "blacklight_art.h"
#include "press_type.h"
#include "silver_art.h"

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
constexpr Ink bg = 0xff101213, field = 0xff17191b, card = 0xff303438, rule = 0xff60645f;
constexpr Ink grid = 0xff373b3d, muted = 0xffa7ada9, paper = 0xfffcf2d8, live = 0xffc6ff05;
constexpr Ink metal = 0xffcdd0d5, alternate = 0xffbfc2c8, steel = 0xffacaea8, header = 0xff474c49;
constexpr Ink cobalt = 0xff5255ed, violet = 0xffaaa4ff, pink = 0xffef52bd, cursor = 0xff35450f;
}

struct PlotPalette
{
    Ink bg, field, grid, rule, paper, muted, live, cursor, pink, violet;
};

inline constexpr PlotPalette pressInk { ink::bg, ink::field, ink::grid, ink::rule, ink::paper,
                                        ink::muted, ink::live, ink::cursor, ink::pink, ink::violet };
inline constexpr PlotPalette ticketInk { 0xff090a23, 0xff141060, 0xff312c55, 0xff544e7f, 0xfff1efff,
                                         0xffcac0fa, 0xffc5ff06, 0xff2d2261, 0xffff4bc9, 0xffb2a9ff };

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

enum class Key : uint8_t { wheel = 0, push = 1, nudge = 2 }; // as MELODEE_SCREEN_WHEEL, _PUSH, _STEP

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
    bool smoothType = false;
    int originX = 0, originY = 0;

    void box (int x, int y, int wide, int high, Ink c) const
    {
        const int left = std::max (0, x), right = std::min (w, x + wide);
        if (right <= left) return;
        for (int j = std::max (0, y); j < std::min (h, y + high); ++j)
            std::fill (px + j * w + left, px + j * w + right, c);
    }

    Ink brushed (int x, int y, Ink metal) const
    {
        const int shade = silver::brush[(originY+y) & 31][(originX+x) & 127];
        return metal + shade * 0x010101;
    }

    void surface (int x, int y, int wide, int high, Ink c) const
    {
        if (c != ink::metal && c != ink::alternate && c != silver::cardMetal)
        {
            box (x, y, wide, high, c);
            return;
        }
        const int left = std::max (0, x), right = std::min (w, x+wide);
        for (int j = std::max (0, y); j < std::min (h, y+high); ++j)
            for (int i = left; i < right; ++i)
                px[j*w+i] = brushed (i, j, c);
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

    template<size_t Rows, size_t W>
    void stamp (int x, int y, const uint8_t (&data)[Rows][W], const Ink* palette) const
    {
        if (x < 0 || x + int (W) > w) return;
        // Adjacent damage regions can share a plate while painting only their own scanlines.
        const int first = std::max (0, -y), end = std::min (int (Rows), h-y);
        for (int j = first; j < end; ++j)
            for (size_t i = 0; i < W; ++i)
                px[(y + j) * w + x + i] = palette[data[j][i]];
    }

    // Move only a narrow printed strip across the hanging tips of the smiley.
    void slip (int left, int top, int wide, int high, int distance) const
    {
        for (int y = top; y < top + high; ++y)
        {
            Ink* line = px + y * w + left;
            std::memmove (line, line - distance, wide * sizeof (Ink));
        }
    }

    static unsigned glyphIndex (char ch)
    {
        const unsigned c = static_cast<unsigned char> (ch);
        return (c >= 32 && c < 127 ? c : '?') - 32;
    }

    void coverageGlyph (int x, int y, int end, const uint64_t* rows, int height, Ink color) const
    {
        constexpr unsigned weight[] = { 0, 2, 5, 7, 9, 11, 14, 16 };
        end = std::min (w, end);
        for (int j = std::max (0, -y); j < std::min (height, h-y); ++j)
        {
            uint64_t bits = rows[j];
            for (int col = x; bits && col < end; ++col, bits >>= 3)
            {
                const unsigned a = unsigned (bits & 7u);
                if (!a || col < 0) continue;
                auto& dest = px[(y+j)*w + col];
                if (a == 7) { dest = color; continue; }
                // Blend red and blue in separate 16-bit lanes, using the actual panel colour below the glyph.
                const unsigned fg = weight[a], bg = 16-fg;
                constexpr Ink rb = 0x00ff00ff, g = 0x0000ff00;
                dest = 0xff000000u | ((((color & rb)*fg + (dest & rb)*bg + 0x80008u) >> 4) & rb)
                                   | ((((color & g)*fg + (dest & g)*bg + 0x800u) >> 4) & g);
            }
        }
    }

    static int faceWidth (const char* s, const pressType::Face& face)
    {
        int width = 0;
        for (; *s; ++s) width += face.advance[glyphIndex (*s)];
        return width;
    }

    int faceText (int x, int y, int end, const char* s, Ink color, const pressType::Face& face) const
    {
        end = std::min (end, w);
        for (; *s; ++s)
        {
            const auto i = glyphIndex (*s);
            if (x + face.advance[i] > end) break;
            coverageGlyph (x, y, end, face.rows + i*face.height, face.height, color);
            x += face.advance[i];
        }
        return x;
    }

    int readingWidth (const char* s, bool lead = false) const
    {
        if (smoothType) return faceWidth (s, lead ? pressType::lead : pressType::value);
        int width = 0;
        for (; *s; ++s)
            width += (lead ? art::leadAdvance : art::readingAdvance)[glyphIndex (*s)];
        return width;
    }

    void reading (int x, int y, int end, const char* s, Ink color, bool lead = false) const
    {
        if (smoothType)
        {
            faceText (x, y, end, s, color, lead ? pressType::lead : pressType::value);
            return;
        }
        for (; *s; ++s)
        {
            const auto i = glyphIndex (*s);
            const int advance = (lead ? art::leadAdvance : art::readingAdvance)[i];
            if (x + advance > end) break;
            for (int j = 0; j < (lead ? 23 : 19); ++j)
                for (unsigned bits = lead ? art::lead[i][j] : art::reading[i][j], k = 0; bits; bits >>= 1, ++k)
                    if ((bits & 1) && x + int (k) < end) dot (x + int (k), y+j, color);
            x += advance;
        }
    }

    int text (int x, int y, const char* s, Ink color, bool shortCaps = false) const
    {
        if (smoothType) return faceText (x, y, w, s, color, shortCaps ? pressType::compact : pressType::label);
        for (; *s; ++s, x += font::glyphWidth)
        {
            const unsigned ch = static_cast<unsigned char> (*s);
            const unsigned i = (ch >= 32 && ch <= 127 ? ch : '?') - 32;
            const uint8_t* glyph = shortCaps ? font::smallCaps[i] : font::glyphs[i];
            for (int j = 0; j < (shortCaps ? 6 : font::glyphHeight); ++j)
                for (int bits = glyph[j], k = 0; bits; bits >>= 1, ++k)
                    if (bits & 1) dot (x + k, y + j, color);
        }
        return x;
    }

    void fittedValue (int x, int y, int end, const char* value, Ink color) const
    {
        for (const auto* face : { &pressType::value, &pressType::label, &pressType::caption, &pressType::compact })
            if (faceWidth (value, *face) <= end-x)
            {
                faceText (x, y + (face == &pressType::value ? 0 : 4), end, value, color, *face);
                return;
            }
        const int tail = faceText (x, y+4, end-faceWidth ("..", pressType::compact), value, color, pressType::compact);
        faceText (tail, y+4, end, "..", color, pressType::compact);
    }

    void right (int end, int y, const char* s, Ink c) const
    {
        const int width = smoothType ? faceWidth (s, pressType::label) : 6 * int (std::strlen (s));
        text (end-width, y, s, c);
    }

    template<size_t Bytes>
    void mask (int x, int y, const uint8_t (*data)[Bytes], int rows, Ink color) const
    {
        for (int j = 0; j < rows; ++j)
            for (size_t i = 0; i < Bytes; ++i)
                for (int bits = data[j][i], k = 0; bits; bits >>= 1, ++k)
                    if (bits & 1) dot (x + int (i) * 8 + k, y + j, color);
    }

    template<size_t Rows, size_t Bytes>
    void hex (int x, int y, unsigned seed, Ink color, const uint8_t (&digits)[Rows][Bytes], int advance) const
    {
        static_assert (Rows % 16 == 0);
        constexpr int h = int (Rows/16);
        for (int shift = 12; shift >= 0; shift -= 4, x += advance)
            mask (x, y, digits + h * ((seed >> shift) & 15), h, color);
    }
};

// Retain the last plug-in pixels independently of any host overlays. Compare only
// invalidated regions. Scratch space is shared by all regions and bounded at compile time.
class Picture
{
public:
    Ink background = ink::bg;
    bool smoothType = false;

    template<int W, int H, typename Draw>
    bool region (Ink* dest, int stride, bool whole, int x, int y, Draw draw, int first = 0, int end = W)
    {
        static_assert (W * H <= capacity);
        Raster r { scratch, W, H, smoothType, x, y };
        std::fill_n (scratch, W * H, background);
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
