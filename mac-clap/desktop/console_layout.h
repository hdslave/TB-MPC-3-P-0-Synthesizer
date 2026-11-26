// Logical desktop geometry, shared by painting and pointer hit testing.
#pragma once
#include "panel_renderer.h"
#include "../src/params.h"
#include <array>

namespace hdslave_desktop
{
namespace id = tbmpc::ParamId;
enum class Item
{
    PresetPrev,
    PresetBox,
    PresetNext,
    Fine,
    Learn,
    ClearCc,
    Motion,
    Count
};
enum Plot
{
    filterPlot = id::count,
    pitchPlot,
    cutoffPlot,
    slotCount
};
struct Section
{
    const char* name;
    uint32_t ink;
    int columns;
    std::array<int, 6> slots;
};
inline constexpr Section sections[] = {
    {"Perform", 0xffb7c5ff, 3, {id::cutoff, id::resonance, id::envMod, id::decay, id::accent, id::volume}},
    {"Voice", 0xff9aafff, 3, {id::waveform, id::tuning, id::drive, id::filter, id::peakSpread, filterPlot}},
    {"Glide", 0xffdea3ff, 2, {id::glide, id::slideTime, id::gate, id::accentDecay, -1, -1}},
    {"Pattern", 0xffb3ec87, 3, {id::seedMode, id::mutate, id::density, id::root, id::scale, id::transpose}},
    {"Modulation",
     0xffc7a7ff,
     3,
     {pitchPlot, id::pitchLfoAmount, id::pitchLfoSpeed, cutoffPlot, id::cutoffLfoAmount, id::cutoffLfoSpeed}},
    {"Clock", 0xff8dd6da, 3, {id::mode, id::clockDiv, id::swing, id::reset, id::steps, id::octave}}};

struct ConsoleLayout
{
    std::array<Rect, size_t(Item::Count)> tools{};
    std::array<Rect, std::size(sections)> groups{};
    std::array<Rect, slotCount> slots{};
    Rect toolbar, status, brand, logo, crest, counter, play, face, storm;
    Rect score, caption, grid;
    int headerHeight = 0, gutter = 0;

    void arrange(int width, int height)
    {
        const int margin = std::clamp(width / 110, 8, 22), gap = std::clamp(height / 100, 5, 13);
        const int usable = width - 2 * margin, toolHeight = std::clamp(height / 24, 24, 34);
        toolbar = {margin, margin, usable, toolHeight};
        int x = margin;
        auto tool = [&](Item item, int w)
        {
            tools[size_t(item)] = {x, margin, w, toolHeight};
            x += w + 3;
        };
        tool(Item::PresetPrev, 26);
        tool(Item::PresetBox, std::clamp(usable / 6, 140, 245));
        tool(Item::PresetNext, 26);
        const int statusLeft = x + 9;
        x = width - margin - 53 - 89 - 75 - 92 - 9;
        status = {statusLeft, margin, x - statusLeft - 10, toolHeight};
        tool(Item::Fine, 53);
        tool(Item::Learn, 89);
        tool(Item::ClearCc, 75);
        tool(Item::Motion, 92);

        const int y = margin + toolHeight + gap, h = std::clamp(height * 15 / 100, 72, 160);
        const int brandWidth = std::clamp(usable / 5, 184, 380), faceWidth = h * 4 / 5;
        brand = {margin, y, brandWidth, h};
        const int footer = std::clamp(h / 3, 24, 39);
        logo = {brand.x + 5, y + 3, brandWidth - 10, h - footer - 5};
        slots[id::seedBack] = {brand.x + brandWidth - std::clamp(brandWidth / 4, 48, 74), y + h - footer,
                               std::clamp(brandWidth / 4, 48, 74), footer};
        slots[id::seed] = {brand.x, y + h - footer, brandWidth - slots[id::seedBack].w - 2, footer};
        slots[id::regen] = {width - margin - faceWidth, y, faceWidth, h};
        face = {slots[id::regen].x + 3, y + 2, faceWidth - 6, h - footer + 1};
        const int playWidth = std::clamp(usable / 11, 86, 145);
        play = {slots[id::regen].x - gap - playWidth, y, playWidth, h};
        const int counterWidth = h * 100 / 90;
        counter = {play.x - counterWidth, y, counterWidth, h};
        crest = {brand.x + brandWidth + gap, y, counter.x - brand.x - brandWidth - 2 * gap, h};

        const int scoreY = y + h + gap, scoreH = std::clamp(height * 32 / 100, 158, 365);
        score = {margin, scoreY, usable, scoreH};
        const int captionH = std::clamp(scoreH / 7, 23, 38);
        caption = {margin, scoreY, usable, captionH};
        storm = {margin + usable - std::clamp(usable / 5, 150, 320) - 5, scoreY + 3,
                 std::clamp(usable / 5, 150, 320), captionH - 6};
        gutter = std::clamp(usable / 13, 86, 110);
        grid = {margin + gutter, scoreY + captionH, usable - gutter - 2, scoreH - captionH - 2};

        const int deskY = scoreY + scoreH + gap;
        headerHeight = std::clamp((height - margin - deskY) / 11, 20, 36);
        for (int i = 0; i < int(std::size(sections)); ++i)
        {
            const int gx = margin + (usable + gap) * (i % 3) / 3;
            const int gx1 = margin + (usable + gap) * (i % 3 + 1) / 3 - gap;
            const int row = i / 3, span = height - margin - deskY + gap;
            const int gy = deskY + span * row / 2, gy1 = deskY + span * (row + 1) / 2 - gap;
            groups[i] = {gx, gy, gx1 - gx, gy1 - gy};
            const int cols = sections[i].columns, top = gy + headerHeight;
            for (int j = 0; j < 2 * cols; ++j)
            {
                const int x0 = gx + 2 + (gx1 - gx - 4) * (j % cols) / cols,
                          x1 = gx + 2 + (gx1 - gx - 4) * (j % cols + 1) / cols;
                const int y0 = top + (gy1 - 2 - top) * (j / cols) / 2,
                          y1 = top + (gy1 - 2 - top) * (j / cols + 1) / 2;
                slots[sections[i].slots[j]] = {x0, y0, x1 - x0, y1 - y0};
            }
        }
    }

    int hitSlot(int x, int y) const
    {
        for (int i = 0; i < slotCount; ++i)
            if (slots[i].contains(x, y)) return i;
        return -1;
    }
    Rect lane(int i) const
    {
        // A single ruler above the pitch trace and its three switch lanes.
        constexpr int pct[]{0, 14, 57, 72, 86, 100};
        const int top = grid.y + grid.h * pct[i] / 100, end = grid.y + grid.h * pct[i + 1] / 100;
        return {grid.x, top, grid.w, end - top};
    }
    Rect column(int i, int count) const
    {
        const int x = grid.x + grid.w * i / count;
        return {x, grid.y, grid.x + grid.w * (i + 1) / count - x, grid.h};
    }
    int stepAt(int x, int count) const
    {
        if (x < grid.x || x >= grid.x + grid.w) return -1;
        return ((x - grid.x + 1) * count - 1) / grid.w;
    }
    Rect plotArea(int slot) const
    {
        const Rect r = slots[slot].inset(4);
        const int title = std::clamp(r.h / 3, 12, 21);
        return {r.x + 2, r.y + title, r.w - 4, r.h - title - 2};
    }
};
} // namespace hdslave_desktop
