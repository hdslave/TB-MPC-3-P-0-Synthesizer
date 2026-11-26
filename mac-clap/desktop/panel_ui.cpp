#include "panel_ui.h"
#include <chrono>
#include <cstdio>

namespace hdslave_desktop
{
using Lane = tbmpc::sp16::StepLane;
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
    const double value=p_->displayValue(param);
    if (pr.id==id::pitchLfoAmount || pr.id==id::cutoffLfoAmount)
    {
        char text[24];
        const bool pitch=pr.id==id::pitchLfoAmount;
        std::snprintf(text,sizeof(text),pitch?"%.2f st":"%.2f oct",value*(pitch?12*tbmpc::pitchLfoOctaves:tbmpc::cutoffLfoOctaves));
        return text;
    }
    return p_->formatParamValue(pr.id,value);
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
    layoutPopup (layout_.tools[(size_t)Item::PresetBox]);
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

    popupItems_.push_back ("Reset to default (" + p_->formatParamValue (pr.id, pr.defVal) + ")");
    popupValues_.push_back (0);

    const int cc = p_->findCcForParam (pr.id);
    if (cc >= 0)
    {
        char buf[48];
        std::snprintf (buf, sizeof (buf), "Clear MIDI CC %d", cc);
        popupItems_.push_back (buf);
        popupValues_.push_back (2);
    }
    popupItems_.push_back ("Learn MIDI CC for " + pr.name);
    popupValues_.push_back (1);

    if (tbmpc::paramTable[tbmpc::paramIndex (pr.id)].kind == tbmpc::Kind::choice)
    {
        popupItems_.push_back ("Choose from list...");
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

void PanelUI::edit(int param,Rect anchor,int button,uint32_t mods,int clicks)
{
    if(param<0) return;
    const auto& pr=p_->params[size_t(param)];
    lastTouched_=param;
    if (p_->midiLearnTarget.load() == DesktopPlugin::kLearnArmedNoParam)
    {
        p_->midiLearnTarget.store(pr.id);
        return;
    }
    if (button == 1)
    {
        openMenu(param, downX_, downY_);
        return;
    }
    if (clicks >= 2 || (mods & (kModAlt | kModCmd)))
    {
        resetParam(param);
        return;
    }
    dragParam_ = param;
    dragRect_ = anchor;
    start_ = norm(param);
}

PanelUI::PanelUI(DesktopPlugin* p)
    : p_(p), art_(SIXTEEN_SCREEN_WIDTH * SIXTEEN_SCREEN_HEIGHT), previousArt_(art_.size())
{
    for (uint32_t i = 0; i < id::count; ++i)
        paramIndex_[i] = p_->findParamIndex(i);
    setSize(w_, h_, 1);
    setVisible(true);
}
PanelUI::~PanelUI()
{
    setVisible(false);
}

void PanelUI::setVisible(bool visible)
{
    if (visible_ == visible) return;
    visible_ = visible;
    if (visible)
    {
        p_->panel->open(p_->inner);
        p_->panel->holdMotion(p_->inner, !motion_);
        dirty_ = true;
    }
    else
    {
        finishDrag();
        endWheelGesture();
        const bool playing = p_->panel->localPlay(p_->inner);
        p_->panel->close(p_->inner);
        if (playing && p_->realHost->request_process) p_->realHost->request_process(p_->realHost);
        closePopup();
    }
}

void PanelUI::setSize(int w, int h, int scale)
{
    w = std::clamp(w, int(kMinGuiWidth), int(kMaxGuiWidth));
    h = std::clamp(h, int(kMinGuiHeight), int(kMaxGuiHeight));
    scale = std::clamp(scale, 1, 3);
    if (w_ == w && h_ == h && scale_ == scale && !frame_.empty()) return;
    finishDrag();
    w_ = w;
    h_ = h;
    scale_ = scale;
    frame_.resize(size_t(physWidth()) * physHeight());
    layout();
    closePopup();
    dirty_ = true;
}

int PanelUI::focus() const
{
    return std::clamp(pattern_.running ? int(pattern_.current) : selected_, 0,
                      std::max(0, int(pattern_.steps) - 1));
}

Rect PanelUI::update(uint64_t now)
{
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    if (wheelParam_ >= 0 && now - wheelAt_ > 175) endWheelGesture();
    if (!visible_) return {};
    const auto next = p_->panel->readPattern(p_->inner);
    const bool patternChanged = !(pattern_ == next);
    pattern_ = next;
    selected_ = std::clamp(selected_, 0, std::max(0, int(pattern_.steps) - 1));
    const bool local = p_->panel->localPlay(p_->inner), playChanged = local != localPlay_;
    localPlay_ = local;
    const uint32_t learn = p_->midiLearnTarget.load();
    const bool full = dirty_;
    std::array<bool, id::count> changed{};
    for (uint32_t id = 0; id < changed.size(); ++id)
    {
        auto& reading = readings_[id];
        const double value = p_->displayValue(paramIndex_[id]);
        const int cc = p_->findCcForParam(id);
        changed[id] = full || value != reading.value || cc != reading.cc;
        reading.value = value;
        reading.cc = cc;
    }
    changed[id::cutoff] |= changed[id::resonance] || changed[id::filter] || changed[id::peakSpread];
    for (uint32_t id = 0; id < changed.size(); ++id)
        if (changed[id]) readings_[id].text = valueText(paramIndex_[id]);

    const bool focusChanged = focus() != paintedFocus_;
    const bool hoverChanged = hover_ != paintedHover_,
               stepHoverChanged = hoverStep_ != paintedHoverStep_ || hoverLane_ != paintedHoverLane_;
    const auto hoveredHeader = [](int slot) { return slot == int(id::seedBack) || slot == int(id::regen); };
    const bool header = full || focusChanged || playChanged || patternChanged || changed[id::seed] ||
                        changed[id::mode] ||
                        (hoverChanged && (hoveredHeader(hover_) || hoveredHeader(paintedHover_)));
    const bool score =
        full || patternChanged || focusChanged || stepHoverChanged || changed[id::scale] || changed[id::root];
    const bool toolbar = full || lastLearn_ != learn || paintedPreset_ != p_->currentPresetIndex || hoverChanged ||
                         stepHoverChanged || (hover_ >= 0 && hover_ < int(id::count) && changed[hover_]);
    lastLearn_ = learn;
    c_.begin(frame_.data(), physWidth(), physHeight(), scale_);
    Rect damage;
    if (full)
    {
        damage = {0, 0, w_, h_};
        c_.fill(damage, 0xff090910);
    }
    if (toolbar)
    {
        paintToolbar();
        damage = damage.unite(layout_.toolbar);
    }
    if (header)
    {
        paintHeader();
        damage = damage.unite(layout_.brand.unite(layout_.slots[id::regen]));
    }
    if (score)
    {
        paintScore();
        damage = damage.unite(layout_.score);
    }
    damage =
        damage.unite(paintArt(full || focusChanged || patternChanged || changed[id::seed], header, score, now));
    for (size_t i = 0; i < std::size(sections); ++i)
    {
        if (full)
        {
            paintSection(i);
            continue;
        }
        const auto& group = sections[i];
        for (int j = 0; j < group.columns * 2; ++j)
        {
            const int slot = group.slots[j];
            bool repaint = hoverChanged && (slot == hover_ || slot == paintedHover_);
            if (slot < int(id::count))
                repaint |= changed[slot];
            else if (slot == filterPlot)
                repaint |= changed[id::cutoff];
            else if (slot == pitchPlot)
                repaint |= changed[id::pitchLfoAmount] || changed[id::pitchLfoSpeed];
            else
                repaint |= changed[id::cutoffLfoAmount] || changed[id::cutoffLfoSpeed];
            if (repaint)
            {
                paintSlot(slot, group.ink);
                damage = damage.unite(layout_.slots[slot]);
            }
        }
    }
    if (popup_ != PopupKind::None && !damage.intersect(popupRect_).empty()) drawPopup();
    paintedFocus_ = focus();
    paintedPreset_ = p_->currentPresetIndex;
    paintedHover_ = hover_;
    paintedHoverStep_ = hoverStep_;
    paintedHoverLane_ = hoverLane_;
    dirty_ = false;
    renderMs_ = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return damage;
}

void PanelUI::finishDrag()
{
    if (moved_ && dragParam_ >= 0) p_->endEdit(dragParam_);
    dragParam_ = -1;
    dragPlot_ = -1;
    moved_ = false;
    if (startStep_ >= 0) p_->panel->endStep(p_->inner);
    startStep_ = -1;
}

void PanelUI::setHover(float x, float y)
{
    hover_ = layout_.hitSlot(x, y);
    hoverStep_ = hoverLane_ = -1;
    if (!layout_.grid.contains(x, y)) return;
    const int step = layout_.stepAt(int(x), columns());
    if (step < 0 || step >= pattern_.steps) return;
    for (int i = 0; i < 5; ++i)
        if (layout_.lane(i).contains(x, y))
        {
            hoverStep_ = step;
            hoverLane_ = i;
            return;
        }
}

void PanelUI::movePlot(float x, float y)
{
    const Rect plot = layout_.plotArea(dragPlot_);
    const double u = std::clamp(double(x - plot.x) / (plot.w - 1), 0., 1.);
    const double v = 1 - std::clamp(double(y - plot.y) / (plot.h - 1), 0., 1.);
    const bool filter = dragPlot_ == filterPlot, pitch = dragPlot_ == pitchPlot;
    const int horizontal = filter ? id::cutoff : pitch ? id::pitchLfoSpeed : id::cutoffLfoSpeed;
    const int vertical = filter ? id::resonance : pitch ? id::pitchLfoAmount : id::cutoffLfoAmount;
    const double rate = tbmpc::lfoMinRate * std::pow(tbmpc::lfoMaxRate / tbmpc::lfoMinRate, u);
    p_->setValueOnce(paramIndex_[horizontal], filter ? u : rate);
    p_->setValueOnce(paramIndex_[vertical], v);
    lastTouched_ = paramIndex_[horizontal];
}

void PanelUI::mouseDown(float x, float y, int buttonId, uint32_t mods, int clicks)
{
    finishDrag();
    endWheelGesture();
    downX_ = x;
    downY_ = y;
    if (popup_ != PopupKind::None)
    {
        for (size_t i = 0; i < popupRects_.size(); ++i)
            if (popupRects_[i].contains(x, y))
            {
                popupPick(popupFirst_ + int(i));
                return;
            }
        closePopup();
        return;
    }
    if (!visible_) return;
    setHover(x, y);
    for (size_t i = 0; i < layout_.tools.size(); ++i)
    {
        if (!layout_.tools[i].contains(x, y)) continue;
        dirty_ = true;
        switch (Item(i))
        {
        case Item::PresetPrev:
        case Item::PresetNext:
        {
            const int n = int(p_->presets.size());
            if (n)
            {
                const int cur = p_->currentPresetIndex;
                const bool next = Item(i) == Item::PresetNext;
                const int index = cur < 0 ? (next ? 0 : n - 1) : (cur + (next ? 1 : n - 1)) % n;
                p_->loadPresetByIndex(index);
            }
            break;
        }
        case Item::PresetBox:
            openPresets();
            break;
        case Item::Fine:
            fine_ = !fine_;
            break;
        case Item::Learn:
            p_->midiLearnTarget.store(p_->midiLearnTarget.load() == CLAP_INVALID_ID
                                          ? DesktopPlugin::kLearnArmedNoParam
                                          : CLAP_INVALID_ID);
            break;
        case Item::ClearCc:
            if (lastTouched_ >= 0 && p_->findCcForParam(p_->params[lastTouched_].id) >= 0)
                p_->clearCcForParam(p_->params[lastTouched_].id);
            else
                p_->clearAllCc();
            break;
        case Item::Motion:
            motion_ = !motion_;
            p_->panel->holdMotion(p_->inner, !motion_);
            break;
        default:
            break;
        }
        return;
    }
    if (layout_.play.contains(x, y) && buttonId == 0)
    {
        if ((mods & kModShift) && !p_->panel->localPlay(p_->inner) && p_->hostTransport &&
            p_->hostTransport->request_toggle_play)
            p_->hostTransport->request_toggle_play(p_->realHost);
        else
            p_->panel->togglePlay(p_->inner);
        return;
    }
    if (layout_.crest.contains(x, y) && buttonId == 0)
    {
        p_->panel->camera(p_->inner, bool(mods & kModShift));
        return;
    }
    if (hover_ >= 0 && hover_ < int(id::count))
    {
        const bool special = buttonId != 0 || clicks >= 2 || (mods & (kModCmd | kModAlt)) ||
                             p_->midiLearnTarget.load() == DesktopPlugin::kLearnArmedNoParam;
        if (!special && (hover_ == int(id::regen) || hover_ == int(id::seedBack)))
        {
            p_->panel->press(p_->inner, hover_);
            lastTouched_ = paramIndex_[hover_];
            return;
        }
        edit(paramIndex_[hover_], layout_.slots[hover_], buttonId, mods, clicks);
        return;
    }
    if (buttonId != 0) return;
    if (hover_ >= filterPlot)
    {
        dragPlot_ = hover_;
        movePlot(x, y);
        return;
    }
    if (hoverStep_ >= 0)
    {
        dragLane_ = Lane(hoverLane_);
        if (p_->panel->beginStep(p_->inner, hoverStep_, dragLane_)) startStep_ = selected_ = hoverStep_;
    }
}

void PanelUI::mouseMove(float x, float y, uint32_t mods)
{
    if (popup_ != PopupKind::None)
    {
        int next = -1;
        for (size_t i = 0; i < popupRects_.size(); ++i)
            if (popupRects_[i].contains(x, y)) next = popupFirst_ + int(i);
        dirty_ |= popupHover_ != next;
        popupHover_ = next;
        return;
    }
    if (dragPlot_ >= 0)
    {
        movePlot(x, y);
        return;
    }
    if (startStep_ >= 0)
    {
        const int step = dragLane_ == Lane::pitch ? startStep_ : layout_.stepAt(int(x), columns());
        if (step < 0 || step >= pattern_.steps || (dragLane_ == Lane::number && !layout_.lane(0).contains(x, y)))
            return;
        selected_ = step;
        p_->panel->moveStep(p_->inner, step, int(std::round((downY_ - y) / 7.f)));
        return;
    }
    if (dragParam_ >= 0)
    {
        const float dx = x - downX_, dy = downY_ - y;
        if (!moved_ && std::abs(dx) + std::abs(dy) < 3) return;
        if (!moved_)
        {
            p_->beginEdit(dragParam_);
            moved_ = true;
        }
        const auto& pr = p_->params[size_t(dragParam_)];
        const double sensitivity = (mods & kModShift || fine_) ? .12 : 1.;
        if (pr.isStepped() && pr.range() < 120)
            setParam(dragParam_, fromNorm(dragParam_, start_) + std::round((dy + dx * .25) * sensitivity / 8.));
        else
            setParam(dragParam_, fromNorm(dragParam_, start_ + (dy + dx * .5) * sensitivity / 130.));
        return;
    }
    setHover(x, y);
}

void PanelUI::mouseUp(float x, float y, int, uint32_t)
{
    if (dragParam_ >= 0 && !moved_)
    {
        const auto& pr = p_->params[size_t(dragParam_)];
        if (pr.id == id::glide && dragRect_.contains(x, y))
            p_->setValueOnce(dragParam_, x < dragRect_.x + dragRect_.w / 2 ? 0 : 1);
        else if (tbmpc::paramTable[tbmpc::paramIndex(pr.id)].kind == tbmpc::Kind::choice)
            openChoices(dragParam_, dragRect_);
    }
    finishDrag();
    setHover(x, y);
}

void PanelUI::wheel(float x, float y, float, float dy, bool precise, uint32_t mods)
{
    if (std::abs(dy) < .0001f) return;
    wheelAccum_ += dy * (precise ? .15 : 1.);
    const int steps = int(wheelAccum_);
    if (!steps) return;
    wheelAccum_ -= steps;
    if (popup_ != PopupKind::None)
    {
        popupFirst_ = std::clamp(popupFirst_ - steps, 0, int(popupItems_.size() - popupRects_.size()));
        popupHover_ = -1;
        dirty_ = true;
        return;
    }
    const int slot = layout_.hitSlot(x, y);
    if (slot < 0 || slot >= int(id::count) || slot == int(id::regen) || slot == int(id::seedBack)) return;
    const int param = paramIndex_[slot];
    if (param < 0) return;
    lastTouched_ = param;
    if ((fine_ || (mods & kModShift)) && !p_->params[param].isStepped())
    {
        if (wheelParam_ != param)
        {
            endWheelGesture();
            wheelParam_ = param;
            p_->beginEdit(param);
        }
        wheelAt_ = nowMs();
        setParam(param, fromNorm(param, norm(param) + steps / 1600.));
    }
    else
        stepParam(param, steps);
}
void PanelUI::mouseExit()
{
    hover_ = hoverStep_ = hoverLane_ = -1;
}

bool PanelUI::key(int k)
{
    if (popup_ != PopupKind::None)
    {
        if (k == 5)
            closePopup();
        else if (k == 4)
            popupPick(popupHover_ >= 0 ? popupHover_ : std::max(0, popupSelected_));
        else if (k == 2 || k == 3)
        {
            popupHover_ = std::clamp((popupHover_ >= 0 ? popupHover_ : popupSelected_) + (k == 2 ? -1 : 1), 0,
                                     int(popupItems_.size()) - 1);
            popupFirst_ =
                std::clamp(popupFirst_, std::max(0, popupHover_ - int(popupRects_.size()) + 1), popupHover_);
        }
        else
            return false;
        dirty_ = true;
        return true;
    }
    if ((k == 0 || k == 1) && !pattern_.running && pattern_.steps > 0)
    {
        selected_ = std::clamp(selected_ + (k == 0 ? -1 : 1), 0, int(pattern_.steps) - 1);
        return true;
    }
    if ((k == 2 || k == 3) && lastTouched_ >= 0)
    {
        stepParam(lastTouched_, k == 2 ? 1 : -1);
        return true;
    }
    if (k == 5)
    {
        finishDrag();
        return true;
    }
    return false;
}
} // namespace hdslave_desktop
