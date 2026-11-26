// Slink instrument screen. See docs/screen.md for its score and controls.
#pragma once

#include "params.h"
#include "ui/groups.h"

#include <atomic>

namespace tbmpc
{
// The pattern as the screen shows it, written by the audio thread when it changes and read by the screen's thread:
// a sequence lock (the version is odd while it is being written, and a read that saw it change is read again), the
// fields relaxed atomics so neither side ever reads a torn word.
struct PatternView
{
    int32_t steps, current, seed, running;
    uint32_t gates, slides, accents;  // a bit a step
    int16_t pitch[32];                // each step's pitch CV, 1/128 semitone
    bool operator== (const PatternView& o) const { return std::memcmp (this, &o, sizeof (*this)) == 0; }
};
static_assert (sizeof (PatternView) == 92, "no padding: compared and copied as bytes");

class PatternState
{
public:
    void write (const PatternView& v)
    {
        const uint32_t at = version.load (std::memory_order_relaxed);
        version.store (at + 1, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        uint64_t words[words64] {};
        std::memcpy (words, &v, sizeof (v));
        for (int i = 0; i < words64; ++i)
            data[i].store (words[i], std::memory_order_relaxed);
        version.store (at + 2, std::memory_order_release);
    }
    PatternView read() const
    {
        PatternView v;
        for (;;)
        {
            const uint32_t before = version.load (std::memory_order_acquire);
            if (before & 1u)
                continue;
            uint64_t words[words64];
            for (int i = 0; i < words64; ++i)
                words[i] = data[i].load (std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_acquire);
            if (version.load (std::memory_order_relaxed) == before)
            {
                std::memcpy (&v, words, sizeof (v));
                return v;
            }
        }
    }

private:
    static constexpr int words64 = (int) ((sizeof (PatternView) + 7) / 8);
    std::atomic<uint32_t> version { 0 };
    std::atomic<uint64_t> data[words64] {};
};

} // namespace tbmpc

#include "ui/plots.h"
#include "ui/pitch_roll.h"

namespace tbmpc
{
class Screen final
{
    using Raster = hdui::Raster;
    using Host = hdui::Host;
    using Ink = hdui::Ink;

    enum class Theme { pressPlate, blacklight };
    struct Changes
    {
        bool page, selection, score, publication, position, panel;
        uint64_t parameters;
    };
    static uint64_t bit (uint32_t id) { return uint64_t (1) << id; }

public:
    Screen (const std::atomic<float>* values, const PatternState& pattern) : values (values), pattern (pattern) {}

    void update (uint64_t) { view = pattern.read(); }

    // Keep sampling a moving score, including when Melodee suspends processing between clocks.
    bool animating (const Host& h) const { return h.playing && view.running && plots::stepCount (view) > 0; }

    void knobs (uint32_t ids[3]) const
    {
        for (int k = 0; k < 3; ++k)
        {
            const auto id = cardField (k);
            ids[k] = id != hdui::noParam && paramTable[paramIndex (id)].kind != Kind::button ? id : hdui::noParam;
        }
    }

    bool input (hdui::Key key, int amount, bool shift, hdui::Edits& out)
    {
        using hdui::Key;
        if (key == Key::nudge && !editing)
        {
            const int next = std::clamp (group + (amount > 0) - (amount < 0), 0, 4);
            if (next == group) return false;
            group = next;
            row = 0;
            return true;
        }
        if (group == 0)
        {
            if (key == Key::push)
                out.press (shift ? ParamId::seedBack : ParamId::regen);
            else if (key == Key::wheel)
            {
                const unsigned seed = unsigned (liveValue (ParamId::seed)) + unsigned (amount) * (shift ? 16u : 1u);
                out.set (ParamId::seed, float (seed & 0xffffu));
                if (liveValue (ParamId::seedMode) < 0.5f) out.set (ParamId::seedMode, 1);
            }
            else return false;
            return true;
        }
        const auto id = fields().fields[row].id;
        if (key == Key::push && id == hdui::noParam)
        {
            theme = theme == Theme::pressPlate ? Theme::blacklight : Theme::pressPlate;
            return true;
        }
        if (key == Key::push)
        {
            if (paramTable[paramIndex (id)].kind == Kind::button) out.press (id);
            else editing = !editing;
        }
        else if (!editing && key == Key::wheel)
            row = int (std::clamp<int64_t> (int64_t (row) + amount, 0, fields().count - 1));
        else if (editing && (key == Key::wheel || key == Key::nudge))
        {
            const auto& p = paramTable[paramIndex (id)];
            const float delta = p.kind == Kind::continuous
                ? (p.max - p.min) / (key == Key::nudge ? 16 : (shift ? 1024 : 128)) : 1.f;
            const float before = liveValue (id);
            const float next = clampParam (p, before + delta * amount);
            if (next != before) out.set (id, next);
        }
        else return false;
        return true;
    }

    bool paint (const Host& h, uint32_t* pixels, int stride, bool whole)
    {
        whole |= lastGroup < 0 || theme != paintedTheme;
        picture.background = theme == Theme::blacklight ? hdui::ticketInk.bg : hdui::pressInk.bg;
        picture.smoothType = true;
        const bool page = whole || group != lastGroup;
        const bool selection = page || row != lastRow || editing != lastEditing;
        const bool patternChanged = !(lastView == view);
        const bool transport = lastHost.playing != h.playing;
        uint64_t parameters = 0;
        static_assert (ParamId::count <= 64);
        bool pots = false;
        for (unsigned i = 0; i < paramCount; ++i)
        {
            const float v = values[i].load (std::memory_order_relaxed);
            if (sampled[i] != v) parameters |= bit (paramTable[i].id);
            sampled[i] = v;
        }
        const auto responseParams = bit (ParamId::filter) | bit (ParamId::peakSpread) | bit (ParamId::resonance);
        if (parameters & responseParams) parameters |= bit (ParamId::cutoff); // Cutoff's reading follows the response.
        for (int k = 0; k < 3; ++k)
            pots |= lastHost.pot[k] != h.pot[k] || lastHost.engaged[k] != h.engaged[k];

        const bool publication = lastView.seed != view.seed || lastView.steps != view.steps;
        const bool position = page || transport || publication || lastView.current != view.current
                              || lastView.running != view.running;
        const int slip = dripOffset (h);
        const bool panel = group < 3 ? (parameters & bit (ParamId::cutoff)) != 0
                                     : (parameters || patternChanged || transport);
        const Changes changed { page, selection, page || transport || patternChanged, publication, position, panel, parameters };
        bool drew = theme == Theme::blacklight ? paintBlacklight (pixels, stride, whole, h, slip, changed)
                                               : paintPressPlate (pixels, stride, whole, h, slip, changed);
        if (selection || parameters || pots || h.shift != lastHost.shift || h.accent != lastHost.accent)
            drew |= picture.region<320,33> (pixels, stride, whole, 0, 185, [&](Raster& r) {
                if (theme == Theme::blacklight) ticketCardBase (r);
                if (h.shift) legend (r);
                else if (themeRow()) themeHint (r);
                else if (theme == Theme::blacklight) ticketCards (r, h);
                else cards (r, h);
            });
        lastSlip = slip;
        paintedTheme = theme;
        lastGroup = group;
        lastRow = row;
        lastEditing = editing;
        lastView = view;
        lastHost = h;
        return drew;
    }

private:
    const std::atomic<float>* values;
    const PatternState& pattern;
    hdui::Picture picture;
    float sampled[paramCount] {};
    PatternView view {}, lastView {};
    Host lastHost {};
    Theme theme = Theme::pressPlate, paintedTheme = Theme::pressPlate;
    int group = 0, row = 0, lastGroup = -1, lastRow = 0;
    bool editing = false, lastEditing = false;
    bool pulse = false;
    uint64_t pulseStart = 0;
    int lastSlip = 0;

    float liveValue (uint32_t id) const { return values[paramIndex (id)].load (std::memory_order_relaxed); }
    float val (uint32_t id) const { return sampled[paramIndex (id)]; }
    const hdui::Group& fields() const { return layout::groups[group]; }
    TextContext context() const { return { val (ParamId::resonance), val (ParamId::filter), val (ParamId::peakSpread) }; }

    template<size_t N> void formatted (uint32_t id, char (&text)[N]) const
    {
        if (id == hdui::noParam)
            std::snprintf (text, N, "%s", theme == Theme::pressPlate ? "Press Plate" : "Blacklight Transfer");
        else formatParam (paramTable[paramIndex (id)], val (id), text, N, context());
    }

    bool pendingSeed() const
    {
        return plots::stepCount (view) && unsigned (val (ParamId::seed)) != unsigned (view.seed);
    }

    bool themeRow() const { return group != 0 && fields().fields[row].id == hdui::noParam; }

    void themeHint (const Raster& r) const
    {
        using namespace hdui;
        if (theme == Theme::pressPlate)
        {
            r.surface (0, 2, 320, 31, silver::cardMetal);
            r.text (8, 12, "PUSH TO SWITCH", ink::bg);
            r.right (312, 12, "Blacklight Transfer", ink::bg);
        }
        else
        {
            r.box (4, 0, 312, 1, ticketInk.violet);
            r.text (8, 11, "PUSH TO SWITCH", ticketInk.muted);
            r.right (312, 11, "Press Plate", ticketInk.paper);
        }
    }

    uint32_t cardField (int k) const
    {
        if (!group) return layout::performance[k].id;
        return row + k < fields().count ? fields().fields[row + k].id : hdui::noParam;
    }

    int dripOffset (const Host& h)
    {
        const int step = view.current;
        if (!animating (h) || step < 0 || step >= plots::stepCount (view))
        {
            pulse = false;
            return 0;
        }
        const bool onset = lastGroup < 0 || !lastHost.playing || !lastView.running || step != lastView.current
                           || lastView.seed != view.seed || lastView.steps != view.steps;
        if (onset && ((step % 4 == 0) || (((view.gates & view.accents) >> step) & 1u))
            && (!pulse || h.nowMs < pulseStart || h.nowMs - pulseStart >= 144))
        {
            pulse = true;
            pulseStart = h.nowMs;
        }
        if (!pulse) return 0;
        const uint64_t age = h.nowMs >= pulseStart ? h.nowMs - pulseStart : 144;
        if (age < 36) return 1;
        if (age < 84) return 2;
        if (age < 144) return 1;
        pulse = false;
        return 0;
    }

    static constexpr int titleHeight = int (std::size (hdui::silver::mpcPlate));

    bool paintPressPlate (uint32_t* pixels, int stride, bool whole, const Host& h, int slip, const Changes& d)
    {
        bool drew = false;
        if (d.page || (d.parameters & bit (ParamId::mode)))
            drew |= picture.region<320,titleHeight> (pixels, stride, whole, 0, 0, [&](Raster& r) { title (r); });
        if (group == 0)
        {
            if (d.position || slip != lastSlip || (d.parameters & (bit (ParamId::seed) | bit (ParamId::seedMode) | bit (ParamId::steps))))
                drew |= picture.region<320,82-titleHeight> (pixels, stride, whole, 0, titleHeight, [&](Raster& r) { patternPlate (r, h, slip); });
            if (d.page || (d.parameters & (bit (ParamId::root) | bit (ParamId::scale) | bit (ParamId::clockDiv)
                                    | bit (ParamId::octave) | bit (ParamId::density))))
                drew |= picture.region<320,16> (pixels, stride, whole, 0, 82, [&](Raster& r) { metadata (r); });
            if (d.score)
                drew |= picture.region<320,87> (pixels, stride, whole, 0, 98, [&](Raster& r) {
                    plots::matrix (r, view, h.playing);
                });
        }
        else
        {
            if (d.selection || slip != lastSlip || (!themeRow() && (d.parameters & bit (fields().fields[row].id))))
                drew |= picture.region<320,60-titleHeight> (pixels, stride, whole, 0, titleHeight, [&](Raster& r) { editorPlate (r, slip); });
            if (group == 4)
            {
                if (d.selection || d.parameters)
                    drew |= picture.region<320,37> (pixels, stride, whole, 0, 60, [&](Raster& r) { clockControls (r); });
                if (d.page || d.panel)
                    drew |= picture.region<320,88> (pixels, stride, whole, 0, 97, [&](Raster& r) { mini (r, h); });
            }
            else
            {
                constexpr int listW = 144;
                if (d.selection || d.parameters)
                    drew |= picture.region<listW,125> (pixels, stride, whole, 0, 60, [&](Raster& r) { list (r); });
                if (d.page || d.panel)
                    drew |= picture.region<320-listW,125> (pixels, stride, whole, listW, 60, [&](Raster& r) {
                        if (group < 3)
                        {
                            char s[32];
                            formatted (ParamId::cutoff, s);
                            plots::response (r, context(), val (ParamId::cutoff), s);
                        }
                        else mini (r, h);
                    });
            }
        }
        return drew;
    }

    void title (const Raster& r) const
    {
        using namespace hdui;
        // The smiley crosses the title/status boundary on the right of the slanted nameplate.
        if (group == 0) r.stamp (0, 18, art::hero, art::palette);
        else r.stamp (0, 18, art::editor, art::palette);
        r.box (0, 0, 320, 18, ink::bg);
        constexpr int width = int (sizeof silver::mpcPlate[0]);
        // Inset the bottom edge one row, exposing the readout background beneath it.
        constexpr int bottom = titleHeight - 2;
        for (int y = 0; y <= bottom; ++y)
        {
            const int edge = width - 2*y/3;
            const auto& row = silver::mpcPlate[y == bottom ? titleHeight - 1 : y];
            for (int x = 0; x < edge; ++x)
                r.px[y*r.w+x] = silver::mpcPlatePalette[row[x]];
            r.box (edge, y, 3, 1, ink::cobalt);
        }
        if (!group && val (ParamId::mode) >= .5f)
        {
            const auto& face = pressType::compact;
            constexpr int end = 316;
            const char* status = "MIDI / SEQ MUTED";
            r.faceText (end-r.faceWidth (fields().name, face), 1, end, fields().name, ink::paper, face);
            r.faceText (end-r.faceWidth (status, face), 10, end, status, ink::paper, face);
        }
        else r.right (315, 5, fields().name, ink::paper);
    }

    void patternPlate (const Raster& r, const Host& h, int slip) const
    {
        using namespace hdui;
        r.stamp (0, 18-titleHeight, art::hero, art::palette);
        if (slip)
        {
            r.slip (254, 65-titleHeight, 41, 3, slip);
            r.slip (254, 71-titleHeight, 41, 2, -slip);
        }
        const int n = plots::stepCount (view);
        const unsigned seed = unsigned (val (ParamId::seed));
        const bool pending = pendingSeed();
        for (int i = 0; i < 4; ++i)
            r.coverageGlyph (22+i*20, 36-titleHeight, r.w, pressType::seed[(seed >> (12-4*i)) & 15], 27, ink::paper);
        r.text (108, pending ? 1 : 3, pending ? "PENDING" : "SEED", pending ? ink::violet : ink::steel);
        r.text (108, pending ? 11 : 17, val (ParamId::seedMode) >= .5f ? "LOCKED" : "RANDOM", ink::live);
        constexpr int transportX = 184;
        if (h.playing)
            for (int i = 0; i < 5; ++i) r.box (transportX+1+i, 2+i/2, 1, 8-i, ink::live);
        else r.box (transportX, 3, 6, 6, ink::steel);
        r.text (transportX+10, 2, h.playing ? "RUN" : "STOP", h.playing ? ink::live : ink::steel);
        char s[24];
        if (view.current >= 0 && view.current < n)
            std::snprintf (s, sizeof (s), "%02d/%02d", view.current+1, n);
        else std::snprintf (s, sizeof (s), "--/%02d", n ? n : int (val (ParamId::steps)));
        r.reading (transportX, 10, 241, s, ink::paper, true);
        if (pending)
        {
            std::snprintf (s, sizeof (s), "SCORE %04X", unsigned (view.seed) & 65535);
            r.text (108, 21, s, ink::paper);
        }
    }

    void metadata (const Raster& r) const
    {
        using namespace hdui;
        r.box (0, 0, r.w, r.h, ink::field);
        char s[48];
        std::snprintf (s, sizeof (s), "%s / %s", scaleNames[int (val (ParamId::scale))], rootChoices[int (val (ParamId::root))]);
        r.text (5, 4, s, ink::paper);
        std::snprintf (s, sizeof (s), "OCT %+d", int (val (ParamId::octave)));
        r.text (137, 4, s, ink::muted);
        std::snprintf (s, sizeof (s), "DEN %+d", int (val (ParamId::density)));
        r.text (189, 4, s, ink::muted);
        char div[12];
        formatted (ParamId::clockDiv, div);
        std::snprintf (s, sizeof (s), "DIV %s", div);
        r.right (315, 4, s, ink::paper);
    }

    void editorPlate (const Raster& r, int slip) const
    {
        using namespace hdui;
        r.stamp (0, 18-titleHeight, art::editor, art::palette);
        if (slip)
        {
            r.slip (276, 49-titleHeight, 27, 2, slip);
            r.slip (276, 53-titleHeight, 27, 1, -slip);
        }
        const auto& f = fields().fields[row];
        char value[32];
        formatted (f.id, value);
        constexpr int left = 16, end = 257;
        const int labelEnd = r.text (left, 3, f.label, ink::live);
        const bool large = r.readingWidth (value, true) <= end-labelEnd-10;
        const int valueX = end - r.readingWidth (value, large);
        r.reading (valueX, large ? -1 : 0, end, value, ink::paper, large);
        if (editing) r.box (left, 12, labelEnd-left-1, 1, ink::violet);
        if (f.id == noParam) return;
        const auto& p = paramTable[paramIndex (f.id)];
        const int rail = labelEnd + 12, width = valueX - 12 - rail;
        if (p.kind == Kind::button || width < 24) return;
        const int at = int (std::lround ((width-1)*(val (f.id)-p.min)/(p.max-p.min)));
        r.box (rail, 7, width, 2, ink::card);
        r.box (rail, 7, at+1, 2, ink::violet);
        r.box (rail+at-1, 5, 3, 6, ink::paper);
    }

    static constexpr int visible = 8, rowH = 15;
    int firstRow() const { return std::clamp (row - 3, 0, std::max (0, fields().count - visible)); }

    void list (const Raster& r) const
    {
        using namespace hdui;
        const int top = firstRow();
        for (int i = top; i < std::min (fields().count, top + visible); ++i)
        {
            const int y = 2 + (i-top)*rowH;
            const auto& f = fields().fields[i];
            const bool focus = i == row;
            r.surface (2, y, r.w-4, rowH-1, focus ? ink::live : (i-top)%2 ? ink::alternate : ink::metal);
            char value[32];
            formatted (f.id, value);
            const int labelEnd = r.text (7, y+3, f.label, ink::bg);
            const int right = r.w-7;
            const auto& face = r.faceWidth (value, pressType::label) <= right-labelEnd-3
                             ? pressType::label : pressType::caption;
            const int valueX = right-r.faceWidth (value, face);
            if (focus && editing) r.box (valueX-2, y+1, right-valueX+5, 12, ink::bg);
            r.faceText (valueX, y+3, right, value, focus && editing ? ink::live : ink::bg, face);
        }
        if (fields().count > visible)
            r.box (r.w-1, 2 + top * 120 / fields().count, 1, visible * 120 / fields().count, ink::steel);
    }

    void clockControls (const Raster& r) const
    {
        using namespace hdui;
        for (int i = 0; i < fields().count; ++i)
        {
            const int x = i*r.w/fields().count + 2, end = (i+1)*r.w/fields().count;
            const bool focus = i == row;
            r.surface (x, 2, end-x-1, r.h-4, focus ? ink::live : ink::metal);
            const auto& f = fields().fields[i];
            r.text (x+5, 5, f.label, ink::bg);
            char value[32];
            formatted (f.id, value);
            if (focus && editing) r.box (x+2, 15, end-x-5, 18, ink::bg);
            const Ink color = focus && editing ? ink::live : ink::bg;
            r.fittedValue (x+4, 15, end-2, value, color);
        }
    }

    void mini (const Raster& r, const Host& h) const
    {
        using namespace hdui;
        plots::scopePanel (r, "SCORE");
        char s[32];
        char seed[8];
        if (plots::stepCount (view)) std::snprintf (seed, sizeof (seed), "%04X", unsigned (view.seed) & 65535);
        else std::snprintf (seed, sizeof (seed), "----");
        std::snprintf (s, sizeof (s), "%s  %s", seedModeChoices[int (val (ParamId::seedMode))], seed);
        r.right (r.w-8, 5, s, ink::paper);
        plots::sequence (r, view, 7, 19, r.w-14, r.h-42, h.playing, pressInk);
        const int footer = r.h-18;
        if (group == 4)
        {
            r.text (8, footer, "SWING", ink::muted);
            formatted (ParamId::swing, s);
            r.right (r.w-8, footer, s, ink::paper);
            constexpr int left = 48;
            const int width = r.w-2*left;
            const int split = int (std::lround (width*val (ParamId::swing)));
            r.box (left, footer+2, split-1, 4, ink::live);
            r.box (left+split+1, footer+2, width-split-1, 4, ink::pink);
            r.box (left+width/2, footer+7, 1, 3, ink::muted);
        }
        else
        {
            r.text (8, footer, h.playing ? "RUN" : "STOP", h.playing ? ink::live : ink::muted);
            if (view.current >= 0 && view.current < plots::stepCount (view))
                std::snprintf (s, sizeof (s), "%02d/%02d", view.current + 1, view.steps);
            else std::snprintf (s, sizeof (s), "--/--");
            r.right (r.w-8, footer, s, ink::paper);
        }
    }

    void legend (const Raster& r, const char* const top[3], const char* const bottom[3]) const
    {
        using namespace hdui;
        for (int k = 0; k < 3; ++k)
        {
            const int x = 320*k/3, w = 320*(k+1)/3-x;
            const bool ticket = theme == Theme::blacklight;
            if (ticket) r.box (x+2, 0, w-4, 1, ticketInk.violet);
            else r.surface (x+1, 2, w-1, 31, silver::cardMetal);
            r.text (x+5, 5, top[k], ticket ? ticketInk.muted : ink::bg);
            if (ticket) r.reading (x+4, 14, x+w-2, bottom[k], ticketInk.paper);
            else r.fittedValue (x+4, 14, x+w-2, bottom[k], ink::bg);
        }
    }

    void legend (const Raster& r) const
    {
        const char* top[] = { "SHIFT + WHEEL", "SHIFT + PUSH", "MINUS / PLUS" };
        const char* bottom[] = { "Seed x16", "Seed Back", "Group" };
        if (group)
        {
            top[1] = "PUSH";
            bottom[0] = editing ? "Fine value" : "Move cursor";
            bottom[1] = themeRow() ? "Switch theme" : editing ? "Finish edit" : "Edit / press";
            bottom[2] = editing ? "Coarse value" : "Group";
        }
        legend (r, top, bottom);
    }

    void cards (const Raster& r, const Host& h) const
    {
        using namespace hdui;
        const Ink track = h.accent ? h.accent | 0xff000000u : ink::cobalt;
        for (int k = 0; k < 3; ++k)
        {
            const int x = 320*k/3, end = 320*(k+1)/3, cx = end-16, cy = 16;
            r.surface (x, 2, end-x, 31, silver::cardMetal);
            if (k) r.box (x, 2, 1, 31, ink::rule);
            const uint32_t id = cardField (k);
            if (id == noParam) continue;
            const auto& p = paramTable[paramIndex (id)];
            const char* name = group ? fields().fields[row+k].label : layout::performance[k].label;
            if (id == ParamId::accentDecay) name = "ACC DECAY";
            if (id == ParamId::peakSpread) name = "PEAK SPRD";
            char label[24];
            std::snprintf (label, sizeof (label), "K%d %s", k+1, name);
            const int textEnd = cx-14;
            const auto& labelFace = r.faceWidth (label, pressType::caption) <= textEnd-x-3
                                  ? pressType::caption : pressType::compact;
            r.faceText (x+3, 5, textEnd, label, ink::bg, labelFace);
            char s[32];
            formatted (id, s);
            if (p.kind == Kind::button)
            {
                r.frame (cx-7, cy-7, 15, 15, ink::rule);
                r.reading (x+3, 14, cx-14, k == 0 ? "Push" : "Button", ink::bg);
                continue;
            }
            r.fittedValue (x+3, 14, textEnd, s, ink::bg);
            const int angle = std::clamp (int (std::lround (48*(val (id)-p.min)/(p.max-p.min))), 0, 48);
            constexpr int dialSize = int (sizeof silver::mpcDialRows[0]);
            for (int j = 0; j < dialSize; ++j)
            {
                const auto& rowPixels = silver::mpcDialRows[silver::mpcDial[angle][j]];
                Ink* dest = r.px + (cy-dialSize/2+j)*r.w + cx-dialSize/2;
                constexpr auto backing = silver::mpcDialRows[silver::mpcDial[0][0]][0];
                for (int i = 0; i < dialSize; ++i)
                    if (rowPixels[i] != backing) dest[i] = silver::palette[rowPixels[i]];
            }
            r.box (cx-3, cy+14, 7, 2, track);
            if (h.pot[k] >= 0 && !h.engaged[k])
            {
                const int pos = std::clamp (int (h.pot[k]), 0, 127) * 48 / 127;
                const int mx = cx + int (std::lround (art::dial[pos][0]*12 / 64.f));
                const int my = cy + int (std::lround (art::dial[pos][1]*12 / 64.f));
                r.box (mx-2, my-2, 5, 5, ink::bg);
                r.frame (mx-2, my-2, 5, 5, ink::paper);
            }
        }
    }

    bool paintBlacklight (uint32_t* pixels, int stride, bool whole, const Host& h, int slip, const Changes& d);
    void ticketTitle (const Raster& r) const;
    static void passArt (const Raster& r, int slip);
    void passData (const Raster& r) const;
    void transportRow (const Raster& r, const Host& h) const;
    void ticketList (const Raster& r) const;
    void ticketMini (const Raster& r, const Host& h) const;
    static void ticketCardBase (const Raster& r);
    void ticketCards (const Raster& r, const Host& h) const;
};
} // namespace tbmpc

#include "ui/blacklight.h"
