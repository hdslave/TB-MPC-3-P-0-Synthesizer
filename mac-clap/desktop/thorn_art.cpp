#include "thorn_art.h"
#include <iterator>
#include "../assets/thorn/index.h"
#ifdef SLINK_RAZOR_PANEL
#include "../assets/razor/index.h"
namespace plates = razor;
#else
namespace plates = thorn;
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "../third_party/stb/stb_image.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
extern "C" const unsigned char slink_panel_bank[];
#endif

namespace hdslave_desktop
{
namespace
{
const unsigned char *bank ()
{
#ifdef _WIN32
    HMODULE module{};
    if (!GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast<LPCWSTR> (&bank), &module))
        return nullptr;
    auto res = FindResourceW (module, MAKEINTRESOURCEW (101), RT_RCDATA);
    if (!res || SizeofResource (module, res) != plates::offset[std::size (plates::offset) - 1]) return nullptr;
    return static_cast<const unsigned char *> (LockResource (LoadResource (module, res)));
#else
    return slink_panel_bank;
#endif
}

struct Fonts
{
    ThornImage regular, italic, regularLarge, italicLarge;
    Fonts ()
    {
        regular.load (2);
        italic.load (3);
        regularLarge.load (4);
        italicLarge.load (5);
    }
};
}

void ThornImage::load (int image)
{
    if (image == index || image < 0 || size_t (image + 1) >= std::size (plates::offset)) return;
    static const auto *bytes = bank ();
    if (!bytes) return;
    int w = 0, h = 0, channels = 0;
    const auto start = plates::offset[image];
    auto *rgba =
        stbi_load_from_memory (bytes + start, int (plates::offset[image + 1] - start), &w, &h, &channels, 4);
    if (!rgba) return;
    width = w;
    height = h;
    index = image;
    alpha = channels == 4 && (image == 1 || image == 6);
#ifdef SLINK_RAZOR_PANEL
    alpha = channels == 4 && (image < 2 || image >= 6);
#endif
    pixels.resize (size_t (w) * h);
    for (size_t i = 0; i < pixels.size (); ++i)
    {
        const auto *src = rgba + i * 4;
        const uint32_t rgb = 0xff000000u | (uint32_t (src[0]) << 16) | (uint32_t (src[1]) << 8) | src[2];
        pixels[i] =
            alpha ? (rgb & 0xffffffu) | (uint32_t (src[3]) << 24) : Canvas::blend (0xff0d0c16u, rgb, src[3]);
    }
    stbi_image_free (rgba);
}

void ThornImage::fit (Canvas &c, Rect box, int frame, int count) const
{
    if (pixels.empty () || box.empty ()) return;
    const int sw = width / count;
    const double scale = std::min (double (box.w) / sw, double (box.h) / height);
    const int w = std::max (1, int (sw * scale)), h = std::max (1, int (height * scale));
    const Rect dst{box.x + (box.w - w) / 2, box.y + (box.h - h) / 2, w, h};
    if (!alpha && count == 1)
        c.blitSharp (pixels.data (), sw, height, dst);
    else
    {
        const int dw = w * c.s, dh = h * c.s;
        // Sample each quadrant of a destination pixel to preserve the narrow metal rims.
        for (int y = 0; y < dh; ++y)
        {
            const auto *row0 = pixels.data () + size_t ((4 * y + 1) * height / (4 * dh)) * width + frame * sw;
            const auto *row1 = pixels.data () + size_t ((4 * y + 3) * height / (4 * dh)) * width + frame * sw;
            auto *out = c.px + size_t (dst.y * c.s + y) * c.pw + dst.x * c.s;
            for (int x = 0; x < dw; ++x)
            {
                const int x0 = (4 * x + 1) * sw / (4 * dw), x1 = (4 * x + 3) * sw / (4 * dw);
                const uint32_t bg = out[x];
                uint32_t rb = 0, green = 0;
                for (uint32_t pixel : {row0[x0], row0[x1], row1[x0], row1[x1]})
                {
                    const auto color = Canvas::blend (bg, pixel, pixel >> 24);
                    rb += color & 0xff00ff;
                    green += color & 0xff00;
                }
                out[x] = 0xff000000 | ((rb >> 2) & 0xff00ff) | ((green >> 2) & 0xff00);
            }
        }
    }
}

ThornArt::ThornArt ()
{
    logo.load (0);
    face.load (1);
    knob.load (6);
#ifdef SLINK_RAZOR_PANEL
    notes.load (razor::notes);
    pearl.load (razor::pearl);
    steel.load (razor::steel);
    steps.load (razor::steps);
    labels.load (razor::labels);
    glass.load (razor::glass);
    counter.load (razor::counter);
#endif
}
void ThornArt::animate (uint64_t cameraMs, uint64_t motionMs)
{
#ifdef SLINK_RAZOR_PANEL
    camera.load (razor::cameraFirst + int (cameraMs * 12 / 1000 % razor::cameraCount));
    storm.load (razor::stormFirst + int (motionMs * 12 / 1000 % razor::stormCount));
#else
    camera.load (thorn::cameraFirst + int (cameraMs / 50 % thorn::cameraCount));
    shaft.load (thorn::shaftFirst + int (cameraMs * 24 / 1000 % thorn::shaftCount));
#endif
}

void lettering (Canvas &c, Rect box, std::string_view text, int size, uint32_t ink, Face face, int align)
{
    if (box.empty () || text.empty ()) return;
    static const Fonts fonts;
    const bool bold = face == Face::italic;
    const auto *advances = bold ? thorn::italicAdvance : thorn::regularAdvance;
    auto glyph = [] (unsigned char ch) { return ch >= 32 && ch < 127 ? ch - 32 : '?' - 32; };
    int advance = 0;
    for (auto ch : text)
        advance += advances[glyph (ch)];
    const float em =
        std::min (float (size), std::min (float (box.h) * 1.12f, box.w * 640.f / std::max (1, advance)));
    const int density = em * c.s > 48 ? 2 : 1;
    const auto &atlas =
        density == 2 ? (bold ? fonts.italicLarge : fonts.regularLarge) : (bold ? fonts.italic : fonts.regular);
    if (atlas.pixels.empty ()) return;
    const float scale = em * c.s / (40.f * density);
    const float tw = advance * em / 640.f;
    float pen = c.s * (box.x + (align == 1 ? (box.w - tw) / 2 : align == 2 ? box.w - tw : 0));
    const float top = c.s * (box.y + (box.h - em) / 2) - 10 * density * scale;
    const Rect clip = c.clip.intersect ({box.x * c.s, box.y * c.s, box.w * c.s, box.h * c.s});
    for (auto ch : text)
    {
        const int g = glyph (ch), sx = (g % 16) * 56 * density, sy = (g / 16) * 56 * density;
        const float left = pen - 4 * density * scale;
        const int limit = std::min (56, int (advances[g] / 16) + 12) * density;
        for (int y = std::max (clip.y, int (std::floor (top)));
             y < std::min (clip.y + clip.h, int (top + 54 * density * scale)); ++y)
        {
            const float fy = std::clamp ((y + .5f - top) / scale, 0.f, 54.f * density);
            const int iy = int (fy);
            const auto *row = atlas.pixels.data () + size_t (sy + iy) * atlas.width + sx;
            for (int x = std::max (clip.x, int (std::floor (left)));
                 x < std::min (clip.x + clip.w, int (left + limit * scale)); ++x)
            {
                const float fx = std::clamp ((x + .5f - left) / scale, 0.f, 54.f * density);
                const int ix = int (fx);
                auto sample = [ix, fx] (const uint32_t *r) {
                    const int a = r[ix] & 255, b = r[ix + 1] & 255;
                    return a + (b - a) * (fx - ix);
                };
                const float a = sample (row);
                const auto alpha = uint32_t (a + (sample (row + atlas.width) - a) * (fy - iy));
                if (alpha) c.plot (x, y, ink, alpha);
            }
        }
        pen += advances[g] * scale * density / 16;
    }
}
}
