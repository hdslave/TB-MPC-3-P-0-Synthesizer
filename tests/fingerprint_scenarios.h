// TB-MPC-3-P-0's fingerprint scenarios (tests/fingerprint.cpp). Every one locks its seed: in Random mode a reset
// picks a new seed from the clock, so the sound would differ from run to run.
#pragma once

namespace
{
// The parameters of TB-MPC-3-P-0 0.1, in its table's order: the random scenarios draw for these.
const std::vector<std::string> order01 { "Tuning", "Cutoff", "Resonance", "Env Mod", "Decay", "Accent", "Waveform", "Volume", "Slide",
    "Seed Mode", "Seed", "Regen", "Density", "Scale", "Root", "Octave", "Steps", "Transpose", "Clock Div", "Reset" };

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
    return list;
}
} // namespace
