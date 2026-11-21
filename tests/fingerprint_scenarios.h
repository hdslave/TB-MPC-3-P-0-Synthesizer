// TB-MPC-3-P-0's fingerprint scenarios (tests/fingerprint.cpp). Every one locks its seed: in Random mode a reset
// picks a new seed from the clock, so the sound would differ from run to run.
#pragma once

namespace
{
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
        s.values = randomValues (2000 + r, { "Regen", "Seed Mode" });
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
    return list;
}
} // namespace
