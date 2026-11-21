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
            drew |= picture.region<320,25> (pixels, stride, whole, 0, 0, [&](Raster& r) { masthead (r); });
        if (group == 0)
        {
            if (page || parameters || patternChanged || transport)
                drew |= picture.region<320,63> (pixels, stride, whole, 0, 25, [&](Raster& r) { seedHead (r, h); });
            if (page || patternChanged || transport)
                drew |= picture.region<320,82> (pixels, stride, whole, 0, 88, [&](Raster& r) {
                    plots::sequence (r, view, 4, 0, 312, 80, true, h.playing);
                });
        }
        else
        {
            if (selection)
                drew |= picture.region<180,145> (pixels, stride, whole, 0, 25, [&](Raster& r) { list (r); });
            else if (parameters)
            {
                const int top = firstRow();
                for (int i = top; i < std::min (fields().count, top + visible); ++i)
                    if (parameters & bit (fields().fields[i].id))
                        drew |= picture.region<170,16> (pixels, stride, false, 4, 30 + (i - top) * rowH,
                                                        [&](Raster& r) { listRow (r, i, 0, 0); });
            }
            const bool panelChanged = group < 3 ? (parameters & bit (ParamId::cutoff)) != 0
                                                : (parameters || patternChanged || transport);
            if (page || panelChanged)
                drew |= picture.region<140,145> (pixels, stride, whole, 180, 25, [&](Raster& r) {
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
            drew |= picture.region<320,48> (pixels, stride, whole, 0, 170, [&](Raster& r) {
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

    void masthead (const Raster& r) const
    {
        using namespace hdui;
        for (int j = 0; j < 11; ++j)
            for (int i = 0; i < 28; ++i)
                r.dot (6 + i, 7 + j, art::inks[art::fold[j][i]]);
        for (size_t j = 0; j < sizeof (art::wordmark) / sizeof (art::wordmark[0]); ++j)
            for (size_t i = 0; i < sizeof (art::wordmark[0]); ++i)
                if (art::wordmark[j][i]) r.dot (42 + int (i), 8 + int (j), ink::paper);
        r.right (312, 7, fields().name, ink::paper);
        for (int i = 0; i < 5; ++i)
            r.box (279 + i * 7, 18, 5, 2, i == group ? ink::soft : ink::rule);
        r.box (6, 24, 308, 1, ink::rule);
    }

    void seedHead (const Raster& r, const Host& h) const
    {
        using namespace hdui;
        const unsigned dialled = unsigned (val (ParamId::seed));
        const int n = plots::stepCount (view);
        const bool pending = n && dialled != unsigned (view.seed);
        const bool notes = val (ParamId::mode) >= 0.5f;
        char s[48];
        std::snprintf (s, sizeof (s), "%s / %s", pending ? "NEXT" : "SEED",
                       val (ParamId::seedMode) >= 0.5f ? "LOCKED" : "RANDOM");
        r.text (8, 4, s, ink::muted);
        r.hex (5, 14, dialled, ink::paper);

        if (h.playing)
            for (int i = 0; i < 4; ++i) r.box (144 + i, 5 + i, 1, 7 - 2 * i, ink::soft);
        else r.box (144, 6, 5, 5, ink::muted);
        r.text (154, 4, h.playing ? "RUN" : "STOP", ink::paper);
        if (n && view.current >= 0 && view.current < n)
            std::snprintf (s, sizeof (s), "%02d/%02d", view.current + 1, n);
        else std::snprintf (s, sizeof (s), "--/%02d", int (val (ParamId::steps)));
        r.right (245, 4, s, ink::paper);
        formatted (ParamId::clockDiv, s);
        r.right (312, 4, s, ink::paper);

        if (pending)
        {
            std::snprintf (s, sizeof (s), "PENDING  /  SCORE %04X", unsigned (view.seed) & 65535);
            r.text (144, 17, s, ink::soft);
        }
        if (notes) r.text (144, pending ? 30 : 23, "MIDI NOTES / SEQ MUTED", ink::paper);
        else if (!pending)
        {
            r.text (144, 23, "TB-3PO", ink::muted);
            r.right (245, 23, "STEP", ink::muted);
            r.right (312, 23, "DIV", ink::muted);
        }
        r.box (6, 43, 308, 1, ink::grid);
        formatted (ParamId::scale, s);
        r.text (8, 50, s, ink::paper);
        std::snprintf (s, sizeof (s), "ROOT %s  OCT %+d  DEN %+d",
                       rootChoices[int (val (ParamId::root))], int (val (ParamId::octave)), int (val (ParamId::density)));
        r.right (312, 50, s, ink::muted);
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
            r.box (x, y, 2, rowH - 1, ink::soft);
        }
        char s[32];
        formatted (f.id, s);
        r.text (x + 5, y + 4, f.label, focus ? ink::paper : ink::muted);
        const int end = x + 164;
        if (focus && editing)
        {
            const int w = int (std::strlen (s)) * 7;
            r.frame (end - w - 3, y + 1, w + 6, 14, ink::paper);
        }
        r.right (end, y + 4, s, focus ? ink::paper : ink::soft);
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
        r.box (0, 4, r.w - 4, r.h - 9, ink::field);
        char s[32];
        r.text (8, 12, "SCORE", ink::muted);
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
            r.box (8, 117, split - 1, 4, ink::soft);
            r.box (8 + split + 1, 117, 118 - split, 4, ink::rule);
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
            r.box (x, 3, 102, 40, ink::field);
            r.text (x + 5, 10, top[k], ink::muted);
            r.text (x + 5, 26, bottom[k], ink::paper);
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
            r.box (x, 3, 102, 40, ink::card);
            char tag[] = { 'K', char ('1' + k), 0 };
            r.text (x + 4, 8, tag, ink::muted);
            if (id == noParam) continue;
            const auto& p = paramTable[paramIndex (id)];
            r.text (x + 23, 8, p.name, ink::paper);
            char s[32];
            formatted (id, s);
            if (p.kind == Kind::button)
            {
                r.text (x + 4, 23, k == 0 ? "Push" : "Button", ink::soft);
                continue;
            }
            // The list has room for every full scale name.
            if (std::strlen (s) > 13) std::memcpy (s + 11, "..", 3);
            r.text (x + 4, 21, s, ink::paper);
            const int start = x + 5, w = 92, rail = 35;
            const float unit = (val (id) - p.min) / (p.max - p.min);
            const int at = start + int (std::lround (unit * (w - 1)));
            r.box (start, rail, w, 2, ink::rule);
            r.box (start, rail, at - start + 1, 2, track);
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
