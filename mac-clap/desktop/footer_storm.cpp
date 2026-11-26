#include "footer_storm.h"
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
extern "C" const uint8_t slink_footer_storm[];
static const uint8_t* desktopFooterBank() { return slink_footer_storm; }
#endif

namespace hdslave_desktop
{
namespace
{
// The embedded bank starts with its dimensions, rate, BGRA palette and frame offsets.
// Each independently compressed frame expands to one palette index per pixel.
struct Film
{
    const uint8_t* data = desktopFooterBank();
    uint16_t width = 0, height = 0, count = 0, fps = 0;
    std::array<uint32_t, 256> palette{};
    static constexpr size_t headerSize = 12, indexStart = headerSize + sizeof(palette);

    Film()
    {
        if (!data || SLINK_FOOTER_BANK_SIZE < indexStart || std::memcmp(data, "SFP3", 4)) return;
        std::memcpy(&width, data + 4, 2);
        std::memcpy(&height, data + 6, 2);
        std::memcpy(&count, data + 8, 2);
        std::memcpy(&fps, data + 10, 2);
        if (!width || !height || width > 4096 || height > 256 || !fps ||
            indexStart + (size_t(count) + 1) * 4 > SLINK_FOOTER_BANK_SIZE)
            count = 0;
        std::memcpy(palette.data(), data + headerSize, sizeof(palette));
    }

    uint32_t offset(int frame) const
    {
        uint32_t at;
        std::memcpy(&at, data + indexStart + size_t(frame) * 4, sizeof(at));
        return at;
    }
};
}

Rect FooterStorm::paint(Canvas& canvas, Rect bounds, uint64_t now, bool motion, bool full)
{
    static const Film film;
    if (!film.count || bounds.empty()) return {};
    if (haveTick_ && motion && now >= lastTick_) elapsed_ += now - lastTick_;
    lastTick_ = now;
    haveTick_ = true;
    // A 60 Hz host can report 16, 33, 50 ms. Round to the nearest sample so
    // millisecond timestamps do not repeat one pose and skip the next.
    const int next = int((elapsed_ * film.fps + 500) / 1000 % film.count);
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
        std::transform(indices_.begin(), indices_.end(), pixels_.begin(),
                       [&](uint8_t index) { return film.palette[index]; });
        frame_ = next;
    }
    else if (!full) return {};

    canvas.blitSharp(pixels_.data(), film.width, film.height, bounds);
    return bounds;
}
}
