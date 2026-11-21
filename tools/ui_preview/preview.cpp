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

    Rig()
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

void ppm (const char* name, const Frame& f)
{
    char path[128];
    std::snprintf (path, sizeof (path), "build/ui-preview/frames/%s.ppm", name);
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
    bindings (r, P::tuning, P::cutoff, P::resonance);
    r.key (Key::wheel, 1);
    r.key (Key::push);
    r.set (P::cutoff, .5f);
    r.key (Key::wheel, 1);
    r.key (Key::wheel, -1, true);
    r.key (Key::nudge, 1);
    check (r.get (P::cutoff) == .5f + 1.f/128 - 1.f/1024 + 1.f/16, "plain, fine and coarse");
    r.key (Key::push);
    r.key (Key::wheel, INT32_MAX);
    bindings (r, P::volume, hdui::noParam, hdui::noParam);
    r.go (1);
    r.key (Key::push);
    r.key (Key::nudge, 1);
    check (r.get (P::filter) == 1, "choice");
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
    bindings (r, P::filter, P::peakSpread, P::drive);
    r.key (Key::nudge, -1);
    bindings (r, P::tuning, P::cutoff, P::resonance);
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

void damage()
{
    Rig r;
    r.paint();
    check (r.screen.animating (r.h), "playing score requests paints");
    auto before = r.frame;
    r.key (Key::wheel, 1);
    check (r.paint(), "pending seed updates the heading");
    check (std::equal (before.begin(), before.begin() + 155 * 320, r.frame.begin()),
           "pending seed preserves the entire score");
    for (int state : { 0, 1, 17 })
    {
        Rig steps;
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
        for (int y = 165; y < 190; ++y)
            for (int x = 4; x < 28; ++x)
            {
                const auto pixel = r.frame[y * 320 + x];
                for (int place = 1; place < 4; ++place)
                    check (r.frame[y * 320 + x + 24 * place] == pixel, "all four live hex digits");
                glyph = (glyph ^ pixel) * 1099511628211ull;
            }
        check (std::find (seen.begin(), seen.begin() + digit, glyph) == seen.begin() + digit, "distinct hex glyph");
        seen[digit] = glyph;
    }
    before = r.frame;
    r.h.accent = 0x46d9b0;
    check (r.paint() && std::equal (before.begin(), before.begin() + 191 * 320, r.frame.begin()),
           "track color stays on cards");
    r.set (P::tuning, 3);
    check (!r.paint(), "hidden parameter leaves pattern untouched");
    for (int group = 1; group < 5; ++group)
    {
        r.go (1);
        r.paint();
        check (r.screen.animating (r.h) == (group >= 3), "only a visible score requests paints");
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

void bench()
{
    Rig r;
    r.paint (true);
    timed ("full / 16 steps", r, true, [](int) {});
    timed ("idle", r, false, [&](int) { ++r.h.nowMs; });
    auto step = [&](int) {
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
        Rig move;
        configure (move, state);
        move.paint();
        Frame prev = move.frame;
        check (!move.paint() && changes (prev, move.frame) == 0, "idle unchanged");
        int worst = 0;
        for (int i = 0; i < move.v.steps; ++i)
        {
            move.v.current = (move.v.current + 1) % move.v.steps;
            move.mailbox.write (move.v);
            move.paint();
            worst = std::max (worst, changes (prev, move.frame));
            prev = move.frame;
        }
        std::printf ("step pixels / %-12s max %d\n", names[state], worst);
        check (worst < 3000, "step budget");
    }
    Rig list;
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
    std::puts ("idle pixels             0");
}

} // namespace

int main (int argc, char** argv)
{
    const char* mode = argc > 1 ? argv[1] : "--render";
    if (!std::strcmp (mode, "--bench")) { bench(); return 0; }
    const bool render = !std::strcmp (mode, "--render"), test = !std::strcmp (mode, "--test");
    check (render || test || !std::strcmp (mode, "--hashes"), "expected --render, --test, --bench or --hashes");
    FILE* ref = test ? std::fopen ("tools/ui_preview/hashes.txt", "r") : nullptr;
    if (test) { check (ref, "golden hash file"); controls(); damage(); }
    for (unsigned i = 0; i < std::size (names); ++i)
    {
        Rig r;
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
        else if (render) ppm (names[i], r.frame);
        else std::printf ("%016llx %s\n", (unsigned long long) hash (r.frame), names[i]);
    }
    if (test)
    {
        char extra[2];
        check (std::fscanf (ref, "%1s", extra) == EOF, "golden file has no extra states");
        std::fclose (ref);
        std::printf ("%zu golden states, controls, K1-K3, allocation checks, idle/whole/stride and lengths 0-32 passed\n", std::size (names));
    }
    if (render)
    {
        Rig r;
        for (int step = 0; step < 16; ++step)
        {
            r.v.current = step;
            r.mailbox.write (r.v);
            r.paint();
            char s[16];
            std::snprintf (s, sizeof (s), "step-%02d", step + 1);
            ppm (s, r.frame);
        }
    }
}
