// Loads Slink.clap as a host does and checks it: description and ports, parameters, state, presets, the
// sequencer against the host's transport (each note where the pattern puts it, gates half a step long, stop, restart,
// a jump), the same output whatever the block size, the seed (Random and Locked, Regen, a new seed while playing),
// transposing by MIDI note or knob, the step pads, the accent, both slides and bounded output at extreme settings.
// Writes demo renders as WAV files into the output folder. tests/reference.cpp checks the sequencer against the
// applet itself; this test uses the same port (src/tb3po.h) to know where the notes must be.
//   host Slink.clap OUT_FOLDER
#include "wav.h"
#include "png.h"
#include "melodee_screen.h"
#include "sixteen_screen.h"
#include "params.h"
#include "tb3po.h"

#include <clap/clap.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <cstdlib>
#include <functional>
#include <limits>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace
{
int failures = 0, checks = 0;
#define CHECK(c) do { ++checks; if (! (c)) { ++failures; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

const clap_plugin_entry* entry = nullptr;
const clap_plugin_factory* factory = nullptr;

// The host side: log, and the two a preset load tells (counted); the plug-in must cope without the rest.
bool freescreenHost = false;
int rescans = 0, textRescans = 0, callbacks = 0, flushes = 0, paints = 0, processRequests = 0, presetsLoaded = 0, presetErrors = 0;
const void* hostExtension (const clap_host*, const char* id)
{
    static const clap_host_log log { [] (const clap_host*, clap_log_severity s, const char* m) { std::printf ("  [plug-in log %d] %s\n", (int) s, m); } };
    static const clap_host_params params { [] (const clap_host*, clap_param_rescan_flags f) {
                                               rescans += (f & CLAP_PARAM_RESCAN_VALUES) != 0;
                                               textRescans += (f & CLAP_PARAM_RESCAN_TEXT) != 0;
                                           },
                                           [] (const clap_host*, clap_id, clap_param_clear_flags) {}, [] (const clap_host*) { ++flushes; } };
    static const melodee_screen_host_t screen { [] (const void*, uint32_t) { ++paints; } };
    if (!freescreenHost && std::strcmp (id, MELODEE_SCREEN_CLAP_HOST) == 0) return &screen;
    static const clap_host_preset_load presetLoad {
        [] (const clap_host*, uint32_t, const char*, const char*, int32_t, const char*) { ++presetErrors; },
        [] (const clap_host*, uint32_t, const char*, const char*) { ++presetsLoaded; },
    };
    if (std::strcmp (id, CLAP_EXT_LOG) == 0) return &log;
    if (std::strcmp (id, CLAP_EXT_PARAMS) == 0) return &params;
    if (std::strcmp (id, CLAP_EXT_PRESET_LOAD) == 0) return &presetLoad;
    return nullptr;
}

clap_host host {
    CLAP_VERSION, nullptr, "Slink test host", "tests", "", "1",
    hostExtension, [] (const clap_host*) {}, [] (const clap_host*) { ++processRequests; }, [] (const clap_host*) { ++callbacks; },
};

// Input events for one process call, in time order.
struct Events
{
    struct Any { union { clap_event_header header; clap_event_param_value param; clap_event_note note; clap_event_midi midi; }; };
    void midiMessage (uint32_t time, uint8_t status, uint8_t data1, uint8_t data2)
    {
        Any a {};
        a.midi.header = { sizeof (clap_event_midi), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0 };
        a.midi.port_index = 0;
        a.midi.data[0] = status, a.midi.data[1] = data1, a.midi.data[2] = data2;
        list.push_back (a);
    }
    std::vector<Any> list;
    clap_input_events in { this, [] (const clap_input_events* e) { return (uint32_t) static_cast<const Events*> (e->ctx)->list.size(); },
                           [] (const clap_input_events* e, uint32_t i) { return &static_cast<const Events*> (e->ctx)->list[i].header; } };
    void param (uint32_t time, clap_id id, double value)
    {
        Any a {};
        a.param = { { sizeof (clap_event_param_value), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 }, id, nullptr, -1, -1, -1, -1, value };
        list.push_back (a);
    }
    void note (uint32_t time, uint16_t type, int16_t key, double velocity)
    {
        Any a {};
        a.note = { { sizeof (clap_event_note), time, CLAP_CORE_EVENT_SPACE_ID, type, 0 }, -1, 0, 0, key, velocity };
        list.push_back (a);
    }
    void sort() { std::stable_sort (list.begin(), list.end(), [] (const Any& a, const Any& b) { return a.header.time < b.header.time; }); }
};

// What the plug-in sends back: parameter values it changed itself (the buttons returning to 0).
struct OutEvents
{
    std::vector<std::pair<clap_id, double>> values;
    std::vector<uint32_t> times; // each value's frame in its block
    std::vector<uint32_t> flags; // and its event flags
    clap_output_events out { this, [] (const clap_output_events* o, const clap_event_header* h) {
        if (h->space_id == CLAP_CORE_EVENT_SPACE_ID && h->type == CLAP_EVENT_PARAM_VALUE)
        {
            const auto* v = reinterpret_cast<const clap_event_param_value*> (h);
            static_cast<OutEvents*> (o->ctx)->values.push_back ({ v->param_id, v->value });
            static_cast<OutEvents*> (o->ctx)->times.push_back (h->time);
            static_cast<OutEvents*> (o->ctx)->flags.push_back (h->flags);
        }
        return true;
    } };
};

// The transport a block hears: Melodee's way, the place at the block's start counted on by its frames.
struct Transport
{
    bool playing = true;
    double bpm = 120.0, beats = 0.0;
};

struct Instance
{
    const clap_plugin* plugin = nullptr;
    const clap_plugin_params* params = nullptr;
    const clap_plugin_state* state = nullptr;
    const clap_plugin_preset_load* presets = nullptr;
    std::map<std::string, clap_id> ids;
    double rate = 48000.0;

    explicit Instance (double sampleRate = 48000.0, uint32_t maxFrames = 4096) : rate (sampleRate)
    {
        const auto* d = factory->get_plugin_descriptor (factory, 0);
        plugin = factory->create_plugin (factory, &host, d->id);
        CHECK (plugin != nullptr && plugin->init (plugin));
        params = static_cast<const clap_plugin_params*> (plugin->get_extension (plugin, CLAP_EXT_PARAMS));
        state = static_cast<const clap_plugin_state*> (plugin->get_extension (plugin, CLAP_EXT_STATE));
        presets = static_cast<const clap_plugin_preset_load*> (plugin->get_extension (plugin, CLAP_EXT_PRESET_LOAD));
        for (uint32_t i = 0; params && i < params->count (plugin); ++i)
        {
            clap_param_info info {};
            params->get_info (plugin, i, &info);
            ids[info.name] = info.id;
        }
        CHECK (plugin->activate (plugin, rate, 1, maxFrames));
        CHECK (plugin->start_processing (plugin));
    }
    ~Instance()
    {
        plugin->stop_processing (plugin);
        plugin->deactivate (plugin);
        plugin->destroy (plugin);
    }
    clap_id id (const std::string& name)
    {
        const auto it = ids.find (name);
        if (it == ids.end())
        {
            std::printf ("FAIL no parameter named \"%s\"\n", name.c_str());
            ++failures;
            return CLAP_INVALID_ID;
        }
        return it->second;
    }
    // A factory preset by its load key, as a host's preset browser loads it.
    bool loadPreset (const char* key)
    {
        return presets != nullptr && presets->from_location (plugin, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, key);
    }
    double value (const std::string& name)
    {
        double v = 0.0;
        params->get_value (plugin, id (name), &v);
        return v;
    }
    // The value whose text is `text` (a choice by its name).
    double valueOf (const std::string& name, const std::string& text)
    {
        double v = 0.0;
        CHECK (params->text_to_value (plugin, id (name), text.c_str(), &v));
        return v;
    }

    // Renders `frames` in blocks of `block`, with `events(frame, list)` adding events for the block starting at
    // `frame`; the transport is asked for each block.
    void render (uint32_t frames, uint32_t block, std::vector<float>& left, std::vector<float>& right,
                 const std::function<Transport (uint64_t)>& transport, const std::function<void (uint64_t, uint32_t, Events&)>& events = {},
                 OutEvents* out = nullptr)
    {
        left.assign (frames, 0.0f);
        right.assign (frames, 0.0f);
        OutEvents ignored;
        for (uint64_t at = 0; at < frames; at += block)
        {
            const uint32_t n = (uint32_t) std::min<uint64_t> (block, frames - at);
            const auto t = transport (at);
            clap_event_transport tr {};
            tr.header = { sizeof (clap_event_transport), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0 };
            tr.flags = CLAP_TRANSPORT_HAS_TEMPO | (t.beats >= 0.0 ? CLAP_TRANSPORT_HAS_BEATS_TIMELINE : 0) | (t.playing ? CLAP_TRANSPORT_IS_PLAYING : 0);
            tr.tempo = t.bpm;
            tr.song_pos_beats = (clap_beattime) std::llround (t.beats * (double) CLAP_BEATTIME_FACTOR);
            Events e;
            if (events)
                events (at, n, e);
            e.sort();
            float* outs[2] = { left.data() + at, right.data() + at };
            clap_audio_buffer buffer {};
            buffer.data32 = outs;
            buffer.channel_count = 2;
            clap_process p {};
            p.steady_time = (int64_t) at;
            p.frames_count = n;
            p.transport = &tr;
            p.audio_outputs = &buffer;
            p.audio_outputs_count = 1;
            p.in_events = &e.in;
            p.out_events = out ? &out->out : &ignored.out;
            CHECK (plugin->process (plugin, &p) != CLAP_PROCESS_ERROR);
        }
    }
    void set (Events& e, const std::string& name, double value) { e.param (0, id (name), value); }
    // Parameter changes before anything plays: one silent, stopped block.
    void setup (const std::vector<std::pair<std::string, double>>& values)
    {
        std::vector<float> l, r;
        render (1, 1, l, r, [] (uint64_t) { return Transport { false, 120.0, 0.0 }; }, [&] (uint64_t at, uint32_t, Events& e) {
            if (at == 0)
                for (auto& [name, v] : values)
                    set (e, name, v);
        });
    }
};

// Sample positions where a hit starts: where the signal passes `threshold`, in a rise that began after at least
// `gap` samples below `quiet`.
std::vector<long> onsets (const std::vector<float>& x, float threshold = 1e-3f, float quiet = 1e-4f, long gap = 96)
{
    std::vector<long> found;
    long silent = gap;      // samples below quiet so far
    bool armed = true;      // this rise began after a long enough quiet
    bool counted = false;   // and has been counted
    for (long i = 0; i < (long) x.size(); ++i)
    {
        const float a = std::fabs (x[(size_t) i]);
        if (a < quiet)
        {
            ++silent;
            continue;
        }
        if (silent > 0) // a rise starts here
            armed = silent >= gap, counted = false;
        silent = 0;
        if (armed && ! counted && a > threshold)
            found.push_back (i), counted = true;
    }
    return found;
}

Transport playingFrom (double startBeats, double bpm, double rate, uint64_t at)
{
    return { true, bpm, startBeats + (double) at * bpm / 60.0 / rate };
}

// The factory presets: listed by preset discovery as a host's browser reads them, loaded by key through
// preset-load, each setting every value (the demos check each one's values and sound).
struct Listed { std::string name, key; bool ours = false; uint32_t flags = 0; };
std::vector<Listed> listPresets (int& locations)
{
    std::vector<Listed> out;
    locations = -1;
    const auto* f = static_cast<const clap_preset_discovery_factory*> (entry->get_factory (CLAP_PRESET_DISCOVERY_FACTORY_ID));
    CHECK (f != nullptr && entry->get_factory (CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT) == (const void*) f);
    if (f == nullptr || f->count (f) != 1)
        return out;
    const auto* d = f->get_descriptor (f, 0);
    CHECK (d != nullptr && f->get_descriptor (f, 1) == nullptr);
    struct Index
    {
        std::vector<clap_preset_discovery_location> found;
        clap_preset_discovery_indexer indexer {};
    } index;
    index.indexer = { CLAP_VERSION, "tests", "", "", "1", &index,
                      [] (const clap_preset_discovery_indexer*, const clap_preset_discovery_filetype*) { return true; },
                      [] (const clap_preset_discovery_indexer* i, const clap_preset_discovery_location* l) {
                          static_cast<Index*> (i->indexer_data)->found.push_back (*l);
                          return true;
                      },
                      [] (const clap_preset_discovery_indexer*, const clap_preset_discovery_soundpack*) { return true; },
                      [] (const clap_preset_discovery_indexer*, const char*) -> const void* { return nullptr; } };
    CHECK (f->create (f, &index.indexer, "com.example.other") == nullptr);
    const auto* provider = f->create (f, &index.indexer, d->id);
    CHECK (provider != nullptr && provider->init (provider));
    if (provider == nullptr)
        return out;
    locations = (int) index.found.size();
    CHECK (locations == 1 && index.found[0].kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN && index.found[0].location == nullptr);
    struct Receive
    {
        clap_preset_discovery_metadata_receiver r {};
        static std::vector<Listed>& of (const clap_preset_discovery_metadata_receiver* r) { return *static_cast<std::vector<Listed>*> (r->receiver_data); }
    } receive;
    using R = Receive;
    receive.r.receiver_data = &out;
    receive.r.on_error = [] (const clap_preset_discovery_metadata_receiver*, int32_t, const char*) { CHECK (false); };
    receive.r.begin_preset = [] (const clap_preset_discovery_metadata_receiver* r, const char* name, const char* key) {
        R::of (r).push_back ({ name ? name : "", key ? key : "" });
        return true;
    };
    receive.r.add_plugin_id = [] (const clap_preset_discovery_metadata_receiver* r, const clap_universal_plugin_id* id) {
        R::of (r).back().ours = std::string (id->abi) == "clap" && std::string (id->id) == "com.hdslave.tb-mpc-3-p-0";
    };
    receive.r.set_soundpack_id = [] (const clap_preset_discovery_metadata_receiver*, const char*) {};
    receive.r.set_flags = [] (const clap_preset_discovery_metadata_receiver* r, uint32_t flags) { R::of (r).back().flags = flags; };
    receive.r.add_creator = [] (const clap_preset_discovery_metadata_receiver*, const char*) {};
    receive.r.set_description = [] (const clap_preset_discovery_metadata_receiver*, const char*) {};
    receive.r.set_timestamps = [] (const clap_preset_discovery_metadata_receiver*, clap_timestamp, clap_timestamp) {};
    receive.r.add_feature = [] (const clap_preset_discovery_metadata_receiver*, const char*) {};
    receive.r.add_extra_info = [] (const clap_preset_discovery_metadata_receiver*, const char*, const char*) {};
    CHECK (! provider->get_metadata (provider, CLAP_PRESET_DISCOVERY_LOCATION_FILE, "/nowhere.preset", &receive.r));
    CHECK (out.empty());
    CHECK (provider->get_metadata (provider, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, &receive.r));
    provider->destroy (provider);
    return out;
}

constexpr double rate48 = 48000.0;

// Where the notes of a locked pattern start (frames), as the plug-in must play it from the top at `bpm` in steps of
// `stepFrames`: a note starts where the gate opens after being shut (a step after a slide is legato: no new note).
std::vector<long> expectedNotes (uint16_t seed, int density, int scale, int steps, double stepFrames, int clocks, double swing = 0.5)
{
    // Swung, each odd clock comes at its pair's start plus Swing's share of the pair.
    auto frameOf = [&] (int k) { return (k & 1) && swing != 0.5 ? (k - 1) * stepFrames + 2.0 * stepFrames * (double) (float) swing : k * stepFrames; };
    tbmpc::Tb3po t;
    t.start (1);
    t.setScale (scale);
    t.setDensity (density + 7);
    t.setSteps (steps);
    t.setSeed (seed);
    t.setLocked (true);
    t.reset (0);
    std::vector<long> out;
    int32_t gate = 0;
    for (int k = 0; k < clocks; ++k)
    {
        t.clock();
        if (t.gateCv > 0 && gate == 0)
            out.push_back (std::lround (frameOf (k)));
        if (t.opened)
            t.halfStepPassed();
        gate = t.gateCv;
    }
    return out;
}

// A patch for timing: an open filter without resonance, so a note sounds from its first samples.
std::vector<std::pair<std::string, double>> timingPatch (uint16_t seed)
{
    return { { "Seed Mode", 1.0 }, { "Seed", seed }, { "Cutoff", 1.0 }, { "Resonance", 0.0 }, { "Env Mod", 0.0 }, { "Accent", 0.0 },
             { "Volume", 1.0 } };
}

void testDescription()
{
    CHECK (factory->get_plugin_count (factory) == 1);
    const auto* d = factory->get_plugin_descriptor (factory, 0);
    CHECK (d != nullptr && std::string (d->name) == "hdslave Slink" && std::string (d->id) == "com.hdslave.tb-mpc-3-p-0");
    bool instrument = false;
    for (const char* const* f = d->features; f && *f; ++f)
        instrument |= std::strcmp (*f, CLAP_PLUGIN_FEATURE_INSTRUMENT) == 0;
    CHECK (instrument);

    Instance a;
    const auto* audio = static_cast<const clap_plugin_audio_ports*> (a.plugin->get_extension (a.plugin, CLAP_EXT_AUDIO_PORTS));
    const auto* notes = static_cast<const clap_plugin_note_ports*> (a.plugin->get_extension (a.plugin, CLAP_EXT_NOTE_PORTS));
    CHECK (audio && audio->count (a.plugin, true) == 0 && audio->count (a.plugin, false) == 1);
    clap_audio_port_info port {};
    CHECK (audio && audio->get (a.plugin, 0, false, &port) && port.channel_count == 2);
    CHECK (notes && notes->count (a.plugin, true) == 1 && notes->count (a.plugin, false) == 0);
}

void testParameters()
{
    Instance a;
    const uint32_t count = a.params->count (a.plugin);
    CHECK (count == 34);
    std::map<clap_id, int> seen;
    for (uint32_t i = 0; i < count; ++i)
    {
        clap_param_info info {};
        CHECK (a.params->get_info (a.plugin, i, &info));
        CHECK (++seen[info.id] == 1);
        CHECK (info.min_value <= info.default_value && info.default_value <= info.max_value);
        double v = 0.0;
        CHECK (a.params->get_value (a.plugin, info.id, &v) && v == info.default_value);
        char text[64] = {};
        CHECK (a.params->value_to_text (a.plugin, info.id, v, text, sizeof (text)) && text[0] != 0);
        double back = -1e9;
        CHECK (a.params->text_to_value (a.plugin, info.id, text, &back));
        CHECK (std::fabs (back - v) <= 0.02 * (info.max_value - info.min_value) + 1e-9);
    }
    for (const char* name : { "Tuning", "Cutoff", "Resonance", "Env Mod", "Decay", "Accent", "Waveform", "Volume", "Slide", "Seed Mode",
                              "Seed", "Regen", "Density", "Scale", "Root", "Octave", "Steps", "Transpose", "Clock Div", "Reset", "Mode", "Seed Back", "Swing", "Gate", "Mutate",
                              "Drive", "Slide Time", "Accent Decay", "Filter", "Peak Spread",
                              "Pitch LFO Amount", "Pitch LFO Speed", "Cutoff LFO Amt", "Cutoff LFO Speed" })
        CHECK (a.ids.count (name) == 1);
    for (const char* name : { "Pitch LFO Speed", "Cutoff LFO Speed" })
    {
        for (const auto& d : tbmpc::lfoDetents)
        {
            const double value = a.valueOf (name, d.label);
            CHECK (std::fabs (value - d.cyclesPerBeat) < 1.0e-6);
            // The nearest position on a real 7-bit pot also locks to this ratio.
            const double steps = std::round ((value - tbmpc::lfoMinRate) * 127.0 / (tbmpc::lfoMaxRate - tbmpc::lfoMinRate));
            const double potValue = tbmpc::lfoMinRate + steps * (tbmpc::lfoMaxRate - tbmpc::lfoMinRate) / 127.0;
            char label[32];
            CHECK (a.params->value_to_text (a.plugin, a.id (name), potValue, label, sizeof (label)));
            CHECK (std::string (label) == d.label);
        }
        CHECK (std::fabs (a.valueOf (name, "0.625 beat") - 1.6) < 1.0e-6);
    }
    char text[64] = {};
    CHECK (a.params->value_to_text (a.plugin, a.id ("Slide"), 0, text, sizeof (text)));
    CHECK (std::string (text) == "TB-3PO");
    CHECK (a.params->value_to_text (a.plugin, a.id ("Slide"), 1, text, sizeof (text)));
    CHECK (std::string (text) == "303");
    CHECK (a.valueOf ("Slide", "TB-3PO") == 0 && a.valueOf ("Slide", "303") == 1);
    CHECK (a.valueOf ("Slide", "Tick") == 0 && a.valueOf ("Slide", "Glide") == 1);
    // The seed reads as the module shows it, four hex digits; density as -7..+7; the applet's first scale.
    a.params->value_to_text (a.plugin, a.id ("Seed"), 0xbeef, text, sizeof (text));
    CHECK (std::string (text) == "beef");
    CHECK (a.valueOf ("Seed", "a3f0") == 0xa3f0);
    a.params->value_to_text (a.plugin, a.id ("Density"), -3, text, sizeof (text));
    CHECK (std::string (text) == "-3");
    a.params->value_to_text (a.plugin, a.id ("Scale"), a.value ("Scale"), text, sizeof (text));
    CHECK (std::string (text) == "Gunakri");
    CHECK (a.valueOf ("Scale", "Aeolian") == 6 && a.valueOf ("Scale", "Semitone") == 0);
    CHECK (a.value ("Seed Mode") == a.valueOf ("Seed Mode", "Locked") && a.value ("Reset") == a.valueOf ("Reset", "Play"));
}

void testState()
{
    std::vector<float> l1, r1, l2, r2;
    Instance a;
    a.setup ({ { "Seed Mode", 1.0 }, { "Seed", 0x1234 }, { "Cutoff", 0.31 }, { "Resonance", 0.9 }, { "Density", -2 }, { "Scale", 3 },
               { "Waveform", 1.0 }, { "Steps", 11 }, { "Transpose", 4 }, { "Slide", 1.0 },
               { "Pitch LFO Amount", 0.3 }, { "Pitch LFO Speed", 2 }, { "Cutoff LFO Amt", 0.7 }, { "Cutoff LFO Speed", 3 } });
    std::vector<uint8_t> saved;
    clap_ostream os { &saved, [] (const clap_ostream* s, const void* d, uint64_t n) -> int64_t {
        auto* v = static_cast<std::vector<uint8_t>*> (s->ctx);
        v->insert (v->end(), (const uint8_t*) d, (const uint8_t*) d + n);
        return (int64_t) n;
    } };
    CHECK (a.state->save (a.plugin, &os));
    Instance b;
    struct Reader { const std::vector<uint8_t>* v; size_t at; } reader { &saved, 0 };
    clap_istream is { &reader, [] (const clap_istream* s, void* to, uint64_t n) -> int64_t {
        auto* r = static_cast<Reader*> (s->ctx);
        const size_t k = std::min<size_t> ((size_t) n, r->v->size() - r->at);
        std::memcpy (to, r->v->data() + r->at, k);
        r->at += k;
        return (int64_t) k;
    } };
    CHECK (b.state->load (b.plugin, &is));
    for (auto& [name, id] : a.ids)
        CHECK (a.value (name) == b.value (name));
    b.setup ({});
    auto t = [] (uint64_t at) { return playingFrom (0.0, 120.0, rate48, at); };
    a.render (48000 * 2, 128, l1, r1, t);
    b.render (48000 * 2, 128, l2, r2, t);
    CHECK (l1 == l2 && r1 == r2);
    std::vector<uint8_t> junk { 'J', 'U', 'N', 'K' };
    Reader bad { &junk, 0 };
    clap_istream isBad { &bad, is.read };
    CHECK (! b.state->load (b.plugin, &isBad));
}

void testPresets()
{
    int locations = 0;
    const auto listed = listPresets (locations);
    std::vector<std::string> names, keys;
    for (const auto& p : listed)
    {
        names.push_back (p.name), keys.push_back (p.key);
        CHECK (p.ours && (p.flags & CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT) != 0);
    }
    CHECK ((names == std::vector<std::string> { "Default", "Acid Line", "Root Pulse", "Square Lead", "Dub Steps", "Note Bass" }));
    CHECK ((keys == std::vector<std::string> { "default", "acid-line", "root-pulse", "square-lead", "dub-steps", "note-bass" }));

    Instance a;
    CHECK (a.presets != nullptr);
    const int errors = presetErrors;
    CHECK (! a.loadPreset ("no-such-preset") && ! a.loadPreset (nullptr));
    CHECK (! a.presets->from_location (a.plugin, CLAP_PRESET_DISCOVERY_LOCATION_FILE, "/nowhere", "acid-line"));
    CHECK (presetErrors == errors + 3);
    a.setup ({ { "Volume", 0.1 }, { "Cutoff", 0.2 }, { "Waveform", 1.0 }, { "Seed", 77 }, { "Clock Div", 7.0 } });
    const int rescanned = rescans, loaded = presetsLoaded;
    CHECK (a.loadPreset ("default"));
    CHECK (rescans == rescanned + 1 && presetsLoaded == loaded + 1);
    for (uint32_t i = 0; i < a.params->count (a.plugin); ++i)
    {
        clap_param_info info {};
        a.params->get_info (a.plugin, i, &info);
        double v = -1.0;
        a.params->get_value (a.plugin, info.id, &v);
        CHECK (v == info.default_value);
    }
    // Loaded while playing: the next block plays it.
    std::vector<float> l, r;
    Instance b;
    auto t = [] (uint64_t at) { return playingFrom (0.0, 120.0, rate48, at); };
    b.render (128, 128, l, r, t);
    CHECK (b.loadPreset ("square-lead") && b.value ("Waveform") == b.valueOf ("Waveform", "Square"));
    b.render (48000, 128, l, r, t);
    bool finite = true;
    for (float s : l)
        finite &= std::isfinite (s);
    CHECK (finite);
}

void testTiming()
{
    // Every note where the pattern puts it, at 120 BPM in sixteenths (6,000 frames a step), at 90 BPM, and at other
    // rates; a note lasts half a step and then its release.
    struct Case { uint16_t seed; int density; int scale; int steps; double bpm; double rate; const char* div; double beats; };
    const Case cases[] = {
        { 0x303, 5, tbmpc::appletDefaultScale, 16, 120.0, 48000.0, "1/16", 0.25 },
        { 0xbeef, -7, 0, 16, 120.0, 48000.0, "1/16", 0.25 },
        { 0x1d0b, 2, 6, 13, 90.0, 48000.0, "1/8", 0.5 },
        { 0x5a17, 6, 1, 32, 128.0, 44100.0, "1/16", 0.25 },
        { 0x0042, 4, 3, 16, 140.0, 96000.0, "1/16", 0.25 },
    };
    for (const auto& c : cases)
    {
        Instance a (c.rate);
        auto patch = timingPatch (c.seed);
        patch.push_back ({ "Density", c.density });
        patch.push_back ({ "Scale", c.scale });
        patch.push_back ({ "Steps", c.steps });
        patch.push_back ({ "Clock Div", a.valueOf ("Clock Div", c.div) });
        a.setup (patch);
        std::vector<float> l, r;
        const uint32_t frames = (uint32_t) (c.rate * 6);
        a.render (frames, 128, l, r, [&c] (uint64_t at) { return playingFrom (0.0, c.bpm, c.rate, at); });
        const double stepFrames = c.beats * 60.0 / c.bpm * c.rate;
        auto expect = expectedNotes (c.seed, c.density, c.scale, c.steps, stepFrames, (int) std::ceil (frames / stepFrames - 1e-9));
        auto found = onsets (l, 1e-3f, 1e-4f, (long) (c.rate / 500));
        if (std::getenv ("TBMPC_DEBUG"))
        {
            std::printf ("debug seed %04x: expected %zu, found %zu:", c.seed, expect.size(), found.size());
            for (size_t i = 0; i < found.size(); ++i) std::printf (" %ld/%ld", found[i], i < expect.size() ? expect[i] : -1);
            std::printf ("\n");
        }
        CHECK (expect.size() >= 6);
        CHECK (found.size() == expect.size());
        bool near = found.size() == expect.size();
        for (size_t i = 0; near && i < found.size(); ++i)
            near = found[i] >= expect[i] && found[i] < expect[i] + (long) (c.rate / 1000);
        CHECK (near);
        CHECK (l == r);
    }

    // A gate lasts half a step: with all steps gated and none sliding the note is loud until half way, then silent
    // well before the next.
    Instance b;
    b.setup ({ { "Seed Mode", 1.0 }, { "Seed", 0x303 }, { "Density", -7 }, { "Cutoff", 1.0 }, { "Resonance", 0.0 }, { "Env Mod", 0.0 },
               { "Accent", 0.0 } });
    std::vector<float> l, r;
    b.render (48000 * 4, 128, l, r, [] (uint64_t at) { return playingFrom (0.0, 120.0, rate48, at); });
    tbmpc::Tb3po t;
    t.start (1);
    t.setDensity (0);
    t.setSeed (0x303);
    t.setLocked (true);
    t.reset (0);
    int checked = 0;
    for (int k = 0; k < 30; ++k)
    {
        const int prev = t.currentStep();
        t.clock();
        if (k < 1 || t.slid (t.currentStep()) || t.slid (prev))
            continue; // not a plain note
        auto peak = [&] (long from, long to) { float m = 0; for (long i = from; i < to; ++i) m = std::max (m, std::fabs (l[(size_t) i])); return m; };
        const long at = k * 6000;
        CHECK (peak (at + 2700, at + 2990) > 0.01f);  // still sounding just before half a step
        CHECK (peak (at + 4600, at + 6000) < 1e-4f);  // released and silent before the next
        ++checked;
    }
    CHECK (checked >= 5);
}

void testStopRestartJump()
{
    std::vector<float> l, r;
    // Plays 2 s, stops 1 s, plays from the top: silent while stopped, then the same notes again (Reset: Play).
    Instance a;
    a.setup (timingPatch (0x303));
    a.render (48000 * 6, 128, l, r, [] (uint64_t at) {
        if (at < 96000) return playingFrom (0.0, 120.0, rate48, at);
        if (at < 144000) return Transport { false, 120.0, 4.0 };
        return playingFrom (0.0, 120.0, rate48, at - 144000);
    });
    float stoppedPeak = 0.0f;
    for (size_t i = 96000 + 4800; i < 144000; ++i)
        stoppedPeak = std::max (stoppedPeak, std::fabs (l[i]));
    CHECK (stoppedPeak == 0.0f);
    auto hits = onsets (l, 1e-3f, 1e-4f, 96);
    std::vector<long> first, again;
    for (long h : hits)
        (h < 96000 ? first : again).push_back (h < 96000 ? h : h - 144000);
    if (std::getenv ("TBMPC_DEBUG"))
    {
        std::printf ("debug restart: first");
        for (long h : first) std::printf (" %ld", h);
        std::printf ("\ndebug restart: again");
        for (long h : again) std::printf (" %ld", h);
        std::printf ("\n");
    }
    again.resize (std::min (again.size(), first.size()));
    first.resize (again.size());
    // (within 1 ms: the oscillator runs on from where it was, so a note's first loud sample moves a little)
    CHECK (first.size() > 8 && std::equal (first.begin(), first.end(), again.begin(), [] (long x, long y) { return std::labs (x - y) <= 48; }));

    // A jump into the middle of a step waits for the next clock.
    Instance b;
    b.setup ({ { "Seed Mode", 1.0 }, { "Seed", 0x303 }, { "Density", -7 }, { "Cutoff", 1.0 }, { "Resonance", 0.0 }, { "Env Mod", 0.0 } });
    constexpr uint64_t jump = 128 * 100;
    b.render (48000 * 2, 128, l, r, [] (uint64_t at) {
        if (at < jump) return playingFrom (0.0, 120.0, rate48, at);
        return playingFrom (3.3, 120.0, rate48, at - jump); // the next clock is at beat 3.5: 0.2 beat on
    });
    hits = onsets (l, 1e-3f, 1e-4f, 96);
    const long expected = (long) jump + 4800;
    CHECK (std::find_if (hits.begin(), hits.end(), [&] (long h) { return h > (long) jump + 3000 && h < expected - 10; }) == hits.end());
}

// Where the notes of a locked pattern start (frames) when clock k comes at frame frameOf (k): as expectedNotes, for a
// clock that changes pace (a tempo or a Clock Div change) or runs on through a loop.
std::vector<long> expectedNotesAt (uint16_t seed, int density, int scale, int steps, int clocks, const std::function<double (int)>& frameOf)
{
    tbmpc::Tb3po t;
    t.start (1);
    t.setScale (scale);
    t.setDensity (density + 7);
    t.setSteps (steps);
    t.setSeed (seed);
    t.setLocked (true);
    t.reset (0);
    std::vector<long> out;
    int32_t gate = 0;
    for (int k = 0; k < clocks; ++k)
    {
        t.clock();
        if (t.gateCv > 0 && gate == 0)
            out.push_back (std::lround (frameOf (k)));
        if (t.opened)
            t.halfStepPassed();
        gate = t.gateCv;
    }
    return out;
}

// Each note found where expected: its onset within a millisecond after.
bool notesAt (const std::vector<long>& found, const std::vector<long>& expect, double rate)
{
    if (std::getenv ("TBMPC_DEBUG"))
    {
        std::printf ("debug notes: expected %zu, found %zu:", expect.size(), found.size());
        for (size_t i = 0; i < found.size() || i < expect.size(); ++i)
            std::printf (" %ld/%ld", i < found.size() ? found[i] : -1, i < expect.size() ? expect[i] : -1);
        std::printf ("\n");
    }
    if (found.size() != expect.size() || expect.size() < 6)
        return false;
    for (size_t i = 0; i < found.size(); ++i)
        if (found[i] < expect[i] || found[i] >= expect[i] + (long) (rate / 1000))
            return false;
    return true;
}

void testLoopsAndClockChanges()
{
    std::vector<float> l, r, lb, rb;
    // A 4-beat loop at 110 BPM whose end falls inside a block (frame 104,727.3), the host telling of the wrap only at
    // the next block as Melodee does (in its own blocks of 128, also when it plays the plug-in in parts of 32): the
    // clocks run on to the loop's end, which is its start. With Reset at Play the pattern runs on through the loop
    // (13 steps over 16 clocks), so every note lands where a song with no loop puts it: the clock at the loop's end is
    // not played twice (the pattern would step twice). And the sound is the very sound of a host that tells the wrap on
    // its frame (blocks of one frame), with Reset at Play and every bar.
    constexpr double perFrame = 110.0 / 60.0 / rate48, loopEnd = 4.0 / perFrame, step = 0.25 / perFrame;
    auto told = [] (uint64_t at) {
        const uint64_t block = at / 128 * 128; // Melodee's block: the place at its start, counted on through it
        return Transport { true, 110.0, std::fmod ((double) block * perFrame, 4.0) + (double) (at - block) * perFrame };
    };
    auto exact = [] (uint64_t at) { return Transport { true, 110.0, std::fmod ((double) at * perFrame, 4.0) }; };
    const uint32_t frames = (uint32_t) (loopEnd + 2.0 * rate48);
    for (const char* reset : { "Play", "1 Bar" })
    {
        auto patch = [&] (Instance& a) {
            auto p = timingPatch (0x5a17);
            p.push_back ({ "Density", 5 });
            p.push_back ({ "Steps", 13 });
            p.push_back ({ "Reset", a.valueOf ("Reset", reset) });
            return p;
        };
        Instance b;
        b.setup (patch (b));
        b.render (frames, 1, lb, rb, exact);
        for (uint32_t part : { 128u, 32u })
        {
            Instance a;
            a.setup (patch (a));
            a.render (frames, part, l, r, told);
            CHECK (l == lb);
            if (std::string (reset) == "Play")
                CHECK (notesAt (onsets (l, 1e-3f, 1e-4f, 96), expectedNotesAt (0x5a17, 5, tbmpc::appletDefaultScale, 13, (int) std::ceil (frames / step - 1e-9),
                                                                            [=] (int k) { return k * step; }), rate48));
        }
    }
    // Drift: a tempo change inside a block (120 to 125 BPM at frame 24,000, 64 frames before the next block) moves the
    // host's place a few frames from where the clocks counted: the clock played 64 frames ago is not played again, and
    // the notes go on at the new tempo, the pattern in step.
    {
        Instance a;
        auto p = timingPatch (0x5a17);
        p.push_back ({ "Density", 5 });
        a.setup (p);
        a.render (96000, 128, l, r, [] (uint64_t at) {
            if (at < 24064) return playingFrom (0.0, 120.0, rate48, at);
            return Transport { true, 125.0, 1.0 + (double) (at - 24000) * 125.0 / 60.0 / rate48 };
        });
        CHECK (notesAt (onsets (l, 1e-3f, 1e-4f, 96), expectedNotesAt (0x5a17, 5, tbmpc::appletDefaultScale, 16, 4 + (int) ((96000 - 24000) / 5760.0 + 1),
                                                                    [] (int k) { return k <= 4 ? k * 6000.0 : 24000.0 + (k - 4) * 5760.0; }), rate48));
    }
    // Clock Div changed on a downbeat while playing (1/16 to 1/8 at beat 2): that downbeat's clock still comes.
    {
        Instance a;
        auto p = timingPatch (0x5a17);
        p.push_back ({ "Density", 5 });
        a.setup (p);
        a.render (96000, 128, l, r, [] (uint64_t at) { return playingFrom (0.0, 120.0, rate48, at); }, [&] (uint64_t at, uint32_t, Events& e) {
            if (at == 48000) a.set (e, "Clock Div", a.valueOf ("Clock Div", "1/8"));
        });
        CHECK (notesAt (onsets (l, 1e-3f, 1e-4f, 96), expectedNotesAt (0x5a17, 5, tbmpc::appletDefaultScale, 16, 12,
                                                                    [] (int k) { return k < 8 ? k * 6000.0 : 48000.0 + (k - 8) * 12000.0; }), rate48));
    }
    // A flush while active (the host's way between process calls) takes a button too: Regen acts and returns to 0. A
    // press stamped past its block (a host's mistake) is answered inside the block.
    {
        Instance a;
        a.setup (timingPatch (0x5a17));
        Events e;
        e.param (0, a.id ("Regen"), 1.0);
        OutEvents out;
        a.params->flush (a.plugin, &e.in, &out.out);
        CHECK (std::find (out.values.begin(), out.values.end(), std::pair<clap_id, double> { a.id ("Regen"), 0.0 }) != out.values.end());
        OutEvents late;
        a.render (1280, 128, l, r, [] (uint64_t) { return Transport { false, 120.0, 0.0 }; }, [&] (uint64_t at, uint32_t, Events& ev) {
            if (at == 128 * 5) ev.param (135, a.id ("Regen"), 1.0);
        }, &late);
        bool inside = ! late.times.empty();
        for (uint32_t t : late.times)
            inside &= t <= 127;
        CHECK (inside);
    }
}

// --- The song's place a little off its line ------------------------------------------------------------------------
// hdslaveOS tells the place only at its own 128-frame blocks, worked out from its ticks, and it is not quite a straight
// line from block to block: a swung tick moves it by up to 0.2 tick (0.0021 of a quarter at swing 60), a tick on a
// block's edge and MTC's speed trim by less, and a plug-in two tracks play is run twice a block (its second call told a
// block on, the next block's first call a block back). None of that is a locate: no step may play twice, none be lost.

// One process call: its frames, the place it is told, and where its output goes in the song (from its frame).
struct Call
{
    uint32_t frames;
    double beats;
    uint64_t at;
};

// The calls hdslaveOS makes over `blocks` of its blocks at `bpm` and `rate`: each block told the place `place (frame)`
// at its start, counted on through it. `cut (block)` in 1..127 plays a block in two calls split there (an instrument
// is played in parts, split where a note falls); `twice` runs every block twice (a plug-in two tracks share).
std::vector<Call> hostCalls (uint64_t blocks, double bpm, double rate, const std::function<double (uint64_t)>& place,
                             const std::function<uint32_t (uint64_t)>& cut = {}, bool twice = false)
{
    const double perFrame = bpm / 60.0 / rate;
    std::vector<Call> calls;
    for (uint64_t b = 0; b < blocks; ++b)
    {
        const uint64_t at = b * 128;
        for (int run = 0; run < (twice ? 2 : 1); ++run)
        {
            const double told = place (at) + run * 128 * perFrame;
            const uint32_t s = cut ? cut (b) : 0;
            if (s > 0 && s < 128)
            {
                calls.push_back ({ s, told, at });
                calls.push_back ({ 128 - s, told + s * perFrame, at + s });
            }
            else
                calls.push_back ({ 128, told, at });
        }
    }
    return calls;
}

// Plays the calls, the song playing throughout; each call's output is added where it goes. `events (k, list)` adds
// events to call k.
std::vector<float> playCalls (Instance& a, const std::vector<Call>& calls, double bpm, const std::function<void (size_t, Events&)>& events = {})
{
    std::vector<float> l (calls.empty() ? 0 : calls.back().at + calls.back().frames, 0.0f), cl (128), cr (128);
    OutEvents ignored;
    int64_t steady = 0;
    for (size_t k = 0; k < calls.size(); ++k)
    {
        const auto& c = calls[k];
        Events none;
        if (events)
            events (k, none);
        clap_event_transport tr {};
        tr.header = { sizeof (clap_event_transport), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0 };
        tr.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_IS_PLAYING;
        tr.tempo = bpm;
        tr.song_pos_beats = (clap_beattime) std::llround (c.beats * (double) CLAP_BEATTIME_FACTOR);
        std::fill (cl.begin(), cl.end(), 0.0f);
        std::fill (cr.begin(), cr.end(), 0.0f);
        float* outs[2] = { cl.data(), cr.data() };
        clap_audio_buffer buffer {};
        buffer.data32 = outs;
        buffer.channel_count = 2;
        clap_process p {};
        p.steady_time = steady;
        p.frames_count = c.frames;
        p.transport = &tr;
        p.audio_outputs = &buffer;
        p.audio_outputs_count = 1;
        p.in_events = &none.in;
        p.out_events = &ignored.out;
        CHECK (a.plugin->process (a.plugin, &p) != CLAP_PROCESS_ERROR);
        steady += c.frames;
        for (uint32_t i = 0; i < c.frames; ++i)
            l[(size_t) (c.at + i)] += cl[i];
    }
    return l;
}

// hdslaveOS's place at a frame with the sequencer's swing (`swing` %): its ticks (96 a quarter) are swing/50 of their
// length in the first sixteenth of each eighth and (100 - swing)/50 in the second, and the place is the next tick's
// number less the frames still to it in plain ticks (EngineHost: song = (tick_now - until_tick / tick_length) / 96).
// So it stands 0.2 tick back at each long tick's start (swing 60), 0.2 on at each short one's; never before the top.
struct SwungPlace
{
    double base;   // frames a plain tick
    double swing;
    std::vector<double> starts { 0.0 }; // each tick's first frame
    size_t from = 0;                    // the tick the last frame asked for was in
    SwungPlace (double bpm, double rate, double swingPercent) : base (rate * 60.0 / bpm / 96.0), swing (swingPercent) {}
    double length (size_t k) const { return (k % 48) < 24 ? base * swing / 50.0 : base * (100.0 - swing) / 50.0; }
    // The first frame of tick k.
    double tickAt (size_t k)
    {
        while (starts.size() <= k + 1)
            starts.push_back (starts.back() + length (starts.size() - 1));
        return starts[k];
    }
    double operator() (uint64_t frame)
    {
        size_t k = tickAt (from) <= (double) frame ? from : 0;
        while (tickAt (k + 1) <= (double) frame)
            ++k;
        from = k;
        return std::max (0.0, ((double) k + 1.0 - (tickAt (k + 1) - (double) frame) / base) / 96.0);
    }
};


// Where notes start: the first frame above `threshold` after at least `gap` frames below `quiet`. A dip inside a note
// (a zero crossing as it starts) neither starts another nor disarms the one starting.
std::vector<long> noteStarts (const std::vector<float>& x, float threshold = 1e-3f, float quiet = 1e-4f, long gap = 96)
{
    std::vector<long> found;
    long silent = gap;
    bool armed = true;
    for (long i = 0; i < (long) x.size(); ++i)
    {
        const float a = std::fabs (x[(size_t) i]);
        if (a < quiet)
        {
            if (++silent >= gap)
                armed = true;
            continue;
        }
        silent = 0;
        if (armed && a > threshold)
            found.push_back (i), armed = false;
    }
    return found;
}

// Each note found near where expected: its onset from `before` frames early to a millisecond and `after` frames late.
bool notesNear (const std::vector<long>& found, const std::vector<long>& expect, double rate, long before, long after, const char* what)
{
    bool ok = found.size() == expect.size() && expect.size() >= 6;
    long worst = 0;
    for (size_t i = 0; ok && i < found.size(); ++i)
    {
        ok = found[i] >= expect[i] - before && found[i] < expect[i] + (long) (rate / 1000) + after;
        worst = std::max (worst, std::labs (found[i] - expect[i]));
    }
    if (std::getenv ("TBMPC_DEBUG"))
    {
        size_t same = 0; // notes where expected, from the start
        while (same < found.size() && same < expect.size() && found[same] >= expect[same] - before
               && found[same] < expect[same] + (long) (rate / 1000) + after)
            ++same;
        std::printf ("debug jitter %s: expected %zu notes, found %zu, the first %zu in place, worst %ld frames\n", what,
                     expect.size(), found.size(), same, ok ? worst : -1L);
    }
    return ok;
}

void testPositionJitter()
{
    // A locked pattern (Density 5, 16 steps) in sixteenths at 44.1 kHz: its notes, found in the output, against where
    // the TB-3PO model puts them for the clocks the song's place makes. A clock played twice steps the pattern on (every
    // later note out of place), one lost holds it back.
    const double rate = 44100.0;
    const uint16_t seed = 0x5a17;
    for (double bpm : { 127.0, 40.0 })
    {
        const double perFrame = bpm / 60.0 / rate, step = 0.25 / perFrame;
        const double beats = bpm > 100.0 ? 64.0 : 24.0;
        const uint64_t blocks = (uint64_t) (beats / perFrame / 128.0);
        const uint64_t frames = blocks * 128;
        const int clocks = (int) std::ceil ((double) frames / step - 1e-9);
        const long offBy = (long) (0.003 / perFrame) + 2; // the place's own error
        // Only the notes up to half a step before the song's last clock count: the place told near its end may bring
        // the next clock in early.
        const long limit = (long) ((std::floor ((double) (frames - 1) / step) - 0.5) * step);
        auto before = [&] (std::vector<long> v) {
            v.erase (std::lower_bound (v.begin(), v.end(), limit), v.end());
            return v;
        };
        auto song = [&] (const std::vector<Call>& calls) {
            Instance a (rate);
            auto p = timingPatch (seed);
            p.push_back ({ "Density", 5 });
            a.setup (p);
            return before (noteStarts (playCalls (a, calls, bpm)));
        };
        auto expect = [&] (const std::function<double (int)>& frameOf) {
            return before (expectedNotesAt (seed, 5, tbmpc::appletDefaultScale, 16, clocks + 1, frameOf));
        };
        char name[64];
        auto named = [&] (const char* what) { std::snprintf (name, sizeof (name), "%.0f BPM %s", bpm, what); return name; };
        const auto straightAt = expect ([&] (int k) { return k * step; });
        CHECK (notesNear (song (hostCalls (blocks, bpm, rate, [&] (uint64_t at) { return (double) at * perFrame; })), straightAt, rate, 0, 0,
                          named ("straight")));

        // Up to 0.002 quarter off the line, either way, block by block; then each block played in two parts split
        // where a note falls.
        std::mt19937 random (7);
        std::vector<double> off (blocks + 1);
        for (auto& o : off)
            o = std::uniform_real_distribution<double> (-0.002, 0.002) (random);
        auto jittered = [&] (uint64_t at) { return std::max (0.0, (double) at * perFrame + off[at / 128]); };
        CHECK (notesNear (song (hostCalls (blocks, bpm, rate, jittered)), straightAt, rate, offBy, offBy, named ("off 0.002")));
        std::vector<uint32_t> cuts (blocks);
        for (auto& c : cuts)
            c = 1 + (uint32_t) (random() % 127);
        CHECK (notesNear (song (hostCalls (blocks, bpm, rate, jittered, [&] (uint64_t b) { return cuts[b]; })), straightAt, rate, offBy, offBy,
                          named ("off 0.002, in parts")));

        // The sequencer's swing at 60 %: the clocks fall where the swung ticks put them.
        SwungPlace swung (bpm, rate, 60.0);
        const auto swungAt = expect ([&] (int k) { return swung.tickAt ((size_t) k * 24); });
        CHECK (notesNear (song (hostCalls (blocks, bpm, rate, [&] (uint64_t at) { return swung (at); })), swungAt, rate, offBy, offBy,
                          named ("swing 60")));

        // Run twice a block (two tracks play it): each clock comes once, in the run told its place first (a block early).
        const auto earlyAt = expect ([&] (int k) { return k == 0 ? 0.0 : k * step - 128.0; });
        CHECK (notesNear (song (hostCalls (blocks, bpm, rate, [&] (uint64_t at) { return (double) at * perFrame; }, {}, true)), earlyAt, rate, 2,
                          2, named ("run twice")));
    }
    // A real locate still counts: the place moved back across a clock by more than a 64th note plays that clock again
    // when the song reaches it (the pattern steps once more); by less, the clock is held. 127 BPM, the place moved back
    // 0.03 quarter after the clock at beat 1 (clock 4).
    {
        const double bpm = 127.0, perFrame = bpm / 60.0 / rate, step = 0.25 / perFrame;
        const uint64_t blocks = (uint64_t) (8.0 / perFrame / 128.0), at = (uint64_t) (1.03 / perFrame / 128.0) * 128;
        for (double by : { 0.05, 0.08 })
        {
            Instance a (rate);
            auto p = timingPatch (seed);
            p.push_back ({ "Density", 5 });
            a.setup (p);
            const auto found = noteStarts (playCalls (a, hostCalls (blocks, bpm, rate, [&] (uint64_t f) { return (double) f * perFrame - (f >= at ? by : 0.0); }), bpm));
            const bool again = by >= 1.0 / 16.0;
            const auto expect = expectedNotesAt (seed, 5, tbmpc::appletDefaultScale, 16, again ? 33 : 32, [&] (int k) {
                if (k <= 4)
                    return k * step;
                return ((k - (again ? 1 : 0)) * 0.25 + by) / perFrame; // clock 4 again, then 5 on; or 5 on, later
            });
            CHECK (notesNear (found, expect, rate, 2, 2, again ? "locate back 0.08" : "locate back 0.05"));
        }
    }
}

// The edges of a jump: the locked pattern (0x5a17, Density 5) at 127 BPM (44.1 kHz), the song from beat 0 in
// hdslaveOS's blocks of 128, every note against the TB-3PO model for the clocks the song's place makes.
void testJumpEdges()
{
    const double rate = 44100.0, bpm = 127.0, perFrame = bpm / 60.0 / rate;
    const uint16_t seed = 0x5a17;
    auto pattern = [&] (Instance& a, std::vector<std::pair<std::string, double>> more = {}) {
        auto p = timingPatch (seed);
        p.push_back ({ "Density", 5 });
        p.insert (p.end(), more.begin(), more.end());
        a.setup (p);
    };
    auto notes = [&] (int clocks, const std::function<double (int)>& frameOf) {
        return expectedNotesAt (seed, 5, tbmpc::appletDefaultScale, 16, clocks, frameOf);
    };
    auto before7 = [&] (std::vector<long> v) { // the notes before beat 7, well inside each song here
        v.erase (std::lower_bound (v.begin(), v.end(), (long) (7.0 / perFrame)), v.end());
        return v;
    };
    // A place told exactly a 64th note behind where the last block's frames led is no jump, as hdslaveOS has it (a move
    // of more than a 64th is one): the clock played (beat 1, in sixteenths) is held, whatever the block before was told.
    // Before, the CLAP rounding of the two places decided, so the same move was a jump after one block (the pattern
    // stepping once more) and not after another.
    {
        const double step = 0.25 / perFrame;
        const uint64_t blocks = (uint64_t) (8.0 / perFrame / 128.0), at = (uint64_t) (1.02 / perFrame / 128.0) * 128;
        int wrong = 0;
        for (double off : { 0.001, -0.001, 0.0013, -0.0007, 0.0002, -0.0019, 0.0, 0.0017 })
        {
            Instance a (rate);
            pattern (a);
            const double before = (double) (at - 128) * perFrame + off; // the block before, told `off` off its line
            const auto found = before7 (noteStarts (playCalls (a, hostCalls (blocks, bpm, rate, [&] (uint64_t f) {
                if (f < at - 128)
                    return (double) f * perFrame;
                if (f < at)
                    return before;
                return before + (double) (f - at + 128) * perFrame - 1.0 / 16.0;
            }), bpm)));
            // Held: the clocks after beat 1 come a 64th later, less `off`. The notes before beat 7 count.
            const auto expect = before7 (notes (32, [&] (int k) { return k <= 4 ? k * step : (k * 0.25 + 1.0 / 16.0 - off) / perFrame; }));
            char what[64];
            std::snprintf (what, sizeof (what), "exactly a 64th back after %+.4f", off);
            wrong += notesNear (found, expect, rate, 2, (long) (0.002 / perFrame) + 2, what) ? 0 : 1;
        }
        CHECK (wrong == 0);
    }
}

void testBlockSizes()
{
    // The same transport and events in blocks of 128, 37, 1 and 4096 frames: the same output, to the bit (with knobs
    // moving, slides of both kinds, a MIDI note and a Regen inside blocks).
    std::vector<float> l128, r128, l, r;
    auto patch = [] () {
        return std::vector<std::pair<std::string, double>> { { "Seed Mode", 1.0 }, { "Seed", 0x5a17 }, { "Resonance", 0.85 }, { "Env Mod", 0.7 },
                                                             { "Accent", 0.9 }, { "Density", 6 } };
    };
    auto events = [] (Instance* a) {
        return [a] (uint64_t at, uint32_t n, Events& e) {
            auto in = [&] (uint64_t when) { return when >= at && when < at + n; };
            if (in (30001)) e.param ((uint32_t) (30001 - at), a->id ("Cutoff"), 0.6);
            if (in (50003)) e.note ((uint32_t) (50003 - at), CLAP_EVENT_NOTE_ON, 67, 1.0);
            if (in (70007)) e.param ((uint32_t) (70007 - at), a->id ("Slide"), 1.0);
            if (in (90011)) e.param ((uint32_t) (90011 - at), a->id ("Regen"), 1.0);
            if (in (110013)) e.param ((uint32_t) (110013 - at), a->id ("Tuning"), 3.5);
        };
    };
    auto t = [] (uint64_t at) { return playingFrom (0.0, 133.0, rate48, at); };
    {
        Instance a;
        a.setup (patch());
        a.render (48000 * 3, 128, l128, r128, t, events (&a));
    }
    for (uint32_t block : { 37u, 1u, 4096u })
    {
        Instance a;
        a.setup (patch());
        a.render (48000 * 3, block, l, r, t, events (&a));
        CHECK (l == l128 && r == r128);
    }
}

void testSeed()
{
    std::vector<float> l, r;
    // Random: each Play is a reset with a new seed, which the host hears as the Seed parameter's value.
    Instance a;
    a.setup ({{"Seed Mode", 0.0}});
    OutEvents out;
    const double before = a.value ("Seed");
    a.render (48000, 128, l, r, [] (uint64_t at) {
        if (at < 24000) return playingFrom (0.0, 120.0, rate48, at);
        if (at < 30000) return Transport { false, 120.0, 0.0 };
        return playingFrom (0.0, 120.0, rate48, at - 30000);
    }, {}, &out);
    std::vector<double> seeds;
    for (auto& [id, v] : out.values)
        if (id == a.id ("Seed"))
            seeds.push_back (v);
    CHECK (seeds.size() == 2 && seeds[0] != before && seeds[0] != seeds[1]);
    CHECK (! seeds.empty() && a.value ("Seed") == seeds.back());
    // Locked: Play keeps it.
    Instance b;
    b.setup ({ { "Seed Mode", 1.0 }, { "Seed", 0x1234 } });
    OutEvents outB;
    b.render (48000, 128, l, r, [] (uint64_t at) { return playingFrom (0.0, 120.0, rate48, at); }, {}, &outB);
    CHECK (outB.values.empty() && b.value ("Seed") == 0x1234);
    // Regen: a reset now; it returns to 0, and in Random mode picks a new seed.
    Instance c;
    c.setup ({{"Seed Mode", 0.0}});
    OutEvents outC;
    c.render (48000, 128, l, r, [] (uint64_t) { return Transport { false, 120.0, 0.0 }; }, [&] (uint64_t at, uint32_t, Events& e) {
        if (at == 128 * 10) c.set (e, "Regen", 1.0);
    }, &outC);
    bool returned = false, newSeed = false;
    for (auto& [id, v] : outC.values)
        returned |= id == c.id ("Regen") && v == 0.0, newSeed |= id == c.id ("Seed");
    CHECK (returned && newSeed && c.value ("Regen") == 0.0);
    // A new seed while playing (locked) plays its pattern from the next clock: until then the output is the same as
    // without the change, and afterwards it is the new seed's.
    std::vector<float> l1, r1, l2, r2;
    Instance d, e;
    d.setup (timingPatch (0x303));
    e.setup (timingPatch (0x303));
    auto t = [] (uint64_t at) { return playingFrom (0.0, 120.0, rate48, at); };
    d.render (48000 * 3, 128, l1, r1, t);
    e.render (48000 * 3, 128, l2, r2, t, [&] (uint64_t at, uint32_t, Events& ev) {
        if (at == 128 * 300) e.set (ev, "Seed", 0x4321); // frame 38,400: in step 6, the next clock at 42,000
    });
    CHECK (std::equal (l1.begin(), l1.begin() + 42000, l2.begin()));
    CHECK (! std::equal (l1.begin() + 42000, l1.end(), l2.begin() + 42000));
}

// Rising zero crossings (with a little hysteresis) in [from, to).
long crossings (const std::vector<float>& x, size_t from, size_t to, float hysteresis = 0.002f)
{
    long n = 0;
    bool high = x[from] > 0.0f;
    for (size_t i = from; i < to; ++i)
    {
        if (! high && x[i] > hysteresis) high = true, ++n;
        else if (high && x[i] < -hysteresis) high = false;
    }
    return n;
}

void testTranspose()
{
    // A MIDI note (C5) and the Transpose knob at +12 do the same; in Semitone, 12 steps up is an octave.
    auto patch = [] () {
        return std::vector<std::pair<std::string, double>> { { "Seed Mode", 1.0 }, { "Seed", 0x303 }, { "Scale", 0 }, { "Density", -7 },
                                                             { "Octave", 2 }, { "Cutoff", 1.0 }, { "Resonance", 0.0 }, { "Env Mod", 0.0 },
                                                             { "Accent", 0.0 } };
    };
    auto t = [] (uint64_t at) { return playingFrom (0.0, 120.0, rate48, at); };
    std::vector<float> plain, l, r, byKnob, byNote;
    {
        Instance a;
        a.setup (patch());
        a.render (48000 * 2, 128, plain, r, t);
    }
    {
        Instance a;
        auto p = patch();
        p.push_back ({ "Transpose", 12 });
        a.setup (p);
        a.render (48000 * 2, 128, byKnob, r, t);
    }
    {
        Instance a;
        a.setup (patch());
        a.render (48000 * 2, 128, byNote, r, t, [] (uint64_t at, uint32_t, Events& e) {
            if (at == 0) e.note (0, CLAP_EVENT_NOTE_ON, 72, 0.8);
        });
    }
    CHECK (byKnob == byNote);
    // Each plain step's note an octave up: twice the zero crossings over the gate's first part.
    tbmpc::Tb3po s;
    s.start (1);
    s.setScale (0);
    s.setDensity (0);
    s.setOctave (2);
    s.setSeed (0x303);
    s.setLocked (true);
    s.reset (0);
    int compared = 0;
    for (int k = 0; k < 15; ++k)
    {
        const int prev = s.currentStep();
        s.clock();
        const bool note = s.opened && ! s.slid (prev);
        if (s.opened)
            s.halfStepPassed();
        if (! note)
            continue;
        const size_t from = (size_t) k * 6000 + 300, to = (size_t) k * 6000 + 2700;
        const long a = crossings (plain, from, to), b = crossings (byKnob, from, to);
        if (std::getenv ("TBMPC_DEBUG"))
            std::printf ("debug transpose step %d: %ld %ld\n", k, a, b);
        CHECK (a >= 6 && std::labs (b - 2 * a) <= 2);
        ++compared;
    }
    CHECK (compared >= 5);
}

void testVoice()
{
    std::vector<float> l, r;
    auto t = [] (uint64_t at) { return playingFrom (0.0, 120.0, rate48, at); };
    auto render = [&] (std::vector<std::pair<std::string, double>> extra) {
        Instance a;
        std::vector<std::pair<std::string, double>> v { { "Seed Mode", 1.0 }, { "Seed", 0x5a17 }, { "Density", 6 } };
        v.insert (v.end(), extra.begin(), extra.end());
        a.setup (v);
        std::vector<float> out, rr;
        a.render (48000 * 4, 128, out, rr, t);
        return out;
    };
    // Accent: accented notes louder (the rest as they were); Cutoff opens the sound; the two waveforms and the two
    // slides differ.
    const auto noAccent = render ({ { "Accent", 0.0 } }), fullAccent = render ({ { "Accent", 1.0 } });
    tbmpc::Tb3po p;
    p.start (1);
    p.setDensity (13);
    p.setSeed (0x5a17);
    p.setLocked (true);
    p.reset (0);
    auto stepRms = [] (const std::vector<float>& x, int k) { double s = 0; for (int i = k * 6000; i < k * 6000 + 3000; ++i) s += (double) x[(size_t) i] * x[(size_t) i]; return std::sqrt (s / 3000.0); };
    int accented = 0, plainSteps = 0;
    for (int k = 0; k < 30; ++k)
    {
        const int prev = p.currentStep();
        p.clock();
        const bool note = p.opened && ! p.slid (prev); // a new note (not a step slid into)
        if (p.opened)
            p.halfStepPassed();
        if (! note)
            continue;
        const double a = stepRms (noAccent, k), b = stepRms (fullAccent, k);
        if (std::getenv ("TBMPC_DEBUG"))
            std::printf ("debug accent step %d %s: %.4f -> %.4f\n", k, p.accented (p.currentStep()) ? "accented" : "plain", a, b);
        if (p.accented (p.currentStep()))
        {
            CHECK (b > a * 1.4);
            ++accented;
        }
        else
        {
            CHECK (b < a * 1.25 && b > a * 0.8);
            ++plainSteps;
        }
    }
    CHECK (accented >= 2 && plainSteps >= 5);
    // Brightness: the share of the signal's change from sample to sample (high frequencies) grows with Cutoff.
    auto brightness = [] (const std::vector<float>& x) {
        double d = 0, e = 0;
        for (size_t i = 1; i < x.size(); ++i)
            d += (double) (x[i] - x[i - 1]) * (x[i] - x[i - 1]), e += (double) x[i] * x[i];
        return std::sqrt (d / (e + 1e-30));
    };
    const auto dark = render ({ { "Cutoff", 0.1 }, { "Env Mod", 0.0 } }), bright = render ({ { "Cutoff", 0.8 }, { "Env Mod", 0.0 } });
    if (std::getenv ("TBMPC_DEBUG"))
        std::printf ("debug brightness dark %.4f bright %.4f\n", brightness (dark), brightness (bright));
    CHECK (brightness (bright) > 2.0 * brightness (dark));
    CHECK (render ({ { "Waveform", 1.0 } }) != render ({ { "Waveform", 0.0 } }));
    CHECK (render ({ { "Slide", 1.0 } }) != render ({ { "Slide", 0.0 } }));
    // Stopped: silence, exactly.
    Instance s;
    s.render (48000, 128, l, r, [] (uint64_t) { return Transport { false, 120.0, 0.0 }; });
    CHECK (std::all_of (l.begin(), l.end(), [] (float v) { return v == 0.0f; }));
}

void testSwing()
{
    // Swing 75 %: every note where the pattern puts it, the odd steps a quarter of a pair late; a swung step's gate is
    // half its own length. Sixteenths at 120 BPM: a pair is 12,000 frames, an even step 9,000, an odd one 3,000.
    for (double swing : { 0.62, 0.75 })
    {
        Instance a;
        auto patch = timingPatch (0x303);
        patch.push_back ({ "Density", 4 });
        patch.push_back ({ "Swing", swing });
        a.setup (patch);
        std::vector<float> l, r;
        a.render (48000 * 6, 128, l, r, [] (uint64_t at) { return playingFrom (0.0, 120.0, rate48, at); });
        const auto expect = expectedNotes (0x303, 4, tbmpc::appletDefaultScale, 16, 6000.0, 48, swing);
        const auto found = onsets (l, 1e-3f, 1e-4f, 96);
        if (std::getenv ("TBMPC_DEBUG"))
            for (size_t i = 0; i < std::max (found.size(), expect.size()); ++i)
                std::printf ("debug swing %.2f: %ld expected %ld\n", swing, i < found.size() ? found[i] : -1L, i < expect.size() ? (long) expect[i] : -1L);
        CHECK (expect.size() > 8 && found.size() == expect.size());
        bool near = found.size() == expect.size();
        for (size_t i = 0; near && i < found.size(); ++i)
            near = found[i] >= expect[i] && found[i] < expect[i] + 48;
        CHECK (near);
    }
    Instance b;
    b.setup ({ { "Seed Mode", 1.0 }, { "Seed", 0x303 }, { "Density", -7 }, { "Cutoff", 1.0 }, { "Resonance", 0.0 }, { "Env Mod", 0.0 },
               { "Accent", 0.0 }, { "Swing", 0.75 } });
    std::vector<float> l, r;
    b.render (48000 * 4, 128, l, r, [] (uint64_t at) { return playingFrom (0.0, 120.0, rate48, at); });
    tbmpc::Tb3po t;
    t.start (1);
    t.setDensity (0);
    t.setSeed (0x303);
    t.setLocked (true);
    t.reset (0);
    auto peak = [&] (long from, long to) { float m = 0; for (long i = from; i < to; ++i) m = std::max (m, std::fabs (l[(size_t) i])); return m; };
    int even = 0, odd = 0;
    for (int k = 0; k < 30; ++k)
    {
        const int prev = t.currentStep();
        t.clock();
        if (k < 1 || t.slid (t.currentStep()) || t.slid (prev))
            continue; // not a plain note
        const long start = (k / 2) * 12000 + ((k & 1) ? 9000 : 0), length = (k & 1) ? 3000 : 9000;
        const float before = peak (start + length / 2 - 300, start + length / 2 - 10);
        CHECK (before > 0.01f);
        if (k & 1)
        {
            CHECK (peak (start + length - 300, start + length - 10) < 0.05f * before); // closed, and fading
            ++odd;
        }
        else
        {
            CHECK (peak (start + length / 2 + 2400, start + length - 10) < 1e-4f); // closed and silent
            ++even;
        }
    }
    CHECK (even >= 3 && odd >= 3);
}

// Every value the plug-in reported back for a parameter, in order.
std::vector<double> reported (OutEvents& out, clap_id id)
{
    std::vector<double> v;
    for (auto& [i, x] : out.values)
        if (i == id)
            v.push_back (x);
    return v;
}

void testSeedBack()
{
    std::vector<float> l, r, l2, r2;
    auto t = [] (uint64_t at) { return playingFrom (0.0, 120.0, rate48, at); };
    // Locked: seed 0303 plays, then 1234; Seed Back (on a clock) goes back to 0303: from the next step the notes are
    // 0303's again, and the host hears Seed 0303 and Seed Mode Locked.
    {
        Instance a, ref;
        a.setup (timingPatch (0x303));
        ref.setup (timingPatch (0x303));
        OutEvents out;
        a.render (48000 * 8, 128, l, r, t, [&] (uint64_t at, uint32_t, Events& e) {
            if (at == 96000) a.set (e, "Seed", 0x1234);
            if (at == 192000) a.set (e, "Seed Back", 1.0);
        }, &out);
        ref.render (48000 * 8, 128, l2, r2, t);
        CHECK (a.value ("Seed") == 0x303 && a.value ("Seed Mode") == 1.0);
        CHECK ((reported (out, a.id ("Seed")) == std::vector<double> { 0x303 }) && (reported (out, a.id ("Seed Mode")) == std::vector<double> { 1.0 }));
        auto from = [] (const std::vector<long>& x, long at) { std::vector<long> y; for (long h : x) if (h >= at) y.push_back (h); return y; };
        const auto mine = from (onsets (l), 198000), theirs = from (onsets (l2), 198000);
        const auto middle = from (onsets (l), 102000), refMiddle = from (onsets (l2), 102000);
        CHECK (mine.size() > 8 && mine.size() == theirs.size());
        bool same = mine.size() == theirs.size();
        for (size_t i = 0; same && i < mine.size(); ++i)
            same = std::labs (mine[i] - theirs[i]) <= 48;
        CHECK (same);
        CHECK (middle != refMiddle); // 1234 played something else in between
    }
    // Random: each Play picks a seed; Seed Back gives the first Play's one back, locked. Twice: further back.
    {
        Instance a;
        a.setup ({{"Seed Mode", 0.0}});
        OutEvents out;
        a.render (48000 * 3, 128, l, r, [] (uint64_t at) {
            if (at < 24000) return playingFrom (0.0, 120.0, rate48, at);
            if (at < 30000) return Transport { false, 120.0, 0.0 };
            if (at < 54000) return playingFrom (0.0, 120.0, rate48, at - 30000);
            if (at < 60000) return Transport { false, 120.0, 0.0 };
            return playingFrom (0.0, 120.0, rate48, at - 60000);
        }, [&] (uint64_t at, uint32_t, Events& e) {
            if (at == 128 * 600) a.set (e, "Seed Back", 1.0);
            if (at == 128 * 800) a.set (e, "Seed Back", 1.0);
        }, &out);
        const auto seeds = reported (out, a.id ("Seed"));
        // Three Plays (three seeds); Back at 76,800 (third playing): the second; Back at 102,400: the first.
        CHECK (seeds.size() == 5 && seeds[3] == seeds[1] && seeds[4] == seeds[0]);
        CHECK (a.value ("Seed Mode") == 1.0 && ! seeds.empty() && a.value ("Seed") == seeds.back());
    }
    // Nothing played yet: Seed Back does nothing.
    {
        Instance a;
        OutEvents out;
        a.render (4800, 128, l, r, [] (uint64_t) { return Transport { false, 120.0, 0.0 }; }, [&] (uint64_t at, uint32_t, Events& e) {
            if (at == 0) a.set (e, "Seed Back", 1.0);
        }, &out);
        CHECK (reported (out, a.id ("Seed")).empty() && a.value ("Seed Mode") == 1.0);
    }
}

void testNotes()
{
    // Mode: Notes with the transport stopped, a clean saw: a note plays at once at its pitch (C3 = 130.8 Hz); one
    // played over it glides to G3 (196 Hz) without a new note; letting the top one go glides back; letting go of the
    // last closes the gate.
    auto patch = [] () {
        return std::vector<std::pair<std::string, double>> { { "Mode", 1.0 }, { "Cutoff", 1.0 }, { "Resonance", 0.0 }, { "Env Mod", 0.0 },
                                                             { "Accent", 0.0 } };
    };
    auto stopped = [] (uint64_t) { return Transport { false, 120.0, 0.0 }; };
    std::vector<float> l, r;
    Instance a;
    a.setup (patch());
    a.render (48000 * 2, 128, l, r, stopped, [] (uint64_t at, uint32_t n, Events& e) {
        auto in = [&] (uint64_t when) { return when >= at && when < at + n; };
        if (in (1000)) e.note ((uint32_t) (1000 - at), CLAP_EVENT_NOTE_ON, 48, 0.6);
        if (in (20000)) e.note ((uint32_t) (20000 - at), CLAP_EVENT_NOTE_OFF, 48, 0.0);
        if (in (30000)) e.note ((uint32_t) (30000 - at), CLAP_EVENT_NOTE_ON, 48, 0.6);
        if (in (40000)) e.note ((uint32_t) (40000 - at), CLAP_EVENT_NOTE_ON, 55, 0.6);
        if (in (52000)) e.note ((uint32_t) (52000 - at), CLAP_EVENT_NOTE_OFF, 55, 0.0);
        if (in (64000)) e.note ((uint32_t) (64000 - at), CLAP_EVENT_NOTE_OFF, 48, 0.0);
    });
    const auto hits = onsets (l, 1e-3f, 1e-4f, 96);
    if (std::getenv ("TBMPC_DEBUG"))
    {
        std::printf ("debug notes:");
        for (long h : hits) std::printf (" %ld", h);
        std::printf ("\n");
    }
    CHECK (hits.size() == 2 && hits[0] >= 1000 && hits[0] < 1048 && hits[1] >= 30000 && hits[1] < 30048);
    auto hz = [&] (size_t from, size_t to) { return crossings (l, from, to) * 48000.0 / (double) (to - from); };
    auto peak = [&] (size_t from, size_t to) { float m = 0; for (size_t i = from; i < to; ++i) m = std::max (m, std::fabs (l[i])); return m; };
    CHECK (std::fabs (hz (4000, 16000) - 130.8) < 3.0);
    CHECK (peak (22000, 29000) < 1e-4f);
    CHECK (std::fabs (hz (44000, 51000) - 196.0) < 4.0);  // glided to G3
    CHECK (peak (40000, 44000) > 0.05f);                   // no gap: the gate stayed open
    CHECK (std::fabs (hz (56000, 63000) - 130.8) < 3.0);  // back to C3, still held
    CHECK (peak (66000, 96000) < 1e-4f);
    // Accent: velocity 100 and up, louder with Accent up.
    auto loud = [&] (double velocity) {
        Instance b;
        auto p = patch();
        p.push_back ({ "Accent", 1.0 });
        b.setup (p);
        std::vector<float> x, y;
        b.render (24000, 128, x, y, stopped, [velocity] (uint64_t at, uint32_t, Events& e) {
            if (at == 0) e.note (0, CLAP_EVENT_NOTE_ON, 48, velocity);
        });
        double sum = 0;
        for (size_t i = 0; i < 9600; ++i) sum += (double) x[i] * x[i];
        return std::sqrt (sum / 9600);
    };
    CHECK (loud (100.0 / 127.0) > loud (99.0 / 127.0) * 1.2);
    // Mode: Notes while the host plays and no note is held: TB-3PO is not heard.
    Instance c;
    c.setup (patch());
    c.render (48000, 128, l, r, [] (uint64_t at) { return playingFrom (0.0, 120.0, rate48, at); });
    CHECK (std::all_of (l.begin(), l.end(), [] (float v) { return v == 0.0f; }));
    // Back to TB-3PO with a key held: the note stops, and TB-3PO plays again on its clock.
    Instance d;
    d.setup (patch());
    d.render (48000 * 2, 128, l, r, [] (uint64_t at) { return playingFrom (0.0, 120.0, rate48, at); }, [&] (uint64_t at, uint32_t, Events& e) {
        if (at == 0) e.note (0, CLAP_EVENT_NOTE_ON, 60, 0.8);
        if (at == 128 * 200) d.set (e, "Mode", 0.0);
    });
    CHECK (peak (12000, 25000) > 0.05f && peak (25600 + 2400, 25600 + 2900) < 1e-3f);
    CHECK (peak (48000, 96000) > 0.01f);
}

void testGateAndMutate()
{
    // Gate 20 %: plain notes sound until a fifth of the step and are silent well before the next; 90 %: until nine
    // tenths (sixteenths at 120 BPM: 6,000 frames).
    for (double gate : { 0.2, 0.9 })
    {
        Instance b;
        b.setup ({ { "Seed Mode", 1.0 }, { "Seed", 0x303 }, { "Density", -7 }, { "Cutoff", 1.0 }, { "Resonance", 0.0 }, { "Env Mod", 0.0 },
                   { "Accent", 0.0 }, { "Gate", gate } });
        std::vector<float> l, r;
        b.render (48000 * 4, 128, l, r, [] (uint64_t at) { return playingFrom (0.0, 120.0, rate48, at); });
        tbmpc::Tb3po t;
        t.start (1);
        t.setDensity (0);
        t.setSeed (0x303);
        t.setLocked (true);
        t.reset (0);
        auto peak = [&] (long from, long to) { float m = 0; for (long i = from; i < to; ++i) m = std::max (m, std::fabs (l[(size_t) i])); return m; };
        int plain = 0;
        for (int k = 0; k < 30; ++k)
        {
            const int prev = t.currentStep();
            t.clock();
            if (k < 1 || t.slid (t.currentStep()) || t.slid (prev))
                continue;
            const long at = k * 6000, open = (long) (6000 * gate);
            CHECK (peak (at + open - 300, at + open - 10) > 0.01f);
            if (gate < 0.5)
                CHECK (peak (at + open + 2400, at + 5990) < 1e-4f);
            ++plain;
        }
        CHECK (plain >= 5);
    }
    // Mutate on the sequencer itself: a Random reset with Mutate takes each step from the new seed's pattern or keeps
    // the old one's, as the new seed's own pick says; 0 % and 100 % are the new pattern whole; Locked changes nothing.
    auto same = [] (const tbmpc::Tb3po& a, int sa, const tbmpc::Tb3po& b, int sb) {
        return a.note (sa) == b.note (sb) && a.gated (sa) == b.gated (sb) && a.slid (sa) == b.slid (sb) && a.accented (sa) == b.accented (sb)
               && a.octaveUp (sa) == b.octaveUp (sb) && a.octaveDown (sa) == b.octaveDown (sb);
    };
    double taken = 0.0;
    int trials = 0;
    bool exact = true, whole = true, locked = true;
    for (uint32_t trial = 0; trial < 200; ++trial)
    {
        const float mutate = trial < 100 ? 0.25f : 0.6f;
        tbmpc::Tb3po m;
        m.start (1);
        m.setSeed ((uint16_t) (trial * 977 + 3));
        m.setLocked (true);
        m.reset (0);
        tbmpc::Tb3po old = m;
        m.setLocked (false);
        m.reset (trial * 7919 + 11, mutate);
        tbmpc::Tb3po fresh;
        fresh.start (1);
        fresh.setSeed (m.currentSeed());
        fresh.setLocked (true);
        fresh.reset (0);
        tbmpc::TeensyRandom pick;
        pick.randomSeed ((uint32_t) m.currentSeed() + 5u);
        int fromNew = 0;
        for (int s = 0; s < 32; ++s)
        {
            const bool takesNew = pick.between (0, 1000) < (int) std::lround (mutate * 1000.0f);
            exact &= takesNew ? same (m, s, fresh, s) : same (m, s, old, s);
            fromNew += takesNew;
        }
        if (trial < 100)
            taken += fromNew / 32.0, ++trials;
        // 0 % and 100 %: the new seed's pattern, whole.
        for (float all : { 0.0f, 1.0f })
        {
            tbmpc::Tb3po w = old;
            w.reset (trial * 7919 + 11, all);
            tbmpc::Tb3po f;
            f.start (1);
            f.setSeed (w.currentSeed());
            f.setLocked (true);
            f.reset (0);
            for (int s = 0; s < 32; ++s)
                whole &= same (w, s, f, s);
        }
        // Locked: the same pattern again, whatever Mutate says.
        tbmpc::Tb3po k = old;
        k.setLocked (true);
        k.reset (trial * 31 + 1, 0.5f);
        for (int s = 0; s < 32; ++s)
            locked &= same (k, s, old, s);
    }
    CHECK (exact && whole && locked);
    CHECK (std::fabs (taken / trials - 0.25) < 0.05);
}

void testBendAndWheel()
{
    // Mode: Notes, a clean saw on C3 (130.8 Hz). Pitch bend and the mod wheel do what Tuning and Cutoff do: full bend
    // up (16383) is Tuning at +1.99976 semitones, the wheel at 127 is Cutoff 3 octaves up (of its 7), to the bit
    // once they have eased in (the note comes after). All notes off (CC 123) closes the gate.
    auto stopped = [] (uint64_t) { return Transport { false, 120.0, 0.0 }; };
    auto render = [&] (std::vector<std::pair<std::string, double>> extra, uint8_t status, uint8_t d1, uint8_t d2) {
        Instance a;
        std::vector<std::pair<std::string, double>> v { { "Mode", 1.0 }, { "Cutoff", 0.1 }, { "Resonance", 0.0 }, { "Env Mod", 0.0 }, { "Accent", 0.0 } };
        v.insert (v.end(), extra.begin(), extra.end());
        a.setup (v);
        std::vector<float> l, r;
        a.render (48000, 128, l, r, stopped, [&] (uint64_t at, uint32_t, Events& e) {
            if (at == 0 && status != 0) e.midiMessage (0, status, d1, d2);
            if (at == 128 * 40) e.note (0, CLAP_EVENT_NOTE_ON, 48, 0.6);
        });
        return l;
    };
    const auto bent = render ({}, 0xe0, 0x7f, 0x7f), tuned = render ({ { "Tuning", 8191.0 * 2.0 / 8192.0 } }, 0, 0, 0);
    const auto wheeled = render ({}, 0xb0, 1, 127), opened = render ({ { "Cutoff", 0.1 + 3.0 / 7.0 } }, 0, 0, 0);
    const auto plain = render ({}, 0, 0, 0);
    auto rmsDiff = [] (const std::vector<float>& a, const std::vector<float>& b) {
        double d = 0, e = 0;
        for (size_t i = 0; i < a.size(); ++i) d += (double) (a[i] - b[i]) * (a[i] - b[i]), e += (double) b[i] * b[i];
        return std::sqrt (d / (e + 1e-30));
    };
    if (std::getenv ("TBMPC_DEBUG"))
        std::printf ("debug bend/wheel: %.6f %.6f against plain %.3f %.3f\n", rmsDiff (bent, tuned), rmsDiff (wheeled, opened), rmsDiff (bent, plain), rmsDiff (wheeled, plain));
    CHECK (rmsDiff (bent, tuned) < 1e-4 && rmsDiff (wheeled, opened) < 1e-4);
    CHECK (rmsDiff (bent, plain) > 0.1 && rmsDiff (wheeled, plain) > 0.1);
    // Bent while the note plays: its pitch moves (130.8 -> 146.8 Hz).
    auto hz = [] (const std::vector<float>& x, size_t from, size_t to) { return crossings (x, from, to) * 48000.0 / (double) (to - from); };
    std::vector<float> l, r;
    Instance a;
    a.setup ({ { "Mode", 1.0 }, { "Cutoff", 1.0 }, { "Resonance", 0.0 }, { "Env Mod", 0.0 }, { "Accent", 0.0 } });
    a.render (96000, 128, l, r, stopped, [] (uint64_t at, uint32_t, Events& e) {
        if (at == 0) e.note (0, CLAP_EVENT_NOTE_ON, 48, 0.6);
        if (at == 128 * 200) e.midiMessage (0, 0xe0, 0x7f, 0x7f);
        if (at == 128 * 600) e.midiMessage (0, 0xb0, 123, 0);
    });
    CHECK (std::fabs (hz (l, 1000, 25000) - 130.8) < 2.0);
    CHECK (std::fabs (hz (l, 28000, 76000) - 146.8) < 2.0);
    float after = 0.0f;
    for (size_t i = 128 * 600 + 2400; i < l.size(); ++i)
        after = std::max (after, std::fabs (l[i]));
    CHECK (after < 1e-4f);
}

void testExtremes()
{
    // Random settings, every one in its range, at a fast tempo: the output stays a number and within full scale.
    std::mt19937 random (7);
    std::vector<float> l, r;
    for (int round = 0; round < 60; ++round)
    {
        Instance a;
        std::vector<std::pair<std::string, double>> values;
        for (uint32_t i = 0; i < a.params->count (a.plugin); ++i)
        {
            clap_param_info info {};
            a.params->get_info (a.plugin, i, &info);
            if (std::string (info.name) == "Regen")
                continue;
            std::uniform_real_distribution<double> d (info.min_value, info.max_value);
            double v = round < 2 ? (round == 0 ? info.min_value : info.max_value) : d (random);
            if (info.flags & CLAP_PARAM_IS_STEPPED)
                v = std::round (v);
            values.push_back ({ info.name, v });
        }
        a.setup (values);
        a.render (24000, 128, l, r, [] (uint64_t at) { return playingFrom (0.0, 170.0, rate48, at); }, [&] (uint64_t at, uint32_t, Events& e) {
            if (at == 128 * 20) e.note (3, CLAP_EVENT_NOTE_ON, (int16_t) (random() % 128), 1.0);
        });
        bool finite = true;
        float peak = 0.0f;
        for (float s : l)
            finite &= std::isfinite (s), peak = std::max (peak, std::fabs (s));
        CHECK (finite && peak <= 1.0f);
    }
}

void demos (const std::string& folder)
{
    // 16 s of each factory preset at 120 BPM. Each preset (loaded by its key) must give exactly the values listed
    // here and the same sound. The files are rendered from the preset.
    struct Demo { const char* file; const char* preset; std::vector<std::pair<std::string, double>> values; bool notes = false; };
    std::vector<Demo> list {
        { "demo-acid-line.wav", "acid-line", { { "Seed Mode", 1 }, { "Seed", 0x5a17 }, { "Scale", 6 }, { "Density", 5 }, { "Cutoff", 0.465 },
                                               { "Resonance", 0.78 }, { "Env Mod", 0.62 }, { "Decay", 0.3 }, { "Accent", 0.8 } } },
        { "demo-root-pulse.wav", "root-pulse", { { "Seed Mode", 1 }, { "Seed", 0x0303 }, { "Scale", 0 }, { "Density", -7 }, { "Cutoff", 0.42 },
                                                 { "Resonance", 0.85 }, { "Env Mod", 0.7 }, { "Decay", 0.25 }, { "Accent", 1.0 } } },
        { "demo-square-lead.wav", "square-lead", { { "Seed Mode", 1 }, { "Seed", 0xbeef }, { "Scale", 3 }, { "Density", 6 }, { "Octave", 1 },
                                                   { "Waveform", 1 }, { "Slide", 1 }, { "Cutoff", 0.571 }, { "Resonance", 0.7 }, { "Env Mod", 0.45 },
                                                   { "Decay", 0.45 }, { "Accent", 0.5 } } },
        { "demo-dub-steps.wav", "dub-steps", { { "Seed Mode", 1 }, { "Seed", 0x1d0b }, { "Scale", 2 }, { "Density", 3 }, { "Steps", 12 },
                                               { "Octave", -1 }, { "Cutoff", 0.496 }, { "Resonance", 0.5 }, { "Env Mod", 0.35 }, { "Decay", 0.75 },
                                               { "Accent", 0.4 }, { "Volume", 0.8 } } },
        // Note Bass is played by notes: a two-bar line, some held over the next (glides), some loud (accents).
        { "demo-note-bass.wav", "note-bass", { { "Mode", 1 }, { "Cutoff", 0.45 }, { "Resonance", 0.74 }, { "Env Mod", 0.62 }, { "Decay", 0.38 },
                                               { "Accent", 0.8 } }, true },
    };
    // The line: sixteenths at 120 BPM (6,000 frames), as (step, key, steps held, velocity); played on repeat.
    struct Note { int step, key, length; double velocity; };
    const Note line[] = { { 0, 36, 1, 1.0 }, { 2, 36, 1, 0.6 }, { 3, 48, 2, 0.6 }, { 4, 46, 1, 0.6 }, { 6, 39, 1, 1.0 }, { 8, 36, 1, 0.6 },
                          { 10, 41, 2, 0.6 }, { 11, 43, 1, 1.0 }, { 12, 36, 1, 0.6 }, { 14, 34, 1, 0.6 }, { 15, 36, 1, 1.0 },
                          { 16, 36, 1, 1.0 }, { 18, 48, 1, 0.6 }, { 19, 36, 2, 0.6 }, { 20, 39, 1, 1.0 }, { 22, 36, 1, 0.6 },
                          { 24, 41, 3, 0.6 }, { 26, 43, 1, 0.6 }, { 27, 46, 1, 1.0 }, { 28, 36, 1, 0.6 }, { 30, 48, 1, 0.6 } };
    auto lineEvents = [&line] (uint64_t at, uint32_t n, Events& e) {
        for (uint64_t loop = at / 192000; loop <= (at + n) / 192000; ++loop)
            for (const auto& x : line)
            {
                const uint64_t on = loop * 192000 + (uint64_t) x.step * 6000, off = on + (uint64_t) x.length * 6000 - 2400;
                if (on >= at && on < at + n) e.note ((uint32_t) (on - at), CLAP_EVENT_NOTE_ON, (int16_t) x.key, x.velocity);
                if (off >= at && off < at + n) e.note ((uint32_t) (off - at), CLAP_EVENT_NOTE_OFF, (int16_t) x.key, 0.0);
            }
    };
    for (auto& demo : list)
    {
        Instance a, b;
        a.setup (demo.values);
        CHECK (b.loadPreset (demo.preset));
        b.setup ({});
        for (auto& [name, id] : a.ids)
            CHECK (a.value (name) == b.value (name));
        std::vector<float> l, r, l2, r2;
        auto t = [] (uint64_t at) { return playingFrom (0.0, 120.0, rate48, at); };
        if (demo.notes)
        {
            a.render (48000 * 16, 128, l, r, t, lineEvents);
            b.render (48000 * 16, 128, l2, r2, t, lineEvents);
        }
        else
        {
            a.render (48000 * 16, 128, l, r, t);
            b.render (48000 * 16, 128, l2, r2, t);
        }
        CHECK (l == l2 && r == r2);
        float peak = 0.0f;
        for (float s : l2)
            peak = std::max (peak, std::fabs (s));
        std::printf ("  %s: peak %.3f\n", demo.file, peak);
        CHECK (peak > 0.1f && peak <= 1.0f);
        const auto path = folder + "/" + demo.file;
        CHECK (writeWav (path.c_str(), l2, r2, 48000));
    }
}
} // namespace

void testDriveSlideAccentDecay()
{
    auto t = [] (uint64_t at) { return playingFrom (0.0, 120.0, rate48, at); };
    auto stopped = [] (uint64_t) { return Transport { false, 120.0, 0.0 }; };
    auto rms = [] (const std::vector<float>& x, size_t from, size_t to) {
        double s = 0;
        for (size_t i = from; i < to; ++i) s += (double) x[i] * x[i];
        return std::sqrt (s / (double) (to - from));
    };
    auto brightness = [] (const std::vector<float>& x, size_t from, size_t to) {
        double d = 0, e = 0;
        for (size_t i = from + 1; i < to; ++i)
            d += (double) (x[i] - x[i - 1]) * (x[i] - x[i - 1]), e += (double) x[i] * x[i];
        return std::sqrt (d / (e + 1e-30));
    };
    // Drive: brighter (more harmonics) the further it turns, its level kept within a few dB of the dry voice, never
    // past full scale, and nothing at all at 0 (the fingerprints hold that to the bit).
    auto pattern = [&] (double drive, std::vector<std::pair<std::string, double>> extra = {}) {
        Instance a;
        std::vector<std::pair<std::string, double>> v { { "Seed Mode", 1.0 }, { "Seed", 0x5a17 }, { "Density", 6 }, { "Drive", drive } };
        v.insert (v.end(), extra.begin(), extra.end());
        a.setup (v);
        std::vector<float> l, r;
        a.render (48000 * 4, 128, l, r, t);
        return l;
    };
    double lastBright = 0.0;
    for (auto patch : std::vector<std::vector<std::pair<std::string, double>>> { {}, { { "Cutoff", 0.15 }, { "Resonance", 0.9 } }, { { "Waveform", 1.0 }, { "Cutoff", 0.7 } } })
    {
        // (Brightness counted from the first drive on: the anti-aliasing's half-sample average takes a little off the
        // very top, so a light drive is not brighter than none by this measure.)
        const auto dry = pattern (0.0, patch);
        lastBright = 0.0;
        for (double drive : { 0.25, 0.5, 0.75, 1.0 })
        {
            const auto wet = pattern (drive, patch);
            const double level = rms (wet, 0, wet.size()) / rms (dry, 0, dry.size()), bright = brightness (wet, 0, wet.size());
            float peak = 0.0f;
            for (float v : wet) peak = std::max (peak, std::fabs (v));
            if (std::getenv ("TBMPC_DEBUG"))
                std::printf ("debug drive %.2f: level %+.2f dB, brightness %.4f, peak %.3f\n", drive, 20.0 * std::log10 (level), bright, peak);
            CHECK (level > 0.71 && level < 2.0); // -3 .. +6 dB
            CHECK (bright > lastBright);
            CHECK (std::isfinite (peak) && peak <= 1.0f);
            lastBright = bright;
        }
    }
    // Slide Time: a note held over another glides to it, there in about the time the knob reads. At 15 ms it has
    // arrived 100 ms on; at 600 ms it is still under half way.
    auto hz = [] (const std::vector<float>& x, size_t from, size_t to) { return crossings (x, from, to) * 48000.0 / (double) (to - from); };
    auto glide = [&] (double slideKnob) {
        Instance a;
        a.setup ({ { "Mode", 1.0 }, { "Cutoff", 1.0 }, { "Resonance", 0.0 }, { "Env Mod", 0.0 }, { "Accent", 0.0 }, { "Slide Time", slideKnob } });
        std::vector<float> l, r;
        a.render (48000, 128, l, r, stopped, [] (uint64_t at, uint32_t, Events& e) {
            if (at == 0) e.note (0, CLAP_EVENT_NOTE_ON, 48, 0.6);
            if (at == 128 * 100) e.note (0, CLAP_EVENT_NOTE_ON, 60, 0.6); // an octave up, held over the first
        });
        return hz (l, 12800 + 4800, 12800 + 4800 + 2400); // 100 to 150 ms after the glide starts
    };
    double fast = 0, slow = 0, slideKnob = -1;
    Instance k;
    CHECK ((slideKnob = k.valueOf ("Slide Time", "15 ms")) >= 0.0 && (fast = glide (slideKnob)) > 0);
    CHECK ((slideKnob = k.valueOf ("Slide Time", "600 ms")) >= 0.0 && (slow = glide (slideKnob)) > 0);
    if (std::getenv ("TBMPC_DEBUG"))
        std::printf ("debug slide: 15 ms %.1f Hz, 600 ms %.1f Hz (130.8 -> 261.6)\n", fast, slow);
    CHECK (std::fabs (fast - 261.6) < 6.0 && slow < 190.0);
    char text[64] = {};
    k.params->value_to_text (k.plugin, k.id ("Slide Time"), k.value ("Slide Time"), text, sizeof (text));
    CHECK (std::string (text) == "60 ms");
    // Accent Decay: an accented note's filter sweep falls more slowly the longer it is (its default, the shortest,
    // is the 303's).
    auto accented = [&] (double accentDecay) {
        Instance a;
        a.setup ({ { "Mode", 1.0 }, { "Cutoff", 0.25 }, { "Resonance", 0.3 }, { "Env Mod", 0.9 }, { "Accent", 1.0 }, { "Accent Decay", accentDecay } });
        std::vector<float> l, r;
        a.render (48000, 128, l, r, stopped, [] (uint64_t at, uint32_t, Events& e) {
            if (at == 0) e.note (0, CLAP_EVENT_NOTE_ON, 36, 1.0);
        });
        return brightness (l, 24000, 33600); // 500 to 700 ms in
    };
    const double shortest = accented (0.0), longest = accented (1.0);
    if (std::getenv ("TBMPC_DEBUG"))
        std::printf ("debug accent decay: brightness 500-700 ms %.4f (200 ms) %.4f (2 s)\n", shortest, longest);
    CHECK (longest > 1.5 * shortest);
}

void testFilters()
{
    auto stopped = [] (uint64_t) { return Transport { false, 120.0, 0.0 }; };
    // A held C1 (32.7 Hz, as the 303's service notes play it), Env Mod and Accent at 0: Resonance full.
    auto held = [&] (std::vector<std::pair<std::string, double>> extra) {
        Instance a;
        std::vector<std::pair<std::string, double>> v { { "Mode", 1.0 }, { "Env Mod", 0.0 }, { "Accent", 0.0 }, { "Resonance", 1.0 } };
        v.insert (v.end(), extra.begin(), extra.end());
        a.setup (v);
        std::vector<float> l, r;
        a.render (48000, 128, l, r, stopped, [] (uint64_t at, uint32_t, Events& e) { if (at == 0) e.note (0, CLAP_EVENT_NOTE_ON, 24, 0.6); });
        return l;
    };
    auto magnitude = [] (const std::vector<float>& x, size_t from, size_t to, double hz) {
        double re = 0, im = 0;
        for (size_t i = from; i < to; ++i)
        {
            const double ph = 2.0 * M_PI * hz * (double) i / 48000.0;
            re += x[i] * std::cos (ph), im += x[i] * std::sin (ph);
        }
        return std::sqrt (re * re + im * im) / (double) (to - from);
    };
    auto strongest = [&] (const std::vector<float>& x) {
        double best = 0, at = 0;
        for (double hz = 150.0; hz < 3000.0; hz += 2.0)
        {
            const double m = magnitude (x, 12000, 36000, hz);
            if (m > best) best = m, at = hz;
        }
        return at;
    };
    // The calibration: Cutoff at its middle, Resonance full, the peak at about 500 Hz in each filter (Twin Peak's
    // lower one: its upper is put 4 octaves away). The harmonics are 32.7 Hz apart.
    for (auto patch : std::vector<std::vector<std::pair<std::string, double>>> { { { "Filter", 0.0 } }, { { "Filter", 2.0 } }, { { "Filter", 1.0 }, { "Peak Spread", 4.0 } } })
    {
        patch.push_back ({ "Cutoff", 0.5 });
        const double at = strongest (held (patch));
        if (std::getenv ("TBMPC_DEBUG"))
            std::printf ("debug filter %.0f: strongest at %.0f Hz\n", patch[0].second, at);
        CHECK (at > 450.0 && at < 550.0);
    }
    // Each filter sounds its own; Twin Peak's spread moves its upper peak.
    const auto f303 = held ({ { "Cutoff", 0.5 }, { "Resonance", 0.7 } }), twin = held ({ { "Cutoff", 0.5 }, { "Resonance", 0.7 }, { "Filter", 1.0 } });
    const auto eighteen = held ({ { "Cutoff", 0.5 }, { "Resonance", 0.7 }, { "Filter", 2.0 } });
    const auto wide = held ({ { "Cutoff", 0.5 }, { "Resonance", 0.7 }, { "Filter", 1.0 }, { "Peak Spread", 3.0 } });
    CHECK (f303 != twin && f303 != eighteen && twin != eighteen && twin != wide);
    // Twin Peak passes a band: little of the fundamental (32.7 Hz), unlike the low passes.
    CHECK (magnitude (twin, 12000, 36000, 32.7) < 0.25 * magnitude (f303, 12000, 36000, 32.7));
    // The patterns' level in each filter: within a few dB of the diode ladder's.
    auto t = [] (uint64_t at) { return playingFrom (0.0, 120.0, rate48, at); };
    auto pattern = [&] (double filter) {
        Instance a;
        a.setup ({ { "Seed Mode", 1.0 }, { "Seed", 0x5a17 }, { "Density", 6 }, { "Filter", filter } });
        std::vector<float> l, r;
        a.render (48000 * 4, 128, l, r, t);
        double e = 0;
        float peak = 0;
        for (float v : l) e += (double) v * v, peak = std::max (peak, std::fabs (v));
        CHECK (std::isfinite (peak) && peak <= 1.0f);
        return std::sqrt (e / (double) l.size());
    };
    const double level303 = pattern (0.0);
    for (double filter : { 1.0, 2.0 })
    {
        const double dB = 20.0 * std::log10 (pattern (filter) / level303);
        if (std::getenv ("TBMPC_DEBUG"))
            std::printf ("debug filter %.0f level against the 303's: %+.2f dB\n", filter, dB);
        CHECK (std::fabs (dB) < 4.0);
    }
    // Cutoff reads what is heard: the diode ladder's edge with no Resonance is a ninth of its nominal frequency
    // (430.5 Hz at the middle), near its peak from 10 % on; the text follows Filter too, and reads back.
    Instance a;
    char text[64] = {};
    auto cutoffText = [&] () {
        a.params->value_to_text (a.plugin, a.id ("Cutoff"), 0.5, text, sizeof (text));
        return std::atof (text);
    };
    a.setup ({ { "Resonance", 0.0 } });
    const double none = cutoffText();
    double back = -1;
    CHECK (a.params->text_to_value (a.plugin, a.id ("Cutoff"), text, &back) && std::fabs (back - 0.5) < 0.01);
    a.setup ({ { "Resonance", 0.6 } });
    const double some = cutoffText();
    a.setup ({ { "Filter", 2.0 }, { "Resonance", 0.0 } });
    const double three = cutoffText();
    if (std::getenv ("TBMPC_DEBUG"))
        std::printf ("debug cutoff text at the middle: 303 %.0f Hz (Resonance 0), %.0f Hz (60 %%); 18 dB %.0f Hz (0)\n", none, some, three);
    CHECK (none > 45.0 && none < 57.0 && some > 450.0 && some < 520.0 && three > 135.0 && three < 152.0);
    // Moving Resonance asks the host (from its main thread) to read the texts again.
    a.plugin->on_main_thread (a.plugin); // what the host does after the setups above asked
    const int callbacksBefore = callbacks, textBefore = textRescans;
    std::vector<float> l, r;
    a.render (128, 128, l, r, stopped, [&] (uint64_t at, uint32_t, Events& e) { if (at == 0) e.param (0, a.id ("Resonance"), 0.3); });
    CHECK (callbacks > callbacksBefore);
    a.plugin->on_main_thread (a.plugin);
    CHECK (textRescans == textBefore + 1);
}

// Melodee's screen: the page draws, answers the wheel, Push and Minus / Plus, names K1-K3, sends what it changes to
// the host and the engine. Its pictures go to OUT_FOLDER/screen-*.png for a look.
void testScreen (const std::string& folder)
{
    Instance a;
    const auto* screen = static_cast<const melodee_screen_t*> (a.plugin->get_extension (a.plugin, MELODEE_SCREEN_CLAP));
    CHECK (screen != nullptr);
    if (screen == nullptr)
        return;
    std::vector<uint32_t> pixels (MELODEE_SCREEN_WIDTH * MELODEE_SCREEN_PAGE_HEIGHT, 0);
    melodee_screen_frame_t frame {};
    frame.pixels = pixels.data();
    frame.width = MELODEE_SCREEN_WIDTH, frame.height = MELODEE_SCREEN_PAGE_HEIGHT, frame.stride = MELODEE_SCREEN_WIDTH;
    frame.area = MELODEE_SCREEN_PAGE;
    frame.colour = 0x00c8f031u;
    for (int k = 0; k < 3; ++k)
        frame.pots[k] = -1, frame.pots_live[k] = 1;
    uint64_t now = 1000;
    auto paint = [&] (bool whole = false) { frame.redraw_all = whole ? 1u : 0u, frame.time_ms = now; return screen->draw (a.plugin, &frame); };
    auto shot = [&] (const char* name) { paint (true); CHECK (writePng ((folder + "/screen-" + name + ".png").c_str(), pixels.data(), 320, 218, 320, 2)); };
    auto in = [&] (uint32_t kind, int value, bool shift = false) { melodee_screen_input_t e { kind, value, shift ? 1u : 0u }; return screen->input (a.plugin, &e); };
    auto knobs = [&] () { std::vector<uint32_t> ids (3); screen->controls (a.plugin, ids.data()); return ids; };
    double beats = 0.0;
    auto play = [&] (uint32_t frames, bool playing, OutEvents* out = nullptr) {
        std::vector<float> l, r;
        const double from = beats;
        a.render (frames, 128, l, r, [&] (uint64_t at) { return playing ? playingFrom (from, 120.0, 48000.0, at) : Transport { false, 120.0, 0.0 }; }, {}, out);
        if (playing)
            beats += frames / 24000.0;
        now += frames * 1000 / 48000;
        return l;
    };

    CHECK (paint (true));
    CHECK (! paint());
    // A value moved from outside the page (a pot, automation, a preset) asks for a paint.
    int before = paints;
    a.setup ({ { "Cutoff", 0.3 } });
    CHECK (paints > before);
    before = paints;
    CHECK (a.loadPreset ("acid-line"));
    CHECK (paints > before);
    CHECK (a.loadPreset ("default"));
    // PATTERN: K1-K3 are Cutoff, Resonance, Env Mod. In Seed Mode Random, Push (Regen) brings a new seed.
    CHECK ((knobs() == std::vector<clap_id> { a.id ("Cutoff"), a.id ("Resonance"), a.id ("Env Mod") }));
    // It asks the host to run the plug-in; a press the host kept waiting over half a second (an instrument it put to
    // sleep) is dropped, not acted on late.
    a.setup ({ { "Seed Mode", 0.0 } });
    play (128, false);
    const double seedBefore = a.value ("Seed");
    const int processBefore = processRequests;
    CHECK (in (MELODEE_SCREEN_PUSH, 0));
    CHECK (processRequests > processBefore);
    std::this_thread::sleep_for (std::chrono::milliseconds (600));
    play (128, false);
    CHECK (a.value ("Seed") == seedBefore);
    CHECK (in (MELODEE_SCREEN_PUSH, 0));
    play (128, false);
    CHECK (a.value ("Seed") != seedBefore);
    // The wheel dials the seed and locks it; the host hears both after the next block.
    const double seed = a.value ("Seed");
    const int flushesBefore = flushes, rescansBefore = rescans;
    CHECK (in (MELODEE_SCREEN_WHEEL, 3));
    CHECK (flushes > flushesBefore);
    CHECK (rescans > rescansBefore); // the host reads the values again at once, run or not
    CHECK (a.value ("Seed") == std::fmod (seed + 3.0, 65536.0) && a.value ("Seed Mode") == 1.0);
    OutEvents told;
    play (128, false, &told);
    CHECK (std::any_of (told.values.begin(), told.values.end(), [&] (auto& v) { return v.first == a.id ("Seed"); }));
    CHECK (std::any_of (told.values.begin(), told.values.end(), [&] (auto& v) { return v.first == a.id ("Seed Mode") && v.second == 1.0; }));
    // Playing: the page asks to be painted as the steps go by.
    a.setup ({ { "Seed", 0x5a17 }, { "Density", 5 }, { "Scale", 6 } });
    const int paintsBefore = paints;
    play (48000 * 2 + 3000, true);
    CHECK (paints > paintsBefore + 8);
    frame.playing = 1;
    const int paintsAsked = paints;
    CHECK (paint (true));
    CHECK (paints > paintsAsked); // a step lighting: the paint asks for the next one itself
    shot ("pattern");
    frame.shift = 1;
    shot ("pattern-shift");
    frame.shift = 0;
    // VOICE: Plus; K1-K3 follow the cursor; the filter's response beside.
    CHECK (in (MELODEE_SCREEN_STEP, 1));
    CHECK ((knobs() == std::vector<uint32_t> { a.id ("Cutoff"), a.id ("Resonance"), a.id ("Env Mod") }));
    CHECK (in (MELODEE_SCREEN_WHEEL, 5));
    CHECK (knobs()[0] == a.id ("Accent Decay"));
    CHECK (in (MELODEE_SCREEN_WHEEL, 2));
    CHECK ((knobs() == std::vector<uint32_t> { a.id ("Tuning"), a.id ("Volume"), a.id ("Cutoff LFO Amt") }));
    CHECK (in (MELODEE_SCREEN_WHEEL, 2));
    CHECK ((knobs() == std::vector<uint32_t> { a.id ("Cutoff LFO Amt"), a.id ("Cutoff LFO Speed"), MELODEE_SCREEN_NO_CONTROL }));
    shot ("voice");
    // TONE opens on Slide, with 303 Time beside it.
    CHECK (in (MELODEE_SCREEN_STEP, 1));
    CHECK ((knobs() == std::vector<uint32_t> { a.id ("Slide"), a.id ("Slide Time"), a.id ("Filter") }));
    CHECK (in (MELODEE_SCREEN_PUSH, 0));
    CHECK (in (MELODEE_SCREEN_WHEEL, 1));
    CHECK (a.value ("Slide") == a.valueOf ("Slide", "303"));
    CHECK (in (MELODEE_SCREEN_WHEEL, -1));
    CHECK (a.value ("Slide") == a.valueOf ("Slide", "TB-3PO"));
    CHECK (in (MELODEE_SCREEN_PUSH, 0));
    CHECK (in (MELODEE_SCREEN_WHEEL, 2));
    CHECK (in (MELODEE_SCREEN_PUSH, 0));
    CHECK (in (MELODEE_SCREEN_WHEEL, 1));
    CHECK (a.value ("Filter") == a.valueOf ("Filter", "Twin Peak"));
    shot ("tone-twin-peak");
    CHECK (in (MELODEE_SCREEN_WHEEL, 1));
    CHECK (in (MELODEE_SCREEN_PUSH, 0));
    shot ("tone-18db");
    CHECK (in (MELODEE_SCREEN_WHEEL, 3));
    CHECK ((knobs() == std::vector<uint32_t> { a.id ("Pitch LFO Amount"), a.id ("Pitch LFO Speed"), MELODEE_SCREEN_NO_CONTROL }));
    shot ("tone-lfo");
    // TB-3PO: Regen is a button: no pot; Push presses it.
    CHECK (in (MELODEE_SCREEN_STEP, 1));
    CHECK (in (MELODEE_SCREEN_WHEEL, 3));
    CHECK (knobs()[0] == MELODEE_SCREEN_NO_CONTROL);
    shot ("tb-3po");
    CHECK (in (MELODEE_SCREEN_STEP, 1));
    CHECK (! in (MELODEE_SCREEN_STEP, 1)); // the last group
    shot ("clock");
}

void testReviewFixes()
{
    // A state saved by 0.1 - 0.3 (version 1, Cutoff over 5.3 octaves from 130 Hz) loads at the same frequency.
    {
        Instance a;
        std::vector<uint8_t> v1;
        auto put = [&] (const void* d, size_t n) { v1.insert (v1.end(), (const uint8_t*) d, (const uint8_t*) d + n); };
        const uint32_t header[2] = { 1, 2 };
        put ("TB3P", 4), put (header, sizeof (header));
        const struct { uint32_t id; float value; } pairs[2] = { { a.id ("Cutoff"), 0.35f }, { a.id ("Resonance"), 0.6f } };
        put (pairs, sizeof (pairs));
        struct Reader { const std::vector<uint8_t>* v; size_t at; } reader { &v1, 0 };
        clap_istream is { &reader, [] (const clap_istream* s, void* to, uint64_t n) -> int64_t {
            auto* r = static_cast<Reader*> (s->ctx);
            const size_t k = std::min<size_t> ((size_t) n, r->v->size() - r->at);
            std::memcpy (to, r->v->data() + r->at, k);
            r->at += k;
            return (int64_t) k;
        } };
        CHECK (a.state->load (a.plugin, &is));
        const double expected = 0.5 + (std::log2 (130.0 / 430.5) + 5.3 * 0.35) / 7.0; // 469 Hz nominal, as it was
        CHECK (std::fabs (a.value ("Cutoff") - expected) < 1e-4 && std::fabs (a.value ("Cutoff") - 0.518) < 0.002);
        // A state saved now loads as it was saved.
        std::vector<uint8_t> saved;
        clap_ostream os { &saved, [] (const clap_ostream* s, const void* d, uint64_t n) -> int64_t {
            auto* v = static_cast<std::vector<uint8_t>*> (s->ctx);
            v->insert (v->end(), (const uint8_t*) d, (const uint8_t*) d + n);
            return (int64_t) n;
        } };
        CHECK (a.state->save (a.plugin, &os));
        Instance b;
        Reader again { &saved, 0 };
        is.ctx = &again;
        CHECK (b.state->load (b.plugin, &is) && b.value ("Cutoff") == a.value ("Cutoff"));
    }
    // Twin Peak's Cutoff reading follows its lower peak smoothly, whichever peak is taller. (From 5 % of Resonance:
    // under it the knob's square-root taper itself moves the feedback fastest, and every filter's reading with it.)
    {
        double worst = 0.0;
        for (float spread : { 0.5f, 0.75f, 0.9f, 1.5f, 3.0f })
        {
            float last = tbmpc::cutoffEdgeRatio (tbmpc::filterTwinPeak, 0.05f, spread);
            for (int i = 11; i <= 200; ++i)
            {
                const float ratio = tbmpc::cutoffEdgeRatio (tbmpc::filterTwinPeak, (float) i / 200.0f, spread);
                worst = std::max (worst, (double) std::fabs (std::log2 (ratio / last)));
                CHECK (ratio < 1.05f); // at or under the lower peak, never past it into the band
                last = ratio;
            }
        }
        if (std::getenv ("TBMPC_DEBUG"))
            std::printf ("debug twin peak reading: largest jump %.3f octaves a 0.5 %% turn\n", worst);
        CHECK (worst < 0.1);
    }
    // Twin Peak at the top: the band stays open, so an accent (opening the filter further) is still the louder.
    {
        auto stopped = [] (uint64_t) { return Transport { false, 120.0, 0.0 }; };
        auto note = [&] (double velocity) {
            Instance a;
            a.setup ({ { "Mode", 1.0 }, { "Filter", 1.0 }, { "Cutoff", 1.0 }, { "Env Mod", 0.5 }, { "Accent", 1.0 }, { "Resonance", 0.5 } });
            std::vector<float> l, r;
            a.render (2400, 128, l, r, stopped, [&] (uint64_t at, uint32_t, Events& e) { if (at == 0) e.note (0, CLAP_EVENT_NOTE_ON, 36, velocity); });
            double s = 0;
            for (float v : l) s += (double) v * v;
            return std::sqrt (s / (double) l.size());
        };
        const double plain = note (0.6), accented = note (1.0);
        if (std::getenv ("TBMPC_DEBUG"))
            std::printf ("debug twin peak at the top: plain %.4f, accented %.4f\n", plain, accented);
        CHECK (accented > plain);
    }
    // Typed as "60 ms", Slide Time is its default exactly (the 303's glide, to the bit).
    {
        Instance a;
        CHECK (a.valueOf ("Slide Time", "60 ms") == a.value ("Slide Time"));
    }
}

// Notes for Mode: Notes, as Melodee sends them: each at the start of its block, in the order given.
struct Played { uint64_t frame; bool on; int key; double velocity; };
std::vector<float> playNotes (const std::vector<std::pair<std::string, double>>& patch, const std::vector<Played>& notes, uint32_t frames)
{
    Instance a;
    a.setup (patch);
    std::vector<float> l, r;
    a.render (frames, 128, l, r, [] (uint64_t) { return Transport { false, 120.0, 0.0 }; }, [&] (uint64_t at, uint32_t n, Events& e) {
        for (const auto& p : notes)
            if (p.frame >= at && p.frame < at + n)
                e.note ((uint32_t) (p.frame - at), p.on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF, (int16_t) p.key, p.velocity);
    });
    return l;
}

// 0.4.4: a key played again while it is still held. Melodee retriggers a key whose note off is still to come by
// moving that off later: the plug-in hears on, on, then a single off. The second note strikes again, as the key let
// go and played again on that frame would (to the bit): both envelopes start over, at its own velocity, at its pitch
// at once. Before, it was taken for a legato note over itself: no new envelope, so repeats ran together.
void testRepeatedNotes()
{
    auto rms = [] (const std::vector<float>& x, size_t from, size_t to) {
        double s = 0;
        for (size_t i = from; i < to; ++i) s += (double) x[i] * x[i];
        return std::sqrt (s / (double) (to - from));
    };
    auto peak = [] (const std::vector<float>& x, size_t from, size_t to) {
        float m = 0;
        for (size_t i = from; i < to; ++i) m = std::max (m, std::fabs (x[i]));
        return m;
    };
    // The MEG heard: a low cutoff that Env Mod opens far, Decay 630 ms.
    const std::vector<std::pair<std::string, double>> patch { { "Mode", 1.0 }, { "Cutoff", 0.2 }, { "Resonance", 0.3 }, { "Env Mod", 0.9 },
                                                              { "Decay", 0.5 }, { "Accent", 1.0 } };
    const uint64_t again = 128 * 150; // 400 ms on
    for (double velocity : { 0.6, 1.0 }) // the second plain, then accented
    {
        const auto repeated = playNotes (patch, { { 0, true, 48, 0.6 }, { again, true, 48, velocity }, { 2 * again, false, 48, 0 } }, 48000);
        const auto struck = playNotes (patch, { { 0, true, 48, 0.6 }, { again, false, 48, 0 }, { again, true, 48, velocity }, { 2 * again, false, 48, 0 } }, 48000);
        const double before = rms (repeated, again - 2400, again), after = rms (repeated, again + 240, again + 2640);
        if (std::getenv ("TBMPC_DEBUG"))
            std::printf ("debug repeated note (velocity %.1f): rms %.4f before, %.4f after\n", velocity, before, after);
        CHECK (repeated == struck);
        CHECK (after > 1.5 * before);                              // a new attack: both envelopes again
        CHECK (peak (repeated, 2 * again + 4800, 48000) < 1e-4f); // the one note off lets it go
    }
    // A key played again while a glide to it is under way (the key it glided from let go): at its pitch at once,
    // not the rest of the glide.
    {
        const std::vector<std::pair<std::string, double>> clean { { "Mode", 1.0 }, { "Cutoff", 1.0 }, { "Resonance", 0.0 }, { "Env Mod", 0.0 },
                                                                  { "Accent", 0.0 }, { "Slide Time", 1.0 } };
        const std::vector<Played> line { { 0, true, 36, 0.6 }, { 4864, true, 48, 0.6 }, { 7168, false, 36, 0 }, { 9600, true, 48, 0.6 } };
        auto struckLine = line;
        struckLine.insert (struckLine.end() - 1, Played { 9600, false, 48, 0 });
        const auto repeated = playNotes (clean, line, 24000), struck = playNotes (clean, struckLine, 24000);
        const double hz = crossings (repeated, 9600 + 480, 9600 + 480 + 12000) * 48000.0 / 12000.0;
        if (std::getenv ("TBMPC_DEBUG"))
            std::printf ("debug repeated note mid-glide: %.1f Hz (C3 130.8)\n", hz);
        CHECK (repeated == struck);
        CHECK (std::fabs (hz - 130.8) < 3.0);
    }
    // Unchanged: with another key held, a key played again glides as any note over a held one (no new envelope),
    // again as its note off and on would.
    {
        const std::vector<Played> line { { 0, true, 36, 0.6 }, { 4864, true, 48, 0.6 }, { 9600, true, 36, 0.6 } };
        auto struckLine = line;
        struckLine.insert (struckLine.end() - 1, Played { 9600, false, 36, 0 });
        CHECK (playNotes (patch, line, 24000) == playNotes (patch, struckLine, 24000));
    }
}

// 0.4.4: the MEG's decay is the step's own, Decay's or (accented) Accent Decay's, however the step was reached: a
// slide or a legato note from an accented step into a plain one falls at Decay's rate. Before, the decay chosen when
// the gate opened stayed until a knob moved, so a plain step after an accented slide fell at Accent Decay's, and any
// knob turned in it (one with nothing to do with the MEG) switched it to Decay's.
void testAccentDecayAcrossSlides()
{
    auto brightness = [] (const std::vector<float>& x, size_t from, size_t to) {
        double d = 0, e = 0;
        for (size_t i = from + 1; i < to; ++i)
            d += (double) (x[i] - x[i - 1]) * (x[i] - x[i - 1]), e += (double) x[i] * x[i];
        return std::sqrt (d / (e + 1e-30));
    };
    // Decay 2 s, Accent Decay 200 ms: far apart.
    const std::vector<std::pair<std::string, double>> patch { { "Cutoff", 0.5 }, { "Resonance", 0.3 }, { "Env Mod", 0.9 }, { "Decay", 1.0 },
                                                              { "Accent Decay", 0.0 }, { "Accent", 1.0 } };
    // Mode: Notes, 10 ms of an accented note, then a plain one played over it (legato): 300 to 500 ms into the plain
    // note the filter is about where it is after 10 ms of a plain note, the MEG falling at Decay's rate in both (a
    // little lower for its accented start: 0.89 as bright; falling at Accent Decay's, it was half).
    {
        auto notes = patch;
        notes.push_back ({ "Mode", 1.0 });
        auto plainAfter = [&] (double first) {
            const auto l = playNotes (notes, { { 0, true, 36, first }, { 512, true, 43, 0.6 } }, 512 + 24000);
            return brightness (l, 512 + 14400, 512 + 24000);
        };
        const double fromAccent = plainAfter (1.0), fromPlain = plainAfter (0.6);
        if (std::getenv ("TBMPC_DEBUG"))
            std::printf ("debug legato from an accent: brightness %.4f, from a plain note %.4f\n", fromAccent, fromPlain);
        CHECK (fromAccent > 0.75 * fromPlain);
    }
    // TB-3PO: a pattern whose accented step slides into a plain one (found with the port). Knobs that do nothing in
    // the 303 filter (Peak Spread is Twin Peak's) turned all through it leave the sound as it is, to the bit.
    {
        uint16_t seed = 0;
        int slideClock = -1;
        for (uint32_t s = 1; s < 4096 && slideClock < 0; ++s)
        {
            tbmpc::Tb3po t;
            t.start (1);
            t.setScale (tbmpc::appletDefaultScale);
            t.setDensity (5 + 7);
            t.setSteps (16);
            t.setSeed ((uint16_t) s);
            t.setLocked (true);
            t.reset (0);
            int32_t gate = 0;
            for (int k = 0; k < 12 && slideClock < 0; ++k)
            {
                t.clock();
                if (gate == tbmpc::Tb3po::gateAccent && t.gateCv == tbmpc::Tb3po::gateNormal)
                    seed = (uint16_t) s, slideClock = k;
                if (t.opened)
                    t.halfStepPassed();
                gate = t.gateCv;
            }
        }
        CHECK (slideClock >= 0);
        if (std::getenv ("TBMPC_DEBUG"))
            std::printf ("debug accent slide: seed %04x, into a plain step at clock %d\n", seed, slideClock);
        auto pattern = [&] (bool turned) {
            Instance a;
            auto v = patch;
            v.insert (v.end(), { { "Seed Mode", 1.0 }, { "Seed", (double) seed }, { "Density", 5.0 }, { "Steps", 16.0 } });
            a.setup (v);
            std::vector<float> l, r;
            a.render (6000 * 16, 128, l, r, [] (uint64_t at) { return playingFrom (0.0, 120.0, rate48, at); }, [&] (uint64_t at, uint32_t, Events& e) {
                if (turned && at % 640 == 0)
                    a.set (e, "Peak Spread", (at / 640) % 2 ? 3.0 : 1.0);
            });
            return l;
        };
        CHECK (pattern (false) == pattern (true));
    }
}

// 0.5.0: the step pads. In Mode: TB-3PO, notes 36 .. 36 + max (16, Steps) - 1 (hdslaveOS's pads A01 - A16 at its
// default octave and key) each play their step once, as the sequencer plays it, the host playing or not, without
// moving the play head; their note offs do nothing. Other notes transpose as before; Mode: Notes plays every note
// chromatically, as before.
using Patch = std::vector<std::pair<std::string, double>>;
// A note, a choke, or a MIDI message (type CLAP_EVENT_MIDI, key = status << 16 | data1 << 8 | data2), at a frame.
struct Hit { uint64_t frame; uint16_t type; int key; double velocity; };
Hit noteOnAt (uint64_t frame, int key, double velocity = 0.6) { return { frame, CLAP_EVENT_NOTE_ON, key, velocity }; }
Hit noteOffAt (uint64_t frame, int key) { return { frame, CLAP_EVENT_NOTE_OFF, key, 0.0 }; }
Hit chokeAt (uint64_t frame, int key) { return { frame, CLAP_EVENT_NOTE_CHOKE, key, 0.0 }; }
Hit midiAt (uint64_t frame, int status, int data1, int data2) { return { frame, CLAP_EVENT_MIDI, (status << 16) | (data1 << 8) | data2, 0.0 }; }
Transport stoppedTransport (uint64_t) { return { false, 120.0, 0.0 }; }

// The output of a patch (set up in one stopped frame) with these hits, in blocks of `block`.
std::vector<float> playHits (const Patch& patch, const std::vector<Hit>& hits, uint32_t frames,
                             const std::function<Transport (uint64_t)>& transport = stoppedTransport, uint32_t block = 128,
                             OutEvents* out = nullptr)
{
    Instance a;
    a.setup (patch);
    std::vector<float> l, r;
    a.render (frames, block, l, r, transport, [&] (uint64_t at, uint32_t n, Events& e) {
        for (const auto& h : hits)
            if (h.frame >= at && h.frame < at + n)
            {
                const auto t = (uint32_t) (h.frame - at);
                if (h.type == CLAP_EVENT_MIDI)
                    e.midiMessage (t, (uint8_t) (h.key >> 16), (uint8_t) (h.key >> 8), (uint8_t) h.key);
                else
                    e.note (t, h.type, (int16_t) h.key, h.velocity);
            }
    }, out);
    CHECK (l == r);
    return l;
}

// The sequencer as the plug-in holds it after these settings and a Regen (its pattern made from the seed).
tbmpc::Tb3po sequencerOf (uint16_t seed, int density, int scale, int root, int octave, int steps, int transpose = 0)
{
    tbmpc::Tb3po t;
    t.start (1);
    t.setScale (scale);
    t.setRoot (root);
    t.setOctave (octave);
    t.setDensity (density + 7);
    t.setSteps (steps);
    t.setSeed (seed);
    t.setLocked (true);
    t.setTransposeInput (transpose * 128);
    t.reset (0);
    return t;
}

// A pad's step lasts Gate's share of a clock at the tempo, in whole frames, counted as the plug-in counts it.
uint32_t padLength (double gate, double bpm, double stepBeats = 0.25)
{
    return (uint32_t) std::max (1.0, std::floor ((double) (float) gate * stepBeats / (bpm / 60.0 / rate48)));
}

// A periodic signal's frequency in [from, to): from its first rising zero crossing to its last (each placed between
// samples, and armed by a dip below -h), so it is exact to a fraction of a percent over a few cycles.
double frequency (const std::vector<float>& x, size_t from, size_t to, float h = 0.002f)
{
    double first = 0.0, last = 0.0;
    int n = 0;
    bool armed = false;
    for (size_t i = from + 1; i < to && i < x.size(); ++i)
    {
        if (x[i] < -h)
            armed = true;
        if (armed && x[i - 1] <= 0.0f && x[i] > 0.0f)
        {
            const double t = (double) (i - 1) + (double) -x[i - 1] / (double) (x[i] - x[i - 1]);
            if (n == 0)
                first = t;
            last = t, ++n, armed = false;
        }
    }
    return n > 1 ? (n - 1) * rate48 / (last - first) : 0.0;
}

void testToneLfos()
{
    tbmpc::SineLfo sine;
    for (int i = 0; i <= 32; ++i)
    {
        const double beat = (double) i / 32.0;
        CHECK (std::fabs (sine.valueAt (beat) - std::sin (2.0 * M_PI * beat)) < 4.0e-6);
    }
    const float before = sine.valueAt (0.375);
    sine.setRate (3.0f, 0.375);
    CHECK (sine.valueAt (0.375) == before);
    CHECK (std::fabs (sine.valueAt (0.375 + 1.0 / 3.0) - before) < 1.0e-6);
    sine.sync();
    CHECK (sine.valueAt (0.0) == 0.0f);

    auto render = [] (double bpm, double pitchAmount, double cutoffAmount, double speed, uint32_t block,
                      bool playing = true, double cutoffSpeed = 0.0) {
        Instance a;
        a.setup ({ { "Mode", 1 }, { "Filter", 2 }, { "Cutoff", 0.6 }, { "Resonance", 0 }, { "Env Mod", 0 }, { "Accent", 0 },
                   { "Pitch LFO Amount", pitchAmount }, { "Cutoff LFO Amt", cutoffAmount },
                   { "Pitch LFO Speed", speed }, { "Cutoff LFO Speed", cutoffSpeed > 0.0 ? cutoffSpeed : speed } });
        std::vector<float> l, r;
        a.render (48000, block, l, r, [=] (uint64_t at) {
            return playing ? playingFrom (0.0, bpm, rate48, at) : Transport { false, bpm, 0.0 };
        }, [&] (uint64_t at, uint32_t, Events& e) {
            if (at == 0) e.note (0, CLAP_EVENT_NOTE_ON, 72, 0.7);
        });
        CHECK (l == r);
        return l;
    };
    auto energy = [] (const std::vector<float>& x, size_t from, size_t to) {
        double sum = 0.0;
        for (size_t i = from; i < to; ++i)
            sum += (double) x[i] * x[i];
        return sum;
    };
    for (double bpm : { 90.0, 120.0 })
        for (const auto& detent : tbmpc::lfoDetents)
        {
            const double speed = detent.cyclesPerBeat;
            const double period = rate48 * 60.0 / (bpm * speed);
            const size_t high = (size_t) (0.25 * period), low = (size_t) (0.75 * period), span = (size_t) (0.05 * period);
            const auto dry = render (bpm, 0, 0, speed, 128);
            const auto pitch = render (bpm, 1, 0, speed, 128);
            const auto cutoff = render (bpm, 0, 1, speed, 128);
            const double highHz = frequency (pitch, high-span, high+span);
            const double lowHz = frequency (pitch, low-span, low+span);
            CHECK (highHz > 575.0 && lowHz > 445.0 && lowHz < 480.0);
            CHECK (std::fabs (frequency (cutoff, high-span, high+span) - 523.25) < 2.0);
            const double highGain = energy (cutoff, high-span, high+span) / energy (dry, high-span, high+span);
            const double lowGain = energy (cutoff, low-span, low+span) / energy (dry, low-span, low+span);
            CHECK (highGain > lowGain * 2.0);
        }
    // Running by hand while stopped also hears the host tempo. Host block boundaries never reset either sine.
    for (bool playing : { false, true })
    {
        const auto base = render (120, .8, .6, 2.0, 128, playing, 3.0);
        for (uint32_t block : { 1u, 37u, 4096u })
            CHECK (base == render (120, .8, .6, 2.0, block, playing, 3.0));
    }
    CHECK (render (90, 1, 1, 2, 128, false) != render (120, 1, 1, 2, 128, false));
    CHECK (render (120, 1, 1, 2, 128, true, 3) != render (120, 1, 1, 2, 128, true, 1));
}

double hzOf (int32_t cv) { return (double) tbmpc::voiceBaseHz * std::exp2 ((double) cv / 1536.0); }

void testStepPads()
{
    const bool debug = std::getenv ("TBMPC_DEBUG") != nullptr;
    auto peak = [] (const std::vector<float>& x, size_t from, size_t to) {
        float m = 0;
        for (size_t i = from; i < to && i < x.size(); ++i) m = std::max (m, std::fabs (x[i]));
        return m;
    };
    auto rms = [] (const std::vector<float>& x, size_t from, size_t to) {
        double s = 0;
        for (size_t i = from; i < to; ++i) s += (double) x[i] * x[i];
        return std::sqrt (s / (double) (to - from));
    };
    auto plus = [] (Patch p, const Patch& more) { p.insert (p.end(), more.begin(), more.end()); return p; };
    // The sequencer's settings, then Regen: the pattern is made from them now (a seed set while stopped otherwise
    // waits for the next clock; the pads then play the pattern that clock will make: see below).
    auto tb = [&] (const Patch& p) { return plus (p, { { "Regen", 1.0 } }); };
    auto notes = [&] (const Patch& p) { return plus (p, { { "Mode", 1.0 }, { "Regen", 1.0 } }); };
    auto playing = [] (uint64_t at) { return playingFrom (0.0, 120.0, rate48, at); };

    // A pattern with rests, accents and octave jumps among its first 24 steps (found with the port), in Aeolian: whole
    // semitones, so Mode: Notes can play each step's pitch as a key.
    const int density = -2, scale = 6, root = 3, octave = 0;
    uint16_t seed = 0;
    for (uint32_t s = 0x100; s < 0x4000 && seed == 0; ++s)
    {
        const auto t = sequencerOf ((uint16_t) s, density, scale, root, octave, 24);
        int rests = 0, accents = 0, jumps = 0;
        for (int i = 0; i < 24; ++i)
            rests += ! t.gated (i), accents += t.accented (i), jumps += t.octaveUp (i) || t.octaveDown (i);
        if (rests >= 4 && accents >= 3 && jumps >= 2)
            seed = (uint16_t) s;
    }
    CHECK (seed != 0);
    const Patch acid { { "Seed Mode", 1.0 }, { "Seed", seed }, { "Density", density }, { "Scale", scale }, { "Root", root },
                       { "Octave", octave }, { "Cutoff", 0.45 }, { "Resonance", 0.7 }, { "Env Mod", 0.6 }, { "Decay", 0.3 },
                       { "Accent", 0.9 }, { "Gate", 0.5 } };
    const auto port = sequencerOf (seed, density, scale, root, octave, 24);
    auto keyOf = [&] (int step) { return 36 + port.pitchOfStep (step) / 128; };
    const uint32_t len = padLength (0.5, 120.0); // 3,000 frames: half a 1/16 at 120 BPM
    const uint64_t at = 1280;
    if (debug)
        std::printf ("debug step pads: seed %04x, a pad %u frames\n", seed, len);

    // Each pad plays its step, host stopped, as Mode: Notes plays the step's pitch (to the bit): accented as the step
    // is, for Gate's share of a clock. Its note off does nothing. Rests play too. Steps 24: notes 36 - 59 are steps 1 - 24.
    {
        const Patch steps24 = plus (acid, { { "Steps", 24.0 } });
        int rests = 0;
        for (int s = 0; s < 24; ++s)
        {
            CHECK (port.pitchOfStep (s) % 128 == 0);
            const int key = keyOf (s);
            const auto pad = playHits (tb (steps24), { noteOnAt (at, 36 + s), noteOffAt (at + 200, 36 + s) }, 9600);
            const auto ref = playHits (notes (steps24), { noteOnAt (at, key, port.accented (s) ? 1.0 : 0.6), noteOffAt (at + len, key) }, 9600);
            CHECK (pad == ref);
            CHECK (peak (pad, at, at + 2400) > 0.01f && peak (pad, 0, at) == 0.0f);
            rests += ! port.gated (s);
        }
        CHECK (rests >= 4);
        // Steps 8: notes 36 - 51 are still the pads (steps 9 - 16 of the 32 the pattern holds); Steps 16: note 52 is
        // not one (it transposes: silence, stopped).
        const auto port8 = sequencerOf (seed, density, scale, root, octave, 8);
        for (int s : { 3, 9, 15 })
        {
            const Patch steps8 = plus (acid, { { "Steps", 8.0 } });
            const int key = 36 + port8.pitchOfStep (s) / 128;
            CHECK (playHits (tb (steps8), { noteOnAt (at, 36 + s) }, 9600)
                   == playHits (notes (steps8), { noteOnAt (at, key, port8.accented (s) ? 1.0 : 0.6), noteOffAt (at + len, key) }, 9600));
        }
        const auto above = playHits (tb (acid), { noteOnAt (at, 52) }, 9600);
        CHECK (std::all_of (above.begin(), above.end(), [] (float v) { return v == 0.0f; }));
        CHECK (peak (playHits (tb (acid), { noteOnAt (at, 51) }, 9600), at, at + 2400) > 0.01f);
    }

    // A seed, Density and Scale set while stopped wait for the next clock to make the pattern again (the screen shows
    // the one before, PENDING): the pads play the steps that clock will play, the new pattern's.
    {
        const auto made = sequencerOf (seed, density, scale, root, octave, 16);
        int differ = 0;
        for (int s = 0; s < 16; ++s)
        {
            const int key = 36 + made.pitchOfStep (s) / 128;
            const auto pad = playHits (acid, { noteOnAt (at, 36 + s) }, 6400);
            CHECK (pad == playHits (notes (acid), { noteOnAt (at, key, made.accented (s) ? 1.0 : 0.6), noteOffAt (at + len, key) }, 6400));
            differ += pad != playHits (plus (acid, { { "Seed", seed ^ 0x5555 } }), { noteOnAt (at, 36 + s) }, 6400);
        }
        CHECK (differ >= 8);
    }

    // A transposing note on the pad's frame applies to the pad's step, whichever comes first (the pad plays after the
    // frame's events); the pad leaves the transposition as it is, so the next pad is transposed too.
    {
        const auto up = sequencerOf (seed, density, scale, root, octave, 16, 12);
        auto ref = [&] (int s, uint64_t when) {
            const int key = 36 + up.pitchOfStep (s) / 128;
            return std::vector<Hit> { noteOnAt (when, key, up.accented (s) ? 1.0 : 0.6), noteOffAt (when + len, key) };
        };
        auto both = ref (3, at);
        for (const auto& h : ref (7, at + 6400))
            both.push_back (h);
        const auto first = playHits (tb (acid), { noteOnAt (at, 72), noteOnAt (at, 36 + 3), noteOnAt (at + 6400, 36 + 7) }, 12800);
        const auto second = playHits (tb (acid), { noteOnAt (at, 36 + 3), noteOnAt (at, 72), noteOnAt (at + 6400, 36 + 7) }, 12800);
        CHECK (first == second && first == playHits (notes (acid), both, 12800));
    }

    // The pad's velocity: 100/127 or more accents a plain step (as Mode: Notes' accent); 99/127 does not. MIDI note
    // ons (0x90) and offs play the same as CLAP notes.
    {
        int plain = 0;
        while (plain < 16 && port.accented (plain))
            ++plain;
        CHECK (plain < 16);
        const int key = keyOf (plain);
        const auto loud = playHits (tb (acid), { noteOnAt (at, 36 + plain, 100.0 / 127.0) }, 9600);
        const auto soft = playHits (tb (acid), { noteOnAt (at, 36 + plain, 99.0 / 127.0) }, 9600);
        CHECK (loud == playHits (notes (acid), { noteOnAt (at, key, 1.0), noteOffAt (at + len, key) }, 9600));
        CHECK (soft == playHits (notes (acid), { noteOnAt (at, key, 0.6), noteOffAt (at + len, key) }, 9600));
        CHECK (loud != soft);
        CHECK (playHits (tb (acid), { midiAt (at, 0x90, 36 + plain, 76), midiAt (at + 128, 0x80, 36 + plain, 0) }, 9600) == soft);
    }

    // Two pads on one frame: the louder plays, once; on a tie, the later.
    {
        int other = 1;
        while (other < 16 && keyOf (other) == keyOf (0))
            ++other;
        CHECK (other < 16);
        const int a = 36, b = 36 + other;
        const auto onlyA = playHits (tb (acid), { noteOnAt (at, a, 0.9) }, 9600), onlyB = playHits (tb (acid), { noteOnAt (at, b, 0.9) }, 9600);
        CHECK (onlyA != onlyB && peak (onlyA, at, at + 2400) > 0.01f && peak (onlyB, at, at + 2400) > 0.01f);
        CHECK (playHits (tb (acid), { noteOnAt (at, a, 0.5), noteOnAt (at, b, 0.9) }, 9600) == onlyB);
        CHECK (playHits (tb (acid), { noteOnAt (at, a, 0.9), noteOnAt (at, b, 0.5) }, 9600) == onlyA);
        CHECK (playHits (tb (acid), { noteOnAt (at, a, 0.9), noteOnAt (at, b, 0.9) }, 9600) == onlyB);
        // The same pad twice on one frame (Melodee's on, on): once.
        CHECK (playHits (tb (acid), { noteOnAt (at, a, 0.9), noteOnAt (at, a, 0.9) }, 9600) == onlyA);
    }

    // On, on, off of one pad (Melodee retriggering it): the second on strikes again, both envelopes over (as Mode:
    // Notes' key let go and played again on that frame, to the bit); the one off does nothing. The MEG heard: a low
    // cutoff that Env Mod opens far; a quarter-note clock and Gate 95 % for a long step (475 ms).
    {
        const Patch meg = plus (acid, { { "Cutoff", 0.2 }, { "Resonance", 0.3 }, { "Env Mod", 0.9 }, { "Decay", 0.5 }, { "Gate", 0.95 },
                                        { "Clock Div", 1.0 } });
        const uint32_t long95 = padLength (0.95, 120.0, 1.0); // 22,799 frames (0.95 as a float is a hair under)
        const uint64_t again = 128 * 150; // 400 ms on
        int s = 0;
        while (s < 16 && port.accented (s))
            ++s;
        const int key = keyOf (s);
        const auto repeated = playHits (tb (meg), { noteOnAt (0, 36 + s), noteOnAt (again, 36 + s), noteOffAt (2 * again, 36 + s) }, 48000);
        const auto struck = playHits (notes (meg), { noteOnAt (0, key), noteOffAt (again, key), noteOnAt (again, key), noteOffAt (again + long95, key) }, 48000);
        const double before = rms (repeated, again - 2400, again), after = rms (repeated, again + 240, again + 2640);
        if (debug)
            std::printf ("debug pad on, on, off: rms %.4f before, %.4f after\n", before, after);
        CHECK (repeated == struck);
        CHECK (after > 1.5 * before);
        CHECK (repeated == playHits (tb (meg), { noteOnAt (0, 36 + s), noteOnAt (again, 36 + s) }, 48000)); // the off did nothing
        CHECK (peak (repeated, again + long95 - 2400, again + long95) > 1e-4f);
        CHECK (peak (repeated, again + long95 + 2400, 48000) < 1e-4f);         // the step's own end did
    }

    // Chokes, note offs and Melodee's silence (CC 120 and 123 on every channel, a choke of each key it played): a
    // note off or all notes off leaves the pad's step to its end; a choke of its key (or of every key) or All Sound
    // Off stops it there, as a note off stops a key in Mode: Notes.
    {
        const int s = 4, key = keyOf (s);
        const uint64_t cut = 1280;
        const auto whole = playHits (tb (acid), { noteOnAt (0, 36 + s) }, 9600);
        const auto stopped = playHits (notes (acid), { noteOnAt (0, key, port.accented (s) ? 1.0 : 0.6), noteOffAt (cut, key) }, 9600);
        CHECK (whole != stopped && peak (whole, 0, 2400) > 0.01f);
        std::vector<Hit> allOff { noteOnAt (0, 36 + s) }, storm { noteOnAt (0, 36 + s) };
        for (int ch = 0; ch < 16; ++ch)
        {
            allOff.push_back (midiAt (cut, 0xb0 | ch, 123, 0));
            storm.push_back (midiAt (cut, 0xb0 | ch, 120, 0));
            storm.push_back (midiAt (cut, 0xb0 | ch, 123, 0));
        }
        storm.push_back (chokeAt (cut, 36 + s));
        storm.push_back (noteOffAt (cut, 36 + s));
        CHECK (playHits (tb (acid), { noteOnAt (0, 36 + s), noteOffAt (cut, 36 + s) }, 9600) == whole);
        CHECK (playHits (tb (acid), allOff, 9600) == whole);
        CHECK (playHits (tb (acid), { noteOnAt (0, 36 + s), chokeAt (cut, 36 + s + 1) }, 9600) == whole);
        CHECK (playHits (tb (acid), { noteOnAt (0, 36 + s), chokeAt (cut, 36 + s) }, 9600) == stopped);
        CHECK (playHits (tb (acid), { noteOnAt (0, 36 + s), chokeAt (cut, -1) }, 9600) == stopped);
        CHECK (playHits (tb (acid), { noteOnAt (0, 36 + s), midiAt (cut, 0xb0, 120, 0) }, 9600) == stopped);
        CHECK (playHits (tb (acid), storm, 9600) == stopped);
        // A pad and a choke of it on one frame: nothing. While the sequencer plays and no pad sounds, an All Notes
        // Off storm alone changes nothing, to the bit; the full silence (All Sound Off among it) shuts the
        // sequencer's sounding gate until its next clock (0.5.2: it used to ring on), then the sequence goes on.
        CHECK (peak (playHits (tb (acid), { noteOnAt (cut, 36 + s), chokeAt (cut, 36 + s) }, 9600), 0, 9600) == 0.0f);
        std::vector<Hit> playingStorm, playingAllOff;
        for (const auto& h : storm)
            if (h.type != CLAP_EVENT_NOTE_ON)
                playingStorm.push_back ({ h.frame + 96000, h.type, h.key, h.velocity });
        for (const auto& h : allOff)
            if (h.type != CLAP_EVENT_NOTE_ON)
                playingAllOff.push_back ({ h.frame + 96000, h.type, h.key, h.velocity });
        CHECK (playHits (tb (acid), playingAllOff, 48000 * 3, playing) == playHits (tb (acid), {}, 48000 * 3, playing));
        const auto silenced = playHits (tb (acid), playingStorm, 48000 * 3, playing);
        CHECK (peak (silenced, 96000 + cut + 2400, 102000 - 600) < 1e-4f); // shut until the next clock (frame 102000)
        CHECK (peak (silenced, 102000, 48000 * 3) > 0.01f);                // and playing again after it
    }

    // While the host plays: a pattern (Semitone, a clean saw two octaves up) whose first three clocks open no gate.
    // Pads hit there sound as Mode: Notes' keys do with the sequencer unheard, to the bit, and so do they when the
    // host stops while one sounds (it plays to its end, as a key held by hand does) and once it has stopped.
    const Patch clean { { "Seed Mode", 1.0 }, { "Scale", 0.0 }, { "Density", -2.0 }, { "Octave", 2.0 }, { "Cutoff", 1.0 }, { "Resonance", 0.0 },
                        { "Env Mod", 0.0 }, { "Accent", 0.5 }, { "Volume", 1.0 }, { "Gate", 0.4 } };
    uint16_t quiet = 0;
    for (uint32_t s = 0x100; s < 0x4000 && quiet == 0; ++s)
    {
        auto t = sequencerOf ((uint16_t) s, -2, 0, 0, 2, 16);
        bool silent = true;
        for (int k = 0; k < 3; ++k)
            t.clock(), silent &= ! t.opened;
        if (silent)
            quiet = (uint16_t) s;
    }
    CHECK (quiet != 0);
    {
        const Patch p = plus (clean, { { "Seed", quiet } });
        const auto q = sequencerOf (quiet, -2, 0, 0, 2, 16);
        const uint32_t len40 = padLength (0.4, 120.0);
        auto keyAt = [&] (int step) { return 36 + q.pitchOfStep (step) / 128; };
        const auto pads = playHits (tb (p), { noteOnAt (1280, 36 + 3), noteOnAt (7680, 36 + 9, 1.0) }, 18000, playing);
        const auto ref = playHits (notes (p), { noteOnAt (1280, keyAt (3), q.accented (3) ? 1.0 : 0.6), noteOffAt (1280 + len40, keyAt (3)),
                                                noteOnAt (7680, keyAt (9), 1.0), noteOffAt (7680 + len40, keyAt (9)) }, 18000, playing);
        CHECK (pads == ref);
        CHECK (peak (pads, 1280, 1280 + 2000) > 0.05f && peak (pads, 7680, 7680 + 2000) > 0.05f);
        auto stopsAt = [] (uint64_t at) { return at < 8960 ? playingFrom (0.0, 120.0, rate48, at) : Transport { false, 120.0, 0.0 }; };
        const auto stopping = playHits (tb (p), { noteOnAt (7680, 36 + 9), noteOnAt (14080, 36 + 1) }, 24000, stopsAt);
        const auto stoppingRef = playHits (notes (p), { noteOnAt (7680, keyAt (9), q.accented (9) ? 1.0 : 0.6), noteOffAt (7680 + len40, keyAt (9)),
                                                        noteOnAt (14080, keyAt (1), q.accented (1) ? 1.0 : 0.6), noteOffAt (14080 + len40, keyAt (1)) },
                                           24000, stopsAt);
        CHECK (stopping == stoppingRef);
        CHECK (peak (stopping, 8960, 7680 + len40 - 100) > 0.05f && peak (stopping, 14080, 14080 + 2000) > 0.05f);
    }

    // The play head: the same pattern played for 6 s with a transposing note (D4: +2) at the top, and pads hit where
    // the pattern rests (two clocks without a gate). Each pad sounds its step's pitch, transposed (to 0.5 %), and is
    // gone before the next clock; every sequenced note starts where it does without the pads, at the same pitch: the
    // pads moved neither the play head nor the transposition. The pads report nothing to the host (no seed changed).
    {
        const Patch p = plus (clean, { { "Seed", quiet }, { "Gate", 0.5 }, { "Accent", 0.0 } }); // no accent sweep to bend the pitch read
        auto q = sequencerOf (quiet, -2, 0, 0, 2, 16, 2);
        const auto pitches = q; // the pattern's pitches with the transposition (the pads play these)
        std::vector<Hit> hits { noteOnAt (0, 62, 0.8) };
        std::vector<std::pair<uint64_t, int>> padAt; // frame, step
        bool restBefore = true;
        for (int k = 0; k < 48; ++k)
        {
            q.clock();
            const bool rest = ! q.opened;
            if (k >= 3 && rest && restBefore && padAt.size() < 8)
            {
                const uint64_t f = ((uint64_t) k * 6000 + 128) / 128 * 128 + 128; // a block's start, early in the step
                const int step = (k * 5 + 3) % 16;
                padAt.push_back ({ f, step });
                hits.push_back (noteOnAt (f, 36 + step, 0.6));
            }
            restBefore = rest;
            if (q.opened)
                q.halfStepPassed();
        }
        CHECK (padAt.size() >= 3);
        OutEvents out;
        const auto plain = playHits (tb (p), { noteOnAt (0, 62, 0.8) }, 48000 * 6, playing);
        const auto padded = playHits (tb (p), hits, 48000 * 6, playing, 128, &out);
        CHECK (out.values.empty());
        // Onsets of the signal's envelope (its peak over the last 32 samples): the oscillator's phase differs after a pad,
        // and a note starting near a zero crossing would dip under the onset finder's quiet level in its first samples.
        auto envelope = [] (const std::vector<float>& x) {
            std::vector<float> e (x.size());
            for (size_t i = 0; i < x.size(); ++i)
                for (size_t j = i >= 31 ? i - 31 : 0; j <= i; ++j)
                    e[i] = std::max (e[i], std::fabs (x[j]));
            return e;
        };
        const auto a = onsets (envelope (plain)), b = onsets (envelope (padded));
        if (debug)
            std::printf ("debug pads while playing: %zu pads, %zu notes plain, %zu with the pads\n", padAt.size(), a.size(), b.size());
        CHECK (a.size() > 10 && b.size() == a.size() + padAt.size());
        for (const auto& [f, step] : padAt)
        {
            const double want = hzOf (pitches.pitchOfStep (step)), got = frequency (padded, f + 240, f + 2760);
            if (debug)
                std::printf ("debug pad at %llu, step %d: %.2f Hz, want %.2f\n", (unsigned long long) f, step + 1, got, want);
            CHECK (std::fabs (got / want - 1.0) < 0.005);
            CHECK (std::any_of (b.begin(), b.end(), [f = f] (long o) { return o >= (long) f && o < (long) f + 48; }));
            CHECK (peak (padded, f + 3000 + 2400, (f / 6000 + 1) * 6000) < 1e-4f && peak (plain, f, (f / 6000 + 1) * 6000) == 0.0f);
        }
        int compared = 0;
        for (long o : a)
        {
            const bool found = std::any_of (b.begin(), b.end(), [o] (long x) { return std::labs (x - o) <= 48; });
            CHECK (found);
            if (o + 2640 >= (long) plain.size())
                continue;
            const double x = frequency (plain, (size_t) o + 240, (size_t) o + 2640), y = frequency (padded, (size_t) o + 240, (size_t) o + 2640);
            CHECK (x > 0.0 && std::fabs (y / x - 1.0) < 0.005);
            ++compared;
        }
        CHECK (compared > 10);
    }

    // Play's frame and a clock's frame: a pad hit there gives way to the step the clock brings (the sequence plays as
    // without it, to the bit). Notes outside the pads transpose as they did, to the bit as the Transpose knob: with
    // Steps 16, 52 and 35; with Steps 24, 62 (60 is no transposition, and 59 a pad).
    {
        auto fromStop = [] (uint64_t at) { return at < 2560 ? Transport { false, 120.0, 0.0 } : playingFrom (0.0, 120.0, rate48, at - 2560); };
        const auto none = playHits (tb (acid), {}, 2560 + 120000, fromStop);
        CHECK (playHits (tb (acid), { noteOnAt (2560, 40), noteOnAt (2560 + 96000, 45, 1.0) }, 2560 + 120000, fromStop) == none);
        CHECK (peak (none, 2560, 2560 + 96000) > 0.05f);
        auto transposed = [&] (Patch p, int key, int knob) {
            const auto byNote = playHits (tb (p), { noteOnAt (0, key, 0.8) }, 48000 * 2, playing);
            const auto byKnob = playHits (tb (plus (p, { { "Transpose", (double) knob } })), {}, 48000 * 2, playing);
            return byNote == byKnob;
        };
        CHECK (transposed (acid, 52, -8));
        CHECK (transposed (acid, 35, -25));
        CHECK (transposed (acid, 72, 12));
        const Patch steps24 = plus (acid, { { "Steps", 24.0 } });
        CHECK (transposed (steps24, 62, 2));
        CHECK (transposed (steps24, 60, 0));
        CHECK (transposed (steps24, 59, 0)); // a pad on Play's frame: not heard, no transposition
    }

    // Mode: Notes: the same notes are a keyboard, chromatic from C2 (36) at 0 V, held until let go: E2 and D#3, not the
    // steps' pitches. Switching to TB-3PO stops the key sounding; there the pads play steps again (to the bit as if it
    // had been TB-3PO all along); switching back stops the pad's step.
    {
        const auto keys = playHits (notes (clean), { noteOnAt (0, 40), noteOffAt (24000, 40), noteOnAt (30000, 51), noteOffAt (60000, 51) }, 64000);
        const double e2 = frequency (keys, 2400, 22000), ds3 = frequency (keys, 32400, 58000);
        if (debug)
            std::printf ("debug Mode: Notes keys: %.2f Hz (E2 82.41), %.2f Hz (D#3 155.56)\n", e2, ds3);
        CHECK (std::fabs (e2 / 82.407 - 1.0) < 0.003 && std::fabs (ds3 / 155.563 - 1.0) < 0.003);
        CHECK (peak (keys, 24000 + 2400, 30000) < 1e-4f && peak (keys, 60000 + 2400, 64000) < 1e-4f);
        CHECK (peak (keys, 20000, 23900) > 0.05f); // held past where a pad's step would end
        Instance a;
        a.setup (notes (acid));
        std::vector<float> l, r, l2, r2;
        a.render (12800, 128, l, r, stoppedTransport, [&] (uint64_t at, uint32_t, Events& e) {
            if (at == 1280) a.set (e, "Mode", 0.0);
            if (at == 2560) e.note (0, CLAP_EVENT_NOTE_ON, 36 + 4, 0.6);
        });
        CHECK (l == playHits (tb (acid), { noteOnAt (2560, 36 + 4) }, 12800));
        Instance b;
        b.setup (tb (acid));
        b.render (12800, 128, l2, r2, stoppedTransport, [&] (uint64_t at, uint32_t, Events& e) {
            if (at == 0) e.note (0, CLAP_EVENT_NOTE_ON, 36 + 4, 0.6);
            if (at == 1280) b.set (e, "Mode", 1.0);
            if (at == 2560) e.note (0, CLAP_EVENT_NOTE_ON, 40, 0.6);
        });
        const int key = keyOf (4);
        CHECK (l2 == playHits (notes (acid), { noteOnAt (0, key, port.accented (4) ? 1.0 : 0.6), noteOffAt (1280, key), noteOnAt (2560, 40) }, 12800));
    }

    // The same output for any block size (128, 37, 1, 4096 frames), with pads inside blocks (two on one frame, on,
    // on, off, a choke, MIDI ones and All Sound Off, a pad past 16 with Steps 24), a transposing note, playing and stopped.
    for (bool host : { true, false })
    {
        const std::vector<Hit> hits { noteOnAt (20011, 40, 0.7), noteOnAt (30001, 62, 0.8), noteOnAt (40003, 45, 1.0), noteOnAt (50021, 36, 0.5),
                                      noteOnAt (50021, 38, 0.9), noteOnAt (61007, 59), noteOffAt (61500, 59), noteOnAt (70001, 41), noteOnAt (71001, 41),
                                      noteOffAt (72001, 41), noteOnAt (80001, 44), chokeAt (81013, 44), midiAt (90001, 0x90, 47, 90), midiAt (91001, 0xb0, 120, 0),
                                      midiAt (100003, 0x90, 50, 127), midiAt (100003, 0x80, 50, 0), noteOnAt (110007, 53) };
        const Patch p = tb (plus (acid, { { "Steps", 24.0 } }));
        auto t = [host] (uint64_t at) { return host ? playingFrom (0.0, 133.0, rate48, at) : Transport { false, 133.0, 0.0 }; };
        const auto at128 = playHits (p, hits, 48000 * 3, t, 128);
        CHECK (peak (at128, 20011, 20011 + 1000) > 0.01f);
        for (uint32_t block : { 37u, 1u, 4096u })
            CHECK (playHits (p, hits, 48000 * 3, t, block) == at128);
    }
}

// 0.5.1: the host's tempo is held to 1 - 1000 BPM, and the sequencer's gate timer to 4e9 frames, as a pad's already
// was. In 0.5.0 a tempo of 0.0001 BPM overflowed the gate timer (an eighth at Gate 50 %: 7.2e9 frames, cast to 32
// bits), a pad's step lasted 4e9 frames, and an infinite tempo made every clock due at once, forever.
void testTempoLimits()
{
    const double inf = std::numeric_limits<double>::infinity();
    auto peak = [] (const std::vector<float>& x, size_t from, size_t to) {
        float m = 0;
        for (size_t i = from; i < to && i < x.size(); ++i) m = std::max (m, std::fabs (x[i]));
        return m;
    };
    auto lastSound = [] (const std::vector<float>& x) { // the last frame not exactly 0 (-1: none)
        long i = (long) x.size() - 1;
        while (i >= 0 && x[(size_t) i] == 0.0f) --i;
        return i;
    };
    auto bounded = [] (const std::vector<float>& x) {
        bool ok = true;
        for (float s : x) ok &= std::isfinite (s) && std::fabs (s) <= 1.0f;
        return ok;
    };
    // The host playing at `bpm`, its place moving at `placeBpm` from the top.
    auto told = [] (double bpm, double placeBpm) {
        return [bpm, placeBpm] (uint64_t at) { return Transport { true, bpm, (double) at * placeBpm / 60.0 / rate48 }; };
    };
    auto stoppedAt = [] (double bpm) { return [bpm] (uint64_t) { return Transport { false, bpm, 0.0 }; }; };
    Patch p = timingPatch (0x303);
    p.push_back ({ "Density", -7.0 });
    p.push_back ({ "Gate", 0.5 });

    // The sequencer at 0.0001 BPM plays as at 1 BPM, to the bit: Play's step (gated, not sliding) sounds for Gate's
    // share of a 1-BPM eighth (720,000 frames), then its gate shuts (the next clock is 1,440,000 frames on at 1 BPM).
    {
        const auto t = sequencerOf (0x303, -7, tbmpc::appletDefaultScale, 0, 0, 16);
        CHECK (t.gated (1) && ! t.slid (1));
        Patch eighths = p;
        CHECK (std::string (tbmpc::clockDivChoices[3]) == "1/8" && tbmpc::clockDivBeats[3] == 0.5);
        eighths.push_back ({ "Clock Div", 3.0 }); // 1/8
        const uint32_t frames = 720000 + 9600;
        const auto slow = playHits (eighths, {}, frames, told (0.0001, 0.0001)), one = playHits (eighths, {}, frames, told (1.0, 1.0));
        const long shut = (long) padLength (0.5, 1.0, 0.5);
        if (std::getenv ("TBMPC_DEBUG"))
            std::printf ("debug 0.0001 BPM: last sound at %ld (1 BPM: %ld), the gate due to shut at %ld\n", lastSound (slow), lastSound (one), shut);
        CHECK (slow == one);
        CHECK (peak (slow, 0, 4800) > 0.01f && std::labs (lastSound (slow) - shut) <= 2);
    }
    // A pad's step, the host stopped: at 0.0001 BPM as at 1 BPM (Gate's share of a 1-BPM sixteenth: 360,000 frames);
    // at 5000 BPM and an infinite tempo as at 1000 BPM (360 frames, then the VCA's release).
    {
        const std::vector<Hit> pad { noteOnAt (0, 36) };
        const auto slow = playHits (p, pad, 370000, stoppedAt (0.0001)), one = playHits (p, pad, 370000, stoppedAt (1.0));
        CHECK (slow == one && std::labs (lastSound (slow) - (long) padLength (0.5, 1.0)) <= 2);
        const auto fast = playHits (p, pad, 4800, stoppedAt (5000.0)), endless = playHits (p, pad, 4800, stoppedAt (inf));
        const auto top = playHits (p, pad, 4800, stoppedAt (1000.0));
        CHECK (fast == top && endless == top);
        CHECK (peak (top, 0, 300) > 0.01f && lastSound (top) > (long) padLength (0.5, 1000.0) && lastSound (top) < 2400 + (long) padLength (0.5, 1000.0));
    }
    // Playing at 5000 BPM (sixteenths 144 frames apart), or at an infinite tempo with the host's place moving at
    // 120 BPM: the steps come where the host's place puts them and their gates are timed at 1000 BPM, the same to the
    // bit as a host telling 1000 BPM at that place; the output stays a number within full scale.
    {
        const auto fast = playHits (p, {}, 48000, told (5000.0, 5000.0)), fastAt1000 = playHits (p, {}, 48000, told (1000.0, 5000.0));
        const auto endless = playHits (p, {}, 48000, told (inf, 120.0)), endlessAt1000 = playHits (p, {}, 48000, told (1000.0, 120.0));
        CHECK (fast == fastAt1000 && endless == endlessAt1000);
        CHECK (bounded (fast) && bounded (endless) && peak (fast, 0, 48000) > 0.01f && peak (endless, 0, 48000) > 0.01f);
    }
}

// 0.5.2 (master review fixes): panic closes a sequencer gate, a scale change requantizes the held transposition,
// self-set seed events are DONT_RECORD, nan/inf text is refused, a null flush is safe, CC 121 centres bend and the
// wheel, a state loaded over a sounding note settles at once, Play's window no longer depends on the block size,
// and a state from a newer version is refused.
void testMasterFixes()
{
    auto peak = [] (const std::vector<float>& x, size_t from, size_t to) {
        float m = 0;
        for (size_t i = from; i < to && i < x.size(); ++i) m = std::max (m, std::fabs (x[i]));
        return m;
    };
    auto playing = [] (uint64_t at) { return playingFrom (0.0, 120.0, rate48, at); };

    // Panic while the sequencer's gate is open (half notes at Gate 95 %: the gate is still open at frame 10000):
    // CC 120 and a choke of every key each silence it within a block (then the VCA release); the sequence itself
    // goes on at the next clock (frame 48000).
    {
        Patch p = timingPatch (0x303);
        p.push_back ({ "Density", -7.0 });
        p.push_back ({ "Gate", 0.95 });
        p.push_back ({ "Clock Div", 0.0 }); // 1/2
        for (const auto& hit : std::vector<Hit> { midiAt (10000, 0xb0, 120, 0), chokeAt (10000, -1) })
        {
            const auto x = playHits (p, { hit }, 60000, playing);
            CHECK (peak (x, 1000, 9000) > 0.01f);
            CHECK (peak (x, 14000, 47000) < 1e-4f);
            CHECK (peak (x, 49000, 59000) > 0.01f);
        }
    }
    // A scale change requantizes the held transposition: Transpose +1 is a semitone in Semitone and nothing in
    // Pentatonic maj (its nearest notes are 0 and +2, the tie going to 0). A pad played after the change sounds at
    // the pitch a patch set in the new scale from the start gives, not the stale semitone.
    {
        auto padHz = [&] (const Patch& patch, bool changeScale) {
            Instance a;
            a.setup (patch);
            std::vector<float> l, r;
            a.render (9600, 128, l, r, stoppedTransport, [&] (uint64_t at, uint32_t, Events& e) {
                if (changeScale && at == 0) e.param (0, a.id ("Scale"), 10.0); // Pentatonic maj
                if (at == 128 * 10) e.note (0, CLAP_EVENT_NOTE_ON, 36, 0.6);
            });
            return frequency (l, 128 * 10 + 200, 128 * 10 + 5200);
        };
        Patch base { { "Seed Mode", 1.0 }, { "Seed", 0x303 }, { "Density", -7.0 }, { "Transpose", 1.0 },
                     { "Cutoff", 1.0 }, { "Resonance", 0.0 }, { "Env Mod", 0.0 }, { "Accent", 0.0 }, { "Volume", 1.0 },
                     { "Octave", 2.0 }, { "Gate", 0.95 } };
        Patch semitone = base; semitone.push_back ({ "Scale", 0.0 });
        // The fresh reference must land its Scale before its Transpose: setup applies a patch in order, so
        // appending Scale after Transpose (as semitone does) would transpose in the old scale on both sides
        // and the check below could not tell a stale transposition from a requantized one.
        Patch penta = base; penta.insert (penta.begin(), { "Scale", 10.0 });
        Patch plain = base; plain[3].second = 0.0; plain.push_back ({ "Scale", 0.0 }); // Transpose 0
        const double changed = padHz (semitone, true), fresh = padHz (penta, false);
        if (std::getenv ("TBMPC_DEBUG"))
            std::printf ("debug requantize: changed %.2f fresh %.2f semitone %.2f plain %.2f\n", changed, fresh,
                         padHz (semitone, false), padHz (plain, false));
        CHECK (fresh > 10.0 && std::fabs (changed / fresh - 1.0) < 0.01);
        // The transposition itself is real: in Semitone the same pad is a semitone over the untransposed one.
        CHECK (std::fabs (padHz (semitone, false) / padHz (plain, false) - std::exp2 (1.0 / 12.0)) < 0.01);
    }
    // A reset's new seed is told to the host flagged DONT_RECORD (it is the sequencer's own doing, not a gesture).
    {
        Instance a;
        a.setup ({ { "Seed Mode", 0.0 } }); // Random
        OutEvents out;
        std::vector<float> l, r;
        for (int round = 0; round < 8 && out.values.empty(); ++round)
            a.render (128, 128, l, r, stoppedTransport, [&] (uint64_t at, uint32_t, Events& e) {
                if (at == 0) e.param (0, a.id ("Regen"), 1.0);
            }, &out);
        bool seedTold = false, flagged = true;
        for (size_t i = 0; i < out.values.size(); ++i)
            if (out.values[i].first == a.id ("Seed"))
                seedTold = true, flagged &= (out.flags[i] & CLAP_EVENT_DONT_RECORD) != 0;
        CHECK (seedTold && flagged);
    }
    // Text that is not a finite number is refused, not clamped to an end of the range.
    {
        Instance a;
        double v = 0.0;
        for (const char* name : { "Cutoff", "Density", "Scale" })
            for (const char* text : { "nan", "inf", "-inf" })
                CHECK (! a.params->text_to_value (a.plugin, a.id (name), text, &v));
    }
    // CC 121 centres the pitch bend and the mod wheel: bent and wheeled, then reset before the note, the sound is
    // the plain patch's, to the bit.
    {
        auto render = [&] (bool bendAndWheel, bool reset) {
            Instance a;
            a.setup ({ { "Mode", 1.0 }, { "Cutoff", 0.3 }, { "Resonance", 0.2 }, { "Env Mod", 0.0 }, { "Accent", 0.0 } });
            std::vector<float> l, r;
            a.render (9600, 128, l, r, stoppedTransport, [&] (uint64_t at, uint32_t, Events& e) {
                if (at == 0)
                {
                    if (bendAndWheel) e.midiMessage (0, 0xe0, 0x7f, 0x7f), e.midiMessage (0, 0xb0, 1, 127);
                    if (reset) e.midiMessage (0, 0xb0, 121, 0);
                    e.note (0, CLAP_EVENT_NOTE_ON, 48, 0.6);
                }
            });
            return l;
        };
        CHECK (render (true, true) == render (false, false));
        CHECK (render (true, false) != render (false, false));
    }
    // A state loaded over a sounding note settles its smoothers at once: only Volume differs, so from the load's
    // block on the sound is the louder instance's, to the bit (the gain smoother does not ease over ~100 samples).
    {
        auto patch = [] (double volume) {
            return Patch { { "Mode", 1.0 }, { "Volume", volume }, { "Cutoff", 0.3 }, { "Resonance", 0.0 },
                           { "Env Mod", 0.0 }, { "Accent", 0.0 } };
        };
        Instance a, b;
        a.setup (patch (0.2));
        b.setup (patch (0.4));
        std::vector<uint8_t> saved;
        clap_ostream os { &saved, [] (const clap_ostream* s, const void* d, uint64_t n) -> int64_t {
            auto* v = static_cast<std::vector<uint8_t>*> (s->ctx);
            v->insert (v->end(), (const uint8_t*) d, (const uint8_t*) d + n);
            return (int64_t) n;
        } };
        CHECK (b.state->save (b.plugin, &os));
        auto note = [] (uint64_t at, uint32_t, Events& e) { if (at == 0) e.note (0, CLAP_EVENT_NOTE_ON, 48, 0.6); };
        std::vector<float> la, ra, lb, rb;
        a.render (9600, 128, la, ra, stoppedTransport, note);
        b.render (9600, 128, lb, rb, stoppedTransport, note);
        CHECK (la != lb);
        struct Reader { const std::vector<uint8_t>* v; size_t at; } reader { &saved, 0 };
        clap_istream is { &reader, [] (const clap_istream* s, void* to, uint64_t n) -> int64_t {
            auto* r = static_cast<Reader*> (s->ctx);
            const size_t k = std::min<size_t> ((size_t) n, r->v->size() - r->at);
            std::memcpy (to, r->v->data() + r->at, k);
            r->at += k;
            return (int64_t) k;
        } };
        CHECK (a.state->load (a.plugin, &is));
        a.render (9600, 128, la, ra, stoppedTransport);
        b.render (9600, 128, lb, rb, stoppedTransport);
        CHECK (la == lb);
        // The same bytes with a version from the future are refused, and nothing changes.
        std::vector<uint8_t> future = saved;
        future[4] = 99;
        Instance c;
        Reader fr { &future, 0 };
        clap_istream fis { &fr, is.read };
        const double before = c.value ("Volume");
        CHECK (! c.state->load (c.plugin, &fis) && c.value ("Volume") == before);
    }
    // Play landing past a grid line: whether that step still plays is a musical window (128 frames), the same at
    // every block size. 1000 frames past, the step is skipped everywhere; 50 frames past, it plays at once.
    {
        Patch p = timingPatch (0x303);
        p.push_back ({ "Density", -7.0 });
        for (double lateFrames : { 1000.0, 50.0 })
        {
            const double start = (6000.0 + lateFrames) / 24000.0; // beats: a 1/16 at 120 BPM is 6000 frames
            auto t = [start] (uint64_t at) { return Transport { true, 120.0, start + (double) at / 24000.0 }; };
            const auto at128 = playHits (p, {}, 24000, t, 128);
            for (uint32_t block : { 1u, 37u, 4096u })
                CHECK (playHits (p, {}, 24000, t, block) == at128);
            if (lateFrames > 500.0)
                CHECK (peak (at128, 0, 4500) < 1e-4f && peak (at128, 5500, 11000) > 0.01f);
            else
                CHECK (peak (at128, 0, 2000) > 0.01f);
        }
    }
    // A host activating at no rate at all still gets finite, bounded output.
    {
        Instance a (0.0);
        a.setup ({ { "Mode", 1.0 } });
        std::vector<float> l, r;
        a.render (9600, 128, l, r, stoppedTransport, [] (uint64_t at, uint32_t, Events& e) {
            if (at == 0) e.note (0, CLAP_EVENT_NOTE_ON, 48, 0.6);
        });
        bool ok = true;
        for (float s : l) ok &= std::isfinite (s) && std::fabs (s) <= 1.0f;
        CHECK (ok && peak (l, 0, l.size()) > 0.01f);
    }
    // A null event list flushes safely (last: on a build without the guard this is the crash it proves).
    {
        Instance a;
        a.params->flush (a.plugin, nullptr, nullptr);
        CHECK (true);
    }
}

// 0.8.1: two defects a review reproduced.
void testCleanAttackAndShortNotes()
{
    auto peak = [] (const std::vector<float>& x, size_t from, size_t to) {
        float m = 0;
        for (size_t i = from; i < to && i < x.size(); ++i) m = std::max (m, std::fabs (x[i]));
        return m;
    };
    // A note held until its MEG has died away (Decay 0: about 0.6 s), let go, then a new one: the filter starts the
    // new note clean, as after a shorter note. Before, the VCA's closing went straight to the silent fast path, which
    // never cleared the ladder, so the next attack began on the old note's filter state (0.15 off for 7 - 20 ms).
    // The reference holds its first note at Decay 2 s (the MEG still up when let go, so the filter is cleared on the
    // way to silence), with Decay 0 from the last block before the release: the oscillator, the VCA and the knobs run
    // the same in both, the MEG is 0 in both by the second note, and from that note on the two are the same to the bit.
    for (double filter : { 0.0, 1.0, 2.0 })
    {
        const uint64_t release = 48000, again = 48000 + 128 * 260; // 1 s held, then 0.69 s of silence
        auto playWith = [&] (double firstDecay) {
            Instance a;
            a.setup ({ { "Mode", 1.0 }, { "Filter", filter }, { "Cutoff", 0.45 }, { "Resonance", 0.5 }, { "Env Mod", 0.7 },
                       { "Accent", 0.0 }, { "Decay", firstDecay } });
            std::vector<float> l, r;
            a.render ((uint32_t) (again + 12000), 128, l, r, stoppedTransport, [&] (uint64_t at, uint32_t, Events& e) {
                if (at == 0) e.note (0, CLAP_EVENT_NOTE_ON, 36, 0.6);
                if (at == release - 128) a.set (e, "Decay", 0.0);
                if (at == release) e.note (0, CLAP_EVENT_NOTE_OFF, 36, 0.0);
                if (at == again) e.note (0, CLAP_EVENT_NOTE_ON, 36, 0.6);
                if (at == again + 9600) e.note (0, CLAP_EVENT_NOTE_OFF, 36, 0.0);
            });
            return l;
        };
        const auto longNote = playWith (0.0), cleared = playWith (1.0);
        float worst = 0.0f;
        for (size_t i = again; i < longNote.size(); ++i)
            worst = std::max (worst, std::fabs (longNote[i] - cleared[i]));
        if (std::getenv ("TBMPC_DEBUG"))
            std::printf ("debug filter %.0f: the attack after a long note differs from a clean one by %.4f\n", filter, worst);
        CHECK (peak (longNote, (size_t) release - 4800, (size_t) release) > 0.01f); // the first note sounds to the end
        CHECK (peak (longNote, (size_t) again - 4800, (size_t) again) == 0.0f);    // then silence
        CHECK (peak (longNote, (size_t) again, (size_t) again + 4800) > 0.01f);
        CHECK (worst == 0.0f);
    }

    // Mode: Notes, a note on and its note off in one block (hdslaveOS sends a note shorter than its 128-frame block
    // so, both at frame 0): it plays, its gate open for 128 frames, exactly as when its off comes a block later. Before,
    // the gate shut before a sample sounded, and nothing played.
    {
        const Patch p { { "Mode", 1.0 }, { "Cutoff", 0.5 }, { "Resonance", 0.4 }, { "Env Mod", 0.6 }, { "Accent", 0.6 } };
        const uint64_t at = 1280;
        const auto shortNote = playHits (p, { noteOnAt (at, 48), noteOffAt (at, 48) }, 9600);
        const auto blockLong = playHits (p, { noteOnAt (at, 48), noteOffAt (at + 128, 48) }, 9600);
        if (std::getenv ("TBMPC_DEBUG"))
            std::printf ("debug on and off in one block: peak %.4f (a block long: %.4f)\n", peak (shortNote, 0, 9600), peak (blockLong, 0, 9600));
        CHECK (peak (shortNote, at, at + 2400) > 0.01f);
        CHECK (shortNote == blockLong);
        CHECK (peak (shortNote, at + 4800, 9600) < 1e-4f); // and it ends
        // The same at every block size, and for an accented note, and for on, on, off of the key (Melodee's retrigger).
        for (uint32_t block : { 1u, 37u, 4096u })
            CHECK (playHits (p, { noteOnAt (at, 48), noteOffAt (at, 48) }, 9600, stoppedTransport, block) == shortNote);
        CHECK (playHits (p, { noteOnAt (at, 48), noteOnAt (at, 48), noteOffAt (at, 48) }, 9600) == shortNote);
        CHECK (playHits (p, { noteOnAt (at, 48, 1.0), noteOffAt (at, 48) }, 9600)
               == playHits (p, { noteOnAt (at, 48, 1.0), noteOffAt (at + 128, 48) }, 9600));
        // The rule: the note sounding (the newest key held) let go on the frame it was played, struck or glided to,
        // sounds for 128 frames as if its off came a block later; keys let go meanwhile leave it alone, and then the
        // voice goes to the newest key still held, or the gate shuts. The same at every block size.
        auto sameAtEveryBlock = [&] (const std::vector<Hit>& hits, const std::vector<Hit>& blockLater) {
            const auto want = playHits (p, blockLater, 9600);
            for (uint32_t block : { 128u, 1u, 37u, 4096u })
                CHECK (playHits (p, hits, 9600, stoppedTransport, block) == want);
        };
        // Two keys struck and let go on one frame: the newer, sounding, plays its 128 frames.
        sameAtEveryBlock ({ noteOnAt (at, 48), noteOnAt (at, 55), noteOffAt (at, 55), noteOffAt (at, 48) },
                          { noteOnAt (at, 48), noteOnAt (at, 55), noteOffAt (at, 48), noteOffAt (at + 128, 55) });
        // A short note that arrives legato: A held, then B on, A off, B off on one frame (a slide's order, on before
        // off). B glides and is heard for its 128 frames. Before, the gate shut on that frame, and B was never heard.
        sameAtEveryBlock ({ noteOnAt (0, 48), noteOnAt (at, 55), noteOffAt (at, 48), noteOffAt (at, 55) },
                          { noteOnAt (0, 48), noteOnAt (at, 55), noteOffAt (at, 48), noteOffAt (at + 128, 55) });
        // A short note over a key still held: A held, B on and off on one frame, A let go much later. B is heard for
        // its 128 frames, then the voice glides back to A. Before, it glided back at once: the same as A alone.
        sameAtEveryBlock ({ noteOnAt (0, 48), noteOnAt (at, 55), noteOffAt (at, 55), noteOffAt (at + 2000, 48) },
                          { noteOnAt (0, 48), noteOnAt (at, 55), noteOffAt (at + 128, 55), noteOffAt (at + 2000, 48) });
        CHECK (playHits (p, { noteOnAt (0, 48), noteOnAt (at, 55), noteOffAt (at, 55), noteOffAt (at + 2000, 48) }, 9600)
               != playHits (p, { noteOnAt (0, 48), noteOffAt (at + 2000, 48) }, 9600));
        // B on, B off, then A off, on one frame: B is heard for its 128 frames, then the gate shuts.
        sameAtEveryBlock ({ noteOnAt (0, 48), noteOnAt (at, 55), noteOffAt (at, 55), noteOffAt (at, 48) },
                          { noteOnAt (0, 48), noteOnAt (at, 55), noteOffAt (at, 48), noteOffAt (at + 128, 55) });
        // Inside its 128 frames, a choke of B goes back to A at once, as B's off there would.
        CHECK (playHits (p, { noteOnAt (0, 48), noteOnAt (at, 55), noteOffAt (at, 55), chokeAt (at + 64, 55), noteOffAt (at + 2000, 48) }, 9600,
                         stoppedTransport, 1)
               == playHits (p, { noteOnAt (0, 48), noteOnAt (at, 55), noteOffAt (at + 64, 55), noteOffAt (at + 2000, 48) }, 9600, stoppedTransport, 1));
        // Inside those 128 frames, a choke of the key, All Notes Off and All Sound Off shut it at once, as a note off
        // there would; a choke of another key does not; a new note strikes as after a note off (no glide from it).
        const auto offAt64 = playHits (p, { noteOnAt (at, 48), noteOffAt (at + 64, 48) }, 9600, stoppedTransport, 1);
        CHECK (offAt64 != shortNote);
        for (const Hit& cut : { chokeAt (at + 64, 48), chokeAt (at + 64, -1), midiAt (at + 64, 0xb0, 123, 0), midiAt (at + 64, 0xb0, 120, 0) })
            CHECK (playHits (p, { noteOnAt (at, 48), noteOffAt (at, 48), cut }, 9600, stoppedTransport, 1) == offAt64);
        CHECK (playHits (p, { noteOnAt (at, 48), noteOffAt (at, 48), chokeAt (at + 64, 50) }, 9600, stoppedTransport, 1) == shortNote);
        CHECK (playHits (p, { noteOnAt (at, 48), noteOffAt (at, 48), noteOnAt (at + 64, 55) }, 9600, stoppedTransport, 1)
               == playHits (p, { noteOnAt (at, 48), noteOffAt (at + 64, 48), noteOnAt (at + 64, 55) }, 9600, stoppedTransport, 1));
        // Switching Mode in them stops it, as it stops a held note.
        auto switched = [&] (bool shortOne) {
            Instance a;
            a.setup (p);
            std::vector<float> l, r;
            a.render (9600, 1, l, r, stoppedTransport, [&] (uint64_t f, uint32_t, Events& e) {
                if (f == at) e.note (0, CLAP_EVENT_NOTE_ON, 48, 0.6);
                if (f == (shortOne ? at : at + 64)) e.note (0, CLAP_EVENT_NOTE_OFF, 48, 0.0);
                if (f == at + 64) a.set (e, "Mode", 0.0);
            });
            return l;
        };
        CHECK (switched (true) == switched (false));
    }
}

// Run against the shipped ARM object with freesp16's screen ABI and pad pitches.
void testSp16Panel(const std::string& folder)
{
    Instance touch(44100), pad(44100);
    const auto* ui = static_cast<const sixteen_plugin_screen_t*>(touch.plugin->get_extension(touch.plugin, SIXTEEN_EXT_SCREEN));
    CHECK(ui != nullptr);
    if (!ui) return;
    ui->on_open(touch.plugin);
    for (auto* a : {&touch, &pad})
    {
        CHECK(a->loadPreset("acid-line"));
        a->setup({{"Steps", 32}});
    }
    constexpr int stride = 789, h = SIXTEEN_SCREEN_HEIGHT;
    std::vector<uint32_t> pixels(stride*h, 0x12345678);
    sixteen_screen_surface_t surface {pixels.data(), SIXTEEN_SCREEN_WIDTH, h, stride, 0};
    // Opening before the first process callback must show the same score as the first stopped block.
    // A preset recalled after an earlier publication must replace that score immediately too.
    {
        Instance cold;
        ui->on_open(cold.plugin);
        for (const char* preset : {static_cast<const char*>(nullptr), "acid-line", "square-lead"})
        {
            if (preset) CHECK(cold.loadPreset(preset));
            CHECK(ui->render(cold.plugin,&surface,true));
            const auto beforeAudio = pixels;
            cold.setup({});
            ui->render(cold.plugin,&surface,true);
            CHECK(beforeAudio==pixels);
            CHECK(ui->touch(cold.plugin,SIXTEEN_TOUCH_DOWN,85,113));
            CHECK(ui->touch(cold.plugin,SIXTEEN_TOUCH_UP,85,113));
        }
        ui->on_close(cold.plugin);
    }
    // Selection moves the note rim without changing the black face or its chrome glyph.
    {
        Instance keys;
        ui->on_open(keys.plugin);
        for (int steps : {16,32})
        {
            keys.setup({{"Steps",double(steps)}});
            for (int page : {0,2,4,5})
            {
                CHECK(ui->touch(keys.plugin,SIXTEEN_TOUCH_DOWN,339+page*79,264));
                CHECK(ui->render(keys.plugin,&surface,true));
                const bool pattern = page==4;
                const int left = pattern ? 65 : 9, nextLeft = pattern ? 108 : 56;
                const int top = pattern ? 143 : steps==16 ? 202 : 158;
                const int height = pattern ? 33 : steps==16 ? 42 : 29;
                auto select = [&](int x) {
                    CHECK(ui->touch(keys.plugin,SIXTEEN_TOUCH_DOWN,x,top+12));
                    CHECK(ui->touch(keys.plugin,SIXTEEN_TOUCH_UP,x,top+12));
                    CHECK(ui->render(keys.plugin,&surface,false));
                };
                select(left+18);
                const auto firstSelected = pixels;
                select(nextLeft+18);
                for (int x : {left,nextLeft})
                {
                    CHECK(pixels[top*stride+x+12]!=firstSelected[top*stride+x+12]);
                    CHECK(pixels[(top+4)*stride+x+5]==0xff000000u);
                    bool sameFace = true;
                    for (int y=top+3; y<top+height-3; ++y)
                    {
                        const auto line = y*stride+x+4;
                        sameFace &= std::equal(pixels.begin()+line,pixels.begin()+line+34,firstSelected.begin()+line);
                    }
                    CHECK(sameFace);
                }
            }
        }
        ui->on_close(keys.plugin);
    }
    CHECK(ui->render(touch.plugin, &surface, true));
    CHECK(!ui->render(touch.plugin, &surface, false));
    for (int row = 0; row < h; ++row)
    {
        CHECK(pixels[row*stride+779] != 0x12345678);
        CHECK(pixels[row*stride+780] == 0x12345678);
    }
    uint32_t ids[6];
    char label[SIXTEEN_TAB_NAME_SIZE];
    std::vector<clap_id> reachable;
    const char* bankNames[] = {"Perform","Voice","Slide","LFO","Pattern","Clock"};
    const char* bankParams[][6] = {
        {"Cutoff","Resonance","Env Mod","Decay","Accent","Volume"},
        {"Waveform","Tuning","Filter","Peak Spread","Drive","Accent Decay"},
        {"Slide","Slide Time","Gate","Octave","Transpose",nullptr},
        {"Pitch LFO Amount","Pitch LFO Speed","Cutoff LFO Amt","Cutoff LFO Speed",nullptr,nullptr},
        {"Seed Mode","Seed","Mutate","Density","Scale","Root"},
        {"Clock Div","Swing","Reset","Steps","Mode",nullptr}
    };
    auto openBank = [&](int bank) {
        CHECK(ui->touch(touch.plugin, SIXTEEN_TOUCH_DOWN, 339+bank*79, 264));
        CHECK(ui->knobs(touch.plugin, ids, label) && std::string(label)==bankNames[bank]);
        CHECK(ui->render(touch.plugin, &surface, false));
    };
    for (int bank = 0; bank < 6; ++bank)
    {
        for (int source=0; source<6; ++source)
        {
            openBank(source);
            openBank(bank);
        }
        CHECK(writePng((folder+"/thorn-"+bankNames[bank]+".png").c_str(),pixels.data(),780,h,stride,1));
        const double originalFilter=touch.value("Filter"), originalSlide=touch.value("Slide");
        if (bank==1) touch.setup({{"Filter",1}});
        if (bank==2) touch.setup({{"Slide",1}});
        CHECK(ui->knobs(touch.plugin,ids,label));
        for (int knob=0; knob<SIXTEEN_KNOB_COUNT; ++knob)
        {
            const char* name=bankParams[bank][knob];
            const auto id=ids[knob];
            CHECK(id==(name ? touch.id(name) : SIXTEEN_INVALID_PARAM_ID));
            if (!name) continue;
            double v;
            CHECK(touch.params->get_value(touch.plugin, id, &v));
            CHECK(std::find(reachable.begin(),reachable.end(),id)==reachable.end());
            reachable.push_back(id);
        }
        if (bank==1) touch.setup({{"Filter",originalFilter}});
        if (bank==2) touch.setup({{"Slide",originalSlide}});
    }
    for (const auto& item : touch.ids)
        if (item.first != "Regen" && item.first != "Seed Back")
            CHECK(std::find(reachable.begin(), reachable.end(), item.second) != reachable.end());
    openBank(2);
    const double oldSlideTime=touch.value("Slide Time");
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,150,115));
    CHECK(touch.value("Slide")==1);
    ui->touch(touch.plugin,SIXTEEN_TOUCH_UP,150,115);
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,88,115));
    CHECK(touch.value("Slide")==0 && touch.value("Slide Time")==oldSlideTime);
    ui->touch(touch.plugin,SIXTEEN_TOUCH_UP,88,115);
    CHECK(ui->knobs(touch.plugin,ids,label) && ids[1]==SIXTEEN_INVALID_PARAM_ID);
    CHECK(!ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,250,115));
    CHECK(!ui->touch(touch.plugin,SIXTEEN_TOUCH_MOVE,280,115));
    CHECK(touch.value("Slide Time")==oldSlideTime);
    touch.setup({{"Mode",1}});
    CHECK(ui->knobs(touch.plugin,ids,label) && ids[1]==touch.id("Slide Time"));
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,250,115));
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_UP,250,115));
    touch.setup({{"Mode",0}});
    CHECK(ui->knobs(touch.plugin,ids,label) && ids[1]==SIXTEEN_INVALID_PARAM_ID);
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,150,115));
    ui->touch(touch.plugin,SIXTEEN_TOUCH_UP,150,115);
    CHECK(ui->knobs(touch.plugin,ids,label) && ids[1]==touch.id("Slide Time"));
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,250,115));
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_MOVE,280,115));
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_UP,280,115));
    CHECK(touch.value("Slide")==1 && touch.value("Slide Time")>oldSlideTime);
    touch.setup({{"Slide",0},{"Slide Time",oldSlideTime}});
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,370,115));
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_MOVE,390,115));
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_UP,390,115));
    CHECK(touch.value("Gate") > .5);
    touch.setup({{"Gate",.5}});
    openBank(5);
    const auto straightClock = pixels;
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,220,115));
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_MOVE,270,115));
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_UP,270,115));
    CHECK(touch.value("Swing") > .5);
    CHECK(ui->render(touch.plugin,&surface,false));
    CHECK(!std::equal(pixels.begin()+stride*145,pixels.begin()+stride*158,straightClock.begin()+stride*145));
    CHECK(!ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,200,152));
    touch.setup({{"Swing",.5}});
    CHECK(ui->selector_push(touch.plugin));
    CHECK(ui->knobs(touch.plugin, ids, label) && std::string(label) == "Perform");
    CHECK(ui->selector_turn(touch.plugin, 3));
    CHECK(ui->knobs(touch.plugin, ids, label) && std::string(label) == "LFO");
    CHECK(ui->selector_turn(touch.plugin, -3));
    auto stopped = [](uint64_t) { return Transport{false,120,0}; };
    std::vector<float> left, right, expected, expectedR;
    for (const int step : {0, 15, 16, 31})
    {
        CHECK(ui->touch(touch.plugin, SIXTEEN_TOUCH_DOWN, 32+47*(step%16), step < 16 ? 137 : 193));
        touch.render(1024,64,left,right,stopped);
        pad.render(1024,64,expected,expectedR,stopped,[step](uint64_t at,uint32_t,Events& e){
            if (!at) e.note(0, CLAP_EVENT_NOTE_ON, 60+step, 0.72);
        });
        CHECK(std::any_of(left.begin(), left.end(), [](float v){ return std::fabs(v) > 0.001f; }));
        CHECK(left == expected && right == expectedR);
        CHECK(ui->touch(touch.plugin, SIXTEEN_TOUCH_UP,0,0));
    }
    touch.setup({{"Seed Mode",0}});
    auto playing = [](uint64_t at) { return playingFrom(0,120,44100,at); };
    touch.render(8000,64,left,right,playing);
    const int original = int(touch.value("Seed"));
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,745,75));
    touch.render(8000,64,left,right,[](uint64_t at) { return playingFrom(0,120,44100,8000+at); });
    CHECK(int(touch.value("Seed")) != original);
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,30,74));
    touch.render(8000,64,left,right,[](uint64_t at) { return playingFrom(0,120,44100,16000+at); });
    CHECK(int(touch.value("Seed")) == original && touch.value("Seed Mode") == 1.0);
    auto currentSeed = [&] { return int(touch.value("Seed")); };
    for (int n=0; n<3; ++n)
    {
        const int last=currentSeed();
        CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,745,75));
        touch.render(256,64,left,right,stopped);
        CHECK(currentSeed()!=last && touch.value("Seed Mode")==1);
    }
    openBank(4);
    // Check the score itself after successive stopped edits, without clocking or reactivating.
    auto notesRow = [&] {
        std::vector<uint32_t> row;
        for (int y=129; y<160; ++y)
            row.insert(row.end(),pixels.begin()+stride*y+66,pixels.begin()+stride*y+766);
        return row;
    };
    const int rolled=currentSeed();
    const auto rolledNotes=notesRow();
    for (int seed : {0x1234, 0x2345, rolled})
    {
        touch.render(256,64,left,right,stopped,[&](uint64_t at,uint32_t,Events& ev){
            if (!at) touch.set(ev,"Seed",seed);
        });
        ui->render(touch.plugin,&surface,false);
        CHECK(currentSeed()==seed);
        CHECK((notesRow()==rolledNotes)==(seed==rolled));
    }
    openBank(1);
    CHECK(!ui->touch(touch.plugin,SIXTEEN_TOUCH_MOVE,400,188));
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,40,225));
    CHECK(touch.value("Cutoff")==0 && touch.value("Resonance")==0);
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_MOVE,800,-20));
    CHECK(touch.value("Cutoff")==1 && touch.value("Resonance")==1);
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_MOVE,386,180));
    CHECK(std::abs(touch.value("Cutoff")-.5)<.01 && std::abs(touch.value("Resonance")-.5)<.01);
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_UP,800,-20));
    CHECK(!ui->touch(touch.plugin,SIXTEEN_TOUCH_MOVE,44,142));
    // The response moves during an XY sweep while the frequency ruler stays fixed.
    for (int model=0; model<3; ++model)
    {
        touch.setup({{"Filter",model},{"Cutoff",0},{"Resonance",0},{"Peak Spread",1.5}});
        openBank(1);
        const bool twin=model==1;
        CHECK(ui->knobs(touch.plugin,ids,label) && ids[3]==(twin ? touch.id("Peak Spread") : SIXTEEN_INVALID_PARAM_ID));
        // The painted cards and their hit boxes share both edges, leaving the four-pixel gutters clear.
        for (int column=0; column<4; ++column)
        {
            const int x=8+192*column;
            const bool enabled=column!=1 || twin;
            for (int edge : {x,x+187})
            {
                CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,edge,115)==enabled);
                CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_UP,edge,115)==enabled);
            }
            CHECK(!ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,x+188,115));
            CHECK(!ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,x+94,129));
        }
        CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,310,114)==twin);
        CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_MOVE,345,114)==twin);
        CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_UP,345,114)==twin);
        CHECK(twin ? touch.value("Peak Spread")>1.5 : touch.value("Peak Spread")==1.5);
        ui->render(touch.plugin,&surface,false);
        const auto before = pixels;
        CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,40,225));
        for (int n=1; n<=5; ++n)
        {
            CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_MOVE,40+n*138,225-n*18));
            surface.now_ms += n%2 ? 16 : 17;
            CHECK(ui->render(touch.plugin,&surface,false));
            CHECK(!std::equal(pixels.begin()+stride*135,pixels.begin()+stride*226,before.begin()+stride*135));
            CHECK(std::equal(pixels.begin()+stride*229,pixels.begin()+stride*242,before.begin()+stride*229));
            auto redraw=pixels;
            auto checkSurface=surface;
            checkSurface.pixels=redraw.data();
            CHECK(ui->render(touch.plugin,&checkSurface,true));
            CHECK(redraw==pixels);
            CHECK(!ui->render(touch.plugin,&surface,false));
        }
        CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_UP,730,135));
        surface.now_ms+=300;
        CHECK(ui->render(touch.plugin,&surface,false));
        CHECK(!ui->render(touch.plugin,&surface,false));
    }
    // The shared face animates on the LFO page during playback.
    CHECK(touch.loadPreset("root-pulse"));
    touch.setup({});
    openBank(3);
    ui->render(touch.plugin, &surface, true);
    auto facePixels = [&](const std::vector<uint32_t>& data) {
        std::vector<uint32_t> tile;
        for (int row = 0; row < 58; ++row)
            tile.insert(tile.end(), data.begin()+stride*row+717, data.begin()+stride*row+778);
        return tile;
    };
    ui->on_open(pad.plugin);
    auto restingPixels=pixels;
    auto restingSurface=surface;
    restingSurface.pixels=restingPixels.data();
    std::vector<std::vector<uint32_t>> phases;
    for (int poll = 0; poll < 65; ++poll)
    {
        touch.render(882,64,left,right,[poll](uint64_t at) { return playingFrom(0,120,44100,at+poll*882); });
        surface.now_ms += 20;
        ui->render(touch.plugin, &surface, false);
        restingSurface.now_ms=surface.now_ms;
        ui->render(pad.plugin,&restingSurface,true);
        auto frame = facePixels(pixels);
        CHECK(frame == facePixels(restingPixels));
        if (poll % 5 == 0)
        {
            auto redraw = pixels;
            auto checkSurface = surface;
            checkSurface.pixels = redraw.data();
            CHECK(ui->render(touch.plugin,&checkSurface,true));
            CHECK(redraw == pixels);
        }
        if (std::find(phases.begin(), phases.end(), frame) == phases.end())
            phases.push_back(frame);
    }
    CHECK(phases.size() >= 48);
    ui->on_close(pad.plugin);
    touch.setup({});
    ui->render(touch.plugin, &surface, false);
    // At rest the face still sways. At an identical timestamp it is deterministic.
    const auto resting = facePixels(pixels);
    CHECK(ui->render(touch.plugin, &surface, true));
    CHECK(facePixels(pixels) == resting);
    CHECK(!ui->render(touch.plugin, &surface, false));
    CHECK(ui->selector_turn(touch.plugin,1)); // LFO to Pattern
    CHECK(ui->render(touch.plugin,&surface,true));
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,660,68)); // local Play
    touch.render(22050,64,left,right,stopped);
    CHECK(std::any_of(left.begin(),left.end(),[](float v){return std::fabs(v)>.005f;}));
    CHECK(ui->render(touch.plugin,&surface,false));
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,660,68)); // local Stop
    touch.render(1024,64,left,right,stopped);
    CHECK(ui->render(touch.plugin,&surface,false));
    // Direct control drags update the same parameters the encoders expose.
    openBank(0);
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,75,112));
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_MOVE,220,112));
    CHECK(touch.value("Cutoff")==1.0);
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_UP,220,112));
    openBank(3);
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,19,226));
    CHECK(touch.value("Pitch LFO Amount")==0 && touch.value("Pitch LFO Speed")==tbmpc::lfoMinRate);
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_MOVE,377,134));
    CHECK(touch.value("Pitch LFO Amount")==1 && touch.value("Pitch LFO Speed")==tbmpc::lfoMaxRate);
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_UP,377,134));
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,401,226));
    CHECK(touch.value("Cutoff LFO Amt")==0 && touch.value("Cutoff LFO Speed")==tbmpc::lfoMinRate);
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_MOVE,759,134));
    CHECK(touch.value("Cutoff LFO Amt")==1 && touch.value("Cutoff LFO Speed")==tbmpc::lfoMaxRate);
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_UP,759,134));
    openBank(4);
    // A host Play takes over a local preview. The screen becomes a sync
    // indicator, and the host Stop must not restart the preview.
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,660,68));
    touch.render(4096,64,left,right,stopped);
    touch.render(4096,64,left,right,playing);
    ui->render(touch.plugin,&surface,true);
    CHECK(!ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,660,68));
    touch.render(4096,64,left,right,stopped);
    ui->render(touch.plugin,&surface,true);
    touch.setup({{"Steps",32}});
    ui->render(touch.plugin,&surface,true);
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,560,40)); // select steps 17–32
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,748,145));
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_UP,748,145));
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,560,40));
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,172,145));
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_UP,172,145));
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,345,44));
    ui->render(touch.plugin,&surface,true);
    auto crestPixels=[&] {
        std::vector<uint32_t> tile;
        for(int y=6;y<96;++y) tile.insert(tile.end(),pixels.begin()+y*stride+186,pixels.begin()+y*stride+518);
        return tile;
    };
    auto held=crestPixels();surface.now_ms+=800;ui->render(touch.plugin,&surface,false);
    CHECK(held==crestPixels());
    CHECK(ui->touch(touch.plugin,SIXTEEN_TOUCH_DOWN,345,44));
    surface.now_ms+=800;ui->render(touch.plugin,&surface,false);
    CHECK(held!=crestPixels());
    // A silent note edit, painted switches, project recall and an audible header press.
    Instance editor(44100), recalled(44100);
    for (auto* a : {&editor,&recalled})
    {
        ui->on_open(a->plugin);
        a->setup({{"Seed Mode",1},{"Seed",0x4321},{"Steps",32}});
    }
    auto editSurface=surface;
    auto rendered=pixels;
    editSurface.pixels=rendered.data();
    auto repaint=[&](Instance& a) {
        a.render(512,64,left,right,stopped);
        ui->render(a.plugin,&editSurface,true);
    };
    repaint(editor);
    auto press=[&](int x,int y){return ui->touch(editor.plugin,SIXTEEN_TOUCH_DOWN,x,y);};
    auto drag=[&](int x,int y){return ui->touch(editor.plugin,SIXTEEN_TOUCH_MOVE,x,y);};
    auto lift=[&](int x,int y){return ui->touch(editor.plugin,SIXTEEN_TOUCH_UP,x,y);};
    CHECK(press(561,40)); // second bank
    repaint(editor);
    const auto beforeRail = rendered;
    CHECK(press(85,133) && drag(85,105) && lift(85,105)); // pitch trace, step 17, +4 semitones
    repaint(editor);
    CHECK(!std::equal(rendered.begin()+123*stride,rendered.begin()+143*stride,beforeRail.begin()+123*stride));
    for (int y : {178,206,231})
        CHECK(press(85,y) && drag(213,y) && lift(213,y)); // jump across four columns
    repaint(editor);
    CHECK(std::all_of(left.begin(),left.end(),[](float s){ return s==0; }));
    auto writeState=[](Instance& a) {
        std::vector<uint8_t> data;
        clap_ostream out{&data,[](const clap_ostream* stream,const void* src,uint64_t n)->int64_t{
            auto& bytes=*static_cast<std::vector<uint8_t>*>(stream->ctx);
            const auto* ptr=static_cast<const uint8_t*>(src);
            bytes.insert(bytes.end(),ptr,ptr+n);
            return n;
        }};
        CHECK(a.state->save(a.plugin,&out));
        return data;
    };
    auto saved=writeState(editor);
    auto record=[&](int slot){
        float value=-1;
        const size_t pos=12+(tbmpc::paramCount+slot)*8+4;
        if(pos+4<=saved.size()) std::memcpy(&value,saved.data()+pos,4);
        return int(value);
    };
    CHECK(record(0)==0x4321);
    CHECK((record(17)&255)==4 && (record(18)&255)==0);
    for(int i=17;i<=20;++i) CHECK((record(i)&0x2a00)==0x2a00);
    CHECK(record(16)==0 && record(21)==0);
    struct Cursor{const std::vector<uint8_t>& data;size_t pos=0;} cursor{saved};
    clap_istream input{&cursor,[](const clap_istream* stream,void* dst,uint64_t n)->int64_t{
        auto& c=*static_cast<Cursor*>(stream->ctx);
        const size_t count=std::min<size_t>(n,c.data.size()-c.pos);
        std::memcpy(dst,c.data.data()+c.pos,count);
        c.pos+=count;
        return count;
    }};
    // The live sequencer already has the saved seed, generated with a different density.
    recalled.setup({{"Density",-7},{"Seed",0x4320}});
    recalled.setup({{"Seed",0x4321}});
    CHECK(recalled.state->load(recalled.plugin,&input));
    // Read the restored second bank, including its manual overrides, before audio runs again.
    CHECK(ui->render(recalled.plugin,&editSurface,true));
    CHECK(ui->touch(recalled.plugin,SIXTEEN_TOUCH_DOWN,561,40));
    CHECK(ui->render(recalled.plugin,&editSurface,false));
    const auto beforeRecallAudio=rendered;
    repaint(recalled);
    CHECK(rendered==beforeRecallAudio);
    CHECK(writeState(recalled)==saved);
    CHECK(press(85,113) && lift(85,113));
    editor.render(1024,64,left,right,stopped);
    recalled.render(1024,64,expected,expectedR,stopped,[](uint64_t at,uint32_t,Events& e){
        if(!at) e.note(0,CLAP_EVENT_NOTE_ON,76,0.72);
    });
    CHECK(std::any_of(left.begin(),left.end(),[](float s){ return std::abs(s)>.001f; }));
    CHECK(left==expected && right==expectedR);
    for(int row=0;row<h;++row)
        CHECK(pixels[row*stride+780]==0x12345678);
    CHECK(writePng((folder+"/thorn-pattern.png").c_str(),pixels.data(),780,285,stride,1));
    ui->on_close(touch.plugin);
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: host Slink.clap OUT_FOLDER\n");
        return 2;
    }
    void* library = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr)
    {
        std::printf ("FAIL dlopen: %s\n", dlerror());
        return 1;
    }
    entry = static_cast<const clap_plugin_entry*> (dlsym (library, "clap_entry"));
    if (entry == nullptr || ! entry->init (argv[1]))
    {
        std::printf ("FAIL no clap_entry\n");
        return 1;
    }
    factory = static_cast<const clap_plugin_factory*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    if (argc == 4 && std::strcmp(argv[3], "--sp16") == 0)
    {
        freescreenHost = true;
        testDescription();
        testParameters();
        testState();
        testPresets();
        testSp16Panel(argv[2]);
        entry->deinit();
        std::printf("SP-16: %d checks, %d failed\n", checks, failures);
        return failures == 0 ? 0 : 1;
    }
    testDescription();
    testParameters();
    testState();
    testPresets();
    testTiming();
    testStopRestartJump();
    testLoopsAndClockChanges();
    testPositionJitter();
    testJumpEdges();
    testBlockSizes();
    testSeed();
    testTranspose();
    testVoice();
    testSwing();
    testSeedBack();
    testNotes();
    testGateAndMutate();
    testDriveSlideAccentDecay();
    testFilters();
    testScreen (argv[2]);
    testReviewFixes();
    testRepeatedNotes();
    testAccentDecayAcrossSlides();
    testStepPads();
    testToneLfos();
    testTempoLimits();
    testBendAndWheel();
    testMasterFixes();
    testCleanAttackAndShortNotes();
    testExtremes();
    demos (argv[2]);
    entry->deinit();
    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
