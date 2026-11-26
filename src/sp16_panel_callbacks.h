// Shared UI-thread callbacks. Included alongside each CLAP implementation after applyScreenEdits.
Plugin& panelPlugin(const void* plugin) { return of(static_cast<const clap_plugin*>(plugin)); }
void wakePanel(Plugin& p)
{
    if (p.host->request_process) p.host->request_process(p.host);
}
void panelOpen(const void* plugin)
{
    auto& p = panelPlugin(plugin);
    p.panel.dirty = true;
    p.panelOpen.store(true, std::memory_order_release);
    wakePanel(p);
}
void panelClose(const void* plugin)
{
    auto& p = panelPlugin(plugin);
    p.panelOpen.store(false, std::memory_order_release);
    p.panel.release();
    p.previewRequested.store(false, std::memory_order_release);
    p.touchedStep.store(-1, std::memory_order_release);
}
bool panelRender(const void* plugin, const sixteen_screen_surface_t* s, bool full)
{
    if (!s || !s->pixels || s->width != SIXTEEN_SCREEN_WIDTH || s->height != SIXTEEN_SCREEN_HEIGHT || s->stride < s->width)
        return false;
    auto& p = panelPlugin(plugin);
    const bool preview=p.previewRequested.load(std::memory_order_acquire);
    p.panel.dirty |= preview!=p.panel.localPlay;
    p.panel.localPlay=preview;
    return p.panel.render(*s, full, p.patternState, p.values);
}
bool panelKnobs(const void* plugin, uint32_t ids[6], char label[SIXTEEN_TAB_NAME_SIZE])
{
    const auto& bank = sp16::banks[panelPlugin(plugin).panel.bank];
    std::copy(bank.ids.begin(), bank.ids.end(), ids);
    std::snprintf(label, SIXTEEN_TAB_NAME_SIZE, "%s", bank.name);
    return true;
}
bool panelTurn(const void* plugin, int32_t delta) { return panelPlugin(plugin).panel.turn(delta); }
bool panelPush(const void* plugin)
{
    auto& panel = panelPlugin(plugin).panel;
    panel.select((panel.bank+1) % std::size(sp16::banks));
    return true;
}
void publishStepEdit(Plugin& p)
{
    // Commit an edited random pattern to its displayed seed, including during playback.
    if (valueOf(p,ParamId::seedMode)<.5f)
    {
        hdui::Edits lock;
        lock.set(ParamId::seed,float(p.manual.seed));
        lock.set(ParamId::seedMode,1);
        applyScreenEdits(p,lock);
    }
    p.stepExchange.publish(p.manual);
    const auto* state=static_cast<const clap_host_state*>(p.host->get_extension(p.host,CLAP_EXT_STATE));
    if (state && state->mark_dirty) state->mark_dirty(p.host);
    wakePanel(p);
}

bool dragStep(Plugin& p,int x,int y,bool down)
{
    auto& panel=p.panel;
    if (panel.shown.seed!=panel.gestureSeed) { panel.release(); return false; }
    const auto lane=panel.lane;
    // Keep the original lane and bank while the finger crosses a cell edge.
    const int step=lane==sp16::StepLane::pitch ? panel.startStep : panel.stepAt(x,panel.startY);
    if (step<0 || step>=panel.shown.steps) return true;
    if (lane==sp16::StepLane::number && !down && y!=panel.startY)
    {
        // Only another numbered header may sound during a horizontal scrub.
        if (panel.bank==sp16::patternPage ? (y<103 || y>=125)
              : (y<panel.ribbonTop(step) || y>=panel.ribbonTop(step)+14)) return true;
    }
    const int previous=panel.finger;
    panel.finger=step;
    panel.thorn.selected=step;
    panel.dirty |= previous!=step;
    if (lane==sp16::StepLane::number)
    {
        if (down || previous!=step)
        {
            p.touchedAt.store(steadyMs(),std::memory_order_release);
            p.touchedStep.store(step,std::memory_order_release);
            wakePanel(p);
        }
        return true;
    }
    const int offset=std::clamp(panel.startOffset+int(std::round((panel.startY-y)/7.0)),-127,127);
    if (lane==sp16::StepLane::pitch && (down || (offset==panel.startOffset && panel.gestureSeed!=p.manual.seed)))
        return true;
    if (p.manual.seed!=panel.gestureSeed)
    {
        p.manual={};
        p.manual.seed=panel.gestureSeed;
    }
    if (lane==sp16::StepLane::pitch)
    {
        if (sp16::StepEdits::pitch(p.manual.cells[step])==offset) return true;
        p.manual.setPitch(step,offset);
    }
    else
    {
        if (!down && previous==step) return true;
        const int from=previous<0 ? step : previous;
        for (int i=std::min(from,step);i<=std::max(from,step);++i)
            p.manual.setFlag(i,lane,panel.paintOn);
    }
    publishStepEdit(p);
    return true;
}

bool dragControl(Plugin& p,int x,int y)
{
    using Panel=sp16::Panel;
    using namespace ParamId;
    auto& panel=p.panel;
    hdui::Edits edits;
    if (panel.gesture==Panel::Gesture::filterXY)
    {
        edits.set(cutoff,float(std::clamp(x,Panel::plotLeft,Panel::plotRight)-Panel::plotLeft)/(Panel::plotRight-Panel::plotLeft));
        edits.set(resonance,float(Panel::plotBottom-std::clamp(y,Panel::plotTop,Panel::plotBottom))/(Panel::plotBottom-Panel::plotTop));
    }
    else if (panel.gesture==Panel::Gesture::lfoXY)
    {
        const int left=24+382*panel.lfoLane;
        const float u=float(std::clamp(x-left,0,348))/348.f;
        edits.set(panel.lfoLane ? cutoffLfoSpeed : pitchLfoSpeed,lfoMinRate*std::pow(lfoMaxRate/lfoMinRate,u));
        edits.set(panel.lfoLane ? cutoffLfoAmount : pitchLfoAmount,float(216-std::clamp(y,139,216))/77.f);
    }
    else if (panel.gesture==Panel::Gesture::parameter)
    {
        const auto& param=paramTable[paramIndex(panel.dragParam)];
        const int dx=x-panel.startX, dy=panel.startY-y;
        const int distance=std::abs(dx)>std::abs(dy) ? dx : dy;
        const float delta=param.kind==Kind::continuous ? distance*(param.max-param.min)/140.f : std::round(distance/8.f);
        edits.set(param.id,panel.startValue+delta);
    }
    else return false;
    applyScreenEdits(p,edits);
    return true;
}

bool panelTouch(const void* plugin, int32_t phase, int32_t x, int32_t y)
{
    using Panel=sp16::Panel;
    auto& p = panelPlugin(plugin);
    auto& panel = p.panel;
    if (phase == SIXTEEN_TOUCH_UP)
    {
        const bool held=panel.gesture!=Panel::Gesture::none;
        panel.release();
        panel.dirty |= held;
        return held;
    }
    if (phase==SIXTEEN_TOUCH_MOVE)
        return panel.gesture==Panel::Gesture::steps ? dragStep(p,x,y,false) : dragControl(p,x,y);
    if (phase!=SIXTEEN_TOUCH_DOWN) return false;
    panel.release();
    panel.startX=x;
    panel.startY=y;
    if (x < 0 || x >= SIXTEEN_SCREEN_WIDTH || y < 0 || y >= SIXTEEN_SCREEN_HEIGHT)
        return false;
    const int tab = sp16::Panel::tabAt(x,y);
    if (tab >= 0)
    {
        panel.select(tab);
        return true;
    }
    if (x>=186 && x<518 && y>=6 && y<96)
    {
        panel.thorn.heldCamera=!panel.thorn.heldCamera;
        return true;
    }
    if (panel.bank==sp16::patternPage && x>=528 && x<620 && y>=6 && y<96 && panel.shown.steps>16 && !panel.shown.running)
    {
        panel.thorn.selected=panel.thorn.selected>=16?0:16;
        panel.dirty=true;return true;
    }
    if (x>=622 && x<703 && y>=55 && y<84)
    {
        const bool preview=p.previewRequested.load(std::memory_order_acquire);
        if ((panel.shown.running && !preview) || valueOf(p,ParamId::mode)>=.5f) return false;
        p.previewRequested.store(!preview,std::memory_order_release);
        wakePanel(p);return true;
    }
    const bool regen = x>=719 && x<775 && y>=61 && y<91;
    const bool back = x>=7 && x<72 && y>=50 && y<96;
    if (regen || back)
    {
        hdui::Edits edits;
        edits.press(regen ? ParamId::regen : ParamId::seedBack);
        applyScreenEdits(p,edits);
        return true;
    }
    auto captureParam=[&](uint32_t id) {
        panel.gesture=Panel::Gesture::parameter;
        panel.dragParam=id;
        panel.startValue=valueOf(p,id);
        return true;
    };
    if (x>=72 && x<179 && y>=50 && y<96) return captureParam(ParamId::seed);
    if (x>=7 && x<122 && y>=252 && y<279) return captureParam(x<37 ? ParamId::root : ParamId::scale);
    if (panel.bank==sp16::voicePage)
    {
        if (y>=103 && y<137)
        {
            if (x>=16 && x<174) return captureParam(ParamId::filter);
            if (x>=406 && x<756) return captureParam(x<590 ? ParamId::cutoff : ParamId::resonance);
        }
        if (x<Panel::plotLeft || x>Panel::plotRight || y<Panel::plotTop || y>Panel::plotBottom) return false;
        panel.gesture=Panel::Gesture::filterXY;
        return dragControl(p,x,y);
    }
    if (panel.bank==sp16::lfoPage)
    {
        const int lane=x>=394;
        const int left=12+382*lane;
        if (x<left || x>=left+373 || y<103 || y>=244) return false;
        if (y<139) return captureParam(x<left+242 ? (lane ? ParamId::cutoffLfoSpeed : ParamId::pitchLfoSpeed)
                                                   : (lane ? ParamId::cutoffLfoAmount : ParamId::pitchLfoAmount));
        panel.gesture=Panel::Gesture::lfoXY;
        panel.lfoLane=lane;
        return dragControl(p,x,y);
    }
    if (panel.bank!=sp16::patternPage && y>=103 && y<128 && x>=8 && x<384 && (x-8)%190<186)
        return captureParam(panel.ribbonParam((x-8)/190));
    const int step=panel.stepAt(x,y);
    if (step<0) return false;
    panel.lane=panel.bank==sp16::patternPage ? Panel::patternLane(y)
        : y<panel.ribbonTop(step)+14 ? sp16::StepLane::number : sp16::StepLane::pitch;
    if (panel.lane==sp16::StepLane::number && valueOf(p,ParamId::mode)>=.5f) return false;
    panel.gesture=Panel::Gesture::steps;
    panel.startStep=step;
    panel.gestureSeed=uint16_t(panel.shown.seed);
    panel.startOffset=p.manual.seed==panel.gestureSeed ? sp16::StepEdits::pitch(p.manual.cells[step]) : 0;
    panel.thorn.capturedBank=step/16*16;
    const auto bits=panel.lane==sp16::StepLane::gate ? panel.shown.gates :
                    panel.lane==sp16::StepLane::accent ? panel.shown.accents : panel.shown.slides;
    // A second gesture can arrive before the next audio block publishes its view.
    const bool generated=(bits>>step)&1;
    panel.paintOn=!generated;
    if (panel.lane>=sp16::StepLane::gate && p.manual.seed==panel.gestureSeed)
        panel.paintOn=!sp16::StepEdits::flag(p.manual.cells[step],panel.lane,generated);
    return dragStep(p,x,y,true);
}
const sixteen_plugin_screen_t panelExtension {panelOpen, panelClose, panelRender, panelTouch, panelTurn, panelPush, panelKnobs};
