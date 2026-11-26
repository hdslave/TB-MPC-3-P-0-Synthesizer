#include "panel_ui.h"
#include "../src/params.h"
#include <chrono>
#include <cstdio>

namespace hdslave_desktop
{
namespace
{
namespace id = tbmpc::ParamId;
constexpr int bw=SIXTEEN_SCREEN_WIDTH, bh=SIXTEEN_SCREEN_HEIGHT;
constexpr uint32_t ground=0xff080812, glass=0xff141021, lineInk=0xff514765;
constexpr uint32_t light=0xfff1efff, muted=0xffb9b1cb, cobalt=0xff567bea, lilac=0xffb7a2f7;
std::string upper (std::string s)
{
    for (auto& c : s) if (c>='a' && c<='z') c-=32;
    return s;
}
void plate(Canvas& c,Rect r,uint32_t color=glass,uint32_t edge=lineInk)
{
    c.fillChamfer(r,5,edge);
    c.fillChamfer(r.inset(1),4,color);
}
void button(Canvas& c,Rect r,const std::string& name,bool active=false)
{
    plate(c,r,active?0xff283b80:glass,active?0xff89a7fa:lineInk);
    c.textAligned(r.inset(4),name,active?light:muted,1,1);
}
}

PanelUI::PanelUI(DesktopPlugin* p) : p_(p), bodyPixels_(bw*bh), previousBody_(bw*bh)
{
    setSize(w_,h_,1);
    setVisible(true);
}

PanelUI::~PanelUI() { setVisible(false); }

void PanelUI::setVisible(bool visible)
{
    if (visible_==visible) return;
    visible_=visible;
    if (visible)
    {
        p_->panel->screen->on_open(p_->inner);
        fullBody_=dirty_=true;
    }
    else
    {
        finishDrag();
        endWheelGesture();
        const bool wasPlaying=p_->panel->localPlay(p_->inner);
        p_->panel->screen->on_close(p_->inner);
        if (wasPlaying && p_->realHost->request_process) p_->realHost->request_process(p_->realHost);
        closePopup();
    }
}

void PanelUI::setSize(int w,int h,int scale)
{
    if (w==w_ && h==h_ && scale==scale_ && !frame_.empty()) return;
    w_=w; h_=h; scale_=std::clamp(scale,1,3);
    frame_.assign(size_t(physWidth())*physHeight(),ground);
    layout();
    closePopup();
    dirty_=true;
}

void PanelUI::layout()
{
    const int m=std::clamp(w_/78,12,28), top=m;
    const int contentW=w_-2*m, toolH=28;
    int x=m;
    auto tool=[&](Item item,int width) {
        items_[size_t(item)]={x,top,width,toolH};
        x+=width+4;
    };
    tool(Item::PresetPrev,28);
    tool(Item::PresetBox,std::clamp(contentW/5,165,280));
    tool(Item::PresetNext,28);
    const int statusX=x+10;
    x=w_-m-(56+92+82+96+12);
    status_={statusX,top,x-statusX-10,toolH};
    tool(Item::Fine,56); tool(Item::Learn,92); tool(Item::ClearCc,82); tool(Item::Motion,96);
    const int firstY=top+toolH+9;
    const int availableH=h_-m-firstY;
    const int stripH=std::clamp(h_/7,78,118);
    const int panelW=std::min(contentW,(availableH-stripH-8)*bw/bh);
    const int panelH=(panelW*bh+bw/2)/bw;
    const int left=(w_-panelW)/2;
    const int y=firstY+std::max(0,(availableH-panelH-stripH-8)/2);
    body_={left,y,panelW,panelH};
    strip_={left,y+panelH+8,panelW,stripH};
    for(int i=0;i<6;++i)
    {
        const int span=panelW+6;
        const int l=span*i/6,r=span*(i+1)/6;
        cards_[i]={left+l,strip_.y,r-l-6,stripH};
    }
}

int PanelUI::hitCard(int x,int y) const
{
    for(int i=0;i<6;++i)
        if (cards_[i].contains(x,y)) return cardParams_[i];
    return -1;
}

int PanelUI::bodyX(float x) const { return int(std::floor((x-body_.x)*bw/body_.w)); }
int PanelUI::bodyY(float y) const { return int(std::floor((y-body_.y)*bh/body_.h)); }

int PanelUI::bodyParam(int x,int y) const
{
    if (x>=72 && x<179 && y>=50 && y<96) return p_->findParamIndex(id::seed);
    if (x>=7 && x<122 && y>=252 && y<279) return p_->findParamIndex(x<37?id::root:id::scale);
    return -1;
}

void PanelUI::paintToolbar()
{
    c_.fill(0,0,w_,items_[0].y+items_[0].h+5,ground);
    const auto learn=p_->midiLearnTarget.load();
    button(c_,items_[size_t(Item::PresetPrev)],"<");
    const int preset=p_->currentPresetIndex;
    button(c_,items_[size_t(Item::PresetBox)],preset<0?"Presets":p_->presets[size_t(preset)].name);
    button(c_,items_[size_t(Item::PresetNext)],">");
    button(c_,items_[size_t(Item::Fine)],"Fine",fine_);
    button(c_,items_[size_t(Item::Learn)],"MIDI Learn",learn!=CLAP_INVALID_ID);
    button(c_,items_[size_t(Item::ClearCc)],"Clear CC");
    button(c_,items_[size_t(Item::Motion)],motion_?"Motion on":"Motion off",motion_);
    if (status_.w>0)
    {
        std::string status=learn==DesktopPlugin::kLearnArmedNoParam?"Choose a control":
              learn!=CLAP_INVALID_ID?"Move a MIDI controller":"Slink  /  Thorn Crown";
        c_.textAligned(status_,status,muted,1,0);
    }
}

void PanelUI::paintStrip()
{
    c_.fill(strip_,ground);
    for(int i=0;i<6;++i)
    {
        const int index=cardParams_[i];
        if (index<0) continue;
        const auto& param=p_->params[size_t(index)];
        const Rect r=cards_[i];
        const bool active=index==hover_ || index==dragParam_;
        plate(c_,r,active?0xff242044:glass,active?0xff8b9fd8:lineInk);
        c_.gradient({r.x+4,r.y+4,r.w-8,r.h-8},active?0xff28233f:0xff211a31,glass);
        const int left=r.x+10, width=r.w-20;
        char badge[8];
        const int cc=p_->findCcForParam(param.id);
        if (cc>=0) std::snprintf(badge,sizeof(badge),"CC %d",cc);
        else std::snprintf(badge,sizeof(badge),"0%d",i+1);
        c_.textAligned({left,r.y+7,width,14},badge,muted,1,2);
        c_.textAligned({left,r.y+24,width,15},param.name,muted,1,0);
        const int size=r.w>=155 && c_.textWidth(valueText(index),2)<=width ? 2 : 1;
        c_.textAligned({left,r.y+39,width,r.h-54},valueText(index),light,size,0);
        c_.fill(left,r.y+r.h-9,width,2,lineInk);
        const int meter=int(std::lround(norm(index)*width));
        if (meter>0) c_.fill(left,r.y+r.h-9,meter,2,cobalt);
    }
}

Rect PanelUI::update(uint64_t now)
{
    using Clock=std::chrono::steady_clock;
    const auto start=Clock::now();
    if (wheelParam_>=0 && now-wheelAt_>175) endWheelGesture();
    if (!visible_) return {};
    sixteen_screen_surface_t surface {bodyPixels_.data(),bw,bh,bw,uint32_t(now)};
    const bool changed=p_->panel->screen->render(p_->inner,&surface,fullBody_);
    fullBody_=false;
    uint32_t ids[6] {};
    char label[SIXTEEN_TAB_NAME_SIZE] {};
    p_->panel->screen->knobs(p_->inner,ids,label);
    bool refreshStrip=stripDirty_;
    stripDirty_=false;
    for(int i=0;i<6;++i)
    {
        const int index=p_->findParamIndex(ids[i]);
        const double value=p_->displayValue(index);
        const auto formatted=valueText(index);
        const int cc=index<0 ? -1 : p_->findCcForParam(ids[i]);
        refreshStrip|=cardParams_[i]!=index || displayed_[i]!=value || readings_[i]!=formatted || ccs_[i]!=cc;
        cardParams_[i]=index;
        displayed_[i]=value;
        readings_[i]=formatted;
        ccs_[i]=cc;
    }
    const auto learn=p_->midiLearnTarget.load();
    const bool refreshToolbar=lastLearn_!=learn;
    lastLearn_=learn;
    const bool full=dirty_ || (popup_!=PopupKind::None && (changed || refreshStrip || refreshToolbar));
    dirty_=false;
    c_.begin(frame_.data(),physWidth(),physHeight(),scale_);
    Rect damage;
    if (full)
    {
        damage={0,0,w_,h_};
        c_.fill(damage,ground);
        paintToolbar();
        paintStrip();
        c_.blitSharp(bodyPixels_.data(),bw,bh,body_);
        if (popup_!=PopupKind::None) drawPopup();
        previousBody_=bodyPixels_;
    }
    else
    {
        if (refreshStrip) { paintStrip(); damage=strip_; }
        if (refreshToolbar)
        {
            paintToolbar();
            damage=damage.unite({0,0,w_,items_[0].y+items_[0].h+5});
        }
        if (changed)
        {
            // Compare small source tiles before scaling. Crest, face and storm normally
            // change independently; never rescale the undamaged score just to animate them.
            for(int sy=0;sy<bh;sy+=24)
                for(int sx=0;sx<bw;sx+=64)
                {
                    const int width=std::min(64,bw-sx), height=std::min(24,bh-sy);
                    bool tileChanged=false;
                    for(int y=sy;y<sy+height;++y)
                    {
                        const auto off=size_t(y)*bw+sx;
                        const auto bytes=size_t(width)*sizeof(uint32_t);
                        if (!std::memcmp(bodyPixels_.data()+off,previousBody_.data()+off,bytes)) continue;
                        std::memcpy(previousBody_.data()+off,bodyPixels_.data()+off,bytes);
                        tileChanged=true;
                    }
                    if (!tileChanged) continue;
                    const int x0=body_.x+sx*body_.w/bw, y0=body_.y+sy*body_.h/bh;
                    const int x1=body_.x+((sx+width)*body_.w+bw-1)/bw;
                    const int y1=body_.y+((sy+height)*body_.h+bh-1)/bh;
                    const Rect patch{x0-2,y0-2,x1-x0+4,y1-y0+4};
                    c_.setClip(patch.intersect(body_));
                    c_.blitSharp(bodyPixels_.data(),bw,bh,body_);
                    damage=damage.unite(patch.intersect(body_));
                }
            c_.resetClip();
        }
    }
    renderMs_=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
    return damage;
}

double PanelUI::norm (int param) const
{
    if (param < 0 || param >= int (p_->params.size ())) return 0.0;
    const auto &pr = p_->params[(size_t)param];
    const double r = pr.range ();
    return r > 0.0 ? std::clamp ((p_->displayValue (param) - pr.minVal) / r, 0.0, 1.0) : 0.0;
}

double PanelUI::fromNorm (int param, double n) const
{
    if (param < 0 || param >= int (p_->params.size ())) return 0.0;
    const auto &pr = p_->params[(size_t)param];
    n = std::clamp (n, 0.0, 1.0);
    double v = pr.minVal + n * pr.range ();
    if (pr.isStepped ()) v = std::clamp (std::round (v), pr.minVal, pr.maxVal);
    return v;
}

void PanelUI::setParam (int param, double v)
{
    if (param < 0 || param >= int (p_->params.size ())) return;
    p_->setEdit (param, v);
    lastTouched_ = param;
}

void PanelUI::stepParam (int param, int steps)
{
    if (param < 0 || param >= int (p_->params.size ()) || steps == 0) return;
    if (wheelParam_ != param)
    {
        endWheelGesture ();
        wheelParam_ = param;
        p_->beginEdit (param);
    }
    wheelAt_ = nowMs ();
    const auto &pr = p_->params[(size_t)param];
    if (pr.isStepped ())
    {
        setParam (param, std::clamp (p_->displayValue (param) + double (steps), pr.minVal, pr.maxVal));
    }
    else
    {
        setParam (param, fromNorm (param, norm (param) + double (steps) / 160.0));
    }
}

void PanelUI::endWheelGesture ()
{
    if (wheelParam_ >= 0)
    {
        p_->endEdit (wheelParam_);
        wheelParam_ = -1;
    }
}

void PanelUI::resetParam (int param)
{
    if (param < 0 || param >= int (p_->params.size ())) return;
    endWheelGesture ();
    p_->setValueOnce (param, p_->params[(size_t)param].defVal);
    lastTouched_ = param;
}

std::string PanelUI::valueText (int param) const
{
    if (param < 0 || param >= int (p_->params.size ())) return "";
    const auto &pr = p_->params[(size_t)param];
    return p_->formatParamValue (pr.id, p_->displayValue (param));
}

void PanelUI::openPresets ()
{
    if (p_->presets.empty ()) return;
    popup_ = PopupKind::Presets;
    popupParam_ = -1;
    popupItems_.clear ();
    popupValues_.clear ();
    for (size_t i = 0; i < p_->presets.size (); ++i)
    {
        char num[12];
        std::snprintf (num, sizeof (num), "%02zu. ", i + 1);
        popupItems_.push_back (std::string (num) + p_->presets[i].name);
        popupValues_.push_back (double (i));
    }
    popupSelected_ = p_->currentPresetIndex;
    layoutPopup (items_[(size_t)Item::PresetBox]);
}

void PanelUI::openChoices (int param, const Rect &anchor)
{
    if (param < 0 || param >= int (p_->params.size ())) return;
    const auto &pr = p_->params[(size_t)param];
    const int count = int (std::round (pr.range ())) + 1;
    if (count < 2 || count > tbmpc::scaleCount) return;

    popup_ = PopupKind::Choices;
    popupParam_ = param;
    popupItems_.clear ();
    popupValues_.clear ();
    const int cur = int (std::round (p_->displayValue (param)));
    popupSelected_ = -1;
    for (int i = 0; i < count; ++i)
    {
        const double v = pr.minVal + double (i);
        popupItems_.push_back (p_->formatParamValue (pr.id, v));
        popupValues_.push_back (v);
        if (int (std::round (v)) == cur) popupSelected_ = i;
    }
    layoutPopup (anchor);
}

void PanelUI::openMenu (int param, float x, float y)
{
    if (param < 0 || param >= int (p_->params.size ())) return;
    const auto &pr = p_->params[(size_t)param];
    popup_ = PopupKind::Menu;
    popupParam_ = param;
    popupItems_.clear ();
    popupValues_.clear ();

    popupItems_.push_back ("RESET TO DEFAULT (" + p_->formatParamValue (pr.id, pr.defVal) + ")");
    popupValues_.push_back (0);

    const int cc = p_->findCcForParam (pr.id);
    if (cc >= 0)
    {
        char buf[48];
        std::snprintf (buf, sizeof (buf), "CLEAR MIDI CC %d", cc);
        popupItems_.push_back (buf);
        popupValues_.push_back (2);
    }
    popupItems_.push_back ("MIDI LEARN CC FOR " + upper (pr.name));
    popupValues_.push_back (1);

    if (tbmpc::paramTable[tbmpc::paramIndex (pr.id)].kind == tbmpc::Kind::choice)
    {
        popupItems_.push_back ("CHOOSE VALUE FROM LIST...");
        popupValues_.push_back (3);
    }
    popupSelected_ = -1;
    layoutPopup ({int (x), int (y), 12, 12});
}

void PanelUI::closePopup ()
{
    popup_ = PopupKind::None;
    popupParam_ = -1;
    popupItems_.clear ();
    popupValues_.clear ();
    popupRects_.clear ();
    popupHover_ = -1;
    dirty_ = true;
}

void PanelUI::popupPick (int i)
{
    if (i < 0 || i >= int (popupValues_.size ()))
    {
        closePopup ();
        return;
    }
    const PopupKind k = popup_;
    const int param = popupParam_;
    const double val = popupValues_[(size_t)i];
    const Rect anchor = popupRect_;
    closePopup ();

    if (k == PopupKind::Presets)
    {
        p_->loadPresetByIndex (int (val));
    }
    else if (k == PopupKind::Choices && param >= 0)
    {
        p_->setValueOnce (param, val);
        lastTouched_ = param;
    }
    else if (k == PopupKind::Menu && param >= 0)
    {
        const auto &pr = p_->params[(size_t)param];
        if (int (val) == 0)
            resetParam (param);
        else if (int (val) == 1)
            p_->midiLearnTarget.store (pr.id, std::memory_order_release);
        else if (int (val) == 2)
            p_->clearCcForParam (pr.id);
        else if (int (val) == 3)
            openChoices (param, {anchor.x, anchor.y, 20, 20});
    }
}

void PanelUI::layoutPopup (const Rect &anchor)
{
    const int n = int (popupItems_.size ());
    if (!n)
    {
        closePopup ();
        return;
    }
    const int rows = std::min (n, std::min (12, (h_ - 40) / 25));
    const int width = std::min (w_ - 16, 292), height = rows * 25 + 12;
    popupFirst_ = std::clamp (popupSelected_ - rows / 2, 0, n - rows);
    const int x = std::clamp (anchor.x, 8, w_ - width - 8);
    const int y =
        std::clamp (anchor.y + anchor.h + 3 <= h_ - height - 8 ? anchor.y + anchor.h + 3 : anchor.y - height - 3,
                    8, h_ - height - 8);
    popupRect_ = {x, y, width, height};
    popupRects_.resize (rows);
    for (int i = 0; i < rows; ++i)
        popupRects_[i] = {x + 5, y + 6 + i * 25, width - 10, 25};
    popupHover_ = -1;
    dirty_ = true;
}

void PanelUI::drawPopup ()
{
    plate (c_, popupRect_, 0xff191526);
    for (size_t i = 0; i < popupRects_.size (); ++i)
    {
        const auto r = popupRects_[i];
        const int index = popupFirst_ + int (i);
        if (index == popupHover_ || index == popupSelected_) c_.fill (r, 0xff374073);
        c_.textAligned ({r.x + 8, r.y, r.w - 23, r.h},popupItems_[index],light,1,0);
    }
    if (popupRects_.size () < popupItems_.size ())
    {
        const int scale = popupRect_.h - 12;
        c_.fill (popupRect_.x + popupRect_.w - 5,
                 popupRect_.y + 6 + scale * popupFirst_ / int (popupItems_.size ()), 2,
                 std::max (8, scale * int (popupRects_.size ()) / int (popupItems_.size ())), lilac);
    }
}

void PanelUI::edit(int param,Rect anchor,int button,uint32_t mods,int clicks)
{
    if(param<0) return;
    const auto& pr=p_->params[size_t(param)];
    hover_=lastTouched_=param;
    if (p_->midiLearnTarget.load()==DesktopPlugin::kLearnArmedNoParam)
    {
        p_->midiLearnTarget.store(pr.id);
        return;
    }
    if (button==1) { openMenu(param,downX_,downY_); return; }
    if (clicks>=2 || (mods&(kModAlt|kModCmd))) { resetParam(param); return; }
    dragParam_=param;
    dragRect_=anchor;
    start_=norm(param);
}

void PanelUI::finishDrag()
{
    if (moved_ && dragParam_>=0) p_->endEdit(dragParam_);
    dragParam_=-1;
    moved_=false;
    if (bodyDrag_)
    {
        p_->panel->screen->touch(p_->inner,SIXTEEN_TOUCH_UP,0,0);
        bodyDrag_=false;
    }
}

void PanelUI::mouseDown(float x,float y,int buttonId,uint32_t mods,int clicks)
{
    finishDrag();
    endWheelGesture();
    downX_=x; downY_=y;
    if (popup_!=PopupKind::None)
    {
        for(size_t i=0;i<popupRects_.size();++i)
            if (popupRects_[i].contains(x,y)) { popupPick(popupFirst_+int(i)); return; }
        closePopup();
        return;
    }
    if (!visible_) return;
    for (size_t i = 0; i < size_t (Item::Count); ++i)
    {
        if (!items_[i].contains (x, y)) continue;
        dirty_=true;
        switch (Item (i))
        {
        case Item::PresetPrev:
        case Item::PresetNext:
            if (!p_->presets.empty ())
            {
                const int n = int (p_->presets.size ());
                const int index = p_->currentPresetIndex < 0
                                      ? (Item (i) == Item::PresetNext ? 0 : n - 1)
                                      : (p_->currentPresetIndex + (Item (i) == Item::PresetNext ? 1 : n - 1)) % n;
                p_->loadPresetByIndex (index);
            }
            break;
        case Item::PresetBox:
            openPresets ();
            break;
        case Item::Fine:
            fine_ = !fine_;
            break;
        case Item::Learn:
            p_->midiLearnTarget.store (p_->midiLearnTarget.load () == CLAP_INVALID_ID
                                           ? DesktopPlugin::kLearnArmedNoParam
                                           : CLAP_INVALID_ID);
            break;
        case Item::ClearCc:
            if (lastTouched_ >= 0 && p_->findCcForParam (p_->params[lastTouched_].id) >= 0)
                p_->clearCcForParam (p_->params[lastTouched_].id);
            else
                p_->clearAllCc ();
            break;
        case Item::Motion:
            motion_ = !motion_;
            p_->panel->holdMotion(p_->inner,!motion_);
            break;
        default:
            break;
        }
        return;
    }
    const int card=hitCard(x,y);
    if (card>=0)
    {
        for(int i=0;i<6;++i)
            if (cards_[i].contains(x,y)) { edit(card,cards_[i],buttonId,mods,clicks); break; }
        dirty_=true;
        return;
    }
    if (!body_.contains(x,y)) return;
    const int tx=bodyX(x), ty=bodyY(y);
    if (buttonId==0 && (mods&kModShift) && tx>=622 && tx<703 && ty>=55 && ty<84 &&
        !p_->panel->localPlay(p_->inner) && p_->hostTransport && p_->hostTransport->request_toggle_play)
    {
        p_->hostTransport->request_toggle_play(p_->realHost);
        return;
    }
    if (buttonId==0 && (mods&kModShift) && tx>=186 && tx<518 && ty>=6 && ty<96)
    {
        p_->panel->cameraCut(p_->inner);
        return;
    }
    const int param=bodyParam(tx,ty);
    if (param>=0 && (buttonId==1 || p_->midiLearnTarget.load()==DesktopPlugin::kLearnArmedNoParam ||
                     clicks>=2 || (mods&(kModAlt|kModCmd))))
    {
        edit(param,{int(x),int(y),2,2},buttonId,mods,clicks);
        return;
    }
    if (buttonId==0)
    {
        bodyDrag_=p_->panel->screen->touch(p_->inner,SIXTEEN_TOUCH_DOWN,tx,ty);
        // Some shared controls capture first and only write on move.
        if (param>=0) lastTouched_=param;
    }
}

void PanelUI::mouseMove(float x,float y,uint32_t mods)
{
    if (popup_!=PopupKind::None)
    {
        int next=-1;
        for(size_t i=0;i<popupRects_.size();++i)
            if(popupRects_[i].contains(x,y)) next=popupFirst_+int(i);
        dirty_|=popupHover_!=next;
        popupHover_=next;
        return;
    }
    if (bodyDrag_)
    {
        p_->panel->screen->touch(p_->inner,SIXTEEN_TOUCH_MOVE,bodyX(x),bodyY(y));
        return;
    }
    if (dragParam_>=0)
    {
        const float dx=x-downX_,dy=downY_-y;
        if (!moved_ && std::abs(dx)+std::abs(dy)<3) return;
        if (!moved_) { p_->beginEdit(dragParam_); moved_=true; }
        const auto& pr=p_->params[size_t(dragParam_)];
        const double sensitivity=(mods&kModShift || fine_) ? .12 : 1.;
        if (pr.isStepped() && pr.range()<120)
            setParam(dragParam_,fromNorm(dragParam_,start_)+std::round((dy+dx*.25)*sensitivity/8.));
        else
            setParam(dragParam_,fromNorm(dragParam_,start_+(dy+dx*.25)*sensitivity/130.));
        return;
    }
    const int next=hitCard(x,y);
    if (hover_!=next)
    {
        hover_=next;
        // Invalidate the controls without decoding or repainting the shared artwork.
        stripDirty_=true;
    }
}

void PanelUI::mouseUp(float,float,int,uint32_t)
{
    if (dragParam_>=0 && !moved_)
    {
        const auto& pr=p_->params[size_t(dragParam_)];
        if (tbmpc::paramTable[tbmpc::paramIndex(pr.id)].kind==tbmpc::Kind::choice)
            openChoices(dragParam_,dragRect_);
    }
    finishDrag();
    stripDirty_=true;
}

void PanelUI::wheel(float x,float y,float,float dy,bool precise,uint32_t mods)
{
    if (std::abs(dy)<.0001f) return;
    wheelAccum_+=dy*(precise?.15:1.);
    const int steps=int(wheelAccum_);
    if (!steps) return;
    wheelAccum_-=steps;
    if (popup_!=PopupKind::None)
    {
        popupFirst_=std::clamp(popupFirst_-steps,0,int(popupItems_.size()-popupRects_.size()));
        popupHover_=-1;
        dirty_=true;
        return;
    }
    int param=hitCard(x,y);
    if (param<0 && body_.contains(x,y)) param=bodyParam(bodyX(x),bodyY(y));
    if (param<0) return;
    lastTouched_=param;
    if ((fine_ || (mods&kModShift)) && !p_->params[param].isStepped())
    {
        if (wheelParam_!=param) { endWheelGesture(); wheelParam_=param; p_->beginEdit(param); }
        wheelAt_=nowMs();
        setParam(param,fromNorm(param,norm(param)+steps/1600.));
    }
    else stepParam(param,steps);
}

void PanelUI::mouseExit() { hover_=-1; stripDirty_=true; }

bool PanelUI::key(int k)
{
    if (popup_ != PopupKind::None)
    {
        if (k == 5)
            closePopup ();
        else if (k == 4)
            popupPick (popupHover_ >= 0 ? popupHover_ : std::max (0, popupSelected_));
        else if (k == 2 || k == 3)
        {
            popupHover_ = std::clamp ((popupHover_ >= 0 ? popupHover_ : popupSelected_) + (k == 2 ? -1 : 1), 0,
                                      int (popupItems_.size ()) - 1);
            popupFirst_ =
                std::clamp (popupFirst_, std::max (0, popupHover_ - int (popupRects_.size ()) + 1), popupHover_);
        }
        else
            return false;
        dirty_=true;
        return true;
    }
    if (k==0 || k==1) return p_->panel->screen->selector_turn(p_->inner,k==0?-1:1);
    if ((k==2 || k==3) && lastTouched_>=0) { stepParam(lastTouched_,k==2?1:-1); return true; }
    if (k==5) { finishDrag(); return true; }
    return false;
}
}
