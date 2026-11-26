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
//   The gate's half-step timer: half a clock at the host's tempo, held to 1 - 1000 BPM. (The applet measures it from
//     the last two clocks: the same while the tempo holds; on the module the first note after the clock starts is
//     long.)
//   Swing: the second clock of each pair comes later, at Swing's share of the pair, as an MPC's swing does; a
//     swung step's gate is half of its own length.
//   Seed Back: the seeds whose patterns played, the last 16, to go back to; going back locks the seed.
//   Gate: the half-step timer's share of the step (half by default, as the applet). Mutate: see Tb3po::reset.
//   Pitch bend and the mod wheel: they bend the voice and open its filter, in either mode.
//   Mode: Notes: MIDI notes play the voice instead of TB-3PO (whose count goes on, unheard, so switching back lands
//     in time): a note over a held one glides to it with the 303's glide and keeps the gate open, a velocity of 100
//     or more accents, and letting go of the newest note goes back to the one still held. A key played again while
//     held is a new note, as if let go first (it strikes again, or glides over another key still held). The note
//     sounding let go on the frame it was played (struck, or glided to), before a sample of it sounded, keeps the
//     voice for shortGateFrames, as if its off came then: keys let go in that time leave it alone, and at its end the
//     voice goes to the newest key still held, or the gate shuts. A choke of it ends it at once; All Notes Off, All
//     Sound Off or a Mode switch shut the gate; a new note takes the voice as after its off.
//   Mode: Sequence's step pads: notes 36 .. 36 + max (16, Steps) - 1 play steps 1 .. (hdslaveOS's pads A01 - A16 at
//     its default octave and key), host playing or not. A pad plays its step once, as the sequencer would play it
//     (its pitch with the scale, root, octave and transposition, from the pattern its next clock plays; its accent,
//     or the pad's if hit at accentVelocity or louder), at once (no slide), gated or a rest, for Gate's share of a
//     clock (120 BPM until the host tells a tempo); its note off does nothing, a choke or All Sound Off stops it.
//     The play head, the seed, Seed Back's memory and the transposition stay as they are: the pad's step has the
//     voice until the sequencer's next clock, which takes it back as it would have been (a pad on a clock's own
//     frame gives way to the clock's step). Two pads on one frame: the louder plays, on a tie the later. Other notes
//     transpose, as before.
#pragma once

#include "params.h"
#include "tb3po.h"
#include "voice.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

// tests/reference.cpp sees each step and gate change through this (0: a clock's step, 1: the half-step timer).
#ifndef TBMPC_TRACE
#define TBMPC_TRACE(what)
#endif

namespace tbmpc
{
// A clock less than this many frames away is due now. CLAP gives the song's place in steps of 2^-31 beats, which
// at 48 kHz is about 1e-5 frames: a clock that falls on a block's first frame could otherwise come a frame late, by
// how the host's rounding fell for that block, so the sound would depend on the block size.
constexpr double clockSlack = 1.0e-3;
// The host's tempo is held to this range (see Engine::transport).
constexpr double minBpm = 1.0, maxBpm = 1000.0;
// Mode: Notes, the note sounding let go on the very frame it was played (hdslaveOS sends a note shorter than its
// 128-frame block so: its on and off both at frame 0, struck or slid to) keeps the voice this long, as a note a block
// long would.
constexpr uint32_t shortGateFrames = 128;

class Engine
{
public:
    void prepare (double hostRate, uint32_t)
    {
        rate = hostRate > 0.0 && std::isfinite (hostRate) ? hostRate : 48000.0; // a host's no rate at all
        voice.prepare (rate);
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
        clockedAt = -(int64_t (1) << 40); // no clock played yet
        clockedReset = false;
        held = 0;
        padWanted = padHasVoice = padOffPending = false;
        shortOffPending = false;
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
            case ParamId::mode:
                if ((v >= 0.5f) != notesMode)
                {
                    // Either way the note sounding stops: a held key or a sequencer's gate belongs to the other mode.
                    notesMode = v >= 0.5f;
                    held = 0;
                    seq.stop();
                    gateOffPending = false;
                    padWanted = padHasVoice = padOffPending = false;
                    shortOffPending = false;
                    voice.gate (false, false);
                }
                break;
            case ParamId::regen:
            case ParamId::seedBack:
            case ParamId::swing:
            case ParamId::gate:
            case ParamId::mutate: break;
            default: voice.set (id, v, frameCount); break;
        }
    }

    // The Regen button: a reset pulse now. Seed Back: the seed before, locked.
    void press (uint32_t id)
    {
        if (id == ParamId::regen)
            regenPressed = true;
        else if (id == ParamId::seedBack)
            seedBack();
    }

    // For the screen: the pattern, and whether the host's clock is moving it (TB-3PO mode, the host playing).
#ifdef TBMPC_SP16
    void setStepEdits(const sp16::StepEdits& edits) { seq.setStepEdits(edits); }
#endif
    const Tb3po& sequencer() const { return seq; }
    bool running() const { return playing && ! notesMode; }

    // A MIDI note. TB-3PO: a step pad plays its step (at the next sound, after this frame's other events, so a note
    // transposing or a knob turned on the same frame applies to it); any other note transposes the pattern from the
    // next step, and holds (as a CV keyboard's last note does). Notes: it plays.
    void note (int key, float velocity)
    {
        if (! notesMode)
        {
            const int s = key - firstStepPad;
            if (s >= 0 && s < std::max (stepPads, seq.stepCount()))
            {
                // Two pads on one frame: the louder plays, once; on a tie the later.
                if (! padWanted || velocity >= wantedVelocity)
                    padWanted = true, wantedStep = s, wantedKey = key, wantedVelocity = velocity;
                return;
            }
            midiTranspose = key - 60;
            updateTranspose();
            return;
        }
        // A key played again while held (a host retriggering it before its note off: Melodee then sends one off for
        // both) counts as let go first: on its own it strikes again, over another held key it glides as any note.
        release (key);
        shortOffPending = false; // a short note's frames end: this note takes the voice (struck, or glided to over a key)
        const bool legato = held > 0;
        if (held == maxHeld)
            std::copy (heldKeys + 1, heldKeys + maxHeld, heldKeys), std::copy (heldAccent + 1, heldAccent + maxHeld, heldAccent),
                std::copy (heldAt + 1, heldAt + maxHeld, heldAt), --held;
        heldKeys[held] = (int8_t) key;
        heldAccent[held] = velocity >= accentVelocity - 1.0e-6f;
        heldAt[held] = (int64_t) frameCount;
        ++held;
        const int32_t cv = (key - noteAtZeroVolts) * 128;
        if (legato)
            voice.glide (cv); // the gate stays open: no new envelope
        else
        {
            voice.pitch (cv, cv);
            voice.gate (false, false); // still open (the key played again): shut on this frame, so both envelopes start over
        }
        voice.gate (true, heldAccent[held - 1]);
    }

    // A MIDI message other than a note: pitch bend (+-2 semitones), the mod wheel (CC 1), reset all controllers
    // (CC 121: bend and the wheel centred), All Sound Off (CC 120: panic in sequencer mode, below; in Notes
    // mode CC 120 and All Notes Off (CC 123) release the held notes).
    void midi (int status, int data1, int data2)
    {
        if (status == 0xe0)
            voice.bendTo ((float) (((data2 << 7) | data1) - 8192) * (bendSemitones / 8192.0f));
        else if (status == 0xb0 && data1 == 1)
            voice.modWheelTo ((float) data2 / 127.0f);
        else if (status == 0xb0 && data1 == 121)
            voice.bendTo (0.0f), voice.modWheelTo (0.0f);
        else if (status == 0xb0 && (data1 == 120 || data1 == 123) && notesMode && (held > 0 || shortOffPending))
        {
            held = 0;
            shortOffPending = false;
            voice.gate (false, false);
        }
        else if (status == 0xb0 && data1 == 120 && ! notesMode)
            panic(); // All Sound Off only: All Notes Off leaves a pad's one-shot step to its end, as its note off does
    }

    // A choke (key -1: every key). Notes: as the key's note off. TB-3PO: the pad's step that key played, if it still
    // sounds, stops (its note off does nothing: a pad plays its whole step).
    void choke (int key)
    {
        if (notesMode)
        {
            if (key >= 0)
                noteOff (key);
            else
                for (int k = 0; k < 128; ++k)
                    noteOff (k);
            if (shortOffPending && (key < 0 || key == shortKey)) // a short note's frames: a choke of it ends them now
            {
                shortOffPending = false;
                toNewestHeld();
            }
            return;
        }
        if (key < 0) // every key: the sequencer's own gate shuts too (it was played by no key)
        {
            panic();
            return;
        }
        if (padWanted && key == wantedKey)
            padWanted = false;
        if (padOffPending && key == padKey)
        {
            padOffPending = false;
            voice.gate (false, false);
        }
    }

    // All Sound Off in sequencer mode: the gate the sequencer holds (a slid step can hold it past
    // its timer) shuts now, as does a pad's step. The sequence goes on: the next clock plays its step as it would.
    void panic()
    {
        seq.stop();
        gateOffPending = false;
        padWanted = padHasVoice = padOffPending = false;
        voice.gate (false, false);
    }

    // A state was loaded: the voice's knobs settle at their new values at once, not eased over the next samples.
    void settle() { voice.settle(); }

    void noteOff (int key)
    {
        if (! notesMode || held == 0)
            return;
        const bool wasTop = heldKeys[held - 1] == key;
        int64_t playedAt = -1; // the frame this key was played (struck, or glided to)
        for (int i = 0; i < held; ++i)
            if (heldKeys[i] == key)
                playedAt = heldAt[i];
        const int before = held;
        release (key);
        if (held == before)
            return; // not held
        if (shortOffPending)
            return; // a short note has the voice: when its frames run out it goes to the newest key still held
        if (wasTop && playedAt == (int64_t) frameCount)
        {
            // The note sounding let go on the frame it was played, before a sample of it sounded: it keeps the voice
            // for a block's length first, as if its off came then.
            shortOffPending = true, shortOffIn = shortGateFrames, shortKey = key;
            return;
        }
        if (held == 0)
            voice.gate (false, false);
        else if (wasTop)
            toNewestHeld();
    }

    // The voice to the newest key still held (a glide, the gate open with its accent), or the gate shut if none is.
    void toNewestHeld()
    {
        if (held == 0)
        {
            voice.gate (false, false);
            return;
        }
        voice.glide ((heldKeys[held - 1] - noteAtZeroVolts) * 128);
        voice.gate (true, heldAccent[held - 1]);
    }

    // A value the engine set itself (a new seed from a reset, the seed and Seed Mode from Seed Back), once each: the
    // host must hear them as the parameters' values.
    bool takeChange (uint32_t& id, float& value)
    {
        if (changed & (1u << 0))
        {
            changed &= ~(1u << 0);
            id = ParamId::seed, value = (float) seq.currentSeed();
            return true;
        }
        if (changed & (1u << 1))
        {
            changed &= ~(1u << 1);
            id = ParamId::seedMode, value = knob[ParamId::seedMode];
            return true;
        }
        return false;
    }

    // Where the sequence's own count is: for a host that gives no song position (0 when it last stopped).
    double ownPosition() const { return playing ? position : 0.0; }

    // The host's transport at the start of a process call of `frames`. barBeats: quarter notes in a bar. The tempo is
    // held to 1 - 1000 BPM: a host's absurd tempo (0.0001, or infinite) would otherwise make the gate timers
    // overflow, or every clock due at once, forever.
    void transport (bool isPlaying, double beats, double bpm, double barBeats, uint32_t frames)
    {
        perFrame = (std::isnan (bpm) ? 120.0 : std::clamp (bpm, minBpm, maxBpm)) / 60.0 / rate;
        bar = barBeats > 0.0 ? barBeats : 4.0;
        const double window = (double) std::max (frames, lastBlock);
        lastBlock = frames;
        const bool started = ! playing;
        const double moved = std::fabs (beats - position);
        const bool relocateClock = started || (moved > 0.5 * perFrame && moved >= window * perFrame);
        voice.syncLfos (frameCount, perFrame, isPlaying && relocateClock, beats);
        if (! isPlaying)
        {
            if (playing)
            {
                // Stopped: the sequencer's gate shuts (the note releases); notes played by hand go on, and so does a
                // pad's step.
                seq.stop();
                if (! notesMode && ! padHasVoice)
                    voice.gate (false, false);
                gateOffPending = false;
            }
            playing = false;
            return;
        }
        // A Play, or a place other than where the last block ended by more than a block (a jump, a loop going
        // round): line up again. Less than that is drift (a tempo change inside the last block), followed as it is:
        // the clocks already played are not played again, one passed is played at once.
        if (relocateClock)
            relocate (beats, window, started);
        playing = true;
        position = beats;
    }

    void render (float* left, float* right, uint32_t frames)
    {
        if (frames && !playing && !regenPressed && seq.currentSeed() != seq.playingSeed())
            seq.regenerate();
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
                const double next = clockBeat (lastClock + 1);
                const double ahead = (next - position) / perFrame;
                if (ahead <= clockSlack)
                {
                    ++lastClock;
                    clock (lastClock);
                    continue;
                }
                const double untilClock = std::ceil (ahead - clockSlack);
                if (untilClock < (double) run)
                    run = (uint32_t) untilClock;
            }
            if (padWanted)
            {
                padWanted = false;
                playPad();
            }
            if (gateOffPending)
            {
                if (gateOffIn == 0)
                {
                    gateOffPending = false;
                    seq.halfStepPassed();
                    if (! padHasVoice) // a pad's step has the voice until the next clock
                        voice.gate (seq.gateCv > 0, seq.gateCv == Tb3po::gateAccent);
                    TBMPC_TRACE (1);
                    continue;
                }
                run = std::min (run, gateOffIn);
            }
            if (padOffPending)
            {
                if (padOffIn == 0)
                {
                    padOffPending = false;
                    voice.gate (false, false);
                    continue;
                }
                run = std::min (run, padOffIn);
            }
            if (shortOffPending)
            {
                if (shortOffIn == 0)
                {
                    shortOffPending = false;
                    toNewestHeld();
                    continue;
                }
                run = std::min (run, shortOffIn);
            }
            voice.render (left, right, run, frameCount);
            frameCount += run;
            if (gateOffPending)
                gateOffIn -= run;
            if (padOffPending)
                padOffIn -= run;
            if (shortOffPending)
                shortOffIn -= run;
            if (playing)
                position += run * perFrame;
            left += run;
            if (right)
                right += run;
            frames -= run;
        }
    }

private:
    double stepBeats() const { return clockDivBeats[(int) knob[ParamId::clockDiv]]; }
    bool swung() const { return knob[ParamId::swing] != 0.5f; }

    // Clock k's place in beats. Swung, every odd clock moves to its pair's start plus Swing's share of the pair; at
    // 50 % the clocks are k grids from the top, worked out exactly as before Swing was added.
    double clockBeat (int64_t k) const
    {
        const double grid = stepBeats();
        if (! swung() || (k & 1) == 0)
            return (double) k * grid;
        return (double) (k - 1) * grid + 2.0 * grid * (double) knob[ParamId::swing];
    }
    // The last clock at or before `beats`.
    int64_t clockAtOrBefore (double beats) const
    {
        const double grid = stepBeats();
        const int64_t b = (int64_t) std::floor (beats / grid + 1.0e-9);
        if (! swung() || (b & 1) == 0)
            return b;
        return beats / grid + 1.0e-9 < clockBeat (b) / grid ? b - 1 : b; // between the grid line and its late clock
    }
    // The first clock at or after `beats`.
    int64_t clockAtOrAfter (double beats) const
    {
        if (! swung())
            return (int64_t) std::ceil (beats / stepBeats() - 1.0e-9);
        const int64_t k = clockAtOrBefore (beats);
        return clockBeat (k) / stepBeats() >= beats / stepBeats() - 1.0e-9 ? k : k + 1;
    }
    int resetEvery() const { return resetBars[(int) knob[ParamId::reset]]; } // bars, 0: at Play or never
    bool resetAtPlay() const { return (int) knob[ParamId::reset] == 0; }

    // The bar-reset line a beat is in (counting from the song's top).
    int64_t lineOf (double beats) const { return (int64_t) std::floor (beats / (resetEvery() * bar) + 1.0e-9); }
    // A clock is a reset clock when a bar-reset line falls in (the clock before, this clock].
    bool resetClock (int64_t k) const
    {
        return resetEvery() > 0 && lineOf (clockBeat (k)) != lineOf (clockBeat (k - 1));
    }

    void relocate (double beats, double, bool started)
    {
        const int64_t b = clockAtOrBefore (beats);
        const double late = (beats - clockBeat (b)) / perFrame; // frames since the host's clock b
        // A jump whose clock b came on the very frame a clock played here: a loop gone round inside a block (the host
        // tells only at the next one, so the clocks ran on past the loop's end; its end is its start). That clock was
        // b: it is not played again (the pattern would step twice); a bar reset that b would have made, it makes now.
        // Otherwise, just past a clock (a Play landing a hair late, a wrap seen early enough): that clock comes at
        // once; further in, the next is the first. "Just past" is a fixed window, not the host's block: at 4096-frame
        // blocks a Play well into a step used to replay it, at 1-frame blocks a hair-late Play skipped it, so the
        // same Play sounded different in different hosts. 128 frames is the window hdslaveOS's blocks gave it.
        constexpr double playWindow = 128.0;
        const bool played = ! started && std::fabs ((double) ((int64_t) frameCount - clockedAt) - late) <= 2.0;
        lastClock = played ? b : b - (late < playWindow ? 1 : 0);
        position = beats;
        regridWanted = false;
        if (played && resetClock (b) && ! clockedReset)
            resetPulse();
        const int64_t first = lastClock + 1;
        if (resetEvery() > 0)
        {
            if (resetClock (first))
                return; // the first clock is a reset clock: it resets as it comes
            // Between lines: as if reset at the last one's clock and stepped on since (a Play is a reset there too).
            const double line = (double) lineOf (clockBeat (first)) * resetEvery() * bar;
            const int64_t from = clockAtOrAfter (line);
            if (started)
                resetPulse();
            seq.place (first - from);
            return;
        }
        if (started && resetAtPlay())
            resetPulse();
    }

    // Clock Div changed: count clocks on the new grid from here, without a burst of them. A clock of the new grid due
    // on this frame (in the last frame's time) still comes.
    void regrid()
    {
        lastClock = clockAtOrBefore (position - perFrame * (1.0 - clockSlack));
        regridWanted = false;
    }

    void resetPulse()
    {
        const uint16_t before = seq.currentSeed();
        seq.reset (entropy(), knob[ParamId::mutate]);
        if (seq.currentSeed() != before)
            changed |= 1u << 0;
    }

    void clock (int64_t k)
    {
        clockedAt = (int64_t) frameCount;
        clockedReset = resetClock (k);
        if (clockedReset)
            resetPulse();
        const int32_t gateBefore = seq.gateCv;
        seq.clock();
        remember();
        padWanted = false; // a pad hit on a clock's frame: the step the clock brings is the one heard
        if (notesMode)
            return; // the count goes on; the notes play the voice
        if (padHasVoice)
        {
            // A pad's step had the voice: it goes back to the sequencer's gate as it was before this clock (open only
            // when the step before slides into this one), so the step plays as it would have.
            padHasVoice = padOffPending = false;
            voice.gate (gateBefore > 0, gateBefore == Tb3po::gateAccent);
        }
        voice.pitch (seq.slideStart, seq.slideEnd);
        voice.gate (seq.gateCv > 0, seq.gateCv == Tb3po::gateAccent);
        if (seq.opened)
        {
            gateOffPending = true;
            // Half the step: at Swing 50 % half a grid, as before Swing; swung, half of this step's own length.
            const double length = swung() ? clockBeat (k + 1) - clockBeat (k) : stepBeats();
            gateOffIn = (uint32_t) std::clamp (std::floor ((double) knob[ParamId::gate] * length / perFrame), 1.0, 4.0e9);
        }
        TBMPC_TRACE (0);
    }

    // A step pad's step, once: its pitch at once, the gate open with its accent (or the pad's) for Gate's share of a
    // clock at the host's tempo (120 BPM until one is told), shut first if it was open so both envelopes start over.
    // The step is the one the sequencer will play: when the pattern waits to be made again (a seed, density or scale
    // set since it was, say while stopped or by a project loaded), from a copy made again as the next clock will make
    // it. Nothing of the sequencer's changes: its pattern, play head, seed, Seed Back's memory, transposition and gate
    // timer.
    void playPad()
    {
        const Tb3po* from = &seq;
        Tb3po next;
        if (seq.patternWaiting())
        {
            next = seq;
            next.regenerate();
            from = &next;
        }
        const int32_t cv = from->pitchOfStep (wantedStep);
        const bool accent = from->accented (wantedStep) || wantedVelocity >= accentVelocity - 1.0e-6f;
        voice.pitch (cv, cv);
        voice.gate (false, false);
        voice.gate (true, accent);
        const double beatsPerFrame = perFrame > 0.0 ? perFrame : 120.0 / 60.0 / rate;
        padOffIn = (uint32_t) std::clamp (std::floor ((double) knob[ParamId::gate] * stepBeats() / beatsPerFrame), 1.0, 4.0e9);
        padOffPending = padHasVoice = true;
        padKey = wantedKey;
    }

    // Seed Back's memory: when a clock plays a pattern from a new seed, the seed before goes in, if its pattern played.
    void remember()
    {
        const uint16_t now = seq.playingSeed();
        if (now != shownSeed)
        {
            if (shownPlayed && ! goingBack)
            {
                if (remembered == historySize)
                    std::copy (history + 1, history + historySize, history), --remembered;
                history[remembered++] = shownSeed;
            }
            goingBack = false;
            shownSeed = now;
            shownPlayed = false;
        }
        shownPlayed = true;
    }

    void seedBack()
    {
        // Back past seeds equal to the one asked for already.
        while (remembered > 0 && history[remembered - 1] == seq.currentSeed())
            --remembered;
        if (remembered == 0)
            return;
        const uint16_t s = history[--remembered];
        seq.setSeed (s);
        seq.setLocked (true);
        knob[ParamId::seed] = (float) s;
        knob[ParamId::seedMode] = 1.0f;
        goingBack = true; // the pattern left now is not remembered: Back again goes further back
        changed |= (1u << 0) | (1u << 1);
    }

    // A key let go (or played again): out of the held list.
    void release (int key)
    {
        int n = 0;
        for (int i = 0; i < held; ++i)
            if (heldKeys[i] != key)
                heldKeys[n] = heldKeys[i], heldAccent[n] = heldAccent[i], heldAt[n] = heldAt[i], ++n;
        held = n;
    }

    void updateTranspose()
    {
        const int semitones = std::clamp (transposeKnob + midiTranspose, -36, 72); // the module's CV range, -3 to +6 V
        seq.setTransposeInput (semitones * 128);
    }

    // The module seeds random() from micros() for a new seed; here, no clock on the audio thread: the frames
    // rendered, a count of the seeds asked for, and this engine's own address (so two instances differ).
    uint32_t entropy()
    {
        const uint64_t us = frameCount ^ ((uint64_t) (uintptr_t) this >> 4);
        uint32_t x = (uint32_t) (us ^ (us >> 32)) ^ (0x9e3779b9u * ++draws);
        x ^= x >> 16, x *= 0x7feb352du, x ^= x >> 15;
        return x;
    }

    Tb3po seq;
    Voice voice;
    double rate = 48000.0, perFrame = 0.0, position = 0.0, bar = 4.0;
    float knob[ParamId::count] {};
    int transposeKnob = 0, midiTranspose = 0;
    bool playing = false, regridWanted = false, gateOffPending = false, regenPressed = false, notesMode = false;
    uint32_t changed = 0; // values to tell the host: bit 0 the seed, bit 1 Seed Mode
    // Mode: Notes: the keys held, oldest first.
    static constexpr int maxHeld = 16;
    int8_t heldKeys[maxHeld] {};
    bool heldAccent[maxHeld] {};
    int held = 0;
    int64_t heldAt[maxHeld] {};   // the frame each was played (struck, or glided to)
    bool shortOffPending = false; // the note sounding let go on that frame: it keeps the voice until shortOffIn ends
    uint32_t shortOffIn = 0;
    int shortKey = -1;
    // Step pads (TB-3PO): the one asked for on this frame (the loudest; it plays at the next sound), and the one that
    // has the voice until the next clock (its gate open until padOffIn runs out).
    bool padWanted = false, padHasVoice = false, padOffPending = false;
    int wantedStep = 0, wantedKey = -1, padKey = -1;
    float wantedVelocity = 0.0f;
    uint32_t padOffIn = 0;
    // Seed Back: the seeds whose patterns played, oldest first.
    static constexpr int historySize = 16;
    uint16_t history[historySize] {};
    int remembered = 0;
    uint16_t shownSeed = 0;
    bool shownPlayed = false, goingBack = false;
    int64_t lastClock = -1;
    uint32_t gateOffIn = 0, lastBlock = 0, draws = 0;
    uint64_t frameCount = 0; // frames rendered since prepare()
    int64_t clockedAt = 0;   // the frame the last clock came on
    bool clockedReset = false; // and whether it was a bar reset
};
} // namespace tbmpc
