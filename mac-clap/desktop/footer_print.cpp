#include "footer_print.h"
#include <array>
#include <cstring>

#define STB_IMAGE_STATIC
#define STBI_ONLY_ZLIB
#define STBI_SUPPORT_ZLIB
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb/stb_image.h"

#ifdef _WIN32
const uint8_t* desktopFooterBank();
#else
extern "C" const uint8_t slink_footer_print[];
static const uint8_t* desktopFooterBank() { return slink_footer_print; }
#endif

namespace hdslave_desktop
{
namespace
{
// The embedded bank starts with its dimensions, rate, two BGRA palettes and frame offsets.
// Each independently compressed frame expands to one palette index per pixel.
struct Film
{
    const uint8_t* data = desktopFooterBank();
    uint16_t width = 0, height = 0, count = 0, fps = 0;
    std::array<uint16_t, 2> cuts{};
    std::array<uint32_t, 256> prints{}, stencil{};
    static constexpr size_t headerSize = 16, indexStart = headerSize + sizeof(prints) + sizeof(stencil);

    Film()
    {
        if (!data || SLINK_FOOTER_BANK_SIZE < indexStart || std::memcmp(data, "SFP2", 4)) return;
        std::memcpy(&width, data + 4, 2);
        std::memcpy(&height, data + 6, 2);
        std::memcpy(&count, data + 8, 2);
        std::memcpy(&fps, data + 10, 2);
        std::memcpy(cuts.data(), data + 12, sizeof(cuts));
        if (!height || width > 4096 || height > 256 || !fps ||
            !cuts[0] || cuts[0] >= cuts[1] || cuts[1] >= width ||
            indexStart + (size_t(count) + 1) * 4 > SLINK_FOOTER_BANK_SIZE)
            count = 0;
        std::memcpy(prints.data(), data + headerSize, sizeof(prints));
        std::memcpy(stencil.data(), data + headerSize + sizeof(prints), sizeof(stencil));
    }

    uint32_t offset(int frame) const
    {
        uint32_t at;
        std::memcpy(&at, data + indexStart + size_t(frame) * 4, sizeof(at));
        return at;
    }
};
}

Rect FooterPrint::paint(Canvas& canvas, Rect bounds, uint64_t now, bool motion, bool full)
{
    static const Film film;
    if (!film.count || bounds.empty()) return {};
    if (haveTick_ && motion && now >= lastTick_) elapsed_ += now - lastTick_;
    lastTick_ = now;
    haveTick_ = true;
    const int next = int(elapsed_ * film.fps / 1000 % film.count);
    if (next != frame_)
    {
        const uint32_t begin = film.offset(next), end = film.offset(next + 1);
        if (end <= begin || end > SLINK_FOOTER_BANK_SIZE) return {};
        const int samples = int(film.width) * film.height;
        indices_.resize(samples);
        const int size = stbi_zlib_decode_buffer(reinterpret_cast<char*>(indices_.data()), samples,
                                                 reinterpret_cast<const char*>(film.data + begin), int(end - begin));
        if (size != samples) return {};
        pixels_.resize(samples);
        for (int y = 0; y < film.height; ++y)
        {
            const int row = y * film.width;
            const auto first = indices_.begin() + row;
            const auto split = first + film.cuts[1];
            auto dest = std::transform(first, split, pixels_.begin() + row,
                                       [&](uint8_t index) { return film.prints[index]; });
            std::transform(split, first + film.width, dest,
                           [&](uint8_t index) { return film.stencil[index]; });
        }
        frame_ = next;
    }
    else if (!full) return {};

    // Keep the prints in proportion and distribute the space between them at wider host sizes.
    canvas.fill(bounds, 0xff080812);
    const int edges[]{0, film.cuts[0], film.cuts[1], film.width};
    for (int i = 0; i < 3; ++i)
    {
        const int left = bounds.x + bounds.w * edges[i] / film.width;
        const int right = bounds.x + bounds.w * edges[i+1] / film.width;
        const int srcWidth = edges[i+1] - edges[i];
        const double scale = std::min(double(right - left) / srcWidth, double(bounds.h) / film.height);
        const int w = int(std::lround(srcWidth * scale)), h = int(std::lround(film.height * scale));
        const Rect dest{left + (right - left - w) / 2, bounds.y + (bounds.h - h) / 2, w, h};
        canvas.blitSharp(pixels_.data() + edges[i], srcWidth, film.height, dest, film.width);
    }
    for (uint16_t cut : film.cuts)
    {
        const int x = bounds.x + bounds.w * cut / film.width;
        canvas.line(x - 3, bounds.y + bounds.h - 5, x + 3, bounds.y + 5, .8f, 0xff605c71);
    }
    return bounds;
}
}
