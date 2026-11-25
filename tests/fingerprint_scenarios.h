// Slink's fingerprint scenarios (tests/fingerprint.cpp). Every one locks its seed: in Random mode a reset
// picks a new seed from the clock, so the sound would differ from run to run.
#pragma once

#include <cmath>

namespace
{
// The parameters of Slink 0.1, in its table's order: the random scenarios draw for these.
const std::vector<std::string> order01 { "Tuning", "Cutoff", "Resonance", "Env Mod", "Decay", "Accent", "Waveform", "Volume", "Slide",
    "Seed Mode", "Seed", "Regen", "Density", "Scale", "Root", "Octave", "Steps", "Transpose", "Clock Div", "Reset" };
// 0.4's: every parameter but the ones that would silence or reseed it (Mode, the buttons, Seed Mode).
const std::vector<std::string> order04 { "Tuning", "Cutoff", "Resonance", "Env Mod", "Decay", "Accent", "Waveform", "Volume", "Slide",
    "Slide Time", "Accent Decay", "Drive", "Filter", "Peak Spread", "Seed", "Mutate", "Density", "Scale", "Root", "Octave", "Steps",
    "Gate", "Transpose", "Clock Div", "Swing", "Reset" };

std::vector<Scenario> scenarios()
{
    std::vector<Scenario> list;
    for (const char* key : { "default", "acid-line", "root-pulse", "square-lead", "dub-steps" })
    {
        Scenario s;
        s.name = std::string ("preset-") + key;
        s.preset = key;
        s.values = { { "Seed Mode", 1.0 } };
        list.push_back (s);
    }
    // The bench's patches.
    Scenario worst;
    worst.name = "bench-worst";
    worst.values = { { "Seed Mode", 1 }, { "Seed", 0x5a17 }, { "Density", -7 }, { "Clock Div", 9 }, { "Resonance", 1.0 }, { "Env Mod", 1.0 },
                     { "Accent", 1.0 }, { "Decay", 1.0 }, { "Slide", 1 }, { "Waveform", 1 } };
    list.push_back (worst);
    Scenario typical;
    typical.name = "bench-typical";
    typical.values = { { "Seed Mode", 1 }, { "Seed", 0x5a17 } };
    list.push_back (typical);
    // Random settings with Regen, MIDI and knobs moving while it plays.
    for (uint32_t r = 0; r < 24; ++r)
    {
        Scenario s;
        s.name = "random-" + std::to_string (r);
        s.values = randomValues (2000 + r, { "Regen", "Seed Mode" }, order01);
        s.values.push_back ({ "Seed Mode", 1.0 });
        s.seconds = 3.0, s.bpm = 140.0;
        s.events = { { 33600, Event::param, "Regen", 1.0 }, { 57611, Event::noteOn, "", 67 }, { 70000, Event::noteOff, "", 67 },
                     { 91207, Event::param, "Cutoff", 0.25 }, { 100003, Event::param, "Slide", 1.0 }, { 120000, Event::param, "Density", -2.0 },
                     { 130001, Event::param, "Transpose", 5.0 } };
        list.push_back (s);
    }
    // Other block sizes (the same sound as random-0) and other rates.
    for (uint32_t block : { 1u, 37u, 4096u })
    {
        Scenario s = list[7];
        s.name = "random-0-block-" + std::to_string (block);
        s.block = block;
        list.push_back (s);
    }
    for (double rate : { 44100.0, 96000.0 })
    {
        Scenario s;
        s.name = "rate-" + std::to_string ((int) rate);
        s.preset = "acid-line";
        s.rate = rate;
        s.seconds = 4.0;
        list.push_back (s);
    }
    // Stopped, started again, jumped, a loop's wrap; and the same with a reset every bar.
    for (const char* reset : { "Play", "1 Bar" })
    {
        Scenario moves;
        moves.name = std::string ("stop-start-jump-") + (reset[0] == 'P' ? "play" : "bar");
        moves.preset = "acid-line";
        moves.values = { { "Reset", reset[0] == 'P' ? 0.0 : 1.0 } };
        moves.seconds = 6.0;
        moves.transport = [] (uint64_t at, double bpm, double rate) -> std::pair<bool, double> {
            const double f = (double) at;
            if (at < 48000) return { true, f * bpm / 60.0 / rate };
            if (at < 72000) return { false, 2.0 };
            if (at < 144000) return { true, (f - 72000) * bpm / 60.0 / rate };
            if (at < 200000) return { true, 9.3 + (f - 144000) * bpm / 60.0 / rate };
            return { true, 100.0 / 24000.0 + (f - 200000) * bpm / 60.0 / rate };
        };
        list.push_back (moves);
    }
    // 0.2: Swing (with a reset every bar and a jump), Seed Back, Mode: Notes, and a switch between the modes.
    for (double swing : { 0.62, 0.75 })
    {
        Scenario s;
        s.name = "swing-" + std::to_string ((int) std::lround (swing * 100));
        s.preset = "acid-line";
        s.values = { { "Swing", swing }, { "Reset", 1.0 } };
        s.seconds = 6.0;
        s.transport = [] (uint64_t at, double bpm, double rate) -> std::pair<bool, double> {
            const double f = (double) at;
            if (at < 150000) return { true, f * bpm / 60.0 / rate };
            return { true, 6.6 + (f - 150000) * bpm / 60.0 / rate };
        };
        list.push_back (s);
    }
    Scenario back;
    back.name = "seed-back";
    back.preset = "acid-line";
    back.values = { { "Seed Mode", 1.0 } };
    back.events = { { 96000, Event::param, "Seed", (double) 0x1234 }, { 192000, Event::param, "Seed", (double) 0x4321 },
                    { 288000, Event::param, "Seed Back", 1.0 }, { 336000, Event::param, "Seed Back", 1.0 } };
    list.push_back (back);
    for (bool playing : { true, false })
    {
        Scenario s;
        s.name = std::string ("notes-") + (playing ? "playing" : "stopped");
        s.preset = "acid-line";
        s.values = { { "Mode", 1.0 } };
        s.seconds = 4.0;
        // A line with held notes over others (glides), loud ones (accents), a let-go back to a held note.
        const int keys[] = { 36, 48, 43, 39, 36, 41, 46, 36 };
        for (int i = 0; i < 8; ++i)
        {
            const uint64_t on = 6000 + (uint64_t) i * 18000;
            s.events.push_back ({ on, Event::noteOn, "", (double) keys[i] });
            s.events.push_back ({ on + (i % 3 == 1 ? 24000 : 9000), Event::noteOff, "", (double) keys[i] });
        }
        if (! playing)
            s.transport = [] (uint64_t, double, double) -> std::pair<bool, double> { return { false, 0.0 }; };
        list.push_back (s);
    }
    Scenario both;
    both.name = "mode-switch";
    both.preset = "root-pulse";
    both.seconds = 5.0;
    both.events = { { 60000, Event::param, "Mode", 1.0 }, { 70000, Event::noteOn, "", 40 }, { 100000, Event::noteOn, "", 47 },
                    { 120000, Event::param, "Mode", 0.0 }, { 130000, Event::noteOff, "", 40 }, { 150000, Event::noteOn, "", 62 } };
    list.push_back (both);
    Scenario noteBass;
    noteBass.name = "preset-note-bass";
    noteBass.preset = "note-bass";
    noteBass.seconds = 4.0;
    const int line[] = { 36, 36, 48, 39, 41, 36, 43, 34 };
    for (int i = 0; i < 8; ++i)
    {
        const uint64_t on = (uint64_t) i * 12000;
        noteBass.events.push_back ({ on, Event::noteOn, "", (double) line[i] });
        noteBass.events.push_back ({ on + (i == 2 || i == 5 ? 13000 : 6000), Event::noteOff, "", (double) line[i] });
    }
    list.push_back (noteBass);
    // 0.3: Gate short and long; pitch bend and the mod wheel in both modes. (Mutate acts only on Random resets,
    // whose seeds come from the clock: tests/host.cpp checks it on the sequencer itself.)
    for (double gate : { 0.15, 0.9 })
    {
        Scenario s;
        s.name = "gate-" + std::to_string ((int) std::lround (gate * 100));
        s.preset = "acid-line";
        s.values = { { "Gate", gate } };
        s.seconds = 5.0;
        list.push_back (s);
    }
    for (bool notes : { true, false })
    {
        Scenario s;
        s.name = std::string ("bend-wheel-") + (notes ? "notes" : "tb-3po");
        s.preset = notes ? "note-bass" : "acid-line";
        s.seconds = 4.0;
        if (notes)
            s.events = { { 2000, Event::noteOn, "", 43.0 }, { 150000, Event::noteOff, "", 43.0 } };
        for (int i = 0; i < 24; ++i) // a bend up and back down, the wheel up and down
        {
            const int bendValue = 8192 + (int) (8191.0 * std::sin (i * 0.4)), wheel = (int) (63.5 + 63.5 * std::sin (i * 0.27));
            s.events.push_back ({ (uint64_t) (6000 + i * 6000), Event::midi, "", (double) ((0xe0u << 16) | ((uint32_t) (bendValue & 0x7f) << 8) | (uint32_t) (bendValue >> 7)) });
            s.events.push_back ({ (uint64_t) (9000 + i * 6000), Event::midi, "", (double) ((0xb0u << 16) | (1u << 8) | (uint32_t) wheel) });
        }
        list.push_back (s);
    }
    // 0.4: Drive at a third and full, and turned while it plays; each filter, Twin Peak's spread moving, the three
    // stages ringing on their own at full Resonance; Slide Time short and long (the 303 glide and a note's); Accent
    // Decay long; random patches over every 0.4 parameter.
    for (double drive : { 0.3, 1.0 })
    {
        Scenario s;
        s.name = "drive-" + std::to_string ((int) std::lround (drive * 100));
        s.preset = "acid-line";
        s.values = { { "Drive", drive } };
        s.seconds = 4.0;
        list.push_back (s);
    }
    Scenario driveTurned;
    driveTurned.name = "drive-turned";
    driveTurned.preset = "root-pulse";
    driveTurned.seconds = 4.0;
    for (int i = 0; i < 16; ++i)
        driveTurned.events.push_back ({ (uint64_t) (4000 + i * 11000), Event::param, "Drive", 0.5 + 0.5 * std::sin (i * 0.5) });
    list.push_back (driveTurned);
    for (const char* filter : { "Twin Peak", "18 dB" })
        for (const char* preset : { "acid-line", "dub-steps" })
        {
            Scenario s;
            s.name = std::string ("filter-") + (filter[0] == 'T' ? "twin-peak-" : "18db-") + preset;
            s.preset = preset;
            s.values = { { "Filter", filter[0] == 'T' ? 1.0 : 2.0 } };
            s.seconds = 4.0;
            list.push_back (s);
        }
    Scenario spread;
    spread.name = "twin-peak-spread";
    spread.preset = "acid-line";
    spread.values = { { "Filter", 1.0 }, { "Resonance", 0.95 } };
    spread.seconds = 4.0;
    for (int i = 0; i < 16; ++i)
        spread.events.push_back ({ (uint64_t) (3000 + i * 11000), Event::param, "Peak Spread", 2.25 + 1.75 * std::sin (i * 0.6) });
    list.push_back (spread);
    Scenario ringing;
    ringing.name = "18db-self-oscillating";
    ringing.preset = "dub-steps";
    ringing.values = { { "Filter", 2.0 }, { "Resonance", 1.0 } };
    ringing.seconds = 4.0;
    list.push_back (ringing);
    Scenario switching;
    switching.name = "filter-switched";
    switching.preset = "acid-line";
    switching.seconds = 4.0;
    for (int i = 0; i < 9; ++i)
        switching.events.push_back ({ (uint64_t) (9000 + i * 19000), Event::param, "Filter", (double) ((i + 1) % 3) });
    list.push_back (switching);
    for (double slide : { 0.0, 1.0 })
        for (const char* preset : { "square-lead", "note-bass" })
        {
            Scenario s;
            s.name = std::string ("slide-time-") + (slide > 0.5 ? "long-" : "short-") + preset;
            s.preset = preset;
            s.values = { { "Slide Time", slide } };
            s.seconds = 4.0;
            if (preset[0] == 'n')
                for (int i = 0; i < 8; ++i) // a legato line: each note held over the next
                {
                    const double key = 36 + (i * 7) % 13;
                    s.events.push_back ({ (uint64_t) i * 12000, Event::noteOn, "", key });
                    s.events.push_back ({ (uint64_t) i * 12000 + 15000, Event::noteOff, "", key });
                }
            list.push_back (s);
        }
    Scenario accentLong;
    accentLong.name = "accent-decay-long";
    accentLong.preset = "root-pulse";
    accentLong.values = { { "Accent Decay", 1.0 } };
    accentLong.seconds = 4.0;
    list.push_back (accentLong);
    for (uint32_t r = 0; r < 8; ++r)
    {
        Scenario s;
        s.name = "random04-" + std::to_string (r);
        s.values = randomValues (4000 + r, {}, order04);
        s.values.push_back ({ "Seed Mode", 1.0 });
        s.seconds = 3.0, s.bpm = 132.0;
        s.events = { { 41000, Event::param, "Filter", (double) (r % 3) }, { 77000, Event::param, "Drive", 0.1 * r } };
        list.push_back (s);
    }
    // A 4-beat loop at 110 BPM wrapping inside a block, told at the next block as Melodee does, in whole blocks and in
    // parts of 32 frames (the place told only at each 128), the pattern resetting every bar.
    for (uint32_t part : { 128u, 32u })
    {
        Scenario s;
        s.name = "loop-wrap-" + std::to_string (part);
        s.preset = "acid-line";
        s.values = { { "Seed Mode", 1.0 }, { "Steps", 13 }, { "Reset", 1 } };
        s.bpm = 110.0, s.seconds = 10.0, s.block = part;
        s.transport = [] (uint64_t at, double bpm, double rate) -> std::pair<bool, double> {
            const uint64_t block = at / 128 * 128;
            const double perFrame = bpm / 60.0 / rate;
            return { true, std::fmod ((double) block * perFrame, 4.0) + (double) (at - block) * perFrame };
        };
        list.push_back (s);
    }
    // 0.5: the step pads (Mode: TB-3PO, notes 36 on, Steps 24 so up to 59), stopped and playing: a pad every 50 blocks
    // (loud ones as CLAP notes, soft ones as MIDI), two on one frame, a pad on, on, off, All Sound Off over one, a
    // transposing note and a pad after it; the same played in blocks of 37.
    for (bool playing : { false, true })
    {
        Scenario s;
        s.name = std::string ("step-pads-") + (playing ? "playing" : "stopped");
        s.preset = "acid-line";
        s.values = { { "Seed Mode", 1.0 }, { "Steps", 24.0 } };
        s.seconds = 4.0;
        for (int i = 0; i < 24; ++i)
        {
            const uint64_t at = 128 * (uint64_t) (10 + i * 50);
            const int key = 36 + (i * 7) % 24;
            if (i % 3 == 2)
                s.events.push_back ({ at, Event::midi, "", (double) ((0x90u << 16) | ((uint32_t) key << 8) | 64u) });
            else
                s.events.push_back ({ at, Event::noteOn, "", (double) key });
            s.events.push_back ({ at + 300, Event::noteOff, "", (double) key });
        }
        s.events.push_back ({ 128 * 1200, Event::noteOn, "", 40.0 });
        s.events.push_back ({ 128 * 1200, Event::noteOn, "", 47.0 });
        s.events.push_back ({ 128 * 1250, Event::noteOn, "", 45.0 });
        s.events.push_back ({ 128 * 1270, Event::noteOn, "", 45.0 });
        s.events.push_back ({ 128 * 1290, Event::noteOff, "", 45.0 });
        s.events.push_back ({ 128 * 1320, Event::noteOn, "", 50.0 });
        s.events.push_back ({ 128 * 1330, Event::midi, "", (double) ((0xb0u << 16) | (120u << 8)) });
        s.events.push_back ({ 128 * 1380, Event::noteOn, "", 64.0 });
        s.events.push_back ({ 128 * 1390, Event::noteOn, "", 36.0 });
        if (! playing)
            s.transport = [] (uint64_t, double, double) -> std::pair<bool, double> { return { false, 0.0 }; };
        list.push_back (s);
        if (playing)
        {
            s.name += "-block-37";
            s.block = 37;
            list.push_back (s);
        }
    }
    return list;
}
} // namespace
