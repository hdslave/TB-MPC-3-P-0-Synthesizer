#include "header_plate.h"
#include <cstring>

#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb/stb_image.h"

#ifdef _WIN32
const uint8_t* desktopHeaderBank();
const uint8_t* desktopSmileyBank();
const uint8_t* desktopFontBank();
#else
extern "C" const uint8_t slink_desktop_header[], slink_desktop_type[], slink_desktop_smiley[];
static const uint8_t* desktopSmileyBank() { return slink_desktop_smiley; }
static const uint8_t* desktopHeaderBank() { return slink_desktop_header; }
static const uint8_t* desktopFontBank() { return slink_desktop_type; }
#endif

namespace hdslave_desktop
{
const uint8_t* desktopTypeMasks() { return desktopFontBank(); }

bool HeaderPlate::paint(Canvas& canvas, int index, Rect bounds)
{
    if (!prepare(desktopHeaderBank(), SLINK_HEADER_BYTES, 639, index,
                 bounds.w * canvas.s, bounds.h * canvas.s)) return false;
    draw(canvas, bounds);
    return true;
}

bool HeaderPlate::prepare(const uint8_t* bank, uint32_t bytes, int count, int index, int dw, int dh)
{
    if (!bank || dw <= 0 || dh <= 0 || index < 0 || index >= count) return false;
    if (index != index_ || dw != width_ || dh != height_)
    {
        uint32_t range[2];
        std::memcpy(range, bank + 4 * index, sizeof(range));
        if (range[0] < (count + 1) * sizeof(uint32_t) || range[1] <= range[0] || range[1] > bytes) return false;
        int w, h, components;
        auto* src = stbi_load_from_memory(bank + range[0], int(range[1] - range[0]), &w, &h, &components, 4);
        if (!src) return false;
        // Box-filter successive half-size levels before the final bilinear sample.
        // This keeps the fine highlights intact on both standard and Retina displays.
        while (w >= 2 * dw || h >= 2 * dh)
        {
            const int sx = w >= 2 * dw ? 2 : 1, sy = h >= 2 * dh ? 2 : 1;
            const int nw = (w + sx - 1) / sx, nh = (h + sy - 1) / sy;
            for (int y=0; y<nh; ++y)
                for (int x=0; x<nw; ++x)
                {
                    const int a = 4 * (y*sy*w + x*sx);
                    const int b = a + (sx==2 && x*sx+1<w ? 4 : 0);
                    const int down = sy==2 && y*sy+1<h ? 4*w : 0;
                    for (int c=0; c<3; ++c)
                        src[4*(y*nw+x)+c] = uint8_t((src[a+c]+src[b+c]+src[a+down+c]+src[b+down+c]+2)/4);
                }
            w=nw; h=nh;
        }
        struct Sample { int first, next, weight; };
        const auto position = [](int i,int from,int to) {
            const double q = std::clamp((i+.5)*from/to-.5,0.,double(from-1));
            return Sample{int(q),std::min(int(q)+1,from-1),int((q-int(q))*256+.5)};
        };
        std::vector<Sample> cols(dw);
        for (int x=0;x<dw;++x) cols[x]=position(x,w,dw);
        pixels_.resize(size_t(dw)*dh);
        for (int y=0;y<dh;++y)
        {
            const auto row=position(y,h,dh);
            for (int x=0;x<dw;++x)
            {
                const auto col=cols[x];
                const auto channel=[&](int k) {
                    const auto horizontal=[&](int r) {
                        const auto* line=src+4*r*w+k;
                        return line[4*col.first]*(256-col.weight)+line[4*col.next]*col.weight;
                    };
                    return unsigned((horizontal(row.first)*(256-row.weight)+horizontal(row.next)*row.weight+32768)>>16);
                };
                pixels_[size_t(y)*dw+x]=0xff000000u|(channel(0)<<16)|(channel(1)<<8)|channel(2);
            }
        }
        stbi_image_free(src);
        index_=index; width_=dw; height_=dh;
    }
    return true;
}

void HeaderPlate::draw(Canvas& canvas, Rect bounds, const HeaderPlate* next, unsigned weight) const
{
    const int x0 = bounds.x * canvas.s, y0 = bounds.y * canvas.s;
    const Rect dst = canvas.clip.intersect({x0, y0, width_, height_});
    if (dst.empty()) return;
    for (int y = dst.y; y < dst.y + dst.h; ++y)
    {
        const size_t start = size_t(y - y0) * width_ + dst.x - x0;
        auto* out = canvas.px + size_t(y) * canvas.pw + dst.x;
        if (!next)
            std::copy_n(pixels_.data() + start, dst.w, out);
        else
            for (int x = 0; x < dst.w; ++x)
            {
                const uint32_t a = pixels_[start + x], b = next->pixels_[start + x];
                const auto blend = [&](uint32_t mask) {
                    return ((((a & mask) * (256-weight) + (b & mask) * weight) >> 8) & mask);
                };
                out[x] = 0xff000000 | blend(0xff00ff) | blend(0x00ff00);
            }
    }
}

void Smiley::paint(Canvas& canvas, int position, Rect bounds)
{
    constexpr int count = 33;
    position = std::clamp(position, 0, (count - 1) * 256);
    const int low = position / 256, weight = position % 256;
    const auto load = [&](int frame) {
        return poses_[frame % 2].prepare(desktopSmileyBank(), SLINK_SMILEY_BYTES, count,
                                         frame, bounds.w * canvas.s, bounds.h * canvas.s);
    };
    if (!load(low) || (weight && !load(low+1))) return;
    poses_[low % 2].draw(canvas, bounds, weight ? &poses_[(low+1) % 2] : nullptr, weight);
}
}
