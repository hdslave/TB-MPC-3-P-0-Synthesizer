// Holds the plug-in's TB-3PO against the applet (tests/applet.h, the module's code run a tick at a time):
//   - random() against Park and Miller's published check value, and the quantizer's known answers;
//   - the pattern each seed, density and scale make, field by field, for thousands of settings;
//   - whole runs: the applet ticking with clock and reset pulses, the plug-in's engine with the host's transport, the
//     same settings changed mid-run on both: the same steps, pitches, slides and gate changes, at the same times
//     (the applet's 2 ms ADC wait taken out), and the slide's pitch every tick;
//   - where the engine places the pattern when Play starts between bar resets.
//   reference
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace tbmpc
{
class Tb3po;
}
// What the engine traces: each step (0) and each half-step timer (1), at its frame.
struct Traced
{
    int what;
    uint64_t frame;
    int step;
    int32_t start, end, gate;
};
static std::vector<Traced> traced;
static void trace (int what, uint64_t frame, const tbmpc::Tb3po& seq);
#define TBMPC_TRACE(what) trace (what, frameCount, seq)

#include "applet.h"
#include "engine.h"

static void trace (int what, uint64_t frame, const tbmpc::Tb3po& seq)
{
    traced.push_back ({ what, frame, seq.currentStep(), seq.slideStart, seq.slideEnd, seq.gateCv });
}

namespace
{
int failures = 0, checks = 0;
#define CHECK(c) do { ++checks; if (! (c)) { ++failures; if (failures < 40) std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

using tbmpc::Tb3po;

void testRandom()
{
    // Park and Miller (1988): from 1, the 10,000th value is 1043618065.
    tbmpc::TeensyRandom r;
    r.randomSeed (1);
    int32_t x = 0;
    for (int i = 0; i < 10000; ++i)
        x = r.next();
    CHECK (x == 1043618065);
    // A state of 0 starts from 123459876; randomSeed(0) changes nothing.
    tbmpc::TeensyRandom a, b;
    b.randomSeed (123459876);
    a.randomSeed (0);
    CHECK (a.next() == b.next());
    // random(lo, hi) stays in lo .. hi - 1.
    bool inRange = true, sawLow = false, sawHigh = false;
    for (int i = 0; i < 20000; ++i)
    {
        const int v = r.between (1, 100);
        inRange &= v >= 1 && v <= 99;
        sawLow |= v == 1, sawHigh |= v == 99;
    }
    CHECK (inRange && sawLow && sawHigh);
}

void testQuantizer()
{
    tbmpc::Quantizer q;
    q.init();
    q.configure (tbmpc::scaleTable[0]); // Semitone
    CHECK (q.lookup (64) == 0 && q.lookup (76) == 1536 && q.lookup (65) == 128 && q.lookup (53) == -1408);
    // An index exactly an octave below 64 reads past the scale: with nothing there yet, 0, so two octaves down.
    CHECK (q.lookup (52) == -3072);
    // A longer scale first leaves its notes there: 16 notes (16ED), then Semitone; index 52 reads the 16-note
    // scale's 13th note.
    int sixteen = -1;
    for (int i = 0; i < tbmpc::scaleCount; ++i)
        if (tbmpc::scaleTable[i].count == 16 && sixteen < 0)
            sixteen = i;
    CHECK (sixteen >= 0);
    tbmpc::Quantizer r;
    r.init();
    r.configure (tbmpc::scaleTable[sixteen]);
    r.configure (tbmpc::scaleTable[0]);
    CHECK (r.lookup (52) == tbmpc::scaleTable[sixteen].notes[12] - 3072);
    // Process: C# in Ionian goes down to C (a tie keeps the lower note); E stays E; a semitone scale is exact.
    tbmpc::Quantizer ionian;
    ionian.init();
    ionian.configure (tbmpc::scaleTable[1]);
    CHECK (ionian.process (128, 0, 0) == 0);
    CHECK (ionian.process (512, 0, 0) == 512);
    q.init();
    for (int s = -36; s <= 72; ++s)
        CHECK (q.process (s * 128, 0, 0) == s * 128);
}

// The applet with the settings set directly (as its encoder leaves them, without scrolling through the scales
// between: that would leave other notes in the quantizer).
struct Module
{
    applet::Hardware hw;
    applet::TB_3PO app { hw };
    Module() { app.Start(); }
    void scale (int pluginScale)
    {
        // The encoder a step at a time would pass every scale between; set it straight: cursor 6 from one below.
        app.scale = pluginScale + 5 - 1;
        app.cursor = 6;
        app.OnEncoderMove (1);
        app.cursor = 0;
    }
    void tick()
    {
        app.stepped = false;
        app.Controller();
        hw.clockEdge[0] = hw.clockEdge[1] = false;
        ++hw.ticks;
    }
};

void testPatterns()
{
    std::vector<uint32_t> seeds { 0, 1, 2, 3, 255, 256, 0x303, 12345, 0x7fff, 0x8000, 65534, 65535 };
    tbmpc::TeensyRandom pick;
    pick.randomSeed (99);
    for (int i = 0; i < 120; ++i)
        seeds.push_back (pick.below (65536));
    int sixteen = -1, four = -1;
    for (int i = 0; i < tbmpc::scaleCount; ++i)
    {
        if (tbmpc::scaleTable[i].count == 16 && sixteen < 0) sixteen = i;
        if (tbmpc::scaleTable[i].count == 4 && four < 0) four = i;
    }
    const int scales[] = { 0, 1, 6, tbmpc::appletDefaultScale, 13, sixteen, four };
    int compared = 0, same = 0;
    for (uint32_t seed : seeds)
        for (int density = 0; density <= 14; ++density)
            for (int scale : scales)
            {
                Module m;
                m.scale (scale);
                m.app.density_encoder = density;
                m.app.seed = (uint16_t) seed;
                m.app.lock_seed = 1;
                m.app.manual_reset_flag = 1;
                for (int t = 0; t < 6; ++t)
                    m.tick();
                Tb3po port;
                port.start (7);
                port.setScale (scale);
                port.setDensity (density);
                port.setSeed ((uint16_t) seed);
                port.setLocked (true);
                port.reset (0);
                bool ok = true;
                for (int s = 0; s < 32; ++s)
                    ok &= port.note (s) == m.app.notes[s] && port.gated (s) == (((m.app.gates >> s) & 1u) != 0)
                          && port.slid (s) == (((m.app.slides >> s) & 1u) != 0) && port.accented (s) == (((m.app.accents >> s) & 1u) != 0)
                          && port.octaveUp (s) == (((m.app.oct_ups >> s) & 1u) != 0) && port.octaveDown (s) == (((m.app.oct_downs >> s) & 1u) != 0);
                ++compared;
                same += ok;
            }
    std::printf ("patterns: %d of %d settings the same\n", same, compared);
    CHECK (same == compared);
}

// A run: the applet ticked with a clock every 2,000 ticks (0.12 s: sixteenths at 125 BPM) and a reset pulse with the
// first clock (and, with barReset, every 16th); the engine at 48 kHz with the host playing from the top. `changes`
// set the same things on both, half a step after the step it names.
struct Change
{
    int atStep;
    std::function<void (Module&)> module;
    std::function<void (tbmpc::Engine&)> engine;
};

struct Run
{
    std::vector<Traced> steps, gates;   // the engine's steps and gate changes
    std::vector<Traced> appSteps, appGates;
    bool slideOk = true;
};

Run run (int clocks, uint16_t seed, int steps, int resetMode, const std::vector<Change>& changes, bool slideCheck = true, int engineSeedOffset = 0)
{
    constexpr int ticksPerStep = 2000, t0 = 2000, lag = 32;
    constexpr double rate = 48000.0, bpm = 125.0;
    constexpr uint32_t framesPerStep = 5760;
    Run out;

    // The applet.
    Module m;
    m.app.density_encoder = 12;
    m.app.seed = seed;
    m.app.lock_seed = 1;
    m.app.num_steps = (uint8_t) steps;
    m.scale (tbmpc::appletDefaultScale);
    int lastStepTick = -1;
    int32_t gateWas = m.app.curr_gate_cv;
    const int endTick = t0 + clocks * ticksPerStep;
    for (int t = 0; t < endTick; ++t)
    {
        const int since = t - t0;
        if (since >= 0 && since % ticksPerStep == 0)
        {
            const int k = since / ticksPerStep;
            m.hw.clockEdge[0] = true;
            if (k == 0 || (resetMode > 0 && k % 16 == 0))
                m.hw.clockEdge[1] = true, m.hw.gateHigh[1] = true;
        }
        for (const auto& c : changes)
            if (t == t0 + c.atStep * ticksPerStep + lag + ticksPerStep / 2)
                c.module (m);
        m.tick();
        m.hw.gateHigh[1] = false;
        if (m.app.stepped)
        {
            lastStepTick = t;
            out.appSteps.push_back ({ 0, (uint64_t) (t - t0 - lag) * framesPerStep / ticksPerStep, m.app.step, m.app.slide_start_cv,
                                      m.app.slide_end_cv, m.app.curr_gate_cv });
        }
        if (m.app.curr_gate_cv != gateWas)
        {
            gateWas = m.app.curr_gate_cv;
            out.appGates.push_back ({ 0, (uint64_t) ((int64_t) (t - t0 - lag) * framesPerStep / ticksPerStep), m.app.step, 0, 0, gateWas });
        }
        if (slideCheck && lastStepTick >= 0)
        {
            const int32_t expect = Tb3po::slideAfter (m.app.slide_start_cv, m.app.slide_end_cv, t - lastStepTick + 1);
            out.slideOk &= m.app.slide_start_cv == m.app.slide_end_cv ? m.app.curr_pitch_cv == m.app.slide_end_cv : m.app.curr_pitch_cv == expect;
        }
    }

    // The engine.
    tbmpc::Engine e;
    float values[tbmpc::paramCount];
    for (uint32_t i = 0; i < tbmpc::paramCount; ++i)
        values[i] = tbmpc::paramTable[i].def;
    e.start (values);
    e.prepare (rate, 128);
    e.set (tbmpc::ParamId::seedMode, 1.0f);
    e.set (tbmpc::ParamId::seed, (float) (uint16_t) (seed + engineSeedOffset));
    e.set (tbmpc::ParamId::steps, (float) steps);
    e.set (tbmpc::ParamId::reset, resetMode > 0 ? 1.0f : 0.0f); // 1 Bar or Play
    traced.clear();
    std::vector<float> l (128), r (128);
    const uint64_t endFrame = (uint64_t) clocks * framesPerStep;
    std::vector<uint64_t> cuts;
    for (const auto& c : changes)
        cuts.push_back ((uint64_t) c.atStep * framesPerStep + framesPerStep / 2);
    for (uint64_t at = 0; at < endFrame;)
    {
        uint32_t n = (uint32_t) std::min<uint64_t> (128, endFrame - at);
        for (uint64_t cut : cuts)
            if (cut > at && cut < at + n)
                n = (uint32_t) (cut - at);
        for (size_t i = 0; i < changes.size(); ++i)
            if (cuts[i] == at)
                changes[i].engine (e);
        e.transport (true, (double) at * bpm / 60.0 / rate, bpm, 4.0, n);
        e.render (l.data(), r.data(), n);
        at += n;
    }
    int32_t gate = 0;
    for (const auto& t : traced)
    {
        if (t.what == 0)
            out.steps.push_back (t);
        if (t.gate != gate)
            gate = t.gate, out.gates.push_back ({ 0, t.frame, t.step, 0, 0, t.gate });
    }
    return out;
}

bool sameSteps (const Run& r, bool print)
{
    bool ok = r.steps.size() == r.appSteps.size() && r.gates.size() == r.appGates.size();
    for (size_t i = 0; ok && i < r.steps.size(); ++i)
    {
        const auto &a = r.steps[i], &b = r.appSteps[i];
        if (a.frame != b.frame || a.step != b.step || a.start != b.start || a.end != b.end || a.gate != b.gate)
        {
            if (print)
                std::printf ("  step %zu: engine frame %llu step %d %d->%d gate %d; applet frame %llu step %d %d->%d gate %d\n", i,
                             (unsigned long long) a.frame, a.step, a.start, a.end, a.gate, (unsigned long long) b.frame, b.step, b.start, b.end, b.gate);
            ok = false;
        }
    }
    for (size_t i = 0; ok && i < r.gates.size(); ++i)
        if (r.gates[i].frame != r.appGates[i].frame || r.gates[i].gate != r.appGates[i].gate)
        {
            if (print)
                std::printf ("  gate change %zu: engine %llu -> %d, applet %llu -> %d\n", i, (unsigned long long) r.gates[i].frame, r.gates[i].gate,
                             (unsigned long long) r.appGates[i].frame, r.appGates[i].gate);
            ok = false;
        }
    if (! ok && print)
        std::printf ("  engine %zu steps %zu gate changes; applet %zu steps %zu gate changes\n", r.steps.size(), r.gates.size(), r.appSteps.size(),
                     r.appGates.size());
    return ok;
}

void testRuns()
{
    using namespace tbmpc;
    auto encoder = [] (int cursor, int moves) {
        return [cursor, moves] (Module& m) {
            m.app.cursor = cursor;
            for (int i = 0; i < std::abs (moves); ++i)
                m.app.OnEncoderMove (moves > 0 ? 1 : -1);
            m.app.cursor = 0;
        };
    };
    // Settings changed mid-run on both sides: density (cursor 5), scale (straight to it), root, octave, length
    // (cursor 8) and the transpose CV.
    const std::vector<Change> changes {
        { 20, [] (Module& m) { m.app.density_encoder = 3; }, [] (Engine& e) { e.set (ParamId::density, -4.0f); } },
        { 37, [] (Module& m) { m.scale (6); }, [] (Engine& e) { e.set (ParamId::scale, 6.0f); } },
        { 45, [] (Module& m) { m.app.root = 3; }, [] (Engine& e) { e.set (ParamId::root, 3.0f); } },
        { 58, [] (Module& m) { m.app.octave_offset = -1; }, [] (Engine& e) { e.set (ParamId::octave, -1.0f); } },
        { 71, [] (Module& m) { m.app.num_steps = 13; }, [] (Engine& e) { e.set (ParamId::steps, 13.0f); } },
        { 90, encoder (8, -2), [] (Engine& e) { e.set (ParamId::steps, 11.0f); } }, // the encoder: 13 - 2
        { 83, [] (Module& m) { m.hw.inputs[0] = 5 * 128; }, [] (Engine& e) { e.set (ParamId::transpose, 5.0f); } },
        { 97, [] (Module& m) { m.app.density_encoder = 0; }, [] (Engine& e) { e.set (ParamId::density, -7.0f); } },
        { 111, [] (Module& m) { m.hw.inputs[0] = -7 * 128; }, [] (Engine& e) { e.set (ParamId::transpose, -7.0f); } },
        { 124, [] (Module& m) { m.scale (1); m.app.density_encoder = 14; }, [] (Engine& e) { e.set (ParamId::scale, 1.0f); e.set (ParamId::density, 7.0f); } },
        { 140, [] (Module& m) { m.app.octave_offset = 2; m.hw.inputs[0] = 0; }, [] (Engine& e) { e.set (ParamId::octave, 2.0f); e.set (ParamId::transpose, 0.0f); } },
        { 152, [] (Module& m) { m.scale (tbmpc::appletDefaultScale); m.app.root = 0; }, [] (Engine& e) { e.set (ParamId::scale, (float) tbmpc::appletDefaultScale); e.set (ParamId::root, 0.0f); } },
    };
    int good = 0, total = 0;
    bool slides = true;
    long stepsSeen = 0, slidesSeen = 0, upSlides = 0, gateChanges = 0, accentsSeen = 0;
    std::vector<uint16_t> seeds { 0x303, 1, 12345, 0xbeef, 0x5a17, 0xffff, 0 };
    tbmpc::TeensyRandom pick;
    pick.randomSeed (5);
    for (int i = 0; i < 25; ++i)
        seeds.push_back ((uint16_t) pick.below (65536));
    for (uint16_t seed : seeds)
        for (int steps : { 16, 7, 32 })
            for (int resetMode : { 0, 1 })
            {
                const Run plain = run (40, seed, steps, resetMode, {});
                const Run changed = run (170, seed, steps, resetMode, changes);
                const bool ok = sameSteps (plain, failures < 3) && sameSteps (changed, failures < 3);
                if (! ok)
                    std::printf ("  (seed %04x, %d steps, reset %s)\n", seed, steps, resetMode ? "every bar" : "at Play");
                good += ok;
                ++total;
                slides &= plain.slideOk && changed.slideOk;
                CHECK (ok);
                CHECK (plain.steps.size() == 40 && changed.steps.size() == 170);
                for (const auto& t : changed.steps)
                {
                    ++stepsSeen;
                    slidesSeen += t.start != t.end;
                    upSlides += t.end > t.start;
                    accentsSeen += t.gate == Tb3po::gateAccent;
                }
                gateChanges += (long) changed.gates.size();
            }
    std::printf ("runs: %d of %d the same as the applet's (%ld steps, %ld slides of which %ld up, %ld accents, %ld gate changes)\n", good, total,
                 stepsSeen, slidesSeen, upSlides, accentsSeen, gateChanges);
    CHECK (slidesSeen > 100 && upSlides > 30 && accentsSeen > 100 && gateChanges > 1000);
    // The comparison can fail: one seed apart, the runs differ.
    CHECK (! sameSteps (run (40, 0x303, 16, 0, {}, false, 1), false));
    // ...and so does a setting changed on one side only.
    const std::vector<Change> oneSided { { 10, [] (Module& m) { m.app.root = 2; }, [] (Engine&) {} } };
    CHECK (! sameSteps (run (40, 0x303, 16, 0, oneSided, false), false));
    CHECK (slides); // the applet's slide every tick = Tb3po::slideAfter
}

// Play landing between bar resets places the pattern as if it had run from the last one.
void testPlacement()
{
    using namespace tbmpc;
    auto firstSteps = [] (double fromBeats, int resetMode, int steps) {
        Engine e;
        float values[paramCount];
        for (uint32_t i = 0; i < paramCount; ++i)
            values[i] = paramTable[i].def;
        e.start (values);
        e.prepare (48000.0, 128);
        e.set (ParamId::seedMode, 1.0f);
        e.set (ParamId::steps, (float) steps);
        e.set (ParamId::reset, (float) resetMode);
        traced.clear();
        std::vector<float> l (128), r (128);
        for (uint64_t at = 0; at < 48000 * 3; at += 128)
        {
            e.transport (true, fromBeats + (double) at * 120.0 / 60.0 / 48000.0, 120.0, 4.0, 128);
            e.render (l.data(), r.data(), 128);
        }
        std::vector<int> out;
        for (const auto& t : traced)
            if (t.what == 0)
                out.push_back (t.step);
        return out;
    };
    const auto top = firstSteps (0.0, 1, 16);      // 1 Bar resets, from the top
    const auto inBar = firstSteps (10.0, 1, 16);   // from beat 10: two bars and a half in, clock 40
    const auto line = firstSteps (8.0, 1, 16);     // from a bar line
    const auto fourBars = firstSteps (10.0, 3, 5); // 4 Bars: from beat 10, 40 clocks after the line at 0, 5 steps
    CHECK (! top.empty() && top[0] == 1 && top[15] == 0 && top[16] == 1);
    CHECK (! inBar.empty() && inBar[0] == (40 - 32 + 1) % 16);
    CHECK (! line.empty() && line[0] == 1);
    CHECK (! fourBars.empty() && fourBars[0] == (40 + 1) % 5);
    const auto play = firstSteps (10.0, 0, 16); // Reset at Play: wherever Play starts, step 1 first
    CHECK (! play.empty() && play[0] == 1);
}
} // namespace

int main()
{
    testRandom();
    testQuantizer();
    testPatterns();
    testRuns();
    testPlacement();
    std::printf ("reference: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
