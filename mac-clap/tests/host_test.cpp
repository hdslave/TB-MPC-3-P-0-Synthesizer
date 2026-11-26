#include <clap/clap.h>
#include "../desktop/panel_presentation.h"
#include <clap/ext/draft/transport-control.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

extern "C"
{
typedef struct hdslave_desktop_debug_api
{
    uint32_t version;
    bool (*ui_open) (const clap_plugin_t* plugin);
    void (*ui_close) (const clap_plugin_t* plugin);
    const uint32_t* (*ui_render) (const clap_plugin_t* plugin, int w, int h, int scale, int* physW, int* physH);
    void (*ui_mouse) (const clap_plugin_t* plugin, int kind, float x, float y, int button, uint32_t mods, float delta, int clicks);
    int (*preset_count) (const clap_plugin_t* plugin);
    bool (*preset_load) (const clap_plugin_t* plugin, int index);
    double (*ms_per_full_render) (const clap_plugin_t* plugin, int w, int h, int scale);
} hdslave_desktop_debug_api_t;
}

#ifdef __APPLE__
bool testCocoaWindow(const clap_plugin_t*,const clap_plugin_gui_t&);
#endif

namespace
{
// Replay exactly the invalidated logical rectangles into a simulated Retina window.
bool testPresentation()
{
    using namespace hdslave_desktop;
    PanelPresentation frame;
    constexpr int w = 98, h = 82, scale = 2;
    const Rect full {0, 0, w / scale, h / scale};
    std::vector<uint32_t> source (w * h, 0xff405060), window (w * h);
    std::vector<Rect> emitted;
    auto present = [&] (Rect candidate) {
        emitted.clear();
        frame.publish (source.data(), w, h, scale, candidate, [&] (Rect r) {
            emitted.push_back (r);
            for (int row = r.y * scale; row < (r.y + r.h) * scale; ++row)
            {
                const int offset = row * w + r.x * scale;
                std::copy_n (source.data() + offset, r.w * scale, window.data() + offset);
            }
        });
        return window == source && frame.pixels == source;
    };
    if (!present ({}) || emitted.size() != 1 || !present (full) || !emitted.empty()) return false;
    source[3 * w + 7] ^= 1;
    source[77 * w + 91] ^= 2;
    if (!present (full) || emitted.size() != 2) return false;
    for (Rect damage : emitted)
        if (damage.contains (w / 4, h / 4)) return false;
    source.back() ^= 4;
    if (!present ({w / scale - 1, h / scale - 1, 10, 10}) || emitted.size() != 1) return false;

    emitted.clear();
    frame.publish (source.data(), h, w, 1, {}, [&] (Rect r) { emitted.push_back (r); });
    return emitted.size() == 1 && emitted.front().w == h && emitted.front().h == w;
}

constexpr uint64_t kHostCanary = 0x4844534C5544494Full;

struct StrictHostCtx
{
    uint64_t canary = kHostCanary;
    const clap_host_t* expectedHost = nullptr;
    int badHostCalls = 0;
    int paramRescans = 0;
    int flushRequests = 0;
    int presetLoadedCalls = 0;
    int stateDirtyCalls = 0;
    int callbackRequests = 0;
    int transportRequests = 0;
    clap_id openGesture = CLAP_INVALID_ID;
    uint32_t lastOutTime = 0;
    int bareParamValues = 0;
    int outParamValues = 0;
    std::set<clap_id> edited;
};

void checkHostPtr (const clap_host_t* h, const char* fn)
{
    if (!h || !h->host_data)
    {
        std::fprintf (stderr, "FAIL: %s called with null host/host_data!\n", fn);
        std::abort();
    }
    auto* ctx = static_cast<StrictHostCtx*> (h->host_data);
    if (ctx->canary != kHostCanary || h != ctx->expectedHost)
    {
        std::fprintf (stderr, "FAIL: %s called with WRONG clap_host pointer (proxy leak — causes FL Studio crash)!\n", fn);
        std::abort();
    }
}

void CLAP_ABI hParamsRescan (const clap_host_t* h, clap_param_rescan_flags)
{
    checkHostPtr (h, "host_params.rescan");
    ++static_cast<StrictHostCtx*> (h->host_data)->paramRescans;
}
void CLAP_ABI hParamsClear (const clap_host_t* h, clap_id, clap_param_clear_flags)
{
    checkHostPtr (h, "host_params.clear");
}
void CLAP_ABI hParamsFlush (const clap_host_t* h)
{
    checkHostPtr (h, "host_params.request_flush");
    ++static_cast<StrictHostCtx*> (h->host_data)->flushRequests;
}
const clap_host_params_t kHostParams { hParamsRescan, hParamsClear, hParamsFlush };

void CLAP_ABI hPresetError (const clap_host_t* h, uint32_t, const char* loc, const char* key, int32_t, const char*)
{
    checkHostPtr (h, "host_preset_load.on_error");
    if (!loc || !key)
    {
        std::fprintf (stderr, "FAIL: host_preset_load.on_error received NULL string!\n");
        std::abort();
    }
}
void CLAP_ABI hPresetLoaded (const clap_host_t* h, uint32_t, const char* loc, const char* key)
{
    checkHostPtr (h, "host_preset_load.loaded");
    if (!loc || !key)
    {
        std::fprintf (stderr, "FAIL: host_preset_load.loaded received NULL location/key (crashes FL Studio)!\n");
        std::abort();
    }
    ++static_cast<StrictHostCtx*> (h->host_data)->presetLoadedCalls;
}
const clap_host_preset_load_t kHostPresetLoad { hPresetError, hPresetLoaded };

void CLAP_ABI hStateDirty (const clap_host_t* h)
{
    checkHostPtr (h, "host_state.mark_dirty");
    ++static_cast<StrictHostCtx*> (h->host_data)->stateDirtyCalls;
}
const clap_host_state_t kHostState { hStateDirty };

const void* CLAP_ABI hostGetExtension (const clap_host_t* h, const char* id)
{
    checkHostPtr (h, "host.get_extension");
    if (!id) return nullptr;
    if (std::strcmp (id, CLAP_EXT_TRANSPORT_CONTROL) == 0)
    {
        static const clap_host_transport_control_t transport = [] {
            clap_host_transport_control_t ext {};
            ext.request_toggle_play = [] (const clap_host_t* host) {
                checkHostPtr (host, "transport.request_toggle_play");
                ++static_cast<StrictHostCtx*> (host->host_data)->transportRequests;
            };
            return ext;
        }();
        return &transport;
    }
    if (std::strcmp (id, CLAP_EXT_PARAMS) == 0) return &kHostParams;
    if (std::strcmp (id, CLAP_EXT_PRESET_LOAD) == 0 || std::strcmp (id, CLAP_EXT_PRESET_LOAD_COMPAT) == 0) return &kHostPresetLoad;
    if (std::strcmp (id, CLAP_EXT_STATE) == 0) return &kHostState;
    return nullptr;
}
void CLAP_ABI hostRequestRestart (const clap_host_t* h) { checkHostPtr (h, "host.request_restart"); }
void CLAP_ABI hostRequestProcess (const clap_host_t* h) { checkHostPtr (h, "host.request_process"); }
void CLAP_ABI hostRequestCallback (const clap_host_t* h)
{
    checkHostPtr (h, "host.request_callback");
    ++static_cast<StrictHostCtx*> (h->host_data)->callbackRequests;
}

uint32_t CLAP_ABI emptyInSize (const clap_input_events_t*) { return 0; }
const clap_event_header_t* CLAP_ABI emptyInGet (const clap_input_events_t*, uint32_t) { return nullptr; }
bool CLAP_ABI strictOutPush (const clap_output_events_t* list, const clap_event_header_t* h)
{
    auto* ctx = static_cast<StrictHostCtx*> (list->ctx);
    if (!ctx || !h) return false;
    if (h->time < ctx->lastOutTime)
    {
        std::fprintf (stderr, "FAIL: out_events non-monotonic time (%u < %u)!\n", h->time, ctx->lastOutTime);
        std::abort();
    }
    ctx->lastOutTime = h->time;
    if (h->space_id == CLAP_CORE_EVENT_SPACE_ID)
    {
        if (h->type == CLAP_EVENT_PARAM_GESTURE_BEGIN)
            ctx->openGesture = reinterpret_cast<const clap_event_param_gesture_t*> (h)->param_id;
        else if (h->type == CLAP_EVENT_PARAM_GESTURE_END)
            ctx->openGesture = CLAP_INVALID_ID;
        else if (h->type == CLAP_EVENT_PARAM_VALUE)
        {
            ++ctx->outParamValues;
            const auto* pv = reinterpret_cast<const clap_event_param_value_t*> (h);
            ctx->edited.insert (pv->param_id);
            if (ctx->openGesture != pv->param_id)
            {
                ++ctx->bareParamValues;
                std::fprintf (stderr, "FAIL: bare CLAP_EVENT_PARAM_VALUE without gesture (id=%u)!\n", pv->param_id);
                std::abort();
            }
        }
    }
    return true;
}

struct MemStream
{
    std::vector<uint8_t> data;
    size_t readPos = 0;

    static int64_t CLAP_ABI write (const clap_ostream_t* s, const void* buf, uint64_t size)
    {
        auto* self = static_cast<MemStream*> (s->ctx);
        const auto* b = static_cast<const uint8_t*> (buf);
        self->data.insert (self->data.end(), b, b + size);
        return int64_t (size);
    }

    static int64_t CLAP_ABI read (const clap_istream_t* s, void* buf, uint64_t size)
    {
        auto* self = static_cast<MemStream*> (s->ctx);
        size_t avail = (self->readPos < self->data.size()) ? (self->data.size() - self->readPos) : 0;
        size_t n = avail < size ? avail : size_t (size);
        if (n > 0)
        {
            std::memcpy (buf, self->data.data() + self->readPos, n);
            self->readPos += n;
        }
        return int64_t (n);
    }
};

void writePpm (const std::string& path, const uint32_t* argb, int w, int h)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (!f) return;
    std::fprintf (f, "P6\n%d %d\n255\n", w, h);
    std::vector<uint8_t> row (size_t (w) * 3);
    for (int y = 0; y < h; ++y)
    {
        const uint32_t* src = argb + size_t (y) * size_t (w);
        for (int x = 0; x < w; ++x)
        {
            const uint32_t p = src[x];
            row[size_t (x) * 3 + 0] = uint8_t ((p >> 16) & 0xFFu);
            row[size_t (x) * 3 + 1] = uint8_t ((p >> 8) & 0xFFu);
            row[size_t (x) * 3 + 2] = uint8_t (p & 0xFFu);
        }
        std::fwrite (row.data(), 1, row.size(), f);
    }
    std::fclose (f);
}

std::string resolveBinaryPath (const std::string& clapPath, std::string& baseOut)
{
    size_t slash = clapPath.find_last_of ("/\\");
    std::string base = (slash == std::string::npos) ? clapPath : clapPath.substr (slash + 1);
    if (base.size() > 5 && base.substr (base.size() - 5) == ".clap")
        base = base.substr (0, base.size() - 5);
    baseOut = base;
#ifdef __APPLE__
    return clapPath + "/Contents/MacOS/Slink";
#else
    return clapPath;
#endif
}

// Exercise the shared 780x285 panel through desktop mouse events at minimum size and Retina scale.
bool testIntegratedPanel (const clap_plugin_t* plug, const hdslave_desktop_debug_api_t& ui,
                          const clap_plugin_params_t& params, const clap_plugin_state_t& state,
                          clap_process_t& block, const std::string& snapDir)
{
    constexpr int left=12, top=74, w=936, h=342, scale=2;
    const auto px = [](int x){ return left+(x+.5f)*w/780; };
    const auto py = [](int y){ return top+(y+.5f)*h/285; };
    int width=0,height=0;
    auto render=[&] { return ui.ui_render(plug,960,540,scale,&width,&height); };
    auto mouse=[&](int kind,float x,float y,uint32_t mods=0,float delta=0,int button=0) {
        ui.ui_mouse(plug,kind,x,y,button,mods,delta,1);
    };
    auto click=[&](float x,float y,uint32_t mods=0) {
        mouse(0,x,y,mods); mouse(2,x,y,mods);
    };
    auto bodyClick=[&](int x,int y,uint32_t mods=0) {
        click(px(x),py(y),mods);
        render();
    };
    auto bodyDrag=[&](int x,int y,int x1,int y1) {
        mouse(0,px(x),py(y)); mouse(1,px(x1),py(y1)); mouse(2,px(x1),py(y1));
    };
    auto& ctx=*static_cast<StrictHostCtx*>(block.out_events->ctx);
    auto process=[&] {
        ctx.lastOutTime=0;
        block.steady_time+=block.frames_count;
        plug->process(plug,&block);
        plug->on_main_thread(plug);
    };
    auto find=[&](const char* name) {
        for(uint32_t i=0;i<params.count(plug);++i)
        {
            clap_param_info_t info {};
            if(params.get_info(plug,i,&info) && !std::strcmp(name,info.name)) return info;
        }
        clap_param_info_t info {};
        info.id=CLAP_INVALID_ID;
        return info;
    };
    auto value=[&](const char* name) {
        double v=-1;
        params.get_value(plug,find(name).id,&v);
        return v;
    };
    auto set=[&](const char* name,double value) {
        clap_event_param_value_t event {};
        event.header={sizeof(event),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_PARAM_VALUE,0};
        event.param_id=find(name).id;
        event.note_id=event.port_index=event.channel=event.key=-1;
        event.value=value;
        clap_input_events_t events {&event.header,[](const clap_input_events_t*) -> uint32_t { return 1; },
            [](const clap_input_events_t* e,uint32_t) { return static_cast<const clap_event_header_t*>(e->ctx); }};
        const auto* before=block.in_events;
        block.in_events=&events;
        process();
        block.in_events=before;
        render();
    };
    auto area=[&](int x0,int y0,int x1,int y1) {
        const auto* data=render();
        std::vector<uint32_t> sample;
        for(int y=int(py(y0)*scale);y<int(py(y1)*scale);y+=3)
            for(int x=int(px(x0)*scale);x<int(px(x1)*scale);x+=3) sample.push_back(data[y*width+x]);
        return sample;
    };
    auto snap=[&](const char* page) {
        if (snapDir.empty()) return;
        int sw=0,sh=0;
        const auto* frame=ui.ui_render(plug,1352,708,1,&sw,&sh);
        writePpm(snapDir+"/Slink-"+page+".ppm",frame,sw,sh);
        render();
    };
    auto check=[](bool ok,const char* name) {
        if (!ok) std::fprintf(stderr,"FAIL: desktop panel: %s\n",name);
        return ok;
    };
    auto page=[&](int i) { bodyClick(338+79*i,268); };
    if (!check(render() && width==1920 && height==1080,"Retina buffer")) return false;
    if (!check(area(190,8,512,90)!=area(190,11,512,93),"artwork bank rendered")) return false;

    constexpr const char* names[]{"perform","voice","glide","lfo","pattern","clock"};
    std::set<std::vector<uint32_t>> pages;
    for(int i=0;i<6;++i)
    {
        page(i);
        pages.insert(area(10,108,764,243));
        snap(names[i]);
    }
    if (!check(pages.size()==6,"all six unique page bodies")) return false;
    page(0);
    const double cutoff=value("Cutoff");
    mouse(0,85,446); mouse(1,85,416); mouse(2,85,416);
    process();
    if (!check(value("Cutoff")>cutoff && ctx.edited.count(find("Cutoff").id),"perform strip control automates")) return false;

    click(705,25); // MIDI Learn
    click(85,446); // Cutoff in the Perform strip
    clap_event_midi_t cc {};
    cc.header={sizeof(cc),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI,0};
    cc.data[0]=0xb0; cc.data[1]=74; cc.data[2]=95;
    clap_input_events_t midi {&cc.header,[](const clap_input_events_t*) -> uint32_t { return 1; },
        [](const clap_input_events_t* e,uint32_t) { return static_cast<const clap_event_header_t*>(e->ctx); }};
    const auto* input=block.in_events;
    block.in_events=&midi;
    process(); // learn
    cc.data[2]=102;
    process(); // control
    block.in_events=input;
    if (!check(std::abs(value("Cutoff")-102.0/127.0)<.0001,"MIDI learn still controls desktop parameters")) return false;

    page(1);
    ctx.edited.clear();
    bodyDrag(390,175,700,153);
    process();
    if (!check(value("Cutoff")>.94 && value("Resonance")>.8 &&
        ctx.edited.count(find("Cutoff").id) && ctx.edited.count(find("Resonance").id),"filter XY automation")) return false;
    page(3);
    ctx.edited.clear();
    bodyDrag(75,207,350,153);
    bodyDrag(480,207,734,148);
    process();
    if (!check(value("Pitch LFO Amount")>.75 && value("Cutoff LFO Amt")>.8 &&
        ctx.edited.count(find("Pitch LFO Speed").id) && ctx.edited.count(find("Cutoff LFO Speed").id),"both LFO XY plots automate")) return false;

    page(4);
    const double initialSeed=value("Seed");
    bodyClick(749,75);
    process();
    if (!check(initialSeed!=value("Seed"),"Regen rolls a locked seed")) return false;
    const double beforeBarcode=value("Seed");
    bodyDrag(100,70,155,70);
    process();
    if (!check(beforeBarcode!=value("Seed"),"seed barcode drag")) return false;
    // Choices remain keyboard/wheel reachable through the desktop six-control strip.
    click(714,452);
    mouse(3,750,180,0,-1000);
    click(714,400);
    process();
    if (!check(value("Scale")==find("Scale").max_value,"last scale reachable from desktop menu")) return false;

    set("Steps",32);
    const auto numbers=[&] { return area(65,104,205,124); };
    const auto first=numbers();
    bodyClick(565,43);
    const auto second=numbers();
    bodyClick(565,43);
    if (!check(second!=first && numbers()==first,"stopped counter switches pattern halves")) return false;
    set("Seed Mode",0);
    const auto pitchBefore=area(68,127,104,164);
    const int marks=ctx.stateDirtyCalls;
    bodyDrag(85,140,85,119); // +3 semitones
    bodyDrag(85,177,219,177); // paint Gate across four columns
    bodyClick(85,205);
    bodyClick(85,232);
    process();
    const auto pitchEdited=area(68,127,104,164);
    if (!check(value("Seed Mode")==1 && ctx.stateDirtyCalls>marks && pitchEdited!=pitchBefore,
               "step edits lock random seed, notify host and repaint pitch")) return false;

    MemStream saved;
    clap_ostream_t out {&saved,MemStream::write};
    if (!check(state.save(plug,&out),"save edited project")) return false;
    auto word=[&](size_t offset) {
        uint32_t v=0;
        if(offset+4<=saved.data.size()) std::memcpy(&v,saved.data.data()+offset,4);
        return v;
    };
    auto record=[&](uint32_t id) -> int {
        for(size_t off=24;off+8<=12+word(8);off+=8)
            if(word(off)==id) { float f; const auto v=word(off+4);std::memcpy(&f,&v,4);return int(f); }
        return -1;
    };
    const int step=record(0x534c0001);
    if (!check(step>=0 && (step&255)==3 && (step&0x2a00)==0x2a00 &&
             (record(0x534c0004)&0x300)==(step&0x300),"pitch, gate paint, accent and slide persist as SP-16 records")) return false;
    if (!ui.preset_load(plug,0)) return false;
    process();
    clap_istream_t in {&saved,MemStream::read};
    if (!state.load(plug,&in)) return false;
    process();
    if (!check(area(68,127,104,164)==pitchEdited,"edited pattern restored after preset load")) return false;
    auto raw=saved;
    raw.readPos=0;
    raw.data=std::vector<uint8_t>(saved.data.begin()+12,saved.data.begin()+12+word(8));
    clap_istream_t rawIn {&raw,MemStream::read};
    if (!check(state.load(plug,&rawIn),"raw SP-16 state import")) return false;
    process();

    auto peak=[&] {
        double peak=0;
        for(uint32_t i=0;i<block.frames_count;++i)
            peak=std::max(peak,std::abs(double(block.audio_outputs[0].data32[0][i])));
        return peak;
    };
    plug->reset(plug);
    process();
    if (!check(peak()==0,"idle is silent before audition")) return false;
    bodyClick(85,110);
    process();
    if (!check(peak()>.0001,"numbered step audibly auditions while stopped")) return false;

    clap_event_transport_t transport {};
    transport.flags=CLAP_TRANSPORT_HAS_TEMPO|CLAP_TRANSPORT_HAS_BEATS_TIMELINE;
    transport.tempo=120;
    block.transport=&transport;
    plug->reset(plug);
    process();
    render();
    const auto stopped=area(625,58,700,82);
    bodyClick(660,67,1);
    if (!check(ctx.transportRequests==1,"Shift-Play requests DAW transport when supported")) return false;
    bodyClick(660,67);
    double previewPeak=0;
    for(int i=0;i<72;++i) { process(); previewPeak=std::max(previewPeak,peak()); }
    const auto preview=area(625,58,700,82);
    if (!check(preview!=stopped && previewPeak>.0001,"local Play sends audio and shows Stop without running host")) return false;
    bodyClick(660,67);
    process();
    if (!check(area(625,58,700,82)==stopped,"local Stop releases preview control")) return false;
    bodyClick(660,67);
    process();
    transport.flags|=CLAP_TRANSPORT_IS_PLAYING;
    transport.song_pos_beats=8ull*CLAP_BEATTIME_FACTOR;
    process();
    const auto synced=area(625,58,700,82);
    if (!check(synced!=preview && synced!=stopped,"DAW start replaces preview and displays Sync")) return false;
    transport.flags&=~CLAP_TRANSPORT_IS_PLAYING;
    process();
    if (!check(area(625,58,700,82)==stopped,"host stop does not resume preview")) return false;
    block.transport=nullptr;

    const auto crest=[&] { return area(186,6,518,96); };
    const auto storm=[&] { return area(127,253,295,278); };
    bodyClick(350,52);
    const auto held=crest(), moving=storm();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    if (!check(crest()==held && storm()!=moving,"crest hold is independent of storm")) return false;
    bodyClick(350,52);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    if (!check(crest()!=held,"crest resumes")) return false;
    click(900,25);
    const auto frozenCrest=crest(), frozenStorm=storm();
    std::this_thread::sleep_for(std::chrono::milliseconds(130));
    if (!check(crest()==frozenCrest && storm()==frozenStorm,"global motion hold")) return false;
    bodyClick(350,52,1);
    if (!check(crest()!=frozenCrest && storm()==frozenStorm,"manual cut while global motion is held")) return false;
    return true;
}

#ifdef _WIN32
bool testWin32Window (const clap_plugin_t* plug, const clap_plugin_gui_t& gui,
                      const clap_plugin_params_t& params, clap_process_t& block)
{
    HWND parent = CreateWindowExW (0, L"STATIC", L"", WS_POPUP, 0, 0, 1352, 708, nullptr, nullptr,
                                   GetModuleHandleW (nullptr), nullptr);
    if (!parent) return false;
    const bool created = gui.create (plug, CLAP_WINDOW_API_WIN32, false);
    clap_window_t window {};
    window.api = CLAP_WINDOW_API_WIN32;
    window.win32 = parent;
    bool ok = created && gui.set_size (plug, 1352, 708) && gui.set_parent (plug, &window) && gui.show (plug);
    HWND child = GetWindow (parent, GW_CHILD);
    RECT bounds {};
    ok = ok && child && GetClientRect (child, &bounds) && bounds.right == 1352 && bounds.bottom == 708;
    ok = ok && gui.set_size (plug, 960, 540) && GetClientRect (child, &bounds)
            && bounds.right == 960 && bounds.bottom == 540;
    if (ok)
    {
        clap_id seed = CLAP_INVALID_ID;
        for (uint32_t i = 0; i < params.count (plug); ++i)
        {
            clap_param_info_t info {};
            if (params.get_info (plug, i, &info) && std::strcmp (info.name, "Seed") == 0) seed = info.id;
        }
        double before = 0, after = 0;
        ok = params.get_value (plug, seed, &before);
        SendMessageW (child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM (910, 164));
        SendMessageW (child, WM_LBUTTONUP, 0, MAKELPARAM (910, 164));
        static_cast<StrictHostCtx*> (block.out_events->ctx)->lastOutTime = 0;
        block.steady_time += block.frames_count;
        plug->process (plug, &block);
        ok = ok && params.get_value (plug, seed, &after) && before != after;
    }
    if (created) gui.destroy (plug);
    DestroyWindow (parent);
    if (!ok) std::fprintf (stderr, "FAIL: Win32 child creation, physical resize or Regen hit test\n");
    return ok;
}
#endif

bool testOnePlugin (const std::string& clapPath, const std::string& snapDir)
{
    std::string baseName;
    const std::string binPath = resolveBinaryPath (clapPath, baseName);

#ifdef _WIN32
    HMODULE lib = LoadLibraryA (binPath.c_str());
    if (!lib)
    {
        std::fprintf (stderr, "FAIL: LoadLibrary(%s) failed\n", binPath.c_str());
        return false;
    }
    const auto* entry = reinterpret_cast<const clap_plugin_entry_t*> (GetProcAddress (lib, "clap_entry"));
#else
    void* lib = dlopen (binPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!lib)
    {
        std::fprintf (stderr, "FAIL: dlopen(%s): %s\n", binPath.c_str(), dlerror());
        return false;
    }
    const auto* entry = reinterpret_cast<const clap_plugin_entry_t*> (dlsym (lib, "clap_entry"));
#endif

    if (!entry || !entry->init (clapPath.c_str()))
    {
        std::fprintf (stderr, "FAIL: entry missing or init failed for %s\n", binPath.c_str());
        return false;
    }

    const auto* factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    if (!factory || factory->get_plugin_count (factory) == 0)
    {
        std::fprintf (stderr, "FAIL: no plugins in factory for %s\n", clapPath.c_str());
        return false;
    }

    const auto* desc = factory->get_plugin_descriptor (factory, 0);
    StrictHostCtx ctx {};
    clap_host_t host { CLAP_VERSION, &ctx, "hdslave-strict-host", "hdslave", "", "1.0.0",
                       hostGetExtension, hostRequestRestart, hostRequestProcess, hostRequestCallback };
    ctx.expectedHost = &host;

    const clap_plugin_t* plug = factory->create_plugin (factory, &host, desc->id);
    if (!plug || !plug->init (plug))
    {
        std::fprintf (stderr, "FAIL: create/init failed for %s\n", desc->id);
        return false;
    }

    // 1. Verify CLAP_EXT_GUI
    const auto* gui = static_cast<const clap_plugin_gui_t*> (plug->get_extension (plug, CLAP_EXT_GUI));
    const char* api = nullptr;
    bool floating = true;
    uint32_t gw = 0, gh = 0;
    if (!gui || !gui->get_preferred_api (plug, &api, &floating) || floating || !api || !gui->get_size (plug, &gw, &gh) || gw < 1000 || gh < 640)
    {
        std::fprintf (stderr, "FAIL: invalid GUI config for %s (%ux%u)\n", desc->id, gw, gh);
        return false;
    }

    // 2. Verify CLAP_EXT_PARAMS
    const auto* params = static_cast<const clap_plugin_params_t*> (plug->get_extension (plug, CLAP_EXT_PARAMS));
    if (!params) return false;
    struct OldChoice { const char* param;const char* text;double value; };
    constexpr OldChoice legacy[] {{"Filter","303",0},{"Slide","303",1},{"Slide","TB-3PO",0},{"Mode","TB-3PO",0}};
    for (const auto& choice : legacy)
    {
        bool parsed=false;
        for (uint32_t i=0;i<params->count(plug);++i)
        {
            clap_param_info_t info {};
            if (!params->get_info(plug,i,&info) || std::strcmp(info.name,choice.param)!=0) continue;
            double v=-1;
            parsed=params->text_to_value(plug,info.id,choice.text,&v) && v==choice.value;
            break;
        }
        if (!parsed) { std::fprintf(stderr,"FAIL: legacy %s choice %s\n",choice.param,choice.text); return false; }
    }
    const uint32_t pCount = params->count (plug);
    std::set<std::string> modules;
    for (uint32_t i = 0; i < pCount; ++i)
    {
        clap_param_info_t info {};
        if (params->get_info (plug, i, &info))
            modules.insert (info.module);
    }

    // 3. Verify CLAP_EXT_STATE round-trip
    const auto* state = static_cast<const clap_plugin_state_t*> (plug->get_extension (plug, CLAP_EXT_STATE));
    MemStream ms;
    clap_ostream_t os { &ms, MemStream::write };
    if (!state || !state->save (plug, &os) || ms.data.size() < 12 || std::memcmp (ms.data.data(), "HDSD", 4) != 0)
    {
        std::fprintf (stderr, "FAIL: state save envelope invalid for %s\n", desc->id);
        return false;
    }
    clap_istream_t is { &ms, MemStream::read };
    if (!state->load (plug, &is))
    {
        std::fprintf (stderr, "FAIL: state load failed for %s\n", desc->id);
        return false;
    }

    // 4. Audio processing
    if (!plug->activate (plug, 48000.0, 32, 512) || !plug->start_processing (plug))
        return false;

    float inL[256] {}, inR[256] {}, outL[256] {}, outR[256] {};
    float* inPtrs[2] = { inL, inR };
    float* outPtrs[2] = { outL, outR };
    clap_audio_buffer_t inBuf { inPtrs, nullptr, 2, 0, 0 };
    clap_audio_buffer_t outBuf { outPtrs, nullptr, 2, 0, 0 };
    clap_input_events_t inEv { nullptr, emptyInSize, emptyInGet };
    clap_output_events_t outEv { &ctx, strictOutPush };

    for (int blk = 0; blk < 4; ++blk)
    {
        ctx.lastOutTime = 0;
        clap_process_t proc {};
        proc.steady_time = blk * 256;
        proc.frames_count = 256;
        proc.audio_inputs = &inBuf;
        proc.audio_inputs_count = 1;
        proc.audio_outputs = &outBuf;
        proc.audio_outputs_count = 1;
        proc.in_events = &inEv;
        proc.out_events = &outEv;
        plug->process (plug, &proc);
        for (int i = 0; i < 256; ++i)
        {
            if (!std::isfinite (outL[i]) || !std::isfinite (outR[i]))
                return false;
        }
    }

    // 5. Strict Preset Load stress test (exercises proxy host trampolines + deferred on_main_thread!) + UI render
    const auto* dbg = static_cast<const hdslave_desktop_debug_api_t*> (plug->get_extension (plug, "com.hdslave.desktop.debug/1"));
    int pPresets = 0;
    double renderMs = 0.0;
    if (dbg)
    {
        if (!dbg->ui_open (plug)) return false;
        pPresets = dbg->preset_count (plug);
        clap_event_note_t testNote {};
        testNote.header = { sizeof (testNote), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_ON, 0 };
        testNote.note_id = -1;
        testNote.port_index = 0;
        testNote.channel = 0;
        testNote.key = 48;
        testNote.velocity = 0.85;
        auto oneNoteSize = [] (const clap_input_events_t*) -> uint32_t { return 1; };
        auto oneNoteGet = [] (const clap_input_events_t* l, uint32_t) -> const clap_event_header_t* {
            return static_cast<const clap_event_header_t*> (l->ctx);
        };
        clap_input_events_t noteIn { &testNote.header, oneNoteSize, oneNoteGet };

        for (int pi = 0; pi < pPresets; ++pi)
        {
            if (!dbg->preset_load (plug, pi))
            {
                std::fprintf (stderr, "FAIL: preset_load(%d) failed for %s\n", pi, desc->id);
                return false;
            }
            if (plug->on_main_thread) plug->on_main_thread (plug);

            // Render 6 blocks with a note-on + subtle audio input to catch any NaN/Inf in every single preset!
            for (int blk = 0; blk < 6; ++blk)
            {
                for (int i = 0; i < 256; ++i) inL[i] = inR[i] = 0.1f * std::sin (float (i) * 0.05f);
                ctx.lastOutTime = 0;
                clap_process_t proc {};
                proc.steady_time = (pi * 6 + blk) * 256;
                proc.frames_count = 256;
                proc.audio_inputs = &inBuf;
                proc.audio_inputs_count = 1;
                proc.audio_outputs = &outBuf;
                proc.audio_outputs_count = 1;
                proc.in_events = (blk == 0) ? &noteIn : &inEv;
                proc.out_events = &outEv;
                plug->process (plug, &proc);
                for (int i = 0; i < 256; ++i)
                {
                    if (!std::isfinite (outL[i]) || !std::isfinite (outR[i]))
                    {
                        std::fprintf (stderr, "FAIL: non-finite audio on preset %d in %s\n", pi, desc->id);
                        return false;
                    }
                }
            }
        }
        if (pPresets > 0)
        {
            dbg->preset_load (plug, 0);
            if (plug->on_main_thread) plug->on_main_thread (plug);
        }

        // Exercise live screen click/drag + process block to prove SafeOutputEvents brackets every tellHost push
        dbg->ui_mouse (plug, 0, 200, 200, 0, 0, 0, 1);
        dbg->ui_mouse (plug, 1, 260, 200, 0, 0, 0, 1);
        dbg->ui_mouse (plug, 2, 260, 200, 0, 0, 0, 1);
        dbg->ui_mouse (plug, 0, 200, 200, 0, 0, 0, 1);
        dbg->ui_mouse (plug, 2, 200, 200, 0, 0, 0, 1);
        ctx.lastOutTime = 0;
        clap_process_t proc {};
        proc.steady_time = 1024;
        proc.frames_count = 256;
        proc.audio_inputs = &inBuf;
        proc.audio_inputs_count = 1;
        proc.audio_outputs = &outBuf;
        proc.audio_outputs_count = 1;
        proc.in_events = &inEv;
        proc.out_events = &outEv;
        plug->process (plug, &proc);
        if (plug->on_main_thread) plug->on_main_thread (plug);

        int pw = 0, ph = 0;
        const uint32_t* px = dbg->ui_render (plug, int (gw), int (gh), 1, &pw, &ph);
        if (!px || pw != int (gw) || ph != int (gh))
        {
            std::fprintf (stderr, "FAIL: ui_render failed for %s\n", desc->id);
            return false;
        }
        renderMs = dbg->ms_per_full_render (plug, int (gw), int (gh), 1);

        if (!snapDir.empty())
            writePpm (snapDir + "/" + baseName + ".ppm", px, pw, ph);

        if (std::strcmp (desc->id, "com.hdslave.tb-mpc-3-p-0") == 0
            && !testIntegratedPanel (plug, *dbg, *params, *state, proc, snapDir)) return false;

        dbg->ui_close (plug);
#ifdef __APPLE__
        if (!testCocoaWindow(plug,*gui)) return false;
#elif defined(_WIN32)
        if (!testWin32Window (plug, *gui, *params, proc)) return false;
#endif
    }

    std::printf ("PASS  %-22s  gui=%s (%ux%u)  params=%3u  mods=%2zu  presets=%2d  render=%.1fms  rescans=%d\n",
                 desc->name, api, gw, gh, pCount, modules.size(), pPresets, renderMs, ctx.paramRescans);

    plug->stop_processing (plug);
    plug->deactivate (plug);
    plug->destroy (plug);
    entry->deinit();

#ifdef _WIN32
    FreeLibrary (lib);
#else
    dlclose (lib);
#endif
    return true;
}
} // namespace

int main (int argc, char** argv)
{
    if (!testPresentation())
    {
        std::fprintf (stderr, "FAIL: partial panel presentation\n");
        return 1;
    }
    std::string snapDir;
    std::vector<std::string> plugins;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--snap") == 0 && i + 1 < argc)
            snapDir = argv[++i];
        else
            plugins.push_back (argv[i]);
    }
    if (plugins.empty())
    {
        std::fprintf (stderr, "Usage: %s [--snap <dir>] <plugin1.clap> ...\n", argv[0]);
        return 1;
    }
    int failures = 0;
    for (const auto& p : plugins)
    {
        if (!testOnePlugin (p, snapDir))
            ++failures;
    }
    if (failures > 0)
    {
        std::fprintf (stderr, "%d plugin(s) failed.\n", failures);
        return 1;
    }
    std::printf ("All %zu plugins passed strict host, preset & UI tests!\n", plugins.size());
    return 0;
}
