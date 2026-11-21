// The engine: the host's transport as TB-3PO's clock and reset inputs, the sequencer, and the voice it plays.
//
//   Clock: a pulse on every multiple of Clock Div from the song's top while the host plays. The applet waits 2 ms
//     after a clock for its CV inputs to settle before it steps; here the inputs are exact, so it steps on the clock.
//   Reset (the applet's Regen input): when Play starts (Reset: Play), or on every 1, 2, 4 or 8 bars from the song's
//     top (a Play or a jump between them places the pattern as if it had run from the last one), or never (Off).
//     Each one makes the pattern again, from a new seed unless the seed is locked. The Regen button is one too.
//   CV 1 (transpose): the Transpose knob plus the last MIDI note played (C4 is no transposition), held as a CV
//     keyboard holds its last note.
//   CV 2 (density): the Density knob (a host's automation of it does what CV 2 did).
//   The gate's half-step timer: half a clock at the host's tempo. (The applet measures it from the last two clocks:
//     the same while the tempo holds; on the module the first note after the clock starts is long.)
#pragma once

#include "params.h"
#include "tb3po.h"
#include "voice.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

// tests/reference.cpp sees each step and gate change through this (0: a clock's step, 1: the half-step timer).
#ifndef TBMPC_TRACE
#define TBMPC_TRACE(what)
#endif

namespace tbmpc
{
class Engine
{
public:
    void prepare (double hostRate, uint32_t)
    {
        rate = hostRate;
        voice.prepare (hostRate);
        reset();
    }

    void reset()
    {
        voice.reset();
        seq.stop();
        voice.gate (false, false);
        playing = false;
        lastClock = -1;
        gateOffPending = false;
        regenPressed = false;
        frameCount = 0;
    }

    // First values, before anything plays: the applet's Start(), then every parameter.
    void start (const float* values)
    {
        seq.start (entropy());
        for (uint32_t i = 0; i < paramCount; ++i)
            set (paramTable[i].id, values[i]);
        seq.regenerate();
    }

    // A parameter's value, from the frame the next render starts.
    void set (uint32_t id, float v)
    {
        if (id >= ParamId::count)
            return;
        knob[id] = v;
        switch (id)
        {
            case ParamId::seedMode: seq.setLocked (v >= 0.5f); break;
            case ParamId::seed: seq.setSeed ((uint16_t) v); break;
            case ParamId::density: seq.setDensity ((int) v + 7); break;
            case ParamId::scale: seq.setScale ((int) v); break;
            case ParamId::root: seq.setRoot ((int) v); break;
            case ParamId::octave: seq.setOctave ((int) v); break;
            case ParamId::steps: seq.setSteps ((int) v); break;
            case ParamId::transpose: transposeKnob = (int) v; updateTranspose(); break;
            case ParamId::clockDiv: regridWanted = true; break;
            case ParamId::regen: break;
            default: voice.set (id, v); break;
        }
    }

    // The Regen button: a reset pulse now.
    void press (uint32_t id)
    {
        if (id == ParamId::regen)
            regenPressed = true;
    }

    // A MIDI note: it transposes the pattern from the next step, and holds (as a CV keyboard's last note does).
    void note (int key)
    {
        midiTranspose = key - 60;
        updateTranspose();
    }

    // The seed the last reset chose (Seed Mode: Random), once: the host must hear it as the Seed parameter's value.
    bool takeNewSeed (uint16_t& s)
    {
        if (! seedChanged)
            return false;
        seedChanged = false;
        s = seq.currentSeed();
        return true;
    }

    // Where the sequence's own count is: for a host that gives no song position (0 when it last stopped).
    double ownPosition() const { return playing ? position : 0.0; }

    // The host's transport at the start of a process call of `frames`. barBeats: quarter notes in a bar.
    void transport (bool isPlaying, double beats, double bpm, double barBeats, uint32_t frames)
    {
        perFrame = bpm / 60.0 / rate;
        bar = barBeats > 0.0 ? barBeats : 4.0;
        const double window = (double) std::max (frames, lastBlock);
        lastBlock = frames;
        if (! isPlaying)
        {
            if (playing)
            {
                // Stopped: the gate shuts (the note releases).
                seq.stop();
                voice.gate (false, false);
                gateOffPending = false;
            }
            playing = false;
            return;
        }
        // A Play, or a place other than where the last block ended (a jump, a loop going round): line up again.
        const bool started = ! playing;
        if (started || std::fabs (beats - position) > 0.5 * perFrame)
            relocate (beats, window, started);
        playing = true;
        position = beats;
    }

    void render (float* left, float* right, uint32_t frames)
    {
        while (frames > 0)
        {
            if (regenPressed)
            {
                regenPressed = false;
                resetPulse();
            }
            uint32_t run = frames;
            if (playing)
            {
                if (regridWanted)
                    regrid();
                const double next = (double) (lastClock + 1) * stepBeats();
                const double ahead = (next - position) / perFrame;
                if (ahead <= 1.0e-6)
                {
                    ++lastClock;
                    clock (lastClock);
                    continue;
                }
                const double untilClock = std::ceil (ahead - 1.0e-6);
                if (untilClock < (double) run)
                    run = (uint32_t) untilClock;
            }
            if (gateOffPending)
            {
                if (gateOffIn == 0)
                {
                    gateOffPending = false;
                    seq.halfStepPassed();
                    voice.gate (seq.gateCv > 0, seq.gateCv == Tb3po::gateAccent);
                    TBMPC_TRACE (1);
                    continue;
                }
                run = std::min (run, gateOffIn);
            }
            voice.render (left, right, run);
            frameCount += run;
            if (gateOffPending)
                gateOffIn -= run;
            if (playing)
                position += run * perFrame;
            left += run;
            if (right)
                right += run;
            frames -= run;
        }
    }

    const Tb3po& sequencer() const { return seq; }

private:
    double stepBeats() const { return clockDivBeats[(int) knob[ParamId::clockDiv]]; }
    int resetEvery() const { return resetBars[(int) knob[ParamId::reset]]; } // bars, 0: at Play or never
    bool resetAtPlay() const { return (int) knob[ParamId::reset] == 0; }

    // The bar-reset line a beat is in (counting from the song's top).
    int64_t lineOf (double beats) const { return (int64_t) std::floor (beats / (resetEvery() * bar) + 1.0e-9); }
    // A clock is a reset clock when a bar-reset line falls in (the clock before, this clock].
    bool resetClock (int64_t k) const
    {
        const double grid = stepBeats();
        return resetEvery() > 0 && lineOf ((double) k * grid) != lineOf ((double) (k - 1) * grid);
    }

    void relocate (double beats, double window, bool started)
    {
        const double grid = stepBeats();
        const double b = std::floor (beats / grid + 1.0e-9);
        // Just past a clock (less than a block: a loop's wrap inside the last block, a Play landing a hair late):
        // that clock comes at once; further in, the next is the first.
        const double late = (beats - b * grid) / perFrame;
        lastClock = (int64_t) b - (late < window ? 1 : 0);
        position = beats;
        regridWanted = false;
        const int64_t first = lastClock + 1;
        if (resetEvery() > 0)
        {
            if (resetClock (first))
                return; // the first clock is a reset clock: it resets as it comes
            // Between lines: as if reset at the last one's clock and stepped on since (a Play is a reset there too).
            const double line = (double) lineOf ((double) first * grid) * resetEvery() * bar;
            const int64_t from = (int64_t) std::ceil (line / grid - 1.0e-9);
            if (started)
                resetPulse();
            seq.place (first - from);
            return;
        }
        if (started && resetAtPlay())
            resetPulse();
    }

    // Clock Div changed: count clocks on the new grid from here, without a burst of them.
    void regrid()
    {
        lastClock = (int64_t) std::floor (position / stepBeats() + 1.0e-9);
        regridWanted = false;
    }

    void resetPulse()
    {
        const uint16_t before = seq.currentSeed();
        seq.reset (entropy());
        seedChanged = seedChanged || seq.currentSeed() != before;
    }

    void clock (int64_t k)
    {
        if (resetClock (k))
            resetPulse();
        seq.clock();
        voice.pitch (seq.slideStart, seq.slideEnd);
        voice.gate (seq.gateCv > 0, seq.gateCv == Tb3po::gateAccent);
        if (seq.opened)
        {
            gateOffPending = true;
            gateOffIn = (uint32_t) std::max (1.0, std::floor (0.5 * stepBeats() / perFrame));
        }
        TBMPC_TRACE (0);
    }

    void updateTranspose()
    {
        const int semitones = std::clamp (transposeKnob + midiTranspose, -36, 72); // the module's CV range, -3 to +6 V
        seq.setTransposeInput (semitones * 128);
    }

    // The module seeds random() from micros() for a new seed; here, the time and a count of the seeds asked for.
    uint32_t entropy()
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const uint64_t us = (uint64_t) std::chrono::duration_cast<std::chrono::microseconds> (now).count();
        uint32_t x = (uint32_t) (us ^ (us >> 32)) ^ (0x9e3779b9u * ++draws);
        x ^= x >> 16, x *= 0x7feb352du, x ^= x >> 15;
        return x;
    }

    Tb3po seq;
    Voice voice;
    double rate = 48000.0, perFrame = 0.0, position = 0.0, bar = 4.0;
    float knob[ParamId::count] {};
    int transposeKnob = 0, midiTranspose = 0;
    bool playing = false, regridWanted = false, gateOffPending = false, regenPressed = false, seedChanged = false;
    int64_t lastClock = -1;
    uint32_t gateOffIn = 0, lastBlock = 0, draws = 0;
    uint64_t frameCount = 0; // frames rendered since prepare()
};
} // namespace tbmpc
