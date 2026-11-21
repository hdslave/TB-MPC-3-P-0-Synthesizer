// Loads TB-MPC-3-P-0.clap as a host does and checks it: description and ports, parameters, state, presets, the
// sequencer against the host's transport (each note where the pattern puts it, gates half a step long, stop, restart,
// a jump), the same output whatever the block size, the seed (Random and Locked, Regen, a new seed while playing),
// transposing by MIDI note or knob, the accent, both slides and bounded output at extreme settings. Writes demo
// renders as WAV files into the output folder. tests/reference.cpp checks the sequencer against the applet itself;
// this test uses the same port (src/tb3po.h) to know where the notes must be.
//   host TB-MPC-3-P-0.clap OUT_FOLDER
#include "wav.h"
#include "png.h"
#include "melodee_screen.h"
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
int rescans = 0, textRescans = 0, callbacks = 0, flushes = 0, paints = 0, processRequests = 0, presetsLoaded = 0, presetErrors = 0;
const void* hostExtension (const clap_host*, const char* id)
{
    static const clap_host_log log { [] (const clap_host*, clap_log_severity s, const char* m) { std::printf ("  [plug-in log %d] %s\n", (int) s, m); } };
    static const clap_host_params params { [] (const clap_host*, clap_param_rescan_flags f) {
                                               rescans += (f & CLAP_PARAM_RESCAN_VALUES) != 0;
                                               textRescans += (f & CLAP_PARAM_RESCAN_TEXT) != 0;
                                           },
                                           [] (const clap_host*, clap_id, clap_param_clear_flags) {}, [] (const clap_host*) { ++flushes; } };
    static const clap_host_melodee_screen screen { [] (const clap_host*) { ++paints; } };
    if (std::strcmp (id, CLAP_EXT_MELODEE_SCREEN_HOST) == 0) return &screen;
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
    CLAP_VERSION, nullptr, "TB-MPC-3-P-0 test host", "tests", "", "1",
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
    clap_output_events out { this, [] (const clap_output_events* o, const clap_event_header* h) {
        if (h->space_id == CLAP_CORE_EVENT_SPACE_ID && h->type == CLAP_EVENT_PARAM_VALUE)
        {
            const auto* v = reinterpret_cast<const clap_event_param_value*> (h);
            static_cast<OutEvents*> (o->ctx)->values.push_back ({ v->param_id, v->value });
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
    CHECK (d != nullptr && std::string (d->name) == "hdslave TB-MPC-3-P-0" && std::string (d->id) == "com.hdslave.tb-mpc-3-p-0");
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
    CHECK (count == 30);
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
                              "Drive", "Slide Time", "Accent Decay", "Filter", "Peak Spread" })
        CHECK (a.ids.count (name) == 1);
    // The seed reads as the module shows it, four hex digits; density as -7..+7; the applet's first scale.
    char text[64] = {};
    a.params->value_to_text (a.plugin, a.id ("Seed"), 0xbeef, text, sizeof (text));
    CHECK (std::string (text) == "beef");
    CHECK (a.valueOf ("Seed", "a3f0") == 0xa3f0);
    a.params->value_to_text (a.plugin, a.id ("Density"), -3, text, sizeof (text));
    CHECK (std::string (text) == "-3");
    a.params->value_to_text (a.plugin, a.id ("Scale"), a.value ("Scale"), text, sizeof (text));
    CHECK (std::string (text) == "Gunakri");
    CHECK (a.valueOf ("Scale", "Aeolian") == 6 && a.valueOf ("Scale", "Semitone") == 0);
    CHECK (a.value ("Seed Mode") == a.valueOf ("Seed Mode", "Random") && a.value ("Reset") == a.valueOf ("Reset", "Play"));
}

void testState()
{
    std::vector<float> l1, r1, l2, r2;
    Instance a;
    a.setup ({ { "Seed Mode", 1.0 }, { "Seed", 0x1234 }, { "Cutoff", 0.31 }, { "Resonance", 0.9 }, { "Density", -2 }, { "Scale", 3 },
               { "Waveform", 1.0 }, { "Steps", 11 }, { "Transpose", 4 }, { "Slide", 1.0 } });
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
        CHECK (reported (out, a.id ("Seed")).empty() && a.value ("Seed Mode") == 0.0);
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
    // here and the same sound; the files are rendered from the preset. Default runs on its Random seed: a new
    // pattern every Play.
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
    const auto* screen = static_cast<const clap_plugin_melodee_screen*> (a.plugin->get_extension (a.plugin, CLAP_EXT_MELODEE_SCREEN));
    CHECK (screen != nullptr);
    if (screen == nullptr)
        return;
    std::vector<uint32_t> pixels (MELODEE_SCREEN_WIDTH * MELODEE_SCREEN_HEIGHT, 0);
    melodee_screen_canvas_t canvas {};
    canvas.pixels = pixels.data();
    canvas.width = MELODEE_SCREEN_WIDTH, canvas.height = MELODEE_SCREEN_HEIGHT, canvas.stride = MELODEE_SCREEN_WIDTH;
    canvas.accent = 0xffc8f031;
    for (auto& k : canvas.knobs)
        k = { -1, true };
    uint64_t now = 1000;
    auto paint = [&] (bool whole = false) { canvas.whole = whole, canvas.now_ms = now; return screen->paint (a.plugin, &canvas); };
    auto shot = [&] (const char* name) { paint (true); CHECK (writePng ((folder + "/screen-" + name + ".png").c_str(), pixels.data(), 320, 218, 320, 2)); };
    auto in = [&] (uint32_t kind, int value, bool shift = false) { melodee_screen_input_t e { kind, value, shift }; return screen->input (a.plugin, &e); };
    auto knobs = [&] () { std::vector<clap_id> ids (3); screen->knobs (a.plugin, ids.data()); return ids; };
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
    canvas.playing = true;
    const int paintsAsked = paints;
    CHECK (paint (true));
    CHECK (paints > paintsAsked); // a step lighting: the paint asks for the next one itself
    shot ("pattern");
    canvas.shift = true;
    shot ("pattern-shift");
    canvas.shift = false;
    // VOICE: Plus; K1-K3 follow the cursor; the filter's response beside.
    CHECK (in (MELODEE_SCREEN_NUDGE, 1));
    CHECK ((knobs() == std::vector<clap_id> { a.id ("Tuning"), a.id ("Cutoff"), a.id ("Resonance") }));
    CHECK (in (MELODEE_SCREEN_WHEEL, 1));
    shot ("voice");
    // TONE: Filter edited to Twin Peak a detent on; its response.
    CHECK (in (MELODEE_SCREEN_NUDGE, 1));
    CHECK (in (MELODEE_SCREEN_PUSH, 0));
    CHECK (in (MELODEE_SCREEN_WHEEL, 1));
    CHECK (a.value ("Filter") == a.valueOf ("Filter", "Twin Peak"));
    shot ("tone-twin-peak");
    CHECK (in (MELODEE_SCREEN_WHEEL, 1));
    CHECK (in (MELODEE_SCREEN_PUSH, 0));
    shot ("tone-18db");
    // TB-3PO: Regen is a button: no pot; Push presses it.
    CHECK (in (MELODEE_SCREEN_NUDGE, 1));
    CHECK (in (MELODEE_SCREEN_WHEEL, 3));
    CHECK (knobs()[0] == CLAP_INVALID_ID);
    shot ("tb-3po");
    CHECK (in (MELODEE_SCREEN_NUDGE, 1));
    CHECK (! in (MELODEE_SCREEN_NUDGE, 1)); // the last group
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

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: host TB-MPC-3-P-0.clap OUT_FOLDER\n");
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
    testDescription();
    testParameters();
    testState();
    testPresets();
    testTiming();
    testStopRestartJump();
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
    testBendAndWheel();
    testExtremes();
    demos (argv[2]);
    entry->deinit();
    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
