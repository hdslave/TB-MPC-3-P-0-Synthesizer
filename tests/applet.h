// The TB-3PO applet as the module runs it, for tests/reference.cpp to hold the plug-in's port against: the applet's
// Controller() and the code it calls, copied from the Benisphere Suite's HEM_TB3PO.ino (tools/oc-source/) with only
// the hardware replaced by a stand-in that the test drives a tick (1/16666.7 s) at a time: the clock and reset
// inputs, the CV inputs, the tick counter, the ADC lag, micros(). Drawing is left out.
//
// Copyright (c) 2020, Logarhythm. MIT license (licenses/MIT-oc-firmware.txt): the applet's code below keeps its
// structure and names on purpose.
#pragma once

#include "tb3po.h"

#include <cstdint>
#include <cstdlib>

namespace applet
{
#define HEMISPHERE_MAX_CV 7680
#define HEMISPHERE_3V_CV 4608
#define HEMISPHERE_CENTER_CV 0
#define HEMISPHERE_ADC_LAG 33
#define ACID_HALF_STEPS 16
#define ACID_MAX_STEPS 32
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#define int2simfloat(x) ((int32_t) ((uint32_t) (x) << 14))
#define simfloat2int(x) ((x) >> 14)
typedef int32_t simfloat;

// The module around the applet.
struct Hardware
{
    uint32_t ticks = 0;            // OC::CORE::ticks
    bool clockEdge[2] {};          // a rising edge on digital input 1 (clock) or 2 (regen) this tick
    bool gateHigh[2] {};           // their levels
    int inputs[2] {};              // CV 1, CV 2 (1/128 semitone)
    uint32_t microsNow = 1;        // micros()
    int32_t outputs[2] {};
};

// OC::Scales::GetScale: the four user scales (Semitone until edited), Off, then the firmware's table.
inline const tbmpc::ScaleDef& getScale (int index)
{
    static const tbmpc::ScaleDef off { 0, 0, {} };
    if (index < 4)
        return tbmpc::scaleTable[0];
    if (index == 4)
        return off;
    return tbmpc::scaleTable[index - 5];
}
constexpr int NUM_SCALES = 5 + tbmpc::scaleCount;

class TB_3PO
{
public:
    explicit TB_3PO (Hardware& h) : hw (h) {}

    // --- The hardware calls ---
    bool Clock (int ch)
    {
        const bool clocked = hw.clockEdge[ch];
        if (clocked)
        {
            cycle_ticks[ch] = (int) (hw.ticks - last_clock[ch]);
            last_clock[ch] = hw.ticks;
        }
        return clocked;
    }
    bool Gate (int ch) { return hw.gateHigh[ch]; }
    int In (int ch) { return hw.inputs[ch]; }
    int DetentedIn (int ch) { return (In (ch) > (HEMISPHERE_CENTER_CV + 64) || In (ch) < (HEMISPHERE_CENTER_CV - 64)) ? In (ch) : HEMISPHERE_CENTER_CV; }
    int ClockCycleTicks (int ch) { return cycle_ticks[ch]; }
    void Out (int ch, int value) { hw.outputs[ch] = value; }
    void StartADCLag (int ch = 0) { adc_lag_countdown[ch] = HEMISPHERE_ADC_LAG; }
    bool EndOfADCLag (int ch = 0) { return (--adc_lag_countdown[ch] == 0); }
    int Proportion (int numerator, int denominator, int max_value)
    {
        simfloat proportion = int2simfloat ((int32_t) numerator) / (int32_t) denominator;
        int scaled = simfloat2int (proportion * max_value);
        return scaled;
    }
    void randomSeed (uint32_t s) { rng().randomSeed (s); }
    int32_t random (int32_t lo, int32_t hi) { return rng().between (lo, hi); }
    uint32_t micros() { return hw.microsNow; }
    static tbmpc::TeensyRandom& rng() { static tbmpc::TeensyRandom r; return r; } // one random() for the module
    void ResetCursor() {}

    // --- The applet (HEM_TB3PO.ino) ---
    void Start()
    {
        manual_reset_flag = 0;
        curr_step_semitone = 0;
        root = 0;
        octave_offset = 0;
        scale = 29;
        quantizer.init();
        set_quantizer_scale (scale);
        density = 12;
        num_steps = 16;
        gate_off_clock = 0;
        cycle_time = 0;
        curr_gate_cv = 0;
        curr_pitch_cv = 0;
        slide_start_cv = 0;
        slide_end_cv = 0;
        lock_seed = 0;
        reseed();
        regenerate_all();
    }

    void Controller()
    {
        int this_tick = (int) hw.ticks;
        if (Clock (1) || manual_reset_flag)
        {
            manual_reset_flag = 0;
            if (lock_seed == 0)
                reseed();
            regenerate_all();
            step = 0;
        }
        transpose_cv = 0;
        if (DetentedIn (0))
            transpose_cv = quantizer.process (In (0), 0, 0);
        {
            int signal = constrain (DetentedIn (1), -HEMISPHERE_3V_CV, HEMISPHERE_MAX_CV);
            density_cv = Proportion (abs (signal), HEMISPHERE_MAX_CV, 15);
            if (signal < 0)
                density_cv *= -1;
            density = static_cast<uint8_t> (constrain (density_encoder + density_cv, 0, 14));
        }
        if (Clock (0))
        {
            cycle_time = ClockCycleTicks (0);
            regenerate_if_density_or_scale_changed();
            StartADCLag();
        }
        if (EndOfADCLag() && ! Gate (1))
        {
            int step_pv = step;
            step = get_next_step (step);
            if (step_is_slid (step_pv))
            {
                slide_start_cv = get_pitch_for_step (step_pv);
                curr_pitch_cv = slide_start_cv;
                slide_end_cv = get_pitch_for_step (step);
            }
            else
            {
                curr_pitch_cv = get_pitch_for_step (step);
                slide_start_cv = curr_pitch_cv;
                slide_end_cv = curr_pitch_cv;
            }
            if (step_is_gated (step) || step_is_slid (step_pv))
            {
                curr_gate_cv = step_is_accent (step) ? HEMISPHERE_MAX_CV : HEMISPHERE_3V_CV;
                int gate_time = (cycle_time / 2);
                gate_off_clock = this_tick + gate_time;
            }
            stepped = true; // (for the test: a step was taken this tick)
        }
        if (curr_gate_cv > 0 && gate_off_clock > 0 && this_tick >= gate_off_clock)
        {
            gate_off_clock = 0;
            if (! step_is_slid (step))
                curr_gate_cv = 0;
        }
        if (curr_pitch_cv != slide_end_cv)
        {
            int k = 0x0003;
            int x = slide_end_cv;
            x -= curr_pitch_cv;
            x >>= 18;
            x *= k;
            curr_pitch_cv += x;
            if (slide_start_cv < slide_end_cv)
                curr_pitch_cv = constrain (curr_pitch_cv, slide_start_cv, slide_end_cv);
            else
                curr_pitch_cv = constrain (curr_pitch_cv, slide_end_cv, slide_start_cv);
        }
        Out (0, curr_pitch_cv);
        Out (1, curr_gate_cv);
        update_regeneration();
    }

    void OnEncoderMove (int direction)
    {
        if (cursor == 0)
        {
            lock_seed += direction;
            manual_reset_flag = (lock_seed > 1 || lock_seed < 0) ? 1 : 0;
            lock_seed = constrain (lock_seed, 0, 1);
        }
        else if (cursor <= 4)
        {
            int byte_offs = 4 - cursor;
            int shift_amt = byte_offs * 4;
            uint32_t nib = (seed >> shift_amt) & 0xf;
            uint8_t c = (uint8_t) nib;
            c = (uint8_t) constrain (c + direction, 0, 0xF);
            nib = c;
            uint32_t mask = 0xf;
            seed &= (uint16_t) ~(mask << shift_amt);
            seed |= (uint16_t) (nib << shift_amt);
        }
        else if (cursor == 5)
            density_encoder = constrain (density_encoder + direction, 0, 14);
        else if (cursor == 6)
        {
            scale += direction;
            if (scale >= NUM_SCALES) scale = 0;
            if (scale < 0) scale = NUM_SCALES - 1;
            set_quantizer_scale (scale);
            int max_root = scale_size > 12 ? 12 : scale_size;
            if (max_root > 0)
                root = (uint8_t) constrain ((int) root, 0, max_root - 1);
        }
        else if (cursor == 7)
        {
            int r = root + direction;
            int max_root = scale_size > 12 ? 12 : scale_size;
            if (direction > 0 && r >= max_root && octave_offset < 3)
            {
                ++octave_offset;
                r = 0;
            }
            else if (direction < 0 && r < 0 && octave_offset > -3)
            {
                --octave_offset;
                r = max_root - 1;
            }
            root = (uint8_t) constrain (r, 0, max_root - 1);
        }
        else
            num_steps = (uint8_t) constrain (num_steps + direction, 1, 32);
    }

    // --- What the test reads and sets ---
    int cursor = 0;
    int lock_seed = 0;
    uint16_t seed = 0;
    int scale = 0;
    uint8_t root = 0;
    int8_t octave_offset = 0;
    uint8_t density = 0;
    int density_encoder = 0;
    uint8_t num_steps = 16;
    uint8_t step = 0;
    int32_t curr_gate_cv = 0, curr_pitch_cv = 0, slide_start_cv = 0, slide_end_cv = 0;
    int manual_reset_flag = 0;
    bool stepped = false;
    uint32_t gates = 0, slides = 0, accents = 0, oct_ups = 0, oct_downs = 0;
    uint8_t notes[ACID_MAX_STEPS] {};

private:
    Hardware& hw;
    tbmpc::Quantizer quantizer;
    int cycle_ticks[2] {};
    uint32_t last_clock[2] {};
    int adc_lag_countdown[2] {};
    int density_cv = 0;
    int32_t transpose_cv = 0;
    uint8_t current_pattern_density = 0;
    uint8_t scale_size = 0, current_pattern_scale_size = 0;
    int gate_off_clock = 0, cycle_time = 0;
    int curr_step_semitone = 0;
    uint8_t regenerate_phase = 0;

    int get_pitch_for_step (int step_num)
    {
        int quant_note = 64 + int (notes[step_num]) + int (root);
        quant_note += (int (octave_offset) * int (scale_size));
        if (step_is_oct_up (step_num))
            quant_note += scale_size;
        else if (step_is_oct_down (step_num))
            quant_note -= scale_size;
        int out_note = constrain (quant_note, 0, 127);
        int pitch_cv = quantizer.lookup (out_note) + transpose_cv;
        return pitch_cv;
    }

    void reseed()
    {
        randomSeed (micros());
        seed = (uint16_t) random (0, 65535);
    }

    void regenerate_all() { regenerate_phase = 1; }

    void regenerate_if_density_or_scale_changed()
    {
        if (regenerate_phase == 0)
            if (density != current_pattern_density || scale_size != current_pattern_scale_size)
                regenerate_phase = 1;
    }

    void update_regeneration()
    {
        if (regenerate_phase == 0)
            return;
        randomSeed (seed + regenerate_phase);
        switch (regenerate_phase)
        {
            case 1: regenerate_pitches(); ++regenerate_phase; break;
            case 2: apply_density(); ++regenerate_phase; break;
            case 3: regenerate_pitches(); ++regenerate_phase; break;
            case 4: apply_density(); regenerate_phase = 0; break;
            default: break;
        }
    }

    void regenerate_pitches()
    {
        bool bFirstHalf = regenerate_phase < 3;
        int pitch_change_dens = get_pitch_change_density();
        int available_pitches = 0;
        if (scale_size > 0)
        {
            if (pitch_change_dens > 7)
                available_pitches = scale_size - 1;
            else if (pitch_change_dens < 2)
                available_pitches = pitch_change_dens;
            else
            {
                int range_from_scale = scale_size - 3;
                if (range_from_scale < 4)
                    range_from_scale = 4;
                available_pitches = 3 + Proportion (pitch_change_dens - 3, 4, range_from_scale);
                available_pitches = constrain (available_pitches, 1, scale_size - 1);
            }
        }
        if (bFirstHalf)
        {
            oct_ups = 0;
            oct_downs = 0;
        }
        int max_step = (bFirstHalf ? ACID_HALF_STEPS : ACID_MAX_STEPS);
        for (int s = (bFirstHalf ? 0 : ACID_HALF_STEPS); s < max_step; s++)
        {
            int force_repeat_note_prob = 50 - (pitch_change_dens * 6);
            if (s > 0 && rand_bit (force_repeat_note_prob))
                notes[s] = notes[s - 1];
            else
            {
                notes[s] = (uint8_t) random (0, available_pitches + 1);
                oct_ups <<= 1;
                oct_downs <<= 1;
                if (rand_bit (40))
                {
                    if (rand_bit (50))
                        oct_ups |= 0x1;
                    else
                        oct_downs |= 0x1;
                }
            }
        }
        if (scale_size == 0)
            scale_size = 12;
        current_pattern_scale_size = scale_size;
    }

    void apply_density()
    {
        int latest_slide = 0;
        int latest_accent = 0;
        int on_off_dens = get_on_off_density();
        int densProb = 10 + on_off_dens * 14;
        bool bFirstHalf = regenerate_phase < 3;
        if (bFirstHalf)
        {
            gates = 0;
            slides = 0;
            accents = 0;
        }
        for (int i = 0; i < ACID_HALF_STEPS; ++i)
        {
            gates <<= 1;
            gates |= (uint32_t) rand_bit (densProb);
            slides <<= 1;
            latest_slide = rand_bit ((latest_slide ? 10 : 18));
            slides |= (uint32_t) latest_slide;
            accents <<= 1;
            latest_accent = rand_bit ((latest_accent ? 7 : 16));
            accents |= (uint32_t) latest_accent;
        }
        current_pattern_density = density;
    }

    int get_on_off_density()
    {
        int note_dens = int (density) - 7;
        return abs (note_dens);
    }
    int get_pitch_change_density() { return constrain (density, 0, 8); }
    bool step_is_gated (int step_num) { return (gates & (0x01u << step_num)); }
    bool step_is_slid (int step_num) { return (slides & (0x01u << step_num)); }
    bool step_is_accent (int step_num) { return (accents & (0x01u << step_num)); }
    bool step_is_oct_up (int step_num) { return (oct_ups & (0x01u << step_num)); }
    bool step_is_oct_down (int step_num) { return (oct_downs & (0x01u << step_num)); }
    int get_next_step (int step_num)
    {
        if (++step_num >= num_steps)
            return 0;
        return step_num;
    }
    int rand_bit (int prob) { return (random (1, 100) <= prob) ? 1 : 0; }
    void set_quantizer_scale (int new_scale)
    {
        const tbmpc::ScaleDef& quant_scale = getScale (new_scale);
        quantizer.configure (quant_scale);
        scale_size = (uint8_t) quant_scale.count;
    }
};
#undef constrain
} // namespace applet
