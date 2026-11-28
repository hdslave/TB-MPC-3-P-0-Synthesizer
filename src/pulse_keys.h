// Trim used by the freesp16 and desktop controls. Coordinates are physical pixels.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>

namespace slink::keys
{
using Color = uint32_t;
enum class Face { control, lit, dormant, step, bit, litBit, note, noteFocus, window };

inline Color shade(Color a, Color b, int amount)
{
    const auto channel = [&](int shift) {
        const int lo = (a >> shift) & 255, hi = (b >> shift) & 255;
        return unsigned(lo + (hi - lo) * amount / 256) << shift;
    };
    return 0xff000000u | channel(16) | channel(8) | channel(0);
}

// The shoulder sweeps farther on the leading edge. A brushed bevel catches light
// at the crown, then falls into a darker seam along the opposite edge.
template<class Fill>
void key(int x, int y, int w, int h, int scale, Face face, Fill fill)
{
    if (w < 4 * scale || h < 4 * scale) return;
    const bool note = face == Face::note || face == Face::noteFocus;
    const bool bit = face == Face::bit || face == Face::litBit;
    const bool lit = face == Face::lit || face == Face::litBit || face == Face::noteFocus;
    const bool muted = face == Face::dormant;
    const int shoulder = note ? 3 * scale : bit ? 3 * scale : std::max(scale, std::min({14 * scale, w / 5, h / 2}));
    const int heel = std::max(scale, shoulder / 3);
    const Color top = lit ? 0xff4859a9 : muted ? 0xff202027 : face == Face::step ? 0xff221e31 : 0xff2c293d;
    const Color foot = lit ? 0xff202751 : muted ? 0xff13131b : 0xff10101c;
    const Color crown = lit ? 0xffb5c7fe : note ? 0xff39364b : muted ? 0xff41404a : face == Face::step ? 0xff494359 : 0xff696579;
    const Color lip = lit ? 0xff6d81c6 : muted ? 0xff303039 : 0xff393447;
    const auto sweep = [](int radius,int depth) {
        const int d = std::max(0,radius - depth);
        return (d*d + radius/2) / radius;
    };
    for (int j = 0; j < h; ++j)
    {
        const int fromFoot = h - 1 - j;
        const int left = std::max(sweep(heel,j), sweep(shoulder,fromFoot));
        const int right = w - std::max(sweep(shoulder,j), sweep(heel,fromFoot));
        // The film is already on the surface. Trim its corners to the panel well.
        if (face == Face::window)
        {
            if (left > 0) fill(x, y + j, left, 1, 0xff080812);
            if (right < w) fill(x + right, y + j, w - right, 1, 0xff080812);
        }
        const int t = 256 * j / (h - 1);
        const Color rim = bit && !lit ? shade(0xff393249,0xff181522,t) : shade(crown, lip, t);
        if (j < scale || fromFoot < scale)
            fill(x + left, y + j, right - left, 1, rim);
        else
        {
            fill(x + left, y + j, scale, 1, rim);
            if (face != Face::window)
            {
                const Color enamel = note ? 0xff000000 : shade(top, foot, std::min(256, t * 3 / 2));
                fill(x + left + scale, y + j, right - left - 2 * scale, 1, enamel);
            }
            fill(x + right - scale, y + j, scale, 1, shade(rim, foot, 90));
        }
    }
    if (muted || bit) return;
    // A short silver reflection and recessed contact interrupt the continuous rim.
    const int gleam = std::min(w / 4, 16 * scale);
    fill(x + heel + scale, y, gleam, scale,
         lit ? 0xffe1ebff : note ? 0xff524c60 : 0xff908b9f);
    if (!note && h >= 11 * scale)
    {
        const int contact = std::min(w / 5, 12 * scale);
        fill(x + w - heel - contact - 2 * scale, y + h - 2 * scale,
             contact, scale, lit ? 0xff93aaff : 0xff514b67);
    }
}

// A single contact travels along the transport rail with the sequencer.
template<class Fill>
void clockRail(int x, int y, int w, int scale, int step, int count, Fill fill)
{
    if (count < 1 || w < 4 * scale) return;
    fill(x, y, w, scale, 0xff55557b);
    for (int beat = 0; beat < count; beat += 4)
        fill(x + beat * (w - scale) / count, y, scale, 2 * scale, 0xff8384ac);
    const int slot = std::clamp(step, 0, count - 1);
    const int head = x + slot * (w - 3 * scale) / std::max(1, count - 1);
    fill(head, y - scale, 3 * scale, 2 * scale, 0xffc9dbff);
}

// Two leads fold into a single, pointed heart at the transport.
template<class Plot>
void heart(int x, int y, int width, bool running, Plot plot)
{
    struct Point { float x, y; };
    constexpr Point left[]  = {{0,5},{3,5},{4,2},{6,1},{8,1.5f},{10,4},{12,1.5f}};
    constexpr Point right[] = {{12,1.5f},{14,1},{16,2},{17,5},{20,5}};
    constexpr Point tip[]   = {{3,5},{5,6},{10,11},{15,6},{17,5}};
    const float unit = width/21.f;
    const auto stroke = [&](const Point* points, int count, Color ink) {
        for (int i=1; i<count; ++i)
        {
            const float ax=points[i-1].x*unit, ay=points[i-1].y*unit;
            const float bx=points[i].x*unit, by=points[i].y*unit, dx=bx-ax, dy=by-ay;
            const float r = std::max(.65f,.72f*unit), limit = r+.5f;
            for (int py=int(std::floor(std::min(ay,by)-limit)); py<=std::max(ay,by)+limit; ++py)
                for (int px=int(std::floor(std::min(ax,bx)-limit)); px<=std::max(ax,bx)+limit; ++px)
                {
                    const float t=std::clamp(((px-ax)*dx+(py-ay)*dy)/(dx*dx+dy*dy),0.f,1.f);
                    const float u=px-ax-t*dx, v=py-ay-t*dy;
                    const float alpha=std::min(1.f,limit-std::sqrt(u*u+v*v));
                    if (alpha>0) plot(x+px,y+py,ink,unsigned(255*alpha+.5f));
                }
        }
    };
    stroke(tip,5,0xff9dabe7);
    stroke(left,7,0xffd1b8f6);
    stroke(right,5,running ? 0xffe1f2ff : 0xffa1b4e7);
}
}
