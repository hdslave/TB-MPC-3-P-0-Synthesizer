// The voice: a TB-303-style bass synthesizer, made for the least work a sample at the host's rate.
//
//   VCO (saw or square, band-limited with polyBLEP; its pitch from the sequencer, with its slide)
//     --> 4-pole diode ladder (zero-delay feedback, one soft clip in the loop)
//     --> VCA --> Volume --> soft ceiling --> out
//
//   MEG (the filter's envelope): retriggered by each new gate (not by a slide), its decay the Decay knob, or the
//     shortest decay on an accented step. Env Mod sets how far it opens the filter and how far it lowers the
//     cutoff's floor, as on the 303.
//   VEG (the VCA's envelope): a fixed, long decay while the gate is open; a few ms to close when it shuts.
//   Accent (a 5 V gate): the MEG, scaled by Accent, adds to the VCA and, through the "accent sweep" smoother that
//     the Resonance knob slows down (as its second gang does on the 303), to the cutoff: accents in a row at high
//     resonance build up.
//
// Cost, a sample, while sounding: one exp2 (cutoff), one division (the ladder's solve), about 110 other operations;
// the pitch's exp2 only while it slides or the tuning moves. With the VCA shut: the envelopes only, then nothing.
// The knobs are eased every 8 samples.
#pragma once

#include "params.h"
#include "tb3po.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace tbmpc
{
// 2^x to about 3e-6 relative, for x within +-30.
inline float exp2Fast (float x)
{
    const float xi = std::rint (x), f = x - xi;
    const float p = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * (0.00961813f + f * 0.00133336f))));
    const int32_t bits = ((int32_t) xi + 127) << 23;
    float scale;
    std::memcpy (&scale, &bits, sizeof (scale));
    return p * scale;
}

inline float clampf (float x, float lo, float hi) { return std::fmin (std::fmax (x, lo), hi); }

// A cubic soft clip: x - 4/27 x^3 up to +-1.5, where it reaches +-1 with a flat slope, then +-1. No division.
inline float softClip (float x)
{
    x = clampf (x, -1.5f, 1.5f);
    return x * (1.0f - (4.0f / 27.0f) * x * x);
}

// The polyBLEP residual of a unit step at phase 0, for phase t (cycles) at dt a sample: nonzero only within a sample
// either side of the edge.
inline float polyBlep (float t, float dt)
{
    if (t < dt)
    {
        const float x = t / dt;
        return x + x - x * x - 1.0f;
    }
    if (t > 1.0f - dt)
    {
        const float x = (t - 1.0f) / dt;
        return x * x + x + x + 1.0f;
    }
    return 0.0f;
}

// --- The voice's fixed values (the knobs' ranges are in params.h) ---------------------------------------------------
constexpr float megAttackSeconds = 0.0005f;  // the MEG's charge time constant (at its peak in 0.8 ms)
constexpr float vegAttackSeconds = 0.0006f;  // the VCA's (at full in 1 ms)
constexpr float vegDecaySeconds = 3.0f;      // the VCA's decay while the gate is held, to -40 dB
constexpr float vegReleaseSeconds = 0.003f;  // the VCA closing when the gate shuts (time constant)
constexpr float glideSeconds = 0.020f;       // the 303 glide's time constant (95 % there in 60 ms)
constexpr float sweepFastest = 0.002f, sweepSlowest = 0.080f; // the accent sweep's time constant, Resonance 0 .. 1
constexpr float accentOctaves = 3.0f;        // the accent sweep at its top opens the cutoff this far
constexpr float accentVca = 1.2f;            // and lifts the VCA by this share (+7 dB at full Accent)
constexpr float inputDrive = 0.8f;           // the oscillator into the ladder's soft clip
constexpr float makeupShare = 0.75f;         // the part of the feedback's passband loss made up (as a power)
constexpr float outputGain = 0.9f;
// The ladder's angle a sample (pi fc / rate): from 20 Hz at 48 kHz to fc = rate / pi (15 kHz at 48 kHz).
constexpr float minAngle = 0.0013f, maxAngle = 1.0f;

class Voice
{
public:
    void prepare (double hostRate)
    {
        rate = (float) hostRate;
        logPiOverRate = std::log2 ((float) M_PI / rate);
        logBaseOverRate = std::log2 (voiceBaseHz / rate);
        auto pole = [this] (double seconds) { return (float) std::exp (-1.0 / (seconds * rate)); };
        megAttack = 1.0f - pole (megAttackSeconds);
        vegAttack = 1.0f - pole (vegAttackSeconds);
        vegDecay = pole (vegDecaySeconds / std::log (100.0)); // to -40 dB in vegDecaySeconds
        vegRelease = pole (vegReleaseSeconds);
        glideCoeff = 1.0f - pole (glideSeconds);
        accentDecay = pole (decayShortest / std::log (100.0));
        ticksPerFrame = moduleTickRate / hostRate;
        reset();
    }

    void reset()
    {
        phase = 0.0f;
        s1 = s2 = s3 = s4 = 0.0f;
        meg = veg = sweep = 0.0f;
        megAttacking = vegAttacking = gateOpen = accentOn = false;
        sliding = false;
        slideFrames = 0;
        pitchOct = targetOct = 0.0f;
        tick = 0;
        dirty = true;
        retarget();
        for (int i = 0; i < smoothed; ++i)
            smooth[i] = target[i];
        moving = false;
        megDecay = decayKnob;
        tuneMoved = true;
    }

    // A knob, from the next sample.
    void set (uint32_t id, float v)
    {
        if (id >= ParamId::count || knob[id] == v)
            return;
        knob[id] = v;
        dirty = true;
    }

    // A step from the sequencer: the pitch CV the step starts at and the one it slides to (equal: no slide), in
    // 1/128 semitone from 0 V.
    void pitch (int32_t startCv, int32_t endCv)
    {
        slideStartCv = startCv;
        slideEndCv = endCv;
        slideFrames = 0;
        targetOct = (float) endCv * (1.0f / 1536.0f);
        tuneMoved = true;
        if (startCv == endCv)
        {
            pitchOct = targetOct;
            sliding = false;
            return;
        }
        sliding = true;
        pitchOct = (float) startCv * (1.0f / 1536.0f); // the slide moves from the first sample on (slide())
    }

    // The gate: shut, or open (normal or accented). Opening retriggers both envelopes; a change while open (a slide
    // into an accented step) only switches the accent.
    void gate (bool open, bool accent)
    {
        if (open && ! gateOpen)
        {
            megAttacking = vegAttacking = true;
            megDecay = accent ? accentDecay : decayKnob;
        }
        gateOpen = open;
        accentOn = open && accent;
    }

    bool silent() const { return ! gateOpen && veg == 0.0f; }

    void render (float* left, float* right, uint32_t frames)
    {
        for (uint32_t i = 0; i < frames; ++i)
        {
            if ((tick++ & 7) == 0)
                control();
            float y = 0.0f;
            if (! silent())
                y = sample();
            else if (meg != 0.0f || sweep != 0.0f || sliding)
                idle(); // the VCA is shut: only the envelopes and the slide go on
            else
            {
                // Nothing moves until the next gate: the rest of this run is silence. The knobs are taken as they are
                // (eased or not, they end the same, whatever the host's block size).
                if (dirty)
                    retarget();
                if (moving)
                {
                    for (int k = 0; k < smoothed; ++k)
                        smooth[k] = target[k];
                    moving = false;
                    tuneMoved = true;
                }
                const uint32_t n = frames - i;
                std::memset (left + i, 0, sizeof (float) * n);
                if (right)
                    std::memset (right + i, 0, sizeof (float) * n);
                tick += n - 1;
                return;
            }
            left[i] = y;
            if (right)
                right[i] = y;
        }
    }

private:
    bool glide303() const { return knob[ParamId::glide] >= 0.5f; }

    // A silent sample: the filter starts clean at the next gate; the MEG, the accent sweep and the slide go on.
    void idle()
    {
        s1 = s2 = s3 = s4 = 0.0f;
        meg *= megDecay;
        if (meg < 1.0e-6f)
            meg = 0.0f;
        sweep -= sweep * sweepCoeff;
        if (std::fabs (sweep) < 1.0e-6f)
            sweep = 0.0f;
        slide();
    }

    // The slide at this sample: the 303's glide (a one-pole step on), or the applet's (its value at this frame).
    void slide()
    {
        if (! sliding)
            return;
        if (glide303())
        {
            pitchOct += (targetOct - pitchOct) * glideCoeff;
            if (std::fabs (targetOct - pitchOct) < 1.0e-6f)
                pitchOct = targetOct, sliding = false;
        }
        else
        {
            slideToFrame(); // this sample's place on the applet's slide, then the next
            ++slideFrames;
        }
        tuneMoved = true;
    }

    // The applet's slide at frame slideFrames after the step: one tick at the step, one more each 1/16666.7 s.
    void slideToFrame()
    {
        const int64_t ticks = 1 + (int64_t) ((double) slideFrames * ticksPerFrame);
        const int32_t cv = Tb3po::slideAfter (slideStartCv, slideEndCv, ticks);
        pitchOct = (float) cv * (1.0f / 1536.0f);
        if (cv == slideEndCv)
            sliding = false;
    }

    // --- The knobs ---
    enum Smoothed { tune, cutBase, envDepth, reso, makeup, gain, accentAmount, smoothed };

    void retarget()
    {
        // Env Mod: more sweep, and a lower floor under it.
        const float envMod = knob[ParamId::envMod];
        target[tune] = knob[ParamId::tuning] * (1.0f / 12.0f);
        target[cutBase] = std::log2 (cutoffLowHz) + knob[ParamId::cutoff] * cutoffOctaves - envMod * envModFloorOctaves + logPiOverRate;
        target[envDepth] = envMod * envModOctaves;
        // Resonance's taper: the diode ladder's peak rises over its passband only near the top of its feedback (by
        // (1 + k) / (18.4 - k)); a square root spreads that over the knob: +9 dB at 60 %, +14 dB at 80 %, +22 dB full.
        const float k = std::sqrt (knob[ParamId::resonance]) * maxFeedback;
        target[reso] = k;
        // The ladder's feedback lowers its passband by 1 + k; most of that is made up, so the sound thins as a 303's
        // does at high resonance without dropping away.
        target[makeup] = std::pow (1.0f + k, makeupShare);
        target[gain] = knob[ParamId::volume] * knob[ParamId::volume] * outputGain;
        target[accentAmount] = knob[ParamId::accent];
        decayKnob = (float) std::exp (-std::log (100.0) / ((double) decaySeconds (knob[ParamId::decay]) * rate));
        if (gateOpen && ! accentOn)
            megDecay = decayKnob;
        // The accent sweep's smoothing: quick at no resonance, slow at full.
        const double sweepSeconds = sweepFastest + sweepSlowest * knob[ParamId::resonance];
        sweepCoeff = 1.0f - (float) std::exp (-1.0 / (sweepSeconds * rate));
        square = knob[ParamId::waveform] >= 0.5f;
        dirty = false;
        moving = true;
    }

    void control()
    {
        if (dirty)
            retarget();
        if (! moving)
            return;
        float far = 0.0f;
        for (int k = 0; k < smoothed; ++k)
        {
            smooth[k] += (target[k] - smooth[k]) * 0.25f;
            far = std::fmax (far, std::fabs (target[k] - smooth[k]));
        }
        if (far < 1.0e-5f)
        {
            for (int k = 0; k < smoothed; ++k)
                smooth[k] = target[k];
            moving = false;
        }
        tuneMoved = true;
    }

    float sample()
    {
        // Pitch: the slide, then the oscillator's increment (worked out only when the pitch or the tuning moves).
        slide();
        if (tuneMoved)
        {
            dt = std::fmin (exp2Fast (logBaseOverRate + pitchOct + smooth[tune]), 0.45f);
            tuneMoved = false;
        }

        // Envelopes. An attack charges toward 1.25 and stops at 1 (as a capacitor aimed past its peak).
        if (megAttacking)
        {
            meg += (1.25f - meg) * megAttack;
            if (meg >= 1.0f)
                meg = 1.0f, megAttacking = false;
        }
        else
        {
            meg *= accentOn ? accentDecay : megDecay;
            if (meg < 1.0e-6f)
                meg = 0.0f;
        }
        if (gateOpen)
        {
            if (vegAttacking)
            {
                veg += (1.25f - veg) * vegAttack;
                if (veg >= 1.0f)
                    veg = 1.0f, vegAttacking = false;
            }
            else
                veg *= vegDecay;
        }
        else
        {
            veg *= vegRelease;
            if (veg < 1.0e-5f)
                veg = 0.0f;
        }
        const float accentCv = accentOn ? smooth[accentAmount] * meg : 0.0f;
        sweep += (accentCv - sweep) * sweepCoeff;

        // The oscillator.
        float p = phase + dt;
        p -= p >= 1.0f ? 1.0f : 0.0f;
        phase = p;
        float osc;
        if (square)
        {
            float q = p + 0.5f;
            q -= q >= 1.0f ? 1.0f : 0.0f;
            osc = (p < 0.5f ? 1.0f : -1.0f) + polyBlep (p, dt) - polyBlep (q, dt);
        }
        else
            osc = 2.0f * p - 1.0f - polyBlep (p, dt);

        // The cutoff in octaves (with pi / rate folded in, so 2^x is the ladder's angle a sample), then g = tan
        // (its series to x^7: under 2.5 % low at the top of the range).
        const float x = clampf (exp2Fast (smooth[cutBase] + meg * smooth[envDepth] + sweep * accentOctaves), minAngle, maxAngle);
        const float x2 = x * x;
        const float g = x * (1.0f + x2 * (0.33333333f + x2 * (0.13333333f + x2 * 0.05396825f)));

        // The diode ladder, linear part (Zavalishin's model): y1' = w (u - 2 y1 + y2), y2' = w (y1 - 2 y2 + y3),
        // y3' = w (y2 - 2 y3 + y4), y4' = w (y3 - y4); trapezoidal, solved from the top down so each stage is
        // a + b * (the stage under it), and the output y4 = S + G u. The solve's denominators are polynomials in g
        // (q1 .. q4; q4 is the ladder's own, 1 + 7g + 15g^2 + 10g^3 + g^4), so all five reciprocals come from one
        // division of their product: a division takes 13 cycles on the A55, a multiply 4 and pipelined.
        const float q1 = 1.0f + g, q2 = 1.0f + g * (3.0f + g), q3 = 1.0f + g * (5.0f + g * (6.0f + g));
        const float q4 = 1.0f + g * (7.0f + g * (15.0f + g * (10.0f + g)));
        const float k = smooth[reso];
        const float g2 = g * g, qk = q4 + k * g2 * g2; // q4 (1 + k G): the feedback solved without delay
        const float p2 = q1 * q2, p3 = p2 * q3, p4 = p3 * q4;
        float t = 1.0f / (p4 * qk);
        const float iqk = t * p4;
        t *= qk;
        const float iq4 = t * p3;
        t *= q4;
        const float iq3 = t * p2;
        t *= q3;
        const float iq2 = t * q1;
        const float iq1 = t * q2;
        const float n3 = s3 * q1 + g * s4, n2 = s2 * q2 + g * n3, n1 = s1 * q3 + g * n2;
        const float a4 = s4 * iq1, a3 = n3 * iq2, a2 = n2 * iq3, a1 = n1 * iq4;
        const float b4 = g * iq1, b3 = g * q1 * iq2, b2 = g * q2 * iq3, b1 = g * q3 * iq4;
        const float S = a4 + b4 * (a3 + b3 * (a2 + b2 * a1));
        // The feedback, then the input stage's soft clip.
        const float u = softClip ((osc * inputDrive - k * S) * q4 * iqk);
        const float y1 = a1 + b1 * u, y2 = a2 + b2 * y1, y3 = a3 + b3 * y2, y4 = a4 + b4 * y3;
        s1 = y1 + y1 - s1;
        s2 = y2 + y2 - s2;
        s3 = y3 + y3 - s3;
        s4 = y4 + y4 - s4;

        // The VCA: its envelope, and the accent on top.
        const float amp = veg * (1.0f + accentVca * accentCv);
        return softClip (y4 * amp * smooth[makeup] * smooth[gain]);
    }

    float rate = 48000.0f, logPiOverRate = 0.0f, logBaseOverRate = 0.0f;
    float knob[ParamId::count] {};
    float target[smoothed] {}, smooth[smoothed] {};
    float phase = 0.0f, dt = 0.0f;
    float s1 = 0.0f, s2 = 0.0f, s3 = 0.0f, s4 = 0.0f;
    float meg = 0.0f, veg = 0.0f, sweep = 0.0f;
    float megAttack = 0.0f, megDecay = 0.0f, decayKnob = 0.0f, accentDecay = 0.0f;
    float vegAttack = 0.0f, vegDecay = 0.0f, vegRelease = 0.0f, sweepCoeff = 0.0f, glideCoeff = 0.0f;
    float pitchOct = 0.0f, targetOct = 0.0f;
    double ticksPerFrame = 0.0;
    int32_t slideStartCv = 0, slideEndCv = 0;
    int64_t slideFrames = 0;
    uint32_t tick = 0;
    bool megAttacking = false, vegAttacking = false, gateOpen = false, accentOn = false;
    bool sliding = false, square = false, dirty = true, moving = false, tuneMoved = true;
};
} // namespace tbmpc
