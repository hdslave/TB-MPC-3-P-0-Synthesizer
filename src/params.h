// The parameters: one table for the CLAP side (names, ranges, text) and the engine (what a knob means).
// Ids are fixed forever: a saved project keeps its values by id. New parameters take new ids.
#pragma once

#include "scales.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#ifndef TBMPC_VERSION
#define TBMPC_VERSION "0.3.0"
#endif

namespace tbmpc
{
namespace ParamId
{
enum : uint32_t
{
    tuning = 0, cutoff, resonance, envMod, decay, accent, waveform, volume, glide,
    seedMode, seed, regen, density, scale, root, octave, steps, transpose, clockDiv, reset,
    mode, seedBack, swing, // 0.2.0
    gate, mutate, // 0.3.0
    count
};
}

enum class Kind : uint8_t { continuous, integer, choice, button };
// How a value reads.
enum class Unit : uint8_t { semitones, cutoffHz, percent, decay, seedHex, signedInt, plainInt, choice, button };

struct Param
{
    uint32_t id;
    const char* name;
    const char* module;
    float min, max, def;
    Kind kind;
    Unit unit;
    const char* const* choices; // for Kind::choice: max - min + 1 names
};

// --- What the knobs mean -----------------------------------------------------------------------------------------
// 0 V from the sequencer is C2 (65.4 Hz), the 303's low C; Tuning moves it a semitone or so either way, as the
// panel's does (here up to an octave).
constexpr float voiceBaseHz = 65.406391f;

// Cutoff: 130 Hz to 5.1 kHz before the envelope and accent open it further.
constexpr float cutoffLowHz = 130.0f, cutoffOctaves = 5.3f;
inline float cutoffHz (float k) { return cutoffLowHz * std::exp2 (k * cutoffOctaves); }
inline float cutoffKnob (float hz) { return std::log2 (hz / cutoffLowHz) / cutoffOctaves; }

// Env Mod at full: the MEG opens the cutoff 4.5 octaves above a floor 1.3 octaves under the Cutoff knob.
constexpr float envModOctaves = 4.5f, envModFloorOctaves = 1.3f;

// Decay: the MEG's time to fall 40 dB, 200 ms to 2 s (an accented step always takes the shortest).
constexpr float decayShortest = 0.2f, decayLongest = 2.0f;
inline float decaySeconds (float k) { return decayShortest * std::pow (decayLongest / decayShortest, k); }
inline float decayKnob (float s) { return std::log (s / decayShortest) / std::log (decayLongest / decayShortest); }

// Resonance: the diode ladder's feedback. It would ring on its own from about 18.4; full turn stops short of that.
constexpr float maxFeedback = 17.0f;

// The module's update rate: TB-3PO's slide moves a step each tick of it.
constexpr double moduleTickRate = 1.0e6 / 60.0;

inline const char* const waveChoices[] = { "Saw", "Square" };
inline const char* const glideChoices[] = { "TB-3PO", "303" };
inline const char* const seedModeChoices[] = { "Random", "Locked" };
inline const char* const modeChoices[] = { "TB-3PO", "Notes" };
// Mode: Notes: a note at this velocity or louder is accented (as 303-style synths read MIDI).
constexpr float accentVelocity = 100.0f / 127.0f;
// 0 V is C2: in Mode: Notes, MIDI note 36 (C2) plays the pitch the sequencer's 0 V does.
constexpr int noteAtZeroVolts = 36;
// Pitch bend's range, and how far the mod wheel at its top opens the cutoff.
constexpr float bendSemitones = 2.0f, modWheelOctaves = 3.0f;
inline const char* const rootChoices[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
inline const char* const resetChoices[] = { "Play", "1 Bar", "2 Bars", "4 Bars", "8 Bars", "Off" };
constexpr int resetBars[] = { 0, 1, 2, 4, 8, 0 };
// The clock: a step is this many quarter notes at the host's tempo.
inline const char* const clockDivChoices[] = { "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64" };
constexpr double clockDivBeats[] = { 2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625 };
static_assert (sizeof (clockDivBeats) / sizeof (clockDivBeats[0]) == sizeof (clockDivChoices) / sizeof (clockDivChoices[0]), "a length a choice");

using P = Param;
#define CONT(id, name, module, lo, hi, def, unit) P { ParamId::id, name, module, lo, hi, def, Kind::continuous, Unit::unit, nullptr }
#define INT(id, name, module, lo, hi, def, unit) P { ParamId::id, name, module, lo, hi, def, Kind::integer, Unit::unit, nullptr }
#define CHOICE(id, name, module, list, def) P { ParamId::id, name, module, 0.0f, (float) (sizeof (list) / sizeof (list[0]) - 1), def, Kind::choice, Unit::choice, list }
#define BUTTON(id, name, module) P { ParamId::id, name, module, 0.0f, 1.0f, 0.0f, Kind::button, Unit::button, nullptr }

// In the order a host lists them, three to a page as Melodee's Edit page shows them: the 303's panel (Tuning,
// Cutoff, Resonance; Env Mod, Decay, Accent; Waveform, Volume and the slide), then what plays it and TB-3PO's
// settings (the mode and the seed; Regen, Seed Back, density; scale, root, octave; length, transpose) and the clock.
inline constexpr Param paramTable[] = {
    CONT (tuning, "Tuning", "Voice", -12.0f, 12.0f, 0.0f, semitones),
    CONT (cutoff, "Cutoff", "Voice", 0.0f, 1.0f, 0.35f, cutoffHz),
    CONT (resonance, "Resonance", "Voice", 0.0f, 1.0f, 0.6f, percent),
    CONT (envMod, "Env Mod", "Voice", 0.0f, 1.0f, 0.5f, percent),
    CONT (decay, "Decay", "Voice", 0.0f, 1.0f, 0.35f, decay),
    CONT (accent, "Accent", "Voice", 0.0f, 1.0f, 0.6f, percent),
    CHOICE (waveform, "Waveform", "Voice", waveChoices, 0.0f),
    CONT (volume, "Volume", "Voice", 0.0f, 1.0f, 0.75f, percent),
    CHOICE (glide, "Slide", "Voice", glideChoices, 0.0f),
    CHOICE (mode, "Mode", "TB-3PO", modeChoices, 0.0f),
    CHOICE (seedMode, "Seed Mode", "TB-3PO", seedModeChoices, 0.0f),
    INT (seed, "Seed", "TB-3PO", 0.0f, 65535.0f, 12345.0f, seedHex),
    BUTTON (regen, "Regen", "TB-3PO"),
    BUTTON (seedBack, "Seed Back", "TB-3PO"),
    CONT (mutate, "Mutate", "TB-3PO", 0.0f, 1.0f, 0.0f, percent),
    INT (density, "Density", "TB-3PO", -7.0f, 7.0f, 5.0f, signedInt),
    P { ParamId::scale, "Scale", "TB-3PO", 0.0f, (float) (scaleCount - 1), (float) appletDefaultScale, Kind::choice, Unit::choice, scaleNames },
    CHOICE (root, "Root", "TB-3PO", rootChoices, 0.0f),
    INT (octave, "Octave", "TB-3PO", -3.0f, 3.0f, 0.0f, signedInt),
    INT (steps, "Steps", "TB-3PO", 1.0f, 32.0f, 16.0f, plainInt),
    CONT (gate, "Gate", "TB-3PO", 0.1f, 0.95f, 0.5f, percent),
    INT (transpose, "Transpose", "TB-3PO", -36.0f, 36.0f, 0.0f, signedInt),
    CHOICE (clockDiv, "Clock Div", "Clock", clockDivChoices, 5.0f),
    CONT (swing, "Swing", "Clock", 0.5f, 0.75f, 0.5f, percent),
    CHOICE (reset, "Reset", "Clock", resetChoices, 0.0f),
};
#undef CONT
#undef INT
#undef CHOICE
#undef BUTTON

constexpr uint32_t paramCount = sizeof (paramTable) / sizeof (paramTable[0]);
static_assert (paramCount == ParamId::count, "every id has one row");

struct IndexOfId
{
    int16_t row[ParamId::count];
    constexpr IndexOfId() : row()
    {
        for (uint32_t i = 0; i < ParamId::count; ++i)
            row[i] = -1;
        for (uint32_t i = 0; i < paramCount; ++i)
            row[paramTable[i].id] = (int16_t) i;
    }
};
inline constexpr IndexOfId indexOfId {};
inline int paramIndex (uint32_t id) { return id < ParamId::count ? indexOfId.row[id] : -1; }

inline float clampParam (const Param& p, float v)
{
    if (! (v >= p.min)) v = p.min; // NaN too
    if (v > p.max) v = p.max;
    return p.kind == Kind::continuous ? v : std::round (v);
}

inline void formatParam (const Param& p, float v, char* text, uint32_t size)
{
    v = clampParam (p, v);
    switch (p.unit)
    {
        case Unit::semitones: std::snprintf (text, size, "%+.2f st", v); break;
        case Unit::cutoffHz:
        {
            const float hz = cutoffHz (v);
            if (hz < 1000.0f) std::snprintf (text, size, "%.0f Hz", hz);
            else std::snprintf (text, size, "%.2f kHz", hz / 1000.0f);
            break;
        }
        case Unit::percent: std::snprintf (text, size, "%.0f %%", v * 100.0f); break;
        case Unit::decay:
        {
            const float s = decaySeconds (v);
            if (s < 1.0f) std::snprintf (text, size, "%.0f ms", s * 1000.0f);
            else std::snprintf (text, size, "%.2f s", s);
            break;
        }
        case Unit::seedHex: std::snprintf (text, size, "%04x", (unsigned) v); break; // as the module shows it
        case Unit::signedInt: std::snprintf (text, size, "%+d", (int) v); break;
        case Unit::plainInt: std::snprintf (text, size, "%d", (int) v); break;
        case Unit::choice: std::snprintf (text, size, "%s", p.choices[(int) v]); break;
        case Unit::button: std::snprintf (text, size, "%s", v >= 0.5f ? "Pressed" : "Press"); break;
    }
}

// Reads text as formatParam writes it (units optional); a choice by its name or its number; the seed in hex.
inline bool parseParam (const Param& p, const char* text, float& v)
{
    if (p.kind == Kind::choice)
    {
        const int n = (int) (p.max - p.min) + 1;
        for (int i = 0; i < n; ++i)
            if (strcasecmp (text, p.choices[i]) == 0)
                return v = (float) i, true;
    }
    if (p.kind == Kind::button)
    {
        v = (strcasecmp (text, "Pressed") == 0 || std::atof (text) >= 0.5) ? 1.0f : 0.0f;
        return true;
    }
    char* end = nullptr;
    if (p.unit == Unit::seedHex)
    {
        const unsigned long x = std::strtoul (text, &end, 16);
        if (end == text)
            return false;
        v = clampParam (p, (float) x);
        return true;
    }
    const double x = std::strtod (text, &end);
    if (end == text)
        return false;
    while (*end == ' ')
        ++end;
    const bool kilo = (*end == 'k' || *end == 'K');
    const bool milli = (end[0] == 'm' && end[1] == 's');
    switch (p.unit)
    {
        case Unit::cutoffHz: v = cutoffKnob ((float) (kilo ? x * 1000.0 : x)); break;
        case Unit::decay: v = decayKnob ((float) (milli ? x / 1000.0 : x)); break;
        case Unit::percent: v = (float) (x / 100.0); break;
        default: v = (float) x; break;
    }
    v = clampParam (p, v);
    return true;
}
} // namespace tbmpc
