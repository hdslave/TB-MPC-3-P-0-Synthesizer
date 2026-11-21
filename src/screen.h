// TB-MPC-3-P-0 instrument screen. See docs/screen.md for its score and controls.
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

namespace tbmpc
{
class Screen final
{
    using Raster = hdui::Raster;
    using Host = hdui::Host;
    using Ink = hdui::Ink;

public:
    Screen (const std::atomic<float>* values, const PatternState& pattern) : values (values), pattern (pattern) {}

    void update (uint64_t) { view = pattern.read(); }

    // Keep sampling a moving score, including when Melodee suspends processing between clocks.
    bool animating (const Host& h) const { return (group == 0 || group >= 3) && h.playing && view.running; }

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
        const Param& p = paramTable[paramIndex (id)];
        if (key == Key::push)
        {
            if (p.kind == Kind::button) out.press (id);
            else editing = !editing;
        }
        else if (!editing && key == Key::wheel)
            row = int (std::clamp<int64_t> (int64_t (row) + amount, 0, fields().count - 1));
        else if (editing && (key == Key::wheel || key == Key::nudge))
        {
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
        whole |= lastGroup < 0;
        const bool page = whole || group != lastGroup;
        const bool selection = page || row != lastRow || editing != lastEditing;
        const bool patternChanged = !(lastView == view);
        const bool transport = lastHost.playing != h.playing;
        uint64_t parameters = 0;
        static_assert (ParamId::count <= 64);
        auto bit = [](uint32_t id) { return uint64_t (1) << id; };
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

        bool drew = false;
        if (page)
            drew |= picture.region<320,50> (pixels, stride, whole, 0, 0, [&](Raster& r) { masthead (r); });
        if (group == 0)
        {
            if (page || patternChanged || transport)
                drew |= score (h, pixels, stride, whole, page || transport);
            const bool publication = lastView.seed != view.seed || lastView.steps != view.steps;
            if (page || publication || (parameters & (bit (ParamId::seed) | bit (ParamId::seedMode))))
                drew |= picture.region<103,36> (pixels, stride, whole, 0, 155, [&](Raster& r) { seedReadout (r); });
            if (page || patternChanged || transport || parameters)
                drew |= picture.region<141,36> (pixels, stride, whole, 103, 155, [&](Raster& r) { readings (r, h); });
            if (page || patternChanged || transport || (parameters & (bit (ParamId::seed) | bit (ParamId::mode))))
                drew |= picture.region<76,36> (pixels, stride, whole, 244, 155, [&](Raster& r) { stepStatus (r, h); });
        }
        else
        {
            if (selection)
                drew |= picture.region<180,141> (pixels, stride, whole, 0, 50, [&](Raster& r) { list (r); });
            else if (parameters)
            {
                const int top = firstRow();
                for (int i = top; i < std::min (fields().count, top + visible); ++i)
                    if (parameters & bit (fields().fields[i].id))
                        drew |= picture.region<170,16> (pixels, stride, false, 4, 55 + (i - top) * rowH,
                                                        [&](Raster& r) { listRow (r, i, 0, 0); });
            }
            const bool panelChanged = group < 3 ? (parameters & bit (ParamId::cutoff)) != 0
                                                : (parameters || patternChanged || transport);
            if (page || panelChanged)
                drew |= picture.region<140,141> (pixels, stride, whole, 180, 50, [&](Raster& r) {
                    if (group < 3)
                    {
                        char s[32];
                        formatted (ParamId::cutoff, s);
                        plots::response (r, context(), val (ParamId::cutoff), s);
                    }
                    else mini (r, h);
                });
        }
        if (selection || parameters || pots || h.shift != lastHost.shift || h.accent != lastHost.accent)
            drew |= picture.region<320,27> (pixels, stride, whole, 0, 191, [&](Raster& r) {
                if (h.shift) legend (r);
                else cards (r, h);
            });
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
    int group = 0, row = 0, lastGroup = -1, lastRow = 0;
    bool editing = false, lastEditing = false;

    float liveValue (uint32_t id) const { return values[paramIndex (id)].load (std::memory_order_relaxed); }
    float val (uint32_t id) const { return sampled[paramIndex (id)]; }
    const hdui::Group& fields() const { return layout::groups[group]; }
    TextContext context() const { return { val (ParamId::resonance), val (ParamId::filter), val (ParamId::peakSpread) }; }

    template<size_t N> void formatted (uint32_t id, char (&text)[N]) const
    {
        formatParam (paramTable[paramIndex (id)], val (id), text, N, context());
    }

    uint32_t cardField (int k) const
    {
        constexpr uint32_t performance[] = { ParamId::cutoff, ParamId::resonance, ParamId::envMod };
        if (!group) return performance[k];
        return row + k < fields().count ? fields().fields[row + k].id : hdui::noParam;
    }

    bool score (const Host& h, uint32_t* pixels, int stride, bool whole, bool page)
    {
        // A step alone can change only the old and new columns, including the small notes
        // under dense phrases. Include the text overhang when comparing the retained frame.
        constexpr int x = 3, w = 314;
        int first = 0, end = hdui::width;
        auto previous = lastView;
        previous.current = view.current;
        const int n = plots::stepCount (view);
        if (!page && previous == view && lastView.current >= 0 && lastView.current < n &&
            view.current >= 0 && view.current < n)
        {
            const int columns = n <= 16 ? 16 : 32;
            auto edge = [&](int step) { return x + plots::pitchGutter + step * (w - plots::pitchGutter) / columns; };
            first = std::max (0, edge (std::min (lastView.current,view.current)) - 12);
            end = std::min (hdui::width, edge (std::max (lastView.current,view.current) + 1) + 12);
        }
        return picture.region<320,105> (pixels, stride, whole, 0, 50, [&](Raster& r) {
            plots::sequence (r, view, x, 0, w, 103, true, h.playing);
        }, first, end);
    }

    void masthead (const Raster& r) const
    {
        using namespace hdui;
        const auto& palette = group == 4 ? art::amber : (group == 1 || group == 2 ? art::mint : art::flyer);
        const bool checker = group >= 3;
        r.print (0, 0, checker ? art::checkers : art::groove, palette);
        r.mask (checker ? 10 : 159, 32, art::subtitles + 15 * group, 15, ink::bg);
        for (int i = 0; i < 5; ++i)
            r.box ((checker ? 120 : 285) + i * 6, 39, 4, 2, i == group ? palette[11] : ink::rule);
    }

    bool pending() const
    {
        return plots::stepCount (view) && unsigned (val (ParamId::seed)) != unsigned (view.seed);
    }

    void seedReadout (const Raster& r) const
    {
        using namespace hdui;
        char s[32];
        std::snprintf (s, sizeof (s), "%s / %s", pending() ? "NEXT" : "SEED",
                       val (ParamId::seedMode) >= .5f ? "LOCKED" : "RANDOM");
        r.box (3, 1, 97, 9, pending() ? ink::pink : ink::orange);
        r.text (6, 1, s, ink::bg);
        r.box (4, 10, 96, 24, pending() ? ink::soft : ink::live);
        r.box (3, 34, 97, 1, ink::pink);
        r.hex (4, 10, unsigned (val (ParamId::seed)), ink::bg);
    }

    void readings (const Raster& r, const Host& h) const
    {
        using namespace hdui;
        const int n = plots::stepCount (view);
        char s[48], div[12];
        formatted (ParamId::clockDiv, div);
        const char* state = h.playing ? "RUN" : "STOP";
        if (view.current >= 0 && view.current < n)
            std::snprintf (s, sizeof (s), "%s %02d/%02d  %s", state, view.current + 1, n, div);
        else std::snprintf (s, sizeof (s), "%s --/%02d  %s", state, int (val (ParamId::steps)), div);
        r.text (1, 1, s, ink::mint);
        std::snprintf (s, sizeof (s), "%s / %s", scaleNames[int (val (ParamId::scale))], rootChoices[int (val (ParamId::root))]);
        r.text (1, 13, s, ink::soft);
        std::snprintf (s, sizeof (s), "OCT %+d  DEN %+d", int (val (ParamId::octave)), int (val (ParamId::density)));
        r.text (1, 25, s, ink::muted);
    }

    void stepStatus (const Raster& r, const Host& h) const
    {
        using namespace hdui;
        const bool waiting = pending(), midi = val (ParamId::mode) >= .5f;
        if (!waiting && !midi)
        {
            const bool active = h.playing && view.running && view.current >= 0 && view.current < plots::stepCount (view);
            const uint32_t bit = active ? uint32_t (1) << view.current : 0;
            const char* labels[] = { "GATE", "ACC", "SLD" };
            const uint32_t flags[] = { view.gates, view.accents, view.slides };
            const Ink lamps[] = { ink::mint, ink::pink, ink::orange };
            for (int i = 0; i < 3; ++i)
            {
                const int y = 1 + 12 * i;
                const bool on = (flags[i] & bit) != 0;
                r.text (3, y, labels[i], on ? lamps[i] : ink::muted);
                r.box (40, y + 1, 31, 6, on ? lamps[i] : ink::card);
                r.caution (40, y + 1, 31, 6, on ? ink::field : ink::grid);
            }
            return;
        }
        int y = waiting && midi ? 0 : 5;
        if (waiting)
        {
            char s[16];
            std::snprintf (s, sizeof (s), "SCORE %04X", unsigned (view.seed) & 65535);
            r.text (2, y, "PENDING", ink::orange);
            r.text (2, y+9, s, ink::soft);
            y += 18;
        }
        if (midi)
        {
            r.text (2, y, "MIDI NOTES", ink::pink);
            r.text (2, y+9, "SEQ MUTED", ink::soft);
        }
    }

    static constexpr int visible = 8, rowH = 17;
    int firstRow() const { return std::clamp (row - 3, 0, std::max (0, fields().count - visible)); }

    void listRow (const Raster& r, int i, int x, int y) const
    {
        using namespace hdui;
        const auto& f = fields().fields[i];
        const bool focus = i == row;
        if (focus)
        {
            r.box (x, y, 170, rowH - 1, ink::selected);
            r.caution (x, y, 4, rowH - 1, ink::bg);
        }
        else r.box (x + 1, y + rowH - 2, 168, 1, ink::grid);
        char s[32];
        formatted (f.id, s);
        r.text (x + 6, y + 4, f.label, focus ? ink::bg : ink::muted);
        const int end = x + 164;
        if (focus && editing)
        {
            const int w = int (std::strlen (s)) * 7;
            r.box (end - w - 3, y + 1, w + 6, 14, ink::bg);
        }
        r.right (end, y + 4, s, focus ? (editing ? ink::live : ink::bg) : ink::soft);
    }

    void list (const Raster& r) const
    {
        using namespace hdui;
        const int top = firstRow();
        for (int i = top; i < std::min (fields().count, top + visible); ++i)
            listRow (r, i, 4, 5 + (i - top) * rowH);
        if (fields().count > visible)
        {
            const int extent = visible * rowH;
            r.box (176, 5, 1, extent, ink::grid);
            r.box (176, 5 + top * extent / fields().count, 1, visible * extent / fields().count, ink::muted);
        }
    }

    void mini (const Raster& r, const Host& h) const
    {
        using namespace hdui;
        plots::scopePanel (r, "SCORE");
        char s[32];
        if (plots::stepCount (view))
            std::snprintf (s, sizeof (s), "%04X", unsigned (view.seed) & 65535);
        else std::snprintf (s, sizeof (s), "----");
        r.right (128, 12, s, ink::paper);
        plots::sequence (r, view, 5, 31, 125, 52, false, h.playing);
        if (group == 4)
        {
            r.text (8, 95, "SWING", ink::muted);
            formatted (ParamId::swing, s);
            r.right (128, 95, s, ink::paper);
            const int split = int (std::lround (119 * val (ParamId::swing)));
            r.box (8, 117, split - 1, 4, ink::mint);
            r.box (8 + split + 1, 117, 118 - split, 4, ink::pink);
            r.dot (68, 124, ink::muted);
        }
        else
        {
            r.text (8, 95, h.playing ? "RUN" : "STOP", ink::muted);
            if (view.current >= 0 && view.current < plots::stepCount (view))
                std::snprintf (s, sizeof (s), "%02d/%02d", view.current + 1, view.steps);
            else std::snprintf (s, sizeof (s), "--/--");
            r.right (128, 95, s, ink::paper);
            r.text (8, 114, "SEED", ink::muted);
            formatted (ParamId::seedMode, s);
            r.right (128, 114, s, ink::soft);
        }
    }

    static void legend (const Raster& r, const char* const top[3], const char* const bottom[3])
    {
        using namespace hdui;
        for (int k = 0; k < 3; ++k)
        {
            const int x = 4 + 105 * k;
            r.box (x, 0, 102, 23, ink::field);
            r.box (x, 0, 102, 1, ink::pink);
            r.text (x + 5, 2, top[k], ink::pink);
            r.text (x + 5, 13, bottom[k], ink::paper);
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
            bottom[1] = editing ? "Finish edit" : "Edit / press";
            bottom[2] = editing ? "Coarse value" : "Group";
        }
        legend (r, top, bottom);
    }

    void cards (const Raster& r, const Host& h) const
    {
        using namespace hdui;
        const Ink track = h.accent ? h.accent | 0xff000000u : ink::live;
        for (int k = 0; k < 3; ++k)
        {
            const int x = 4 + 105 * k;
            const uint32_t id = cardField (k);
            r.box (x, 0, 102, 24, ink::card);
            r.box (x, 0, 102, 1, ink::grid);
            r.box (x, 1, 20, 10, ink::orange);
            char tag[] = { 'K', char ('1' + k), 0 };
            r.text (x + 3, 2, tag, ink::bg);
            if (id == noParam) continue;
            const auto& p = paramTable[paramIndex (id)];
            r.text (x + 23, 2, p.name, ink::paper);
            char s[32];
            formatted (id, s);
            if (p.kind == Kind::button)
            {
                r.text (x + 3, 12, k == 0 ? "Push" : "Button", ink::soft);
                continue;
            }
            // The list has room for every full scale name.
            if (std::strlen (s) > 13) std::memcpy (s + 11, "..", 3);
            r.text (x + 3, 12, s, ink::paper);
            const int start = x + 5, w = 92, rail = 21;
            const float unit = (val (id) - p.min) / (p.max - p.min);
            const int at = start + int (std::lround (unit * (w - 1)));
            r.box (start, rail, w, 1, ink::rule);
            r.box (start, rail, at - start + 1, 1, track);
            r.box (at, rail - 2, 2, 5, track);
            if (h.pot[k] >= 0 && !h.engaged[k])
            {
                const int pos = start + std::clamp (int (h.pot[k]), 0, 127) * (w - 1) / 127;
                r.box (pos - 2, rail - 2, 5, 6, ink::card);
                r.frame (pos - 2, rail - 2, 5, 6, ink::paper);
            }
        }
    }
};
} // namespace tbmpc
