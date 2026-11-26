#include <clap/clap.h>
#include "../desktop/panel_presentation.h"
#include "../desktop/console_layout.h"
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

// Drive the one-screen editor through the same pointer events the native host sends.
bool testIntegratedPanel (const clap_plugin_t* plug, const hdslave_desktop_debug_api_t& ui,
                          const clap_plugin_params_t& params, const clap_plugin_state_t& state,
                          clap_process_t& block, const std::string& snapDir,bool captureMotion)
{
    using namespace hdslave_desktop;
    constexpr int scale=2;
    ConsoleLayout desk;
    desk.arrange(960,540);
    int width=0,height=0;
    auto render=[&] { return ui.ui_render(plug,960,540,scale,&width,&height); };
    auto mouse=[&](int kind,float x,float y,uint32_t mods=0,float delta=0,int button=0) {
        ui.ui_mouse(plug,kind,x,y,button,mods,delta,1);
    };
    auto click=[&](float x,float y,uint32_t mods=0) {
        mouse(0,x,y,mods); mouse(2,x,y,mods);
    };
    auto press=[&](Rect r,uint32_t mods=0) {
        click(r.x+r.w/2,r.y+r.h/2,mods);
        render();
    };
    auto drag=[&](int x,int y,int x1,int y1) {
        mouse(0,x,y); mouse(1,x1,y1); mouse(2,x1,y1);
    };
    auto dragSlot=[&](int slot,int deltaX,int deltaY) {
        const auto r=desk.slots[slot];
        const int x=r.x+r.w/2,y=r.y+r.h/2;
        drag(x,y,x+deltaX,y+deltaY);
    };
    auto sweepPlot=[&](int slot) {
        const auto p=desk.plotArea(slot);
        drag(p.x,p.y+p.h-1,p.x+p.w-1,p.y);
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
    auto area=[&](Rect r) {
        const auto* data=render();
        std::vector<uint32_t> sample;
        for(int y=r.y*scale;y<(r.y+r.h)*scale;y+=3)
            for(int x=r.x*scale;x<(r.x+r.w)*scale;x+=3) sample.push_back(data[y*width+x]);
        return sample;
    };
    auto snap=[&](const char* page, int w=1352, int h=708, int density=1) {
        if (snapDir.empty()) return;
        int sw=0,sh=0;
        const auto* frame=ui.ui_render(plug,w,h,density,&sw,&sh);
        writePpm(snapDir+"/Slink-"+page+".ppm",frame,sw,sh);
        render();
    };
    auto check=[](bool ok,const char* name) {
        if (!ok) std::fprintf(stderr,"FAIL: desktop panel: %s\n",name);
        return ok;
    };
    if (!check(render() && width==1920 && height==1080,"Retina buffer")) return false;
    const auto art=area(desk.crest);
    if (!check(std::set<uint32_t>(art.begin(),art.end()).size()>100,"chrome artwork bank rendered")) return false;
    const auto logo=area({desk.logo.x,desk.logo.y,desk.logo.w*2/3,desk.logo.h});
    if (!check(std::count_if(logo.begin(),logo.end(),[](uint32_t px) { return ((px>>8)&255)>145; })>50,
               "highlights from the desktop wordmark are visible")) return false;
    for (const auto size : {std::pair{960,540},std::pair{1352,708},std::pair{960,1100},std::pair{2400,540}})
    {
        ConsoleLayout layout;
        layout.arrange(size.first,size.second);
        int pw=0,ph=0;
        const auto* resized=ui.ui_render(plug,size.first,size.second,1,&pw,&ph);
        if (!check(resized && pw==size.first && ph==size.second,"the faceplate renders at each tested aspect ratio")) return false;
        for (int slot : {filterPlot, pitchPlot, cutoffPlot})
        {
            const Rect pad=layout.plotArea(slot);
            if (!check(pad.w>=120 && pad.h>=12 && layout.hitSlot(pad.x,pad.y)==slot &&
                       layout.hitSlot(pad.x+pad.w-1,pad.y+pad.h-1)==slot,"full usable XY pad at each size")) return false;
        }
        if (!check(layout.storm.x==layout.keyScale.x+layout.keyScale.w+4 &&
                   layout.storm.x+layout.storm.w==layout.footer.x+layout.footer.w &&
                   layout.storm.intersect(layout.keyScale).empty(),"one storm fills the footer beside the editable key")) return false;
        const Rect regen=layout.slots[id::regen];
        if (!check(regen.contains(layout.face.x,layout.face.y) &&
                   regen.contains(layout.face.x+layout.face.w-1,layout.face.y+layout.face.h-1) &&
                   layout.mast.contains(layout.play.x,layout.play.y) &&
                   layout.mast.contains(layout.play.x+layout.play.w-1,layout.play.y+layout.play.h-1) &&
                   layout.counter.x>layout.crest.x+layout.crest.w && layout.face.x>layout.play.x+layout.play.w,
                   "hardware header order and interactive plates")) return false;
        for (int i=0;i<slotCount;++i)
        {
            const Rect r=layout.slots[i];
            if (!check(r.w>=40 && r.h>=20 && r.x>=0 && r.y>=0 && r.x+r.w<=size.first && r.y+r.h<=size.second
                       && layout.hitSlot(r.x+r.w/2,r.y+r.h/2)==i,"every parameter and XY plot has a visible hit target")) return false;
            if (!check(r.intersect(layout.crest).empty() && r.intersect(layout.play).empty(),
                       "camera and transport do not intercept parameter edits")) return false;
            for (int j=0;j<i;++j)
                if (!check(r.intersect(layout.slots[j]).empty(),"control hit targets do not overlap")) return false;
        }
    }
    render();
    mouse(1,0,0);
    if (!snapDir.empty())
    {
        if (!ui.preset_load(plug,1)) return false;
        process();
        set("Pitch LFO Amount",.31);
        set("Pitch LFO Speed",4./3.);
        set("Cutoff LFO Amt",.48);
        set("Cutoff LFO Speed",2.);
        press(desk.play);
        for (int i=0;i<110;++i) process();
        snap("console");
        snap("compact",960,540,2);
        if (captureMotion)
        {
            // Capture five seconds of desktop playback from the 48 kHz host.
            constexpr int fps=25;
            int64_t rendered=0;
            const auto start=std::chrono::steady_clock::now();
            for (int i=0;i<5*fps;++i)
            {
                std::this_thread::sleep_until(start+std::chrono::microseconds(1000000LL*i/fps));
                while (rendered<48000LL*i/fps) { process(); rendered+=block.frames_count; }
                char name[32]; std::snprintf(name,sizeof(name),"motion-%03d",i);
                int sw=0,sh=0;
                const auto* frame=ui.ui_render(plug,1352,708,1,&sw,&sh);
                writePpm(snapDir+"/Slink-"+name+".ppm",frame,sw,sh);
            }
            render();
        }
        set("Steps",32);
        for (int i=0;i<505;++i) process();
        snap("32-steps");
        press(desk.play);
        process();
        if (!ui.preset_load(plug,0)) return false;
        plug->reset(plug);
        process();
        render();
    }
    // Each visible section header focuses the readouts, and the print has no input targets.
    if (!ui.preset_load(plug,0)) return false;
    process(); render();
    const int oldCutoff=int(value("Cutoff")*1000);
    for (int bank=0;bank<6;++bank)
    {
        mouse(1,0,0);
        const auto scoreBefore=area(desk.grid);
        press(desk.heading(bank));
        ctx.edited.clear();
        const int stormX=desk.storm.x+desk.storm.w*(2*bank+1)/12;
        click(stormX,desk.storm.y+desk.storm.h/2);
        process();
        if (!check(ctx.edited.empty() && desk.hitSlot(stormX,desk.storm.y+desk.storm.h/2)==-1,
                   "clicking the artwork leaves the focused bank and parameters alone")) return false;
        if (!check(area(desk.grid)==scoreBefore,"changing bank preserves the single score")) return false;
        for (int j=0;j<2;++j)
        {
            const Rect r=desk.ribbon[j];
            const int param=ribbonParams[bank][j], x=r.x+r.w/2, y=r.y+r.h/2;
            const auto& spec=tbmpc::paramTable[tbmpc::paramIndex(param)];
            ctx.edited.clear();
            drag(x,y,x,y+(value(spec.name)>(spec.min+spec.max)/2?22:-22));
            process();
            if (!check(ctx.edited.count(param),"bank readout automates its corresponding parameter")) return false;
        }
        if (!ui.preset_load(plug,0)) return false;
        process(); render();
    }
    if (!check(int(value("Cutoff")*1000)==oldCutoff,"preset recall after ribbon edits")) return false;
    for (int i=0;i<2;++i)
    {
        const Rect r=desk.keyFields[i];
        const int x=r.x+r.w/2, y=r.y+r.h/2;
        ctx.edited.clear();
        drag(x,y,x,y-24);
        process();
        if (!check(ctx.edited.count(i?id::scale:id::root),"the footer edits root and scale")) return false;
    }
    if (!ui.preset_load(plug,0)) return false;
    process(); render();
    press(desk.heading(0));
    // Every non-momentary parameter can send an automation event directly from this screen.
    for (int i=0;i<int(id::count);++i)
    {
        if (i==int(id::regen) || i==int(id::seedBack)) continue;
        const auto& spec=tbmpc::paramTable[tbmpc::paramIndex(i)];
        const double before=value(spec.name);
        const int dy=before>(spec.min+spec.max)/2?24:-24;
        ctx.edited.clear();
        dragSlot(i,0,i==int(id::filter)?-8:dy); // Select Twin Peak before exercising its spread.
        process();
        if (!check(value(spec.name)!=before && ctx.edited.count(i),spec.name)) return false;
    }
    // Clear short-lived drag readouts before a new patch is exercised.
    if (!ui.preset_load(plug,0)) return false;
    process();
    render();
    auto brightText=[&](Rect r) {
        const auto pixels=area(r);
        return std::any_of(pixels.begin(),pixels.end(),[](uint32_t px) {
            return ((px>>16)&255)>185 && ((px>>8)&255)>185 && (px&255)>185;
        });
    };
    auto tryInactive=[&](Rect r) {
        const int x=r.x+r.w/2,y=r.y+r.h/2;
        drag(x,y,x,y-20);
        mouse(3,x,y,0,2);
        ui.ui_mouse(plug,0,x,y,0,0,0,2);
        mouse(2,x,y);
        process();
    };
    set("Peak Spread",2.25);
    for (auto filter : {tbmpc::filter303,tbmpc::filter18dB,tbmpc::filterTwinPeak})
    {
        set("Filter",filter);
        const Rect spread=desk.slots[id::peakSpread];
        const bool active=filter==tbmpc::filterTwinPeak;
        if (!check(brightText(spread)==active,"Peak Spread follows the filter model")) return false;
        ctx.edited.clear();
        if (active) { dragSlot(id::peakSpread,0,-12); process(); }
        else tryInactive(spread);
        if (!check(bool(ctx.edited.count(id::peakSpread))==active && (active || value("Peak Spread")==2.25),
                   "only Twin Peak admits direct spread edits")) return false;
    }
    press(desk.heading(2));
    set("Slide Time",.625);
    for (int mode : {0,1})
        for (int slide : {0,1})
        {
            set("Mode",mode);
            set("Slide",slide);
            const bool active=mode==1 || slide==1;
            const Rect time=desk.slots[id::slideTime], readout=desk.ribbon[1];
            if (!check(brightText(time)==active && brightText(readout)==active,
                       "both 303 Time readouts follow the slide and note modes")) return false;
            if (!active)
            {
                ctx.edited.clear();
                tryInactive(time);
                tryInactive(readout);
                if (!check(!ctx.edited.count(id::slideTime) && value("Slide Time")==.625,
                           "inactive time retains its automated value")) return false;
            }
        }
    if (!ui.preset_load(plug,0)) return false;
    process(); render();
    const auto glide=desk.slots[id::glide];
    click(glide.x+glide.w*3/4,glide.y+glide.h*3/4); process();
    if (!check(value("Slide")==1,"303 slide has a direct button")) return false;
    click(glide.x+glide.w/4,glide.y+glide.h*3/4); process();
    if (!check(value("Slide")==0,"TB-3PO slide has a direct button")) return false;
    press(desk.tools[size_t(Item::Learn)]);
    press(desk.slots[id::slideTime]);
    press(desk.slots[id::cutoff]);
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

    ctx.edited.clear();
    sweepPlot(filterPlot);
    process();
    if (!check(value("Cutoff")>.94 && value("Resonance")>.8 &&
        ctx.edited.count(find("Cutoff").id) && ctx.edited.count(find("Resonance").id),"filter XY automation")) return false;
    ctx.edited.clear();
    sweepPlot(pitchPlot);
    sweepPlot(cutoffPlot);
    process();
    if (!check(value("Pitch LFO Amount")>.75 && value("Cutoff LFO Amt")>.75 &&
        ctx.edited.count(find("Pitch LFO Speed").id) && ctx.edited.count(find("Cutoff LFO Speed").id),"both LFO XY plots automate")) return false;

    const double initialSeed=value("Seed");
    press(desk.slots[id::regen]);
    process();
    if (!check(initialSeed!=value("Seed"),"Regen rolls a locked seed")) return false;
    const double beforeBarcode=value("Seed");
    const Rect barcode=desk.slots[id::seed];
    const int bx=barcode.x+barcode.w/2,by=barcode.y+barcode.h/2;
    const auto beforeSeedDigits=area(desk.slots[id::seedBack]);
    mouse(0,bx,by); mouse(1,bx+35,by);
    if (!check(area(desk.slots[id::seedBack])!=beforeSeedDigits,"seed display follows a barcode drag before host flush")) return false;
    mouse(2,bx+35,by);
    process();
    if (!check(beforeBarcode!=value("Seed"),"seed barcode drag")) return false;
    const auto scaleBox=desk.slots[id::scale];
    press(scaleBox);
    mouse(3,scaleBox.x,scaleBox.y,0,-1000);
    // The scale list opens above this row. Select the last visible choice.
    click(scaleBox.x+35,scaleBox.y-18);
    process();
    if (!check(value("Scale")==find("Scale").max_value,"last scale reachable from desktop menu")) return false;

    set("Steps",32);
    auto cell=[&](int i,int lane) { return desk.column(i,32).intersect(desk.lane(lane)); };
    const auto last=cell(31,1), first=cell(0,1);
    const int fx=first.x+first.w/2,fy=first.y+first.h/2;
    const int lx=last.x+last.w/2,ly=last.y+last.h/2;
    drag(lx,ly,lx,ly+14);
    process();
    set("Seed Mode",0);
    const auto pitchBefore=area(first);
    const int marks=ctx.stateDirtyCalls;
    drag(fx,fy,fx,fy-21);
    const Rect gate=cell(0,2),to=cell(3,2);
    drag(fx,gate.y+gate.h/2,to.x+to.w/2,gate.y+gate.h/2);
    press(cell(0,3));
    press(cell(0,4));
    process();
    const auto pitchEdited=area(first);
    if (!check(value("Seed Mode")==1 && ctx.stateDirtyCalls>marks && pitchEdited!=pitchBefore,
               "step edits lock random seed, notify host and repaint pitch")) return false;
    mouse(1,0,0);

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
    if (!check((record(0x534c0020)&255)==254 && step>=0 && (step&255)==3 && (step&0x2a00)==0x2a00 &&
             (record(0x534c0004)&0x300)==(step&0x300),"pitch, gate paint, accent and slide persist as SP-16 records")) return false;
    if (!ui.preset_load(plug,0)) return false;
    process();
    clap_istream_t in {&saved,MemStream::read};
    if (!state.load(plug,&in)) return false;
    process();
    if (!check(area(first)==pitchEdited,"edited pattern restored after preset load")) return false;
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
    press(cell(0,0));
    process();
    if (!check(peak()>.0001,"numbered step audibly auditions while stopped")) return false;

    clap_event_transport_t transport {};
    transport.flags=CLAP_TRANSPORT_HAS_TEMPO|CLAP_TRANSPORT_HAS_BEATS_TIMELINE;
    transport.tempo=120;
    block.transport=&transport;
    plug->reset(plug);
    process();
    render();
    const auto stopped=area(desk.play);
    press(desk.play,1);
    if (!check(ctx.transportRequests==1,"Shift-Play requests DAW transport when supported")) return false;
    press(desk.play);
    double previewPeak=0;
    for(int i=0;i<72;++i) { process(); previewPeak=std::max(previewPeak,peak()); }
    const auto preview=area(desk.play);
    if (!check(preview!=stopped && previewPeak>.0001,"local Play sends audio and shows Stop without running host")) return false;
    MemStream beforeBrowsing;
    clap_ostream_t savedBrowse {&beforeBrowsing,MemStream::write};
    if (!state.save(plug,&savedBrowse)) return false;
    const int callbacksBefore=ctx.callbackRequests;
    for (int i=0;i<18;++i)
        if (!ui.preset_load(plug,i%6)) return false;
    if (!check(ctx.callbackRequests-callbacksBefore<=1,"rapid recalls share a pending host callback")) return false;
    // Browse through the MIDI-only preset while local playback is armed.
    // Returning to a sequence must produce sustained audio without another click.
    for (int lap=0;lap<3;++lap)
        for (int index=0;index<ui.preset_count(plug);++index)
        {
            if (!ui.preset_load(plug,lap%2 ? 5-index : index)) return false;
            process();
        }
    if (!ui.preset_load(plug,5)) return false;
    for (int i=0;i<200;++i) process();
    if (!check(peak()<.00001,"Note Bass waits for MIDI during internal preset browsing")) return false;
    if (!ui.preset_load(plug,1)) return false;
    double resumedPeak=0;
    for (int i=0;i<200;++i)
    {
        process();
        if (i>=150) resumedPeak=std::max(resumedPeak,peak());
    }
    if (!check(area(desk.play)==preview && resumedPeak>.0001,"sequence audio resumes after rapid preset browsing")) return false;
    // Find a sounding boundary, then verify that switching the waveform joins to it.
    float tail=0;
    for (int i=0;i<180 && std::abs(tail)<.005f;++i)
    {
        process();
        tail=block.audio_outputs[0].data32[0][block.frames_count-1];
    }
    if (!check(std::abs(tail)>=.005f && ui.preset_load(plug,3),"sounding preset transition")) return false;
    process();
    if (!check(std::abs(block.audio_outputs[0].data32[0][0]-tail)<1.e-6f,"continuous sample at live preset boundary")) return false;
    press(desk.play);
    process();
    if (!check(area(desk.play)==stopped,"local Stop releases preview control")) return false;
    press(desk.play);
    process();
    transport.flags|=CLAP_TRANSPORT_IS_PLAYING;
    transport.song_pos_beats=8ull*CLAP_BEATTIME_FACTOR;
    process();
    const auto synced=area(desk.play);
    if (!check(synced!=preview && synced!=stopped,"DAW start replaces preview and displays Sync")) return false;
    // Exercise the live host clock across late song positions, short blocks and preset bursts.
    // A MIDI-only patch may rest until a note arrives. Each sequenced patch must sound again.
    int64_t hostFrame=0;
    const auto hostBlock=[&] {
        transport.song_pos_beats=8ll*CLAP_BEATTIME_FACTOR+hostFrame*CLAP_BEATTIME_FACTOR/24000;
        process();
        hostFrame+=block.frames_count;
        for (uint32_t channel=0;channel<2;++channel)
            for (uint32_t i=0;i<block.frames_count;++i)
                if (!std::isfinite(block.audio_outputs[0].data32[channel][i])) return false;
        return true;
    };
    if (!ui.preset_load(plug,5)) return false;
    for (int i=0;i<200;++i)
        if (!hostBlock()) return false;
    const auto midiPrompt=area(desk.play);
    if (!check(peak()<.00001 && midiPrompt!=synced && midiPrompt!=stopped && midiPrompt!=preview,
               "Note Bass waits silently with a distinct MIDI prompt during DAW playback")) return false;
    clap_event_note_t bassNote {};
    bassNote.header={sizeof(bassNote),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_NOTE_ON,0};
    bassNote.note_id=-1;
    bassNote.key=48;
    bassNote.velocity=.85;
    clap_input_events_t bassInput {&bassNote.header,midi.size,midi.get};
    block.in_events=&bassInput;
    if (!hostBlock() || !check(peak()>.0001,"Note Bass sounds on an incoming MIDI note")) return false;
    bassNote.header.type=CLAP_EVENT_NOTE_OFF;
    if (!hostBlock()) return false;
    block.in_events=input;
    for (int lap=0;lap<8;++lap)
    {
        for (int i=0;i<12;++i)
        {
            if (!ui.preset_load(plug,(i+lap)%6) || !hostBlock()) return false;
        }
        const int finalPreset=lap%5;
        if (!ui.preset_load(plug,finalPreset)) return false;
        double level=0;
        for (int i=0;i<480;++i)
        {
            if (!hostBlock()) return false;
            if (i>=120) level=std::max(level,peak());
        }
        if (!check(level>.0001,"FL-style transport keeps sequenced presets audible after browsing")) return false;
    }
    transport.flags&=~CLAP_TRANSPORT_IS_PLAYING;
    process();
    if (!check(area(desk.play)==stopped,"host stop does not resume preview")) return false;
    block.transport=nullptr;

    clap_istream_t restoreBrowse {&beforeBrowsing,MemStream::read};
    if (!state.load(plug,&restoreBrowse)) return false;
    process();

    // Two seeds must actually sound before the sequencer offers the earlier one for recall.
    set("Seed Mode",1);
    press(desk.play);
    for (int i=0;i<80;++i) process();
    const double playedSeed=value("Seed");
    press(desk.slots[id::regen]);
    for (int i=0;i<80;++i) process();
    press(desk.play);
    process();
    if (!check(value("Seed")!=playedSeed,"the regenerated seed played")) return false;
    press(desk.slots[id::seedBack]);
    process();
    if (!check(value("Seed")==playedSeed,"Recall restores the previous played seed")) return false;

    press(cell(0,0));
    const auto crest=[&] { return area(desk.crest); };
    const auto storm=[&] { return area(desk.storm); };
    const auto smile=[&] { return area(desk.face); };
    const auto stormSamples=[&] {
        std::array<std::vector<uint32_t>,3> result;
        constexpr int samples[][2]={{2,24},{38,62},{77,98}};
        for (size_t i=0;i<result.size();++i)
            result[i]=area({desk.storm.x+desk.storm.w*samples[i][0]/100,desk.storm.y,
                            desk.storm.w*(samples[i][1]-samples[i][0])/100,desk.storm.h});
        return result;
    };
    press(desk.crest);
    const auto held=crest(), moving=storm(), turning=smile(), nextCap=area(cell(1,0));
    const auto movingParts=stormSamples();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const auto laterParts=stormSamples();
    for (size_t i=0;i<laterParts.size();++i)
        if (!check(laterParts[i]!=movingParts[i],"both ends and the crossing of the panoramic storm animate during camera hold")) return false;
    if (!check(crest()==held && storm()!=moving && smile()!=turning && area(cell(1,0))==nextCap,
               "the held crest, turning smiley and moving footer have independent regions")) return false;
    press(desk.crest);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    if (!check(crest()!=held,"crest resumes")) return false;
    press(desk.tools[size_t(Item::Motion)]);
    const auto frozenCrest=crest(), frozenStorm=storm(), frozenFace=smile();
    // Holding the frame isolates the resize from animation. All visible bends
    // must stay in place when more space opens up to their right.
    ConsoleLayout wideDesk;
    wideDesk.arrange(1920,540);
    int wideWidth=0,wideHeight=0;
    const auto* wide=ui.ui_render(plug,1920,540,scale,&wideWidth,&wideHeight);
    if (!check(wide && wideWidth==1920*scale && wideHeight==540*scale,"wide Retina resize")) return false;
    std::vector<uint32_t> commonStorm;
    for (int y=0;y<desk.storm.h*scale;y+=3)
        for (int x=0;x<desk.storm.w*scale;x+=3)
            commonStorm.push_back(wide[(wideDesk.storm.y*scale+y)*wideWidth+wideDesk.storm.x*scale+x]);
    if (!check(commonStorm==frozenStorm,"widening the editor reveals the next whips at the same scale")) return false;
    render();
    set("Resonance",.28);
    mouse(1,0,0);
    const auto* partial=render();
    const std::vector<uint32_t> expected(partial,partial+size_t(width)*height);
    ui.ms_per_full_render(plug,960,540,scale);
    if (!check(std::equal(expected.begin(),expected.end(),render()),"partial and full paints match at Retina scale")) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(130));
    if (!check(crest()==frozenCrest && storm()==frozenStorm && smile()==frozenFace,"global motion hold")) return false;
    press(desk.crest,1);
    if (!check(crest()!=frozenCrest && storm()==frozenStorm,"manual cut while global motion is held")) return false;
    const auto lastCap=area(cell(31,0)), firstCap=area(cell(0,0)), firstCounter=area(desk.counter);
    press(cell(31,0)); mouse(1,0,0);
    if (!check(storm()==frozenStorm && area(cell(31,0))!=lastCap && area(desk.counter)!=firstCounter,
               "the hardware step counter and score follow selection while artwork is frozen")) return false;
    press(cell(0,0)); mouse(1,0,0);
    if (!check(area(cell(0,0))==firstCap && area(cell(31,0))==lastCap && area(desk.counter)==firstCounter,
               "returning to step one restores both the ruler and hardware counter")) return false;
    const auto step17=area(cell(16,0));
    press(desk.counter);
    if (!check(area(cell(16,0))!=step17 && area(desk.counter)!=firstCounter,"step counter jumps to the second half")) return false;
    press(desk.counter);
    if (!check(area(cell(16,0))==step17 && area(desk.counter)==firstCounter,"step counter returns to the first half")) return false;
    press(desk.tools[size_t(Item::Motion)]);
    std::this_thread::sleep_for(std::chrono::milliseconds(140));
    if (!check(storm()!=frozenStorm,"the storm resumes after a global motion hold")) return false;
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
        hdslave_desktop::ConsoleLayout desk;
        desk.arrange(960,540);
        const auto r=desk.slots[tbmpc::ParamId::regen];
        const auto point=MAKELPARAM(r.x+r.w/2,r.y+r.h/2);
        SendMessageW (child, WM_LBUTTONDOWN, MK_LBUTTON, point);
        SendMessageW (child, WM_LBUTTONUP, 0, point);
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

bool testOnePlugin (const std::string& clapPath, const std::string& snapDir,bool captureMotion)
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
    constexpr OldChoice legacy[] {{"Filter","303",0},{"Slide","303",1},{"Slide","TB-3PO",0},{"Slide","Tick",0},{"Slide","Glide",1},{"Mode","TB-3PO",0}};
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
            && !testIntegratedPanel (plug, *dbg, *params, *state, proc, snapDir,captureMotion)) return false;

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
    bool captureMotion=false;
    std::vector<std::string> plugins;
    for (int i = 1; i < argc; ++i)
    {
        if ((!std::strcmp(argv[i],"--snap") || !std::strcmp(argv[i],"--film")) && i+1<argc)
        {
            captureMotion=!std::strcmp(argv[i],"--film");
            snapDir=argv[++i];
        }
        else
            plugins.push_back (argv[i]);
    }
    if (plugins.empty())
    {
        std::fprintf (stderr, "Usage: %s [--snap <dir> | --film <dir>] <plugin1.clap> ...\n", argv[0]);
        return 1;
    }
    int failures = 0;
    for (const auto& p : plugins)
    {
        if (!testOnePlugin (p, snapDir,captureMotion))
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
