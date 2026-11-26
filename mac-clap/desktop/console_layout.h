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
    enum Layout { grid, filterPad, lfoPads };
    const char* name;
    uint32_t ink;
    int columns;
    std::array<int, 6> slots;
    Layout layout = grid;
};
inline constexpr Section sections[] = {
    {"PERFORM", 0xffb6c8fa, 3, {id::cutoff, id::resonance, id::envMod, id::decay, id::accent, id::volume}},
    {"VOICE", 0xffaabfff, 3, {id::waveform, id::tuning, id::drive, id::filter, id::peakSpread, filterPlot}, Section::filterPad},
    {"GLIDE", 0xffb6c8fa, 2, {id::glide, id::slideTime, id::gate, id::accentDecay, -1, -1}},
    {"LFO", 0xffc3b0ed, 3, {pitchPlot, id::pitchLfoAmount, id::pitchLfoSpeed,
                              cutoffPlot, id::cutoffLfoAmount, id::cutoffLfoSpeed}, Section::lfoPads},
    {"PATTERN", 0xffb6c8fa, 3, {id::seedMode, id::mutate, id::density, id::root, id::scale, id::transpose}},
    {"CLOCK", 0xffb6c8fa, 3, {id::mode, id::clockDiv, id::swing, id::reset, id::steps, id::octave}}};
inline constexpr int ribbonParams[][2] = {{id::cutoff, id::resonance}, {id::waveform, id::filter},
    {id::glide, id::slideTime}, {id::pitchLfoAmount, id::cutoffLfoAmount}, {id::density, id::scale},
    {id::clockDiv, id::swing}};

struct ConsoleLayout
{
    std::array<Rect, size_t(Item::Count)> tools{};
    std::array<Rect, std::size(sections)> groups{};
    std::array<Rect, slotCount> slots{};
    std::array<Rect, 2> ribbon{}, keyFields{};
    Rect toolbar, status, mast, logo, crest, face, counter, play;
    Rect caption, score, grid, footer, keyScale, storm, print, legend;
    int headerHeight = 0, gutter = 0;

    // Keep the seed and transport panels at the hardware aspect ratio. The camera bay takes
    // the extra width in a wide, shallow host window. Pointer targets use the very same map.
    Rect headRect(Rect source) const
    {
        constexpr int left = 182, right = 522, nativeWidth = 780;
        const auto scale = [&](int v) { return (v * mast.h + 50) / 100; };
        const auto x = [&](int v) {
            if (v <= left) return scale(v);
            if (v >= right) return mast.w - scale(nativeWidth - v);
            const int span = mast.w - scale(left) - scale(nativeWidth - right);
            return scale(left) + (span * (v - left) + (right - left) / 2) / (right - left);
        };
        return {mast.x + x(source.x), mast.y + scale(source.y), x(source.x + source.w) - x(source.x),
                scale(source.y + source.h) - scale(source.y)};
    }

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

        const int y = margin + toolHeight + 1;
        mast = {0, y, width, std::min((width * 100 + 390) / 780, std::clamp(height * 23 / 100, 118, 280))};
        logo = headRect({9, 11, 166, 29});
        crest = headRect({185, 5, 334, 92});
        face = headRect({717, 0, 61, 58});
        counter = headRect({528, 20, 91, 70});
        play = headRect({622, 55, 81, 29});
        slots[id::seedBack] = headRect({7, 50, 64, 46});
        slots[id::seed] = headRect({71, 50, 107, 46});
        slots[id::regen] = headRect({716, 0, 63, 96});

        const int captionH = std::clamp(height / 23, 24, 42);
        caption = {margin, y + mast.h, usable, captionH};
        const int readW = usable * 24 / 100;
        for (int i = 0; i < 2; ++i)
            ribbon[i] = {margin + i * readW, caption.y + 1, readW - 4, captionH - 3};
        const int legendW = std::clamp(usable / 4, 225, 288);
        legend = {margin + usable - legendW, caption.y + 1, legendW, captionH - 3};

        score = {margin, caption.y + captionH + 2, usable, std::clamp(height * 24 / 100, 130, 370)};
        gutter = std::clamp(usable * 74 / 1000, 66, 104);
        grid = {margin + gutter, score.y, usable - gutter, score.h};

        const int footerH = std::clamp(height / 16, 33, 60);
        footer = {margin, score.y + score.h + 3, usable, footerH};
        const int printLeft = margin + usable * 38 / 100, keyW = usable * 15 / 100;
        keyScale = {margin, footer.y + 3, keyW - 4, footerH - 6};
        const int rootW = std::clamp(keyScale.w / 4, 34, 45);
        keyFields[0] = {keyScale.x, keyScale.y, rootW, keyScale.h};
        keyFields[1] = {keyScale.x + rootW, keyScale.y, keyScale.w - rootW, keyScale.h};
        storm = {margin + keyW, footer.y + 4, printLeft - margin - keyW - 6, footerH - 8};
        print = {printLeft, footer.y + 1, margin + usable - printLeft, footerH - 2};

        const int deskY = footer.y + footer.h + gap;
        headerHeight = std::clamp((height - margin - deskY) / 11, 20, 36);
        for (int i = 0; i < int(std::size(sections)); ++i)
        {
            const int gx = margin + (usable + gap) * (i % 3) / 3;
            const int gx1 = margin + (usable + gap) * (i % 3 + 1) / 3 - gap;
            const int row = i / 3, span = height - margin - deskY + gap;
            const int gy = deskY + span * row / 2, gy1 = deskY + span * (row + 1) / 2 - gap;
            groups[i] = {gx, gy, gx1 - gx, gy1 - gy};
            const int top = gy + headerHeight;
            arrangeControls(sections[i], {gx + 2, top, gx1 - gx - 4, gy1 - top - 2});
        }
    }

    void arrangeControls(const Section& section, Rect body)
    {
        const auto column = [](Rect r, int i, int count) {
            const int x = r.w * i / count;
            return Rect{r.x + x, r.y, r.w * (i + 1) / count - x, r.h};
        };
        if (section.layout == Section::lfoPads)
        {
            // Each LFO has its own pad with depth and rate directly underneath.
            const int controlsH = std::max(22, body.h * 40 / 100);
            for (int i = 0; i < 2; ++i)
            {
                const Rect unit = column(body, i, 2);
                const int padH = unit.h - controlsH;
                const Rect controls{unit.x, unit.y + padH, unit.w, controlsH};
                slots[section.slots[3*i]] = {unit.x, unit.y, unit.w, padH};
                slots[section.slots[3*i+1]] = column(controls, 0, 2);
                slots[section.slots[3*i+2]] = column(controls, 1, 2);
            }
            return;
        }
        const bool filter = section.layout == Section::filterPad;
        const int topH = filter ? std::max(22, body.h * 42 / 100) : body.h / 2;
        const Rect top{body.x, body.y, body.w, topH};
        Rect lower{body.x, body.y + topH, body.w, body.h - topH};
        if (filter)
        {
            // The filter and spread readouts share the left half of the plot row.
            lower.w = body.w * 54 / 100;
            slots[filterPlot] = {body.x + lower.w, lower.y, body.w - lower.w, lower.h};
        }
        for (int j = 0; j < section.columns; ++j)
            slots[section.slots[j]] = column(top, j, section.columns);
        const int readouts = section.columns - int(filter);
        for (int j = 0; j < readouts; ++j)
            slots[section.slots[section.columns+j]] = column(lower, j, readouts);
    }

    Rect heading(size_t bank) const
    {
        const Rect group = groups[bank];
        return {group.x + 1, group.y, group.w - 2, headerHeight - 1};
    }
    int groupAt(int x, int y) const
    {
        for (size_t i = 0; i < groups.size(); ++i)
            if (groups[i].contains(x, y)) return int(i);
        return -1;
    }
    int hitSlot(int x, int y, int bank = 0, Rect* anchor = nullptr) const
    {
        const auto hit = [&](int slot, Rect r) {
            if (anchor) *anchor = r;
            return slot;
        };
        for (int i = 0; i < 2; ++i)
        {
            if (ribbon[i].contains(x, y)) return hit(ribbonParams[bank][i], ribbon[i]);
            if (keyFields[i].contains(x, y)) return hit(i ? id::scale : id::root, keyFields[i]);
        }
        for (int i = 0; i < slotCount; ++i)
            if (slots[i].contains(x, y)) return hit(i, slots[i]);
        return -1;
    }
    Rect lane(int i) const
    {
        // A single ruler above the pitch trace and its three switch lanes.
        constexpr int pct[]{0, 13, 62, 75, 88, 100};
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
        const Rect r = slots[slot];
        const int title = std::clamp(r.h / 4, 13, 21);
        return {r.x + 5, r.y + title, r.w - 10, r.h - title - 4};
    }
};
} // namespace hdslave_desktop
