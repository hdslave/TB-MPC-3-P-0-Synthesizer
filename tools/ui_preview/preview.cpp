// Standalone host for the actual screen code. No dependency on plug-in or DSP execution.
#include "screen.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <new>

namespace
{
size_t allocations = 0;
}

void* operator new (size_t n)
{
    ++allocations;
    if (void* p = std::malloc (n ? n : 1)) return p;
    std::abort();
}
void* operator new[] (size_t n) { return ::operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, size_t) noexcept { std::free (p); }
void operator delete[] (void* p, size_t) noexcept { std::free (p); }

namespace
{
using namespace tbmpc;
using hdui::Key;
using Frame = std::array<uint32_t, hdui::width * hdui::height>;
namespace P = ParamId;

void check (bool ok, const char* message)
{
    if (ok) return;
    std::fprintf (stderr, "FAIL %s\n", message);
    std::exit (1);
}

struct Rig
{
    std::atomic<float> values[paramCount] {};
    PatternState mailbox;
    PatternView v {};
    Screen screen { values, mailbox };
    Frame frame {};
    hdui::Host h;

    explicit Rig (bool blacklight = false)
    {
        for (unsigned i = 0; i < paramCount; ++i) values[i].store (paramTable[i].def);
        set (P::seed, 0xA7F2);
        set (P::seedMode, 1);
        set (P::scale, 2);
        set (P::density, 2);
        set (P::octave, 1);
        set (P::resonance, 0.82f);
        v.steps = 16;
        v.current = 5;
        v.seed = 0xA7F2;
        v.running = 1;
        v.gates = 0xdedb7db7u;
        v.accents = 0x89452491u;
        v.slides = 0x912220a2u;
        constexpr int phrase[] = { 0, 7, 3, 5, 12, 10, 7, 3, 0, 2, 3, 7, 14, 12, 7, 5,
                                   0, 3, 5, 10, 7, 14, 12, 10, -2, 2, 5, 7, 3, 0, -2, 0 };
        for (int i = 0; i < 32; ++i) v.pitch[i] = (phrase[i] + 12) * 128;
        h.playing = true;
        h.accent = 0xff7a5c;
        h.nowMs = 1000;
        mailbox.write (v);
        if (blacklight) toggleFromPattern();
    }

    static int index (uint32_t id)
    {
        const int i = paramIndex (id);
        check (i >= 0, "fixture parameter id");
        return i;
    }
    void set (uint32_t id, float val) { values[index (id)].store (val); }
    float get (uint32_t id) const { return values[index (id)].load(); }
    hdui::Edits key (Key key, int amount = 0, bool shift = false)
    {
        hdui::Edits out;
        screen.input (key, amount, shift, out);
        for (int i = 0; i < out.count; ++i)
            if (!out.list[i].press) set (out.list[i].id, out.list[i].value);
        return out;
    }
    void go (int group) { for (int i = 0; i < group; ++i) key (Key::nudge, 1); }
    void toggleFromPattern()
    {
        go (4);
        key (Key::wheel, 3);
        check (key (Key::push).count == 0, "theme switch emits no parameter edits");
        for (int i = 0; i < 4; ++i) key (Key::nudge, -1);
    }

    bool paint (bool whole = false)
    {
        screen.update (h.nowMs);
        return screen.paint (h, frame.data(), hdui::width, whole);
    }
};

constexpr const char* names[] = {
    "pattern-16", "pattern-32", "pending", "random", "notes", "voice-303", "voice-twin", "voice-18db",
    "tone-drive", "tb3po-scale", "clock-swing", "shift", "pickup", "track-coral", "track-mint", "track-violet", "track-yellow",
    "dense-32", "pending-notes", "awaiting-clock"
};

void configure (Rig& r, int i)
{
    if (i == 1) { r.v.steps = 32; r.v.current = 23; r.set (P::steps, 32); }
    if (i == 2) r.set (P::seed, 0xA802);
    if (i == 3) r.set (P::seedMode, 0);
    if (i == 4) r.set (P::mode, 1);
    if (i >= 5 && i <= 7)
    {
        r.go (1);
        r.key (Key::wheel, 1);
        r.set (P::filter, float (i - 5));
        r.set (P::peakSpread, 3);
        if (i == 7) r.set (P::resonance, 1);
    }
    if (i == 8)
    {
        r.go (2);
        r.key (Key::wheel, 2);
        r.key (Key::push);
        r.set (P::drive, .68f);
    }
    if (i == 9)
    {
        r.go (3);
        r.key (Key::wheel, 7);
        int longest = 0;
        for (int s = 1; s < scaleCount; ++s)
            if (std::strlen (scaleNames[s]) > std::strlen (scaleNames[longest])) longest = s;
        r.set (P::scale, float (longest));
    }
    if (i == 10) { r.go (4); r.key (Key::wheel, 1); r.set (P::swing, .66f); }
    if (i == 11) r.h.shift = true;
    if (i == 12) { r.h.engaged[1] = false; r.h.pot[1] = 17; }
    constexpr uint32_t tracks[] = { 0xff7a5c, 0x46d9b0, 0x9d8cff, 0xe8d94a };
    if (i >= 13 && i <= 16) r.h.accent = tracks[i - 13];
    if (i == 17)
    {
        r.v.steps = 32;
        r.v.current = 31;
        r.set (P::steps, 32);
        for (int step = 0; step < 32; ++step) r.v.pitch[step] = int16_t ((step * 7 % 24) * 128 + step % 3 * 32);
    }
    if (i == 18)
    {
        r.set (P::seed, 0x0DE1);
        r.set (P::mode, 1);
    }
    if (i == 19)
    {
        r.v = {};
        r.v.current = -1;
        r.h.playing = false;
    }
    r.mailbox.write (r.v);
}

uint64_t hash (const Frame& f)
{
    uint64_t out = 14695981039346656037ull;
    for (uint32_t p : f) out = (out ^ p) * 1099511628211ull;
    return out;
}

void ppm (const char* name, const Frame& f, bool blacklight)
{
    char path[128];
    std::snprintf (path, sizeof (path), "build/ui-preview/%s/%s.ppm", blacklight ? "frames-blacklight" : "frames", name);
    FILE* out = std::fopen (path, "wb");
    check (out, "frame output");
    std::fprintf (out, "P6\n320 218\n255\n");
    for (uint32_t p : f)
    {
        const unsigned char rgb[] = { uint8_t (p >> 16), uint8_t (p >> 8), uint8_t (p) };
        std::fwrite (rgb, 1, 3, out);
    }
    check (std::fclose (out) == 0, "write frame");
}

void guardedFrame (Rig& r)
{
    constexpr int stride = hdui::width + 7;
    constexpr uint32_t poison = 0x13579bdf;
    std::array<uint32_t, stride * hdui::height> pad;
    pad.fill (poison);
    check (!r.screen.paint (r.h, pad.data(), stride, false), "idle returns false");
    check (std::all_of (pad.begin(), pad.end(), [](uint32_t v) { return v == poison; }), "idle touches nothing");
    check (r.screen.paint (r.h, pad.data(), stride, true), "whole returns true");
    for (int y = 0; y < hdui::height; ++y)
    {
        check (std::equal (pad.begin() + y * stride, pad.begin() + y * stride + hdui::width,
                           r.frame.begin() + y * hdui::width), "partial and whole agree");
        for (int x = hdui::width; x < stride; ++x) check (pad[y * stride + x] == poison, "stride guard");
    }
}

void silverText()
{
    using namespace hdui;
    std::array<Ink, 40*12> pixels {};
    Raster r { pixels.data(), 40, 12, true };
    pixels.fill (ink::metal);
    r.text (0, 0, "535 Hz", ink::bg);
    check (std::any_of (pixels.begin(), pixels.end(), [](Ink c) { return c != ink::metal && c != ink::bg; }),
           "silver labels retain edge coverage");
    for (int pitch : { -32768, -4000, -128, 128, 32767 })
        for (bool compact : { false, true })
        {
            pixels.fill (ink::metal);
            tbmpc::plots::pitchLabel (r, 20, 0, pitch, 16, compact);
            bool drawn = false;
            for (int y = 0; y < r.h; ++y)
                for (int x = 0; x < r.w; ++x)
                    if (pixels[y*r.w+x] != ink::metal)
                    {
                        check (x >= 12 && x < 28, "note stays inside its matrix cell");
                        drawn = true;
                    }
            check (drawn, "signed note has a label");
        }
}

void bindings (Rig& r, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t ids[3];
    r.screen.knobs (ids);
    check (ids[0] == a && ids[1] == b && ids[2] == c, "K1-K3");
}

void controls()
{
    Rig r;
    const auto before = allocations;
    bindings (r, P::cutoff, P::resonance, P::envMod);
    r.set (P::seedMode, 0);
    r.set (P::seed, 65535);
    check (r.key (Key::wheel, 1).count == 2 && r.get (P::seed) == 0 && r.get (P::seedMode) == 1, "wrap and lock");
    r.key (Key::wheel, -1, true);
    check (r.get (P::seed) == 65520, "shift seed detent");
    auto press = r.key (Key::push);
    check (press.count == 1 && press.list[0].press && press.list[0].id == P::regen, "regen");
    press = r.key (Key::push, 0, true);
    check (press.count == 1 && press.list[0].press && press.list[0].id == P::seedBack, "back");
    r.go (1);
    bindings (r, P::cutoff, P::resonance, P::envMod);
    r.key (Key::push);
    r.set (P::cutoff, .5f);
    r.key (Key::wheel, 1);
    r.key (Key::wheel, -1, true);
    r.key (Key::nudge, 1);
    check (r.get (P::cutoff) == .5f + 1.f/128 - 1.f/1024 + 1.f/16, "plain, fine and coarse");
    r.key (Key::push);
    r.key (Key::wheel, INT32_MAX);
    bindings (r, P::cutoffLfoSpeed, hdui::noParam, hdui::noParam);
    r.key (Key::wheel, -3);
    bindings (r, P::tuning, P::volume, P::cutoffLfoAmount);
    r.go (1);
    bindings (r, P::glide, P::slideTime, P::filter);
    r.key (Key::push);
    r.key (Key::nudge, 1);
    check (r.get (P::glide) == 1, "303 slide");
    r.key (Key::nudge, -1);
    check (r.get (P::glide) == 0, "TB-3PO slide");
    r.key (Key::push);
    r.go (1);
    r.key (Key::wheel, 3);
    bindings (r, hdui::noParam, hdui::noParam, P::mutate);
    check (r.key (Key::push).list[0].press, "row button");
    r.key (Key::wheel, 3);
    r.key (Key::push);
    r.key (Key::wheel, 2, true);
    check (r.get (P::density) == 4, "integer");
    r.key (Key::push);
    r.key (Key::nudge, -1);
    bindings (r, P::glide, P::slideTime, P::filter);
    r.key (Key::nudge, -1);
    bindings (r, P::cutoff, P::resonance, P::envMod);
    r.key (Key::nudge, -1);
    hdui::Edits e;
    check (!r.screen.input (Key::nudge, -1, false, e), "first group boundary");
    for (int n = 0; n <= 32; ++n)
    {
        r.v.steps = n;
        r.v.current = n - 1;
        r.v.pitch[0] = -4000;
        r.mailbox.write (r.v);
        r.paint();
        guardedFrame (r);
    }
    r.h.playing = false;
    r.paint();
    check (!r.screen.animating (r.h), "stopped sleeps");
    r.go (4);
    check (!r.screen.input (Key::nudge, 1, false, e), "last group boundary");
    check (before == allocations, "allocation-free input, update, paint and knobs");
}

void damage (bool blacklight)
{
    Rig r (blacklight);
    r.paint();
    check (r.screen.animating (r.h), "playing score requests paints");
    auto before = r.frame;
    r.key (Key::wheel, 1);
    check (r.paint(), "pending seed updates the heading");
    for (int y = blacklight ? 43 : 98; y < 185; ++y)
    {
        const int left = y*320 + (blacklight ? 84 : 0);
        check (std::equal (before.begin()+left, before.begin()+(y+1)*320, r.frame.begin()+left),
               "pending seed preserves the entire score");
    }
    for (int state : { 0, 1, 17 })
    {
        Rig steps (blacklight);
        configure (steps, state);
        for (int i = 0; i < steps.v.steps; ++i)
        {
            steps.v.current = (i * 7) % steps.v.steps;
            steps.mailbox.write (steps.v);
            steps.paint();
            guardedFrame (steps);
        }
    }
    std::array<uint64_t, 16> seen {};
    for (unsigned digit = 0; digit < 16; ++digit)
    {
        r.set (P::seed, float (digit * 0x1111));
        r.paint();
        uint64_t glyph = 14695981039346656037ull;
        const int x0 = blacklight ? 4 : 22, y0 = blacklight ? 122 : 36;
        const int width = blacklight ? 18 : 20, height = blacklight ? 23 : 27;
        const auto paper = blacklight ? hdui::ticketInk.paper : hdui::pressInk.paper;
        for (int y = y0; y < y0+height; ++y)
            for (int x = x0; x < x0+width; ++x)
            {
                const bool pixel = r.frame[y * 320 + x] == paper;
                for (int place = 1; place < 4; ++place)
                    check ((r.frame[y * 320 + x + width * place] == paper) == pixel, "all four live hex digits");
                glyph = (glyph ^ pixel) * 1099511628211ull;
            }
        check (std::find (seen.begin(), seen.begin() + digit, glyph) == seen.begin() + digit, "distinct hex glyph");
        seen[digit] = glyph;
    }
    before = r.frame;
    r.h.accent = 0x46d9b0;
    check (r.paint() && std::equal (before.begin(), before.begin() + 185 * 320, r.frame.begin()),
           "track color stays on cards");
    r.set (P::tuning, 3);
    check (!r.paint(), "hidden parameter leaves pattern untouched");
    for (int group = 1; group < 5; ++group)
    {
        r.go (1);
        r.paint();
        check (r.screen.animating (r.h), "all playing plates follow the clock");
        for (const auto& p : paramTable)
        {
            r.set (p.id, r.get (p.id) == p.max ? p.min : p.max);
            r.paint();
            guardedFrame (r);
        }
    }
}

int changes (const Frame& a, const Frame& b)
{
    int n = 0;
    for (size_t i = 0; i < a.size(); ++i) n += a[i] != b[i];
    return n;
}

void motion (bool blacklight)
{
    for (int group = 0; group < 5; ++group)
    {
        Rig r (blacklight);
        r.go (group);
        r.paint();
        r.v.current = 0;
        r.mailbox.write (r.v);
        const auto before = allocations;
        r.paint();
        const Frame first = r.frame;
        r.h.nowMs += 48;
        check (r.paint(), "drips respond between clock steps");
        const int count = changes (first, r.frame);
        check (count > 0 && count <= (blacklight ? 46*4+43*3 : group ? 27*3 : 41*5), "only a small strip moves");
        for (int y = 0; y < hdui::height; ++y)
            for (int x = 0; x < hdui::width; ++x)
                if (first[y*320+x] != r.frame[y*320+x])
                {
                    const int local = y-18;
                    const bool tips = blacklight ? ((x >= 15 && x < 61 && y >= 62 && y < 66)
                                                 || (x >= 16 && x < 59 && y >= 70 && y < 73))
                                    : group ? x >= 276 && x < 303 && ((local >= 31 && local <= 32) || local == 35)
                                            : x >= 254 && x < 295 && ((local >= 47 && local <= 49) || (local >= 53 && local <= 54));
                    check (tips, "motion stays inside the smiley tips");
                }
        guardedFrame (r);
        r.h.nowMs += 48;
        check (r.paint() && r.frame == first, "drips ease back");
        r.h.nowMs += 60;
        check (r.paint(), "drips settle");
        const auto neutral = r.frame;
        r.h.nowMs += 400;
        check (!r.paint(), "clock held on one step settles");
        r.v.current = 1; // a gate without an accent
        r.mailbox.write (r.v);
        r.paint();
        r.h.nowMs += 60;
        check (!r.paint(), "ordinary gate has no pulse");
        r.v.current = 7;
        r.v.gates |= 1u<<7;
        r.v.accents |= 1u<<7;
        r.mailbox.write (r.v);
        r.paint();
        r.h.nowMs += 45;
        check (r.paint(), "gated accent between downbeats triggers");
        r.h.playing = false;
        check (r.paint() && !r.screen.animating (r.h), "stopping cancels an active pulse");
        const int left = blacklight ? 0 : group ? 276 : 254;
        const int right = blacklight ? 84 : group ? 303 : 295;
        for (int y = blacklight ? 0 : 18; y < (blacklight ? 106 : group ? 60 : 82); ++y)
            check (std::equal (neutral.begin()+320*y+left, neutral.begin()+320*y+right,
                               r.frame.begin()+320*y+left), "stopped plate is at rest");
        r.h.nowMs += 1000;
        check (!r.paint(), "stopped time causes no writes");
        guardedFrame (r);
        check (allocations == before, "animation has no allocations");
    }
}

template<class Operation> void timed (const char* name, Rig& r, bool whole, Operation prepare)
{
    constexpr int count = 2000;
    int drew = 0;
    using Clock = std::chrono::steady_clock;
    Clock::duration total {};
    // Set up events outside the timer. Include the plug-in's update + paint calls.
    for (int i = 0; i < count; ++i)
    {
        prepare (i);
        const auto start = Clock::now();
        drew += r.paint (whole);
        total += Clock::now() - start;
    }
    std::printf ("%-24s %9.6f ms   %d/%d paints\n", name,
                 std::chrono::duration<double, std::milli> (total).count() / count, drew, count);
}

void bench (bool blacklight)
{
    Rig r (blacklight);
    r.paint (true);
    timed ("full / 16 steps", r, true, [](int) {});
    r.h.playing = false;
    r.paint();
    timed ("idle", r, false, [&](int) { ++r.h.nowMs; });
    r.h.playing = true;
    r.paint();
    auto step = [&](int) {
        r.h.nowMs += 120;
        r.v.current = (r.v.current + 1) % r.v.steps;
        r.mailbox.write (r.v);
    };
    timed ("partial / 16 steps", r, false, step);
    r.v.steps = 32;
    r.mailbox.write (r.v);
    timed ("full / 32 steps", r, true, [](int) {});
    timed ("partial / 32 steps", r, false, step);
    r.go (1);
    r.set (P::filter, filterTwinPeak);
    r.paint();
    timed ("full / Twin Peak", r, true, [](int) {});
    timed ("partial / cursor", r, false, [&](int i) { r.key (Key::wheel, i % 2 ? -1 : 1); });
    auto resonance = [&](int i) { r.set (P::resonance, i % 2 ? .71f : .82f); };
    timed ("full / live resonance", r, true, resonance);
    timed ("partial / live resonance", r, false, resonance);
    for (int state : { 0, 1, 4, 9, 10, 17 })
    {
        Rig move (blacklight);
        configure (move, state);
        move.paint();
        Frame prev = move.frame;
        check (!move.paint() && changes (prev, move.frame) == 0, "idle unchanged");
        int worst = 0;
        for (int i = 0; i < move.v.steps; ++i)
        {
            move.h.nowMs += 120;
            move.v.current = (move.v.current + 1) % move.v.steps;
            move.mailbox.write (move.v);
            move.paint();
            worst = std::max (worst, changes (prev, move.frame));
            prev = move.frame;
        }
        std::printf ("step pixels / %-12s max %d\n", names[state], worst);
        check (worst < 3000, "step budget");
    }
    Rig list (blacklight);
    for (int g = 1; g <= 4; ++g)
    {
        list.go (1);
        list.paint();
        int worst = 0;
        for (int i = 1; i < layout::groups[g].count; ++i)
        {
            Frame prev = list.frame;
            list.key (Key::wheel, 1);
            list.paint();
            worst = std::max (worst, changes (prev, list.frame));
        }
        std::printf ("cursor pixels / %-10s max %d\n", layout::groups[g].name, worst);
        check (worst < 12000, "cursor budget");
    }
    for (int g : { 0, 1 })
    {
        Rig pulse (blacklight);
        pulse.go (g);
        pulse.v.current = 0;
        pulse.mailbox.write (pulse.v);
        pulse.paint();
        int worst = 0;
        for (int i = 0; i < 6; ++i)
        {
            const Frame prev = pulse.frame;
            pulse.h.nowMs += 30;
            pulse.paint();
            worst = std::max (worst, changes (prev, pulse.frame));
        }
        std::printf ("animation pixels / %-7s max %d\n", layout::groups[g].name, worst);
    }
    std::puts ("idle pixels             0");
}

void themeSwitch()
{
    Rig live;
    live.go (4);
    live.key (Key::wheel, 3);
    bindings (live, hdui::noParam, hdui::noParam, hdui::noParam);
    live.paint();
    const Frame press = live.frame;
    const auto before = allocations;
    check (live.key (Key::push).count == 0 && live.paint(), "clock theme switches to Blacklight");
    check (live.frame != press, "theme changes the CLOCK page");
    guardedFrame (live);
    Rig fresh;
    fresh.go (4);
    fresh.key (Key::wheel, 3);
    fresh.paint();
    check (fresh.frame == press, "independent instances start in Press Plate");
    check (live.key (Key::push).count == 0 && live.paint(), "clock theme switches back");
    check (live.frame == press && allocations == before, "theme round trip restores pixels without allocation");
    guardedFrame (live);
}

} // namespace

int main (int argc, char** argv)
{
    const char* mode = argc > 1 ? argv[1] : "--render";
    const bool blacklight = argc > 2 && !std::strcmp (argv[2], "--blacklight");
    if (!std::strcmp (mode, "--bench")) { bench (blacklight); return 0; }
    const bool render = !std::strcmp (mode, "--render"), test = !std::strcmp (mode, "--test");
    check (render || test || !std::strcmp (mode, "--hashes"), "expected --render, --test, --bench or --hashes");
    FILE* ref = test ? std::fopen (blacklight ? "tools/ui_preview/blacklight-hashes.txt" : "tools/ui_preview/hashes.txt", "r") : nullptr;
    if (test && !blacklight) silverText();
    if (test) { check (ref, "golden hash file"); controls(); themeSwitch(); damage (blacklight); motion (blacklight); }
    for (unsigned i = 0; i < std::size (names); ++i)
    {
        Rig r (blacklight);
        const auto before = allocations;
        configure (r, i);
        r.paint();
        if (test)
        {
            guardedFrame (r);
            check (before == allocations, "state rendered without allocation");
            unsigned long long expected;
            char name[48];
            check (std::fscanf (ref, "%llx %47s", &expected, name) == 2
                   && !std::strcmp (name, names[i]) && expected == hash (r.frame), names[i]);
        }
        else if (render) ppm (names[i], r.frame, blacklight);
        else std::printf ("%016llx %s\n", (unsigned long long) hash (r.frame), names[i]);
    }
    if (test)
    {
        char extra[2];
        check (std::fscanf (ref, "%1s", extra) == EOF, "golden file has no extra states");
        std::fclose (ref);
        std::printf ("%s: %zu golden states, controls, K1-K3, allocation checks, clocked motion, idle/whole/stride and lengths 0-32 passed\n", blacklight ? "Blacklight Transfer" : "Press Plate", std::size (names));
    }
    if (render)
    {
        Rig r (blacklight);
        for (int step = 0; step < 16; ++step)
        {
            r.v.current = step;
            r.mailbox.write (r.v);
            for (int tick = 0; tick < 4; ++tick)
            {
                const int frame = 4*step + tick;
                r.h.nowMs = 1000 + frame*30;
                r.paint();
                char s[16];
                if (!tick)
                {
                    std::snprintf (s, sizeof (s), "step-%02d", step+1);
                    ppm (s, r.frame, blacklight);
                }
                std::snprintf (s, sizeof (s), "motion-%02d", frame);
                ppm (s, r.frame, blacklight);
            }
        }
    }
}
