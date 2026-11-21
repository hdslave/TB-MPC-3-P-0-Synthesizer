// TB-MPC-3-P-0's page on Melodee's screen (melodee_screen.h, ui/page.h). PATTERN is TB-3PO's own screen: the seed in
// hex, locked or random, and the pattern's steps (a gated step a bar as high as its pitch, accents bright, slides
// joined to the next, the step playing lit). There the wheel dials the seed (and locks it, so the pattern dialled
// stays), Push is Regen, Shift + Push is Seed Back, and K1-K3 are the 303's hands-on three: Cutoff, Resonance, Env
// Mod. VOICE and TONE list the voice's knobs beside the filter's response; TB-3PO and CLOCK the sequencer's, beside
// the pattern.
#pragma once

#include "params.h"
#include "ui/page.h"

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

class Screen final : public hdui::Page
{
public:
    Screen (const std::atomic<float>* values, const PatternState& pattern) : values (values), pattern (pattern)
    {
        colours = { 0xff0d110f, 0xff151d18, 0xff1f2b23, 0xff2f4a38, 0xffe8f5e0, 0xff8faf96, 0xff3f5a48, 0xffc8f031, 0xff0d110f };
    }

    bool animating (const hdui::Host& h) const override { return flashAt != 0 && h.nowMs - flashAt < flashMs; }

    // The audio thread's news, taken before each paint: a new step lights.
    void update (uint64_t nowMs)
    {
        view = pattern.read();
        if (view.current != seenStep && view.running)
            flashAt = nowMs;
        seenStep = view.current;
    }

protected:
    int groupCount() const override { return (int) (sizeof (groups) / sizeof (groups[0])); }
    const hdui::Group& groupAt (int i) const override { return groups[i]; }
    const char* title() const override { return "TB-MPC-3-P-0"; }

    float value (uint32_t id) const { return values[paramIndex (id)].load (std::memory_order_relaxed); }
    TextContext context() const
    {
        TextContext c;
        c.resonance = value (ParamId::resonance), c.filter = value (ParamId::filter), c.spread = value (ParamId::peakSpread);
        return c;
    }

    void read (uint32_t id, hdui::Value& out) const override
    {
        const int index = paramIndex (id);
        if (index < 0)
            return;
        const Param& p = paramTable[index];
        out.value = value (id);
        out.min = p.min, out.max = p.max, out.def = p.def;
        out.kind = p.kind == Kind::continuous ? hdui::Kind::continuous
                 : p.kind == Kind::integer   ? hdui::Kind::stepped
                 : p.kind == Kind::choice    ? hdui::Kind::choice
                                             : hdui::Kind::button;
        out.bipolar = p.min < 0.0f;
        formatParam (p, out.value, out.text, sizeof (out.text), context());
    }

    const char* customLabel (uint32_t) const override { return ""; }

    // PATTERN: the wheel dials the seed (locking it), Push is Regen, Shift + Push Seed Back.
    bool customInput (int, hdui::Key key, int amount, bool shift, hdui::Edits& out) override
    {
        if (key == hdui::Key::wheel)
        {
            const int seed = ((int) value (ParamId::seed) + amount * (shift ? 16 : 1)) & 0xffff;
            out.set (ParamId::seed, (float) seed);
            if (value (ParamId::seedMode) < 0.5f)
                out.set (ParamId::seedMode, 1.0f);
            return true;
        }
        if (key == hdui::Key::push)
            return out.press (shift ? ParamId::seedBack : ParamId::regen), true;
        return false;
    }

    void customKnobs (int, uint32_t ids[3]) const override
    {
        ids[0] = ParamId::cutoff, ids[1] = ParamId::resonance, ids[2] = ParamId::envMod;
    }

    void legend (const char* lines[6]) const override
    {
        if (groupIndex() == 0)
            lines[0] = "WHEEL  SEED", lines[1] = "(IT LOCKS)", lines[2] = "PUSH  REGEN", lines[3] = "SHIFT: BACK", lines[4] = "-/+  GROUP",
            lines[5] = "K CUT RES ENV";
        else
            Page::legend (lines);
    }

    // The header's right: the clock, where the pattern is, and what plays the voice.
    void drawStatus (hdui::Raster& r, const hdui::Host& h, int right) override
    {
        char text[64];
        hdui::Value clock;
        read (ParamId::clockDiv, clock);
        std::snprintf (text, sizeof (text), "%s  STEP %d/%d", clock.text, view.current + 1, view.steps);
        r.text (right - hdui::textWidth (text), headerY + 1, text, colours.text);
        const bool notes = value (ParamId::mode) >= 0.5f;
        const char* mode = notes ? "NOTES PLAY" : (view.running ? "RUNNING" : "WAITING");
        r.text (right - hdui::textWidth (mode), headerY + 13, mode, notes ? accent (h) : colours.muted);
    }

    void drawPanel (hdui::Raster& r, const hdui::Host& h, int x, int y, int w, int height) override
    {
        const int g = groupIndex();
        if (g == 1 || g == 2)
            drawResponse (r, h, x, y, w, height);
        else
            drawPattern (r, h, x, y + 20, w, height - 20, false);
        if (g >= 3)
        {
            char seed[8];
            std::snprintf (seed, sizeof (seed), "%04X", (unsigned) value (ParamId::seed) & 0xffffu);
            r.text (x, y + 2, seed, accent (h));
            r.text (x + 34, y + 2, value (ParamId::seedMode) >= 0.5f ? "LOCKED" : "RANDOM", colours.muted);
        }
    }

    // PATTERN: the module's screen.
    void drawCustom (hdui::Raster& r, const hdui::Host& h, int x, int y, int w, int height) override
    {
        const hdui::Palette& c = colours;
        const hdui::Ink hi = accent (h);
        char text[64];
        // The seed: a dialled one whose pattern has yet to play (it plays from the next clock) is drawn pale, marked
        // NEXT; the bars are still the pattern playing.
        const unsigned dialled = (unsigned) value (ParamId::seed) & 0xffffu;
        const bool pending = dialled != (unsigned) view.seed && value (ParamId::mode) < 0.5f;
        std::snprintf (text, sizeof (text), "%04X", dialled);
        r.text (x + 6, y + 2, text, pending ? c.muted : hi, 4, 2);
        const bool locked = value (ParamId::seedMode) >= 0.5f;
        drawLock (r, x + 70, y + 6, locked, locked ? c.text : c.muted);
        const int end = r.text (x + 86, y + 2, locked ? "LOCKED" : "RANDOM", locked ? c.text : c.muted);
        if (pending)
            r.text (end + 7, y + 2, "NEXT", hi);
        hdui::Value v;
        read (ParamId::density, v);
        std::snprintf (text, sizeof (text), "DENSITY %s", v.text);
        r.text (x + 86, y + 15, text, c.muted);
        read (ParamId::scale, v);
        r.text (x + w - 6 - hdui::textWidth (v.text, 14), y + 2, v.text, c.text, 14);
        hdui::Value root, octave;
        read (ParamId::root, root);
        read (ParamId::octave, octave);
        std::snprintf (text, sizeof (text), "ROOT %s  OCT %s", root.text, octave.text);
        r.text (x + w - 6 - hdui::textWidth (text), y + 15, text, c.muted);
        drawPattern (r, h, x + 6, y + 32, w - 12, height - 34, true);
    }

private:
    const std::atomic<float>* values;
    const PatternState& pattern;
    PatternView view {};
    int seenStep = -1;
    uint64_t flashAt = 0;
    static constexpr uint64_t flashMs = 140;

    static void drawLock (hdui::Raster& r, int x, int y, bool locked, hdui::Ink ink)
    {
        r.box (x, y + 4, 9, 6, ink);
        r.frame (x + 2, locked ? y : y - 3, 5, 5, ink); // the shackle: down locked, up open
    }

    // The pattern: a gated step a bar from the floor as high as its pitch among the pattern's (accented: bright, with
    // a cap), a slide a line on to the next step, an ungated step a dot; the step playing lit, steps past Steps not
    // there.
    void drawPattern (hdui::Raster& r, const hdui::Host& h, int x, int y, int w, int height, bool big)
    {
        const hdui::Palette& c = colours;
        const hdui::Ink hi = accent (h);
        const int n = std::clamp (view.steps, 1, 32);
        const int column = std::max (3, w / n), bar = std::max (2, column - (big ? 3 : 2));
        int lo = 1 << 20, top = -(1 << 20);
        for (int s = 0; s < n; ++s)
            if ((view.gates >> s) & 1u)
                lo = std::min<int> (lo, view.pitch[s]), top = std::max<int> (top, view.pitch[s]);
        const int span = std::max (top - lo, 12 * 128);
        const int floor = y + height - 3, tallest = height - 10;
        r.box (x, floor + 1, column * n - 1, 1, c.rule);
        const int fade = flashAt != 0 && h.nowMs - flashAt < flashMs ? (int) (256 - 256 * (h.nowMs - flashAt) / flashMs) : 0;
        int lastTop = -1, lastX = 0;
        for (int s = 0; s < n; ++s)
        {
            const int cx = x + s * column;
            const bool gated = (view.gates >> s) & 1u, accented = (view.accents >> s) & 1u, lit = s == view.current && view.running;
            if (lit)
                r.box (cx - 1, y, column, height, hdui::mix (c.panel, hi, 36 + fade / 3));
            if (! gated)
            {
                r.box (cx + bar / 2 - 1, floor - 1, 2, 2, c.dim);
                lastTop = -1;
                continue;
            }
            const int hgt = 4 + (int) ((int64_t) (view.pitch[s] - lo) * (tallest - 4) / span);
            const int ty = floor - hgt;
            const hdui::Ink ink = lit ? c.text : (accented ? hi : c.muted);
            r.box (cx, ty, bar, hgt + 1, hdui::mix (ink, c.bg, accented ? 40 : 110));
            r.box (cx, ty, bar, accented ? 3 : 1, ink);
            if (lastTop >= 0) // the step before slid into this one
                r.line (lastX, lastTop, cx, ty, hi);
            if ((view.slides >> s) & 1u)
                lastTop = ty, lastX = cx + bar - 1;
            else
                lastTop = -1;
        }
    }

    // The filter's response as Cutoff, Resonance, Filter and Peak Spread set it (Env Mod at rest), 20 Hz to 20 kHz
    // across, -36 to +24 dB up, with Cutoff's reading marked.
    void drawResponse (hdui::Raster& r, const hdui::Host& h, int x, int y, int w, int height)
    {
        const hdui::Palette& c = colours;
        const hdui::Ink hi = accent (h);
        const int kind = (int) value (ParamId::filter);
        const float spread = value (ParamId::peakSpread), resonance = value (ParamId::resonance);
        const float k = feedbackOf (kind, resonance), nominal = nominalHz (kind, value (ParamId::cutoff));
        const int top = y + 14, bottom = y + height - 4, plot = bottom - top;
        auto yOf = [&] (double dB) { return top + (int) std::lround ((24.0 - std::clamp (dB, -36.0, 24.0)) / 60.0 * plot); };
        for (double dB : { 0.0, -24.0 })
            r.box (x, yOf (dB), w, 1, c.rule);
        for (double f : { 100.0, 1000.0, 10000.0 })
            r.box (x + (int) std::lround (std::log10 (f / 20.0) / 3.0 * (w - 1)), top, 1, plot, c.rule);
        // The level at the passband's start (the diode ladder's and three stages' DC; Twin Peak's has none) is 0 dB,
        // so a peak reads as how far it stands over the rest.
        const double reference = kind == filterTwinPeak ? filterGain2 (kind, k, spread, std::exp2 (spread * 0.5)) : filterGain2 (kind, k, spread, 1e-3);
        int lastY = -1;
        for (int i = 0; i < w; ++i)
        {
            const double hz = 20.0 * std::pow (1000.0, (double) i / (w - 1));
            const double g = filterGain2 (kind, k, spread, hz / nominal);
            const int py = yOf (10.0 * std::log10 (g / reference + 1e-12));
            if (lastY >= 0)
                r.box (x + i, std::min (py, lastY), 1, std::abs (py - lastY) + 1, hi);
            else
                r.dot (x + i, py, hi);
            lastY = py;
        }
        hdui::Value cutoff;
        read (ParamId::cutoff, cutoff);
        const char* name = filterChoices[kind];
        r.text (x, y + 1, name, c.text);
        r.text (x + w - hdui::textWidth (cutoff.text), y + 1, cutoff.text, hi);
        const double edge = std::clamp (std::log10 (std::max (20.0, std::atof (cutoff.text) * (std::strstr (cutoff.text, "kHz") ? 1000.0 : 1.0)) / 20.0) / 3.0, 0.0, 1.0);
        r.box (x + (int) std::lround (edge * (w - 1)), bottom - 6, 1, 6, c.text);
    }

    static constexpr hdui::Field voice[] = {
        { ParamId::tuning, "TUNING" }, { ParamId::cutoff, "CUTOFF" }, { ParamId::resonance, "RESONANCE" },
        { ParamId::envMod, "ENV MOD" }, { ParamId::decay, "DECAY" }, { ParamId::accent, "ACCENT" },
        { ParamId::waveform, "WAVEFORM" }, { ParamId::volume, "VOLUME" },
    };
    static constexpr hdui::Field tone[] = {
        { ParamId::filter, "FILTER" }, { ParamId::peakSpread, "PEAK SPREAD" }, { ParamId::drive, "DRIVE" },
        { ParamId::glide, "SLIDE" }, { ParamId::slideTime, "SLIDE TIME" }, { ParamId::accentDecay, "ACC DECAY" },
    };
    static constexpr hdui::Field sequencer[] = {
        { ParamId::mode, "MODE" }, { ParamId::seedMode, "SEED MODE" }, { ParamId::seed, "SEED" }, { ParamId::regen, "REGEN" },
        { ParamId::seedBack, "SEED BACK" }, { ParamId::mutate, "MUTATE" }, { ParamId::density, "DENSITY" },
        { ParamId::scale, "SCALE" }, { ParamId::root, "ROOT" }, { ParamId::octave, "OCTAVE" }, { ParamId::steps, "STEPS" },
        { ParamId::gate, "GATE" }, { ParamId::transpose, "TRANSPOSE" },
    };
    static constexpr hdui::Field clock[] = {
        { ParamId::clockDiv, "CLOCK DIV" }, { ParamId::swing, "SWING" }, { ParamId::reset, "RESET" },
    };
    static constexpr hdui::Group groups[] = {
        { "PATTERN", nullptr, 0 }, { "VOICE", voice, 8 }, { "TONE", tone, 6 }, { "TB-3PO", sequencer, 13 }, { "CLOCK", clock, 3 },
    };
};
} // namespace tbmpc
