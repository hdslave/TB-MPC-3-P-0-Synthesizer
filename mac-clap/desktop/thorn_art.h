#pragma once

#include "panel_renderer.h"
#include <string_view>
#include <vector>

namespace hdslave_desktop
{
struct ThornImage
{
    bool alpha = false;
    int index = -1, width = 0, height = 0;
    std::vector<uint32_t> pixels;
    void load (int image);
    void fit (Canvas &c, Rect box, int frame = 0, int count = 1) const;
};

struct ThornArt
{
    ThornImage logo, face, camera, shaft, knob;
#ifdef SLINK_RAZOR_PANEL
    ThornImage notes, pearl, steel, steps, labels, glass, counter;
#endif
    ThornArt ();
    void animate (uint64_t elapsed);
};

enum class Face
{
    regular,
    italic
};
void lettering (Canvas &c, Rect box, std::string_view label, int size, uint32_t ink, Face face = Face::regular,
                int alignment = 0);
}
