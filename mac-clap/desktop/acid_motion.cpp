#include "acid_motion.h"
#include <array>
#include <iterator>
#include <limits>

namespace hdslave_desktop
{
namespace
{
constexpr float pi = 3.14159265f;
constexpr uint32_t field = 0xff090810, ink = 0xffd7caff;

// Fill a vertical interval with fractional coverage at its edges. Coordinates
// are physical pixels so the small counters stay open on a Retina display.
void span (Canvas& c, int x, float y0, float y1, uint32_t colour)
{
    if (y1 < y0) std::swap (y0, y1);
    y0 = std::max (y0, float (c.clip.y));
    y1 = std::min (y1, float (c.clip.y + c.clip.h));
    if (y1 <= y0) return;
    const int top = int (y0), bottom = int (y1);
    if (top == bottom)
    {
        c.plot (x, top, colour, uint32_t ((y1 - y0) * 255));
        return;
    }
    c.plot (x, top, colour, uint32_t ((top + 1 - y0) * 255));
    for (int y = top + 1; y < bottom; ++y)
        c.px[size_t (y) * c.pw + x] = colour;
    c.plot (x, bottom, colour, uint32_t ((y1 - bottom) * 255));
}

struct Fold
{
    float axis, radius, twist, curl;

    // An oblique section of a twisted sheet. Its filaments converge as it
    // turns edge-on, then fan apart around the reverse face.
    float edge (float v) const
    {
        return axis + radius * (.65f * v * std::cos (twist) + (.51f * v * v - .21f) * std::sin (twist) +
                                .16f * (1 - v * v) * std::sin (curl + 2.7f * v));
    }
};

void wing (Canvas& c, int x, float u, float cy, float height, float phase, float accent, bool reverse)
{
    if (u <= 0 || u >= 1) return;
    const float taper = std::sin (pi * u);
    const float t = 2 * pi * u;
    const float twist = 1.48f * t - phase + .72f * std::sin (t + .54f * std::sin (phase));
    const float sweep = .37f * std::sin (t - .62f * std::sin (phase)) + .20f * std::sin (2 * t + .75f * phase);
    const Fold fold{cy + height * taper * sweep, height * taper * (.99f + .14f * accent), twist, t + phase};
    constexpr float fine[] = {-1, -.64f, -.54f, -.29f, -.17f, .05f, .18f, .35f, .49f, .63f, .79f, .91f};
    constexpr float broad[] = {-1, -.48f, -.34f, .03f, .19f, .43f, .65f, .83f};
    const float* edges = reverse ? fine : broad;
    const int count = reverse ? int (std::size (fine)) : int (std::size (broad));
    for (int i = 0; i < count; i += 2)
    {
        const float facing = .81f + .19f * std::cos (twist + edges[i] * 1.7f);
        span (c, x, fold.edge (edges[i]), fold.edge (edges[i + 1]), mix (field, ink, facing));
    }
}

// The sweeping outline doubles back over itself at the tips. Draw its closed
// Bezier contour as a filled path, including the negative space inside the hook.
struct Point
{
    float x, y;
};
struct Cubic
{
    Point a, b, end;
};
constexpr Point start{-152, 3};
constexpr Cubic blade[] = {
    {{-115, 3}, {-86, 1}, {-58, -23}},  {{-64, -11}, {-75, -5}, {-71, -3}}, {{-60, -3}, {-46, -13}, {-30, -19}},
    {{-46, -6}, {-43, 3}, {-19, -2}},   {{-35, 5}, {-60, 10}, {-59, 28}},   {{-63, 12}, {-57, 7}, {-52, 2}},
    {{-76, 13}, {-92, 13}, {-115, 13}}, {{-87, 8}, {-83, 5}, {-82, 2}},     {{-104, 6}, {-125, 5}, {-152, 3}}};
constexpr Point holeStart{-80, 7};
constexpr Cubic hole[] = {{{-68, 3}, {-62, -1}, {-49, -8}}, {{-58, 2}, {-66, 7}, {-80, 7}}};

struct Edge
{
    Point a, b;
};
void bladeShape (Canvas& c, float cx, float cy, float sx, float sy, float bend, uint32_t colour)
{
    std::array<Edge, (std::size (blade) + std::size (hole)) * 12> edges;
    size_t used = 0;
    float xMin = std::numeric_limits<float>::max (), xMax = std::numeric_limits<float>::lowest ();
    auto transform = [&] (Point p) {
        p.x = cx + (p.x + .55f * bend * p.y) * sx;
        p.y = cy + p.y * sy;
        xMin = std::min (xMin, p.x);
        xMax = std::max (xMax, p.x);
        return p;
    };
    auto contour = [&] (Point a, const auto& curves) {
        Point prev = transform (a);
        for (const Cubic& seg : curves)
        {
            for (int j = 1; j <= 12; ++j)
            {
                const float t = j / 12.f, q = 1 - t;
                Point p = transform (
                    {q * q * q * a.x + 3 * q * q * t * seg.a.x + 3 * q * t * t * seg.b.x + t * t * t * seg.end.x,
                     q * q * q * a.y + 3 * q * q * t * seg.a.y + 3 * q * t * t * seg.b.y +
                         t * t * t * seg.end.y});
                edges[used++] = {prev, p};
                prev = p;
            }
            a = seg.end;
        }
    };
    contour (start, blade);
    contour (holeStart, hole);
    for (int x = std::max (c.clip.x, int (std::floor (xMin)));
         x < std::min (c.clip.x + c.clip.w, int (std::ceil (xMax))); ++x)
    {
        std::array<float, 3 * (std::size (blade) + std::size (hole))> cuts;
        size_t n = 0;
        const float scan = x + .5f;
        for (size_t i = 0; i < used; ++i)
        {
            auto [a, b] = edges[i];
            if ((a.x > scan) == (b.x > scan)) continue;
            cuts[n++] = a.y + (scan - a.x) * (b.y - a.y) / (b.x - a.x);
        }
        std::sort (cuts.begin (), cuts.begin () + n);
        for (size_t i = 1; i < n; i += 2)
            span (c, x, cuts[i - 1], cuts[i], colour);
    }
}

void orbit (Canvas& c, int x, float u, float cy, float height, float phase, float accent)
{
    if (std::abs (u) >= 1) return;
    const float arc = std::sqrt (1 - u * u);
    const float tilt = .34f * std::sin (phase);
    const float centre = cy + height * tilt * u;
    const float radius = height * (.54f + .24f * std::cos (phase) + .10f * accent);
    const float outer = radius * arc, r = .78f;
    if (std::abs (u) >= r)
    {
        span (c, x, centre - outer, centre + outer, ink);
        return;
    }
    const float inner = radius * .69f * std::sqrt (1 - u * u / (r * r));
    const float aperture = centre + inner * .17f * std::sin (phase);
    span (c, x, centre - outer, aperture - inner, ink);
    span (c, x, aperture + inner, centre + outer, ink);
    // Needle-shaped reflection suspended in the aperture.
    const float star = std::max (0.f, 1 - std::abs (u) / .93f);
    const float tip = radius * .66f * std::pow (star, 8.f);
    span (c, x, aperture - tip, aperture + tip, ink);
}

}

void drawAcidMotion (Canvas& c, Rect r, double seconds, float accent)
{
    if (r.empty ()) return;
    const Rect saved = c.clip;
    c.clip = saved.intersect ({r.x * c.s, r.y * c.s, r.w * c.s, r.h * c.s});
    c.fill (r, field);
    const float width = float (r.w * c.s), height = r.h * c.s * .49f;
    const float phase = float (std::fmod (seconds, 32.) * (2 * pi / 8.));
    const float axis = (r.y + r.h * .5f) * c.s;
    const float focus = .50f + .026f * std::sin (phase * .50f);
    for (int x = c.clip.x; x < c.clip.x + c.clip.w; ++x)
    {
        const float p = (x + .5f - r.x * c.s) / width;
        const bool left = p < focus;
        const float side = left ? (p - .008f) / (focus - .093f) : (.992f - p) / (.91f - focus);
        const float flow = left ? phase : phase + .72f;
        wing (c, x, side, axis, height, flow, accent, !left);
        orbit (c, x, (p - focus) / .029f, axis, height, phase, accent);
    }
    const float cx = (r.x + focus * r.w) * c.s;
    const float widthScale = width / 1100.f;
    const float heightScale = height * (.028f + .004f * std::cos (phase) + .003f * accent);
    bladeShape (c, cx, axis, widthScale, heightScale, std::sin (phase), ink);
    bladeShape (c, cx, axis, -widthScale, -heightScale, std::sin (phase + .7f), ink);
    c.clip = saved;
}
}
