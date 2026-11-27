// Shared by the MPC and desktop builds. The knob runs from 1/4 to 8 cycles per beat.
#pragma once

#include <cmath>
#include <cstdint>

namespace tbmpc
{
constexpr float lfoMinRate = 0.25f, lfoMaxRate = 8.0f, lfoDefaultRate = 1.0f;
constexpr float pitchLfoOctaves = 2.0f / 12.0f, cutoffLfoOctaves = 3.0f;

struct LfoDetent { double cyclesPerBeat; const char* label; };
inline constexpr LfoDetent lfoDetents[] = { { 1.0, "1/1" }, { 4.0 / 3.0, "3/4" }, { 2.0, "1/2" }, { 3.0, "1/3" } };

inline const LfoDetent* lfoDetent (float rate)
{
    // A half-step either side on the MPC's 7-bit pots. Keep the raw knob value so fine adjustments can cross the detent.
    constexpr double radius = (lfoMaxRate - lfoMinRate) / (2.0 * 127.0);
    for (const auto& d : lfoDetents)
        if (std::fabs (rate - d.cyclesPerBeat) <= radius)
            return &d;
    return nullptr;
}

// The tone LFOs' clock: beats counted on at the tempo from the song's place at a locate. While the song plays it keeps
// to the song's place, beyond this many quarters (a little over hdslaveOS's own error from block to block, 0.0021).
constexpr double lfoSlack = 0.0025;

class LfoClock
{
public:
    double beatAt (uint64_t frame) const
    {
        const double b = beat + (double) (frame - anchor) * step;
        return b < held ? held : b;
    }

    // At a process call's start: the plug-in's frame, the tempo's beats a frame, a locate (Play, a jump) and, `playing`,
    // the song's place. Playing, a clock behind the song by more than lfoSlack (MTC's speed trim adds up; a tempo change
    // inside a block) is set on it; one ahead of it (a plug-in two tracks play is run twice a block, the next block's
    // first call told a block back) holds where it got to until the song is past it, so no stretch is counted twice.
    void set (uint64_t frame, double beatsPerFrame, bool locate, double songBeat, bool playing)
    {
        if (locate || step != beatsPerFrame)
        {
            beat = locate ? songBeat : beatAt (frame);
            anchor = frame;
            step = beatsPerFrame;
            held = -1.0e300;
        }
        if (! playing || locate)
            return;
        const double at = beatAt (frame), off = songBeat - at;
        if (off > lfoSlack || off < -lfoSlack)
        {
            held = off > 0.0 ? -1.0e300 : at;
            beat = songBeat;
            anchor = frame;
        }
    }

private:
    uint64_t anchor = 0;
    double beat = 0.0, step = 0.0;
    double held = -1.0e300; // the beat it holds at until the song is past it (none: -1e300)
};

class SineLfo
{
public:
    void sync() { phase = beat = 0.0; }

    void setRate (float rate, double at)
    {
        const auto* detent = lfoDetent (rate);
        const double next = detent ? detent->cyclesPerBeat : rate;
        if (next != cycles)
        {
            phase = phaseAt (at);
            phase -= std::floor (phase);
            beat = at;
            cycles = next;
        }
    }

    float valueAt (double at) const
    {
        const double p = phaseAt (at);
        const float cycle = (float) (p - std::floor (p + 0.5));
        const float half = std::fabs (cycle);
        // Fold onto [-pi/2, pi/2]. The ninth-order sine is within 4e-6 there.
        const float x = 6.283185307179586f * std::copysign (std::fmin (half, 0.5f - half), cycle);
        const float x2 = x * x;
        return x * (1.0f + x2 * (-1.0f / 6.0f + x2 * (1.0f / 120.0f + x2 * (-1.0f / 5040.0f + x2 / 362880.0f))));
    }

private:
    double phaseAt (double at) const { return phase + (at - beat) * cycles; }

    double phase = 0.0, beat = 0.0, cycles = lfoDefaultRate;
};
} // namespace tbmpc
