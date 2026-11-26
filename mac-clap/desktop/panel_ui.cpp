#include "panel_ui.h"
#include "acid_motion.h"
#include "../src/params.h"
#include <chrono>
#include <cstdio>

namespace hdslave_desktop
{
namespace
{
namespace id = tbmpc::ParamId;
constexpr uint32_t ground = 0xff090810, glass = 0xff0d0c16, lineInk = 0xff423c54;
constexpr uint32_t light = 0xfff0edf9, muted = 0xffb2adc4, quiet = 0xff858298, cobalt = 0xff6688ff,
                   lilac = 0xffac99f1;
struct Field
{
    clap_id id;
    const char *name;
};
constexpr Field voice[] = {{id::cutoff, "Cutoff"},
                           {id::resonance, "Resonance"},
                           {id::envMod, "Env mod"},
                           {id::decay, "Decay"},
                           {id::accent, "Accent"},
                           {id::waveform, "Waveform"},
                           {id::tuning, "Tuning"},
                           {id::volume, "Volume"},
                           {id::cutoffLfoAmount, "Cutoff LFO"},
                           {id::cutoffLfoSpeed, "LFO speed"}};
constexpr Field tone[] = {{id::filter, "Filter"},
                          {id::peakSpread, "Peak spread"},
                          {id::drive, "Drive"},
                          {id::glide, "Slide"},
                          {id::slideTime, "Slide time"},
                          {id::accentDecay, "Accent decay"},
                          {id::pitchLfoAmount, "Pitch LFO"},
                          {id::pitchLfoSpeed, "LFO speed"}};
constexpr Field sequence[] = {{id::mode, "Mode"},        {id::seedMode, "Seed mode"}, {id::seedBack, "Seed back"},
                              {id::mutate, "Mutate"},    {id::density, "Density"},    {id::scale, "Scale"},
                              {id::root, "Root"},        {id::octave, "Octave"},      {id::steps, "Steps"},
                              {id::gate, "Gate length"}, {id::transpose, "Transpose"}};
constexpr Field clockFields[] = {{id::clockDiv, "Division"}, {id::swing, "Swing"}, {id::reset, "Reset"}};
struct ControlGroup
{
    const char *name;
    const Field *fields;
    int count;
};
constexpr ControlGroup groups[] = {
    {"Voice", voice, 10}, {"Tone", tone, 8}, {"Sequencer", sequence, 11}, {"Clock", clockFields, 3}};
std::string upper (std::string s)
{
    for (auto &c : s)
        if (c >= 'a' && c <= 'z') c -= 32;
    return s;
}
std::string number (const char *fmt, int n)
{
    char buf[24];
    std::snprintf (buf, sizeof (buf), fmt, n);
    return buf;
}
std::string noteName (int pitch)
{
    const int midi = tbmpc::noteAtZeroVolts + int (std::lround (pitch / 128.0));
    return std::string (tbmpc::rootChoices[(midi % 12 + 12) % 12]) +
           std::to_string (int (std::floor (midi / 12.0)) - 1);
}
void plate (Canvas &c, Rect r, uint32_t fill = glass)
{
    c.fillChamfer (r, 6, lineInk);
    c.fillChamfer (r.inset (1), 5, fill);
}
void button (Canvas &c, Rect r, std::string_view name, bool active = false)
{
    if (r.empty ()) return;
    c.fillChamfer (r, 3, active ? 0xff6579c2 : lineInk);
    c.fillChamfer (r.inset (1), 2, active ? 0xff303f81 : 0xff1b1729);
    lettering (c, r.inset (3), name, 15, active ? light : muted, Face::regular, 1);
}
}

PanelUI::PanelUI (DesktopPlugin *p) : p_ (p), displayed_ (p->params.size (), 0)
{
    setSize (w_, h_, 1);
}
PanelUI::~PanelUI ()
{
    finishDrag ();
    endWheelGesture ();
}

void PanelUI::setSize (int w, int h, int scale)
{
    w = std::clamp (w, int (kMinGuiWidth), int (kMaxGuiWidth));
    h = std::clamp (h, int (kMinGuiHeight), int (kMaxGuiHeight));
    scale = std::clamp (scale, 1, 4);
    if (w == w_ && h == h_ && scale == scale_ && !frame_.empty ()) return;
    w_ = w;
    h_ = h;
    scale_ = scale;
    frame_.resize (size_t (physWidth ()) * physHeight ());
    c_.begin (frame_.data (), physWidth (), physHeight (), scale_);
    closePopup ();
    layout ();
    dirty_ = true;
}

void PanelUI::layout ()
{
    const int m = std::clamp (w_ / 78, 12, 28), gap = 8, width = w_ - 2 * m;
    const int top = m, toolH = 28;
    int x = m;
    auto tool = [&] (Item item, int size) {
        items_[size_t (item)] = {x, top, size, toolH};
        x += size + 4;
    };
    tool (Item::PresetPrev, 28);
    tool (Item::PresetBox, std::clamp (width / 5, 165, 280));
    tool (Item::PresetNext, 28);
    const int readX = x + 12;
    x = w_ - m - (59 + 4 + 91 + 4 + 76 + 4 + 93);
    status_ = {readX, top, x - readX - 12, toolH};
    tool (Item::Fine, 59);
    tool (Item::Learn, 91);
    tool (Item::ClearCc, 76);
    tool (Item::Motion, 93);
    const int hy = top + toolH + 10, hh = std::clamp (h_ * 21 / 100, 95, 205);
    const int scoreY = hy + hh + 32, scoreH = std::clamp (h_ * 23 / 100, 112, 285);
    const int patternHeight = scoreH + std::clamp (h_ / 22, 24, 52);
    const int motionHeight = std::clamp (h_ * 8 / 100 - 8, 34, 88);
    scoreBox_ = {m, scoreY, width, patternHeight - motionHeight};
#ifndef SLINK_RAZOR_PANEL
    const int crownX = column (4).x, rightX = column (12).x;
    brand_ = {m, hy, crownX - m - gap, hh};
    crown_ = {crownX, hy, rightX - crownX - gap, hh};
    counter_ = {rightX, hy, w_ - m - rightX, hh};
    const int faceW = std::clamp (counter_.w / 3, 68, 130);
    regen_ = {w_ - m - faceW - 4, hy + 3, faceW, hh - 6};
    const int readoutY = hy + hh * 59 / 100, readoutH = hy + hh - readoutY - 3;
    const int seedW = brand_.w * 33 / 100, keyW = brand_.w * 17 / 100;
    seed_ = {brand_.x + 10, readoutY, seedW - 12, readoutH};
    root_ = {brand_.x + seedW + 4, readoutY, keyW - 4, readoutH};
    scaleBox_ = {root_.x + root_.w + 4, readoutY, brand_.x + brand_.w - 14 - root_.x - root_.w, readoutH};
    for (int k = 0; k < 3; ++k)
        banks_[k] = {w_ - m - (3 - k) * 66, scoreY - 30, 62, 24};
#endif
    motionBounds_ = {m, scoreY + scoreBox_.h + 5, width, motionHeight};
#ifdef SLINK_RAZOR_PANEL
    layoutRazor (hy, hh);
#endif
    const int dy = motionBounds_.y + motionBounds_.h + 8;
    deck_ = {m, dy, width, h_ - m - dy};
    const int voiceW = (width - 2 * gap) * 30 / 100, toneW = (width - 2 * gap) * 28 / 100;
    const int seqX = m + voiceW + toneW + 2 * gap, seqW = m + width - seqX;
    const int heading = std::clamp (deck_.h / 10, 19, 28), rowsY = dy + heading + 3;
    const int rowsH = h_ - m - rowsY, rowH = (rowsH + 3) / 5;
    const int clockY = rowsY + 3 * rowH;
    groupTitles_[0] = {m, dy, voiceW, heading};
    groupTitles_[1] = {m + voiceW + gap, dy, toneW, heading};
    groupTitles_[2] = {seqX, dy, seqW, heading};
    groupTitles_[3] = {seqX, clockY + 2, seqW, heading};
    filter_ = {groupTitles_[1].x, rowsY + 4 * rowH, toneW, h_ - m - (rowsY + 4 * rowH)};
    controls_.clear ();
    for (int group = 0; group < 4; ++group)
    {
        const auto &pg = groups[group];
        const auto title = groupTitles_[group];
        const int cols = group < 2 ? 2 : group == 2 ? 4 : 3;
        for (int i = 0; i < pg.count; ++i)
        {
            const int col = i % cols, row = i / cols;
            const int rowCols = std::min (cols, pg.count - row * cols);
            const int x0 = title.x + col * (title.w + 3) / rowCols,
                      x1 = title.x + (col + 1) * (title.w + 3) / rowCols - 3;
            const int y0 = title.y + heading + 3 + row * rowH;
            const int y1 = group == 3 || (group == 0 && row == 4) ? h_ - m : y0 + rowH - 3;
            controls_.push_back (
                {{x0, y0, x1 - x0, y1 - y0}, p_->findParamIndex (pg.fields[i].id), pg.fields[i].name});
        }
    }
}

Rect PanelUI::column (int i) const
{
    const int label = std::clamp (scoreBox_.w / 16, 58, 100);
    const int x = scoreBox_.x + label, width = scoreBox_.w - label;
    const int left = x + i * width / 16 + (i % 4 == 0 ? 4 : 1);
    return {left, scoreBox_.y, x + (i + 1) * width / 16 - left - 1, scoreBox_.h};
}

int PanelUI::hitStep (int x, int y) const
{
    for (int i = 0; i < 16; ++i)
        if (column (i).contains (x, y) && bank_ * 16 + i < score_.steps) return bank_ * 16 + i;
    return -1;
}
int PanelUI::hitParam (int x, int y) const
{
    if (seed_.contains (x, y)) return p_->findParamIndex (id::seed);
    if (root_.contains (x, y)) return p_->findParamIndex (id::root);
    if (scaleBox_.contains (x, y)) return p_->findParamIndex (id::scale);
    if (regen_.contains (x, y)) return p_->findParamIndex (id::regen);
    if (filter_.contains (x, y)) return p_->findParamIndex (id::cutoff);
    for (const auto &ctl : controls_)
        if (ctl.rect.contains (x, y)) return ctl.param;
    return -1;
}

Rect PanelUI::update (uint64_t now)
{
    if (wheelParam_ >= 0 && now >= wheelAt_ + 240) endWheelGesture ();
    const auto next = p_->patternReader->read (p_->inner);
    const bool playing = p_->transportPlaying.load (std::memory_order_relaxed);
    const bool stepChanged = next.current != score_.current || next.seed != score_.seed;
    if (stepChanged && next.running) pulseAt_ = now;
    const bool scoreChanged = !(next == score_) || playing != playing_;
    score_ = next;
    playing_ = playing;
    if (score_.steps <= 16)
        bank_ = 0;
    else if (follow_ && score_.current >= 0)
        bank_ = std::clamp (score_.current / 16, 0, 1);
    if (inspected_ >= score_.steps) inspected_ = -1;
    for (size_t i = 0; i < displayed_.size (); ++i)
    {
        const auto v = p_->displayValue (int (i));
        dirty_ |= displayed_[i] != v;
        displayed_[i] = v;
    }
    const uint64_t delta = motion_ && updated_ && now >= updated_ ? std::min<uint64_t> (now - updated_, 150) : 0;
    elapsed_ += delta;
    const uint64_t oldMotionFrame = motionMs_ / 16;
    motionMs_ += delta;
    const bool vectorMoved = oldMotionFrame != motionMs_ / 16;
    if (delta)
    {
        const bool accented = playing && next.running && next.current >= 0 && next.current < 32 &&
                              ((next.gates & next.accents) & (uint32_t (1) << next.current));
        const float target = accented ? 1.f : 0.f;
        accentLift_ += (target - accentLift_) * float (-std::expm1 (-double (delta) / (accented ? 65. : 180.)));
    }
    updated_ = now;
    const auto start = std::chrono::steady_clock::now ();
    const int oldCamera = art_.camera.index, oldShaft = art_.shaft.index;
    art_.animate (elapsed_);
    const bool external = p_->screenRequested.exchange (false, std::memory_order_acq_rel);
    const bool repaintScore = dirty_ || scoreChanged || external;
    const bool repaintHeader = repaintScore || oldCamera != art_.camera.index || oldShaft != art_.shaft.index;
    if (!repaintHeader && !vectorMoved) return {};
#ifdef SLINK_RAZOR_PANEL
    const Rect movingStrip = ribbon_;
#else
    const Rect movingStrip = motionBounds_;
#endif
    const Rect damage = repaintHeader ? Rect{0, 0, w_, dirty_ ? h_ : deck_.y} : movingStrip;
    c_.resetClip ();
    if (repaintHeader)
    {
        c_.fill ({0, 0, w_, repaintScore ? damage.h : crown_.y + crown_.h}, ground);
        drawHeader ();
    }
    if (repaintScore) drawScore ();
#ifdef SLINK_RAZOR_PANEL
    if (repaintScore) drawRazorFooter ();
    if (vectorMoved || repaintScore) drawAcidMotion (c_, ribbon_, motionMs_ * .001, accentLift_);
#else
    drawAcidMotion (c_, motionBounds_, motionMs_ * .001, accentLift_);
#endif
    if (dirty_) drawDeck ();
    if (popup_ != PopupKind::None && !damage.intersect (popupRect_).empty ()) drawPopup ();
    dirty_ = false;
    renderMs_ = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now () - start).count ();
    return damage;
}

void PanelUI::drawHeader ()
{
    auto label = [&] (Item i, std::string_view name, bool active = false) {
        button (c_, items_[size_t (i)], name, active);
    };
    label (Item::PresetPrev, "<");
    label (Item::PresetNext, ">");
    const int pi = p_->currentPresetIndex;
    label (Item::PresetBox, pi >= 0 && pi < int (p_->presets.size ()) ? "Factory   /   " + p_->presets[pi].name
                                                                      : "Factory presets");
    label (Item::Fine, "Fine", fine_);
    label (Item::Motion, motion_ ? "Motion on" : "Motion off", motion_);
    const bool learn = p_->midiLearnTarget.load (std::memory_order_relaxed) != CLAP_INVALID_ID;
    label (Item::Learn, learn ? "Learning..." : "MIDI learn", learn);
    label (Item::ClearCc, "Clear CC");
    const int active = dragParam_ >= 0 ? dragParam_ : hover_ >= 0 ? hover_ : lastTouched_;
    std::string status;
    if (learn)
        status = p_->midiLearnTarget.load () == DesktopPlugin::kLearnArmedNoParam ? "Choose a control to learn"
                                                                                  : "Move a MIDI controller";
    else if (active >= 0)
    {
        status = p_->params[active].name + "   /   " + valueText (active);
        const int cc = p_->findCcForParam (p_->params[active].id);
        if (cc >= 0) status += "   /   CC " + std::to_string (cc);
    }
#ifdef SLINK_RAZOR_PANEL
    Rect readout = status_;
    if (score_.steps > 16) readout.w = banks_[0].x - status_.x - 4;
    if (inspected_ >= 0 && !learn && hover_ < 0 && dragParam_ < 0)
    {
        const uint32_t bit = uint32_t (1) << inspected_;
        status = "Step " + number ("%02d", inspected_ + 1) + "   " + noteName (score_.pitch[inspected_]);
        status += score_.gates & bit ? "   Gate" : "   Rest";
        if (score_.accents & bit) status += "   Accent";
        if (score_.slides & bit) status += "   Slide";
    }
    lettering (c_, readout, status, 15, muted);
    drawRazorHeader ();
#else
    lettering (c_, status_, status, 15, muted);

    plate (c_, brand_);
    plate (c_, crown_, 0xff030309);
    plate (c_, counter_, 0xff1c152b);
    const Rect mesh = counter_.inset (5);
    for (int x = mesh.x + 11; x < mesh.x + mesh.w; x += 20)
        c_.fill (x, mesh.y, 1, mesh.h, 0xff282137);
    for (int y = mesh.y + 9; y < mesh.y + mesh.h; y += 20)
        c_.hline (mesh.x, y, mesh.w, 0xff282137);
    const int bh = seed_.y - brand_.y - 10;
    art_.logo.fit (c_, {brand_.x + 12, brand_.y + 6, brand_.w - 24, bh});
    c_.hline (seed_.x, seed_.y - 2, brand_.w - 20, lineInk);
    const int seed = int (p_->displayValue (p_->findParamIndex (id::seed)));
    const bool pending = score_.steps > 0 && score_.seed != seed;
    auto readout = [&] (Rect r, const char *name, const std::string &value, int pt) {
        lettering (c_, {r.x + 3, r.y + 1, r.w - 4, r.h / 3}, name, 14, muted);
        lettering (c_, {r.x + 3, r.y + r.h / 3, r.w - 5, r.h * 2 / 3}, value, pt, light);
    };
    readout (seed_, pending ? "NEXT SEED" : "SEED", number ("%04X", seed), 32);
    readout (root_, "KEY", valueText (p_->findParamIndex (id::root)), 28);
    readout (scaleBox_, "SCALE", valueText (p_->findParamIndex (id::scale)), 22);
    c_.fill (root_.x - 4, root_.y + 5, 1, root_.h - 11, lineInk);
    art_.camera.fit (c_, crown_.inset (3));
    const int shot = int (elapsed_ % 20000);
    const char *view = shot < 3200 || shot >= 18500 ? "WIDE"
                       : shot < 8800                ? "LEFT WING"
                       : shot < 13000               ? "CORE"
                                                    : "RIGHT WING";
    lettering (c_, {crown_.x + 10, crown_.y + crown_.h - 19, 90, 15}, view, 13, muted);
    c_.hline (crown_.x + 9, crown_.y + 8, 10, quiet);
    c_.fill (crown_.x + 9, crown_.y + 8, 1, 8, quiet);
    c_.hline (crown_.x + crown_.w - 21, crown_.y + crown_.h - 9, 11, quiet);
    Rect r = counter_.inset (10);
    r.w = regen_.x - r.x - 5;
    art_.shaft.fit (c_, {r.x + r.w / 3, r.y, r.w * 2 / 3 - 4, 21});
    lettering (c_, {r.x, r.y, r.w, 20}, "STEP", 16, muted);
    const int n = score_.steps ? score_.steps : int (p_->displayValue (p_->findParamIndex (id::steps)));
    const bool live = playing_ && score_.running;
    const std::string step = score_.steps && score_.current >= 0 ? number ("%02d", score_.current + 1) : "--";
    lettering (c_, {r.x, r.y + 18, r.w * 6 / 10, r.h - 38}, step, std::min (96, r.h - 30), light, Face::italic);
    lettering (c_, {r.x + r.w * 6 / 10, r.y + 24, r.w * 4 / 10, r.h - 44}, "/ " + std::to_string (n), 26, muted);
    c_.disc (r.x + 4, r.y + r.h - 8, 3, live ? cobalt : quiet);
    const int statusWidth = r.w >= 160 ? r.w * 2 / 3 - 18 : r.w - 14;
    lettering (c_, {r.x + 13, r.y + r.h - 21, statusWidth, 20},
               live       ? "Host playing"
               : playing_ ? "Notes mode"
                          : "Host stopped",
               15, muted);
    if (r.w >= 160) trace ({r.x + r.w * 2 / 3, r.y + r.h - 19, r.w / 3 - 3, 15}, 0xffa8a2d8);
    int pose = 0;
    if (motion_ && live && updated_ >= pulseAt_ && updated_ - pulseAt_ < 250)
        pose = std::min (5, int ((updated_ - pulseAt_) / 45));
    else if (motion_ && elapsed_ % 5100 > 4800)
        pose = std::min (5, int (elapsed_ % 5100 - 4800) / 50);
    art_.face.fit (c_, {regen_.x + 6, regen_.y + 5, regen_.w - 12, regen_.h - 41}, pose, 6);
    button (c_, {regen_.x + 6, regen_.y + regen_.h - 32, regen_.w - 12, 25}, "Regen",
            hover_ == p_->findParamIndex (id::regen));
#endif
}

void PanelUI::trace (Rect r, uint32_t ink)
{
    if (r.empty () || score_.steps < 1) return;
    const int n = std::clamp (score_.steps, 1, 32);
    const auto range = std::minmax_element (score_.pitch, score_.pitch + n);
    const int span = std::max (384, int (*range.second) - int (*range.first));
    int previousY = 0;
    for (int i = 0; i < n; ++i)
    {
        const int x = r.x + i * r.w / n, nextX = r.x + (i + 1) * r.w / n;
        const int y = r.y + r.h - 2 - (score_.pitch[i] - *range.first) * (r.h - 4) / span;
        if (i) c_.fill (x, std::min (previousY, y), 1, std::abs (previousY - y) + 1, lineInk);
        const bool live = playing_ && score_.running && score_.current == i;
        c_.fill (x, y, std::max (1, nextX - x - 1), live ? 3 : 2, live ? light : ink);
        previousY = y;
    }
}

void PanelUI::drawScore ()
{
    const Rect r = scoreBox_;
    const int n = std::clamp (score_.steps, 0, 32);
#ifndef SLINK_RAZOR_PANEL
    lettering (c_, {r.x, r.y - 35, 128, 30}, "Pattern", 26, light, Face::italic);
    std::string subtitle;
    if (!n)
        subtitle = "Awaiting host clock";
    else if (inspected_ >= 0)
    {
        const uint32_t mask = uint32_t (1) << inspected_;
        subtitle = "Step " + number ("%02d", inspected_ + 1) + "   /   " + noteName (score_.pitch[inspected_]);
        subtitle += score_.gates & mask ? "   Gate" : "   Rest";
        if (score_.accents & mask) subtitle += "   Accent";
        if (score_.slides & mask) subtitle += "   Slide";
    }
    else if (score_.seed != int (p_->displayValue (p_->findParamIndex (id::seed))))
        subtitle = "Playing seed " + number ("%04X", score_.seed);
    lettering (c_, {r.x + 104, r.y - 31, r.w / 2, 24}, subtitle, 15, muted);
    if (n > 16)
    {
        button (c_, banks_[0], "01 - 16", bank_ == 0);
        button (c_, banks_[1], "17 - 32", bank_ == 1);
        button (c_, banks_[2], "Follow", follow_);
    }
    const int pct[] = {0, 17, 48, 66, 83, 100};
#else
    const int pct[] = {0, 16, 52, 70, 85, 100};
#endif
#ifndef SLINK_RAZOR_PANEL
    const char *lanes[] = {"STEP", "NOTE", "GATE", "ACC", "SLD"};
#endif
    for (int j = 0; j < 5; ++j)
    {
        const int y = r.y + r.h * pct[j] / 100, height = r.h * pct[j + 1] / 100 - (y - r.y) - 2;
        const Rect title{r.x, y, column (0).x - r.x - 5, height};
        const bool note = j == 1;
        c_.gradient (title, note ? 0xffdaddeb : 0xff32283f, note ? 0xff8992b1 : 0xff1b1728);
#ifdef SLINK_RAZOR_PANEL
        c_.frame (title, note ? 0xffadb6ce : 0xff61566f);
        art_.labels.fit (c_, title.inset (4), j, 8);
#else
        lettering (c_, title.inset (4), lanes[j], std::min (height - 5, 19), note ? ground : light, Face::regular,
                   1);
#endif
        for (int k = 0; k < 16; ++k)
        {
            const int step = 16 * bank_ + k;
            Rect cell = column (k);
            cell.y = y;
            cell.h = height;
            const bool exists = step < n, current = exists && playing_ && score_.running && step == score_.current;
            const bool selected = exists && step == inspected_;
            if (current)
                c_.gradient (cell, note ? 0xff819eff : 0xff5e87f6, note ? 0xff476ce5 : 0xff293888);
            else if (note)
                c_.gradient (cell, exists ? 0xffe1e2f1 : 0xff4a4658, exists ? 0xff9099b7 : 0xff302b3b);
            else
                c_.gradient (cell, j == 0 ? 0xff30243f : 0xff292136, 0xff171423);
            if (note && exists && !current)
            {
                const int horizon = cell.h * 52 / 100;
                c_.gradient ({cell.x, cell.y, cell.w, horizon}, 0xffeeeef9, 0xffc5c9df);
                c_.gradient ({cell.x, cell.y + horizon, cell.w, cell.h - horizon}, 0xff9da3bd, 0xffb9bfd6);
                c_.fill (cell.x, cell.y + 1, 1, cell.h - 2, 0xffe9ebf8);
            }
            c_.hline (cell.x, cell.y, cell.w, current ? 0xffa6bcff : note && exists ? 0xfff5f3ff : lineInk);
            if (current && j == 0)
                for (int i = 0; i < 6; ++i)
                    c_.hline (cell.x + 2, cell.y + 2 + i, 6 - i, light);
            if (selected) c_.hline (cell.x, cell.y, cell.w, lilac);
            const int cx = cell.x + cell.w / 2, cy = cell.y + cell.h / 2;
            const uint32_t ink = current ? 0xffffffff : light;
#ifdef SLINK_RAZOR_PANEL
            if (j == 0 && exists)
                art_.steps.fit (c_, cell.inset (3), step, 32);
            else if (j == 1 && exists)
                drawPitch (cell.inset (2), step, current);
            else
#endif
            if (j == 0)
                lettering (c_, cell, number ("%02d", step + 1), std::min (22, height - 2),
                           exists || !n ? ink : quiet, Face::regular, 1);
            else if (j == 1)
                lettering (c_, cell.inset (2), exists ? noteName (score_.pitch[step]) : "-",
                           std::min (39, height - 2),
                           current  ? light
                           : exists ? 0xff171928
                                    : quiet,
                           Face::italic, 1);
            else if (exists)
            {
                const uint32_t mask = uint32_t (1) << step;
                if (j == 2)
                {
                    const int sz = std::clamp (height / 3, 6, 13);
                    Rect g{cx - sz / 2, cy - sz / 2, sz, sz};
                    if (score_.gates & mask)
                        c_.fill (g, ink);
                    else
                        c_.frame (g, quiet);
                }
                else if (j == 3 && (score_.accents & mask))
                    for (int i = -5; i <= 5; ++i)
                        c_.fill (cx - (5 - std::abs (i)), cy + i, 1 + 2 * (5 - std::abs (i)), 1, ink);
                else if (j == 4 && (score_.slides & mask))
                {
                    c_.line (cx - 5, cy + 4, cx + 4, cy - 4, 2.f, ink);
                    c_.fill (cx - 1, cy - 5, 7, 2, ink);
                    c_.fill (cx + 4, cy - 5, 2, 6, ink);
                }
                else
                    c_.fill (cx - 3, cy, 6, 1, quiet);
            }
        }
    }
}

void PanelUI::drawDeck ()
{
    for (int i = 0; i < 4; ++i)
    {
        const auto t = groupTitles_[i];
#ifdef SLINK_RAZOR_PANEL
        const int titleW = std::min (t.w / 2, i == 2 ? 112 : 77);
        c_.fillChamfer ({t.x, t.y + 2, titleW, t.h - 3}, 4, 0xff2a243b);
        c_.fill (t.x + 3, t.y + 6, 2, t.h - 11, 0xff9faee9);
        lettering (c_, {t.x + 10, t.y, titleW - 13, t.h}, upper (groups[i].name),
                   std::min (22, t.h), light, Face::italic);
        c_.hline (t.x + titleW + 2, t.y + t.h - 3, t.w - titleW - 2, lineInk);
#else
        lettering (c_, {t.x + 2, t.y, t.w, t.h}, groups[i].name, std::min (23, t.h + 1), light, Face::italic);
        const int ruleX = t.x + (i == 2 ? 91 : 52);
        c_.hline (ruleX, t.y + t.h * 2 / 3, t.x + t.w - ruleX, lineInk);
#endif
    }
    for (const auto &ctl : controls_)
    {
        if (ctl.param < 0) continue;
        const Rect r = ctl.rect;
        const auto &pr = p_->params[size_t (ctl.param)];
        const auto kind = tbmpc::paramTable[tbmpc::paramIndex (pr.id)].kind;
        const bool hot = ctl.param == hover_ || ctl.param == dragParam_;
#ifdef SLINK_RAZOR_PANEL
        c_.gradient (r, hot ? 0xff3c3452 : 0xff292333, hot ? 0xff211c30 : 0xff15131e);
#else
        c_.fill (r, hot ? 0xff26223a : 0xff181521);
#endif
        c_.hline (r.x, r.y, r.w, hot ? lilac : lineInk);
        const int inset = std::clamp (r.w / 15, 7, 14);
        const int d = std::clamp (std::min (r.h * 7 / 10, r.w / 4), 17, 52);
        const int tx = r.x + inset, tw = r.w - 2 * inset - d - 5;
        const int half = r.h / 2;
        lettering (c_, {tx, r.y + 2, tw, half - 1}, ctl.label, std::min (16, half), muted);
        lettering (c_, {tx, r.y + half - 1, tw, r.h - half - 1}, valueText (ctl.param), std::min (23, half + 3),
                   light, Face::italic);
        const int cx = r.x + r.w - inset - d / 2, cy = r.y + r.h / 2;
#ifdef SLINK_RAZOR_PANEL
        if (kind == tbmpc::Kind::choice)
        {
            const int halfW = std::clamp (d / 3, 5, 9);
            c_.line (cx - halfW, cy - 2, cx, cy + 3, 1.6f, hot ? light : lilac);
            c_.line (cx, cy + 3, cx + halfW, cy - 2, 1.6f, hot ? light : lilac);
            continue;
        }
#endif
        if (kind == tbmpc::Kind::button)
        {
            c_.frame ({cx - 7, cy - 7, 14, 14}, hot ? light : lilac);
            c_.fill (cx - 3, cy - 3, 6, 6, lilac);
            continue;
        }
        const float radius = d / 2.f;
        art_.knob.fit (c_, {cx - d / 2, cy - d / 2, d, d});
        const double angle = (.75 + 1.5 * norm (ctl.param)) * 3.141592653589793;
        c_.line (cx + std::cos (angle) * radius * .32, cy + std::sin (angle) * radius * .32,
                 cx + std::cos (angle) * (radius - 3), cy + std::sin (angle) * (radius - 3), 2.2f, 0xffcfddff);
        c_.fill (r.x, r.y + r.h - 2, int (r.w * norm (ctl.param)), 2, hot ? cobalt : 0xff5968ac);
    }

    if (!filter_.empty ())
    {
        const Rect r = filter_.inset (4);
        c_.fill (filter_, glass);
        c_.hline (filter_.x, filter_.y, filter_.w, lineInk);
        const int labelW = std::clamp (r.w / 4, 52, 79);
        lettering (c_, {r.x + 3, r.y, labelW - 3, r.h}, "Response", 14, muted);
        Rect plot{r.x + labelW, r.y + 3, r.w - labelW - 4, r.h - 6};
        for (int i = 1; i < 5; ++i)
            c_.fill (plot.x + i * plot.w / 5, plot.y, 1, plot.h, 0xff262232);
        for (int i = 0; i < 4; ++i)
            c_.hline (plot.x, plot.y + i * plot.h / 4, plot.w, 0xff262232);
        auto val = [&] (clap_id id) { return float (p_->displayValue (p_->findParamIndex (id))); };
        const int kind = int (val (id::filter));
        const float k = tbmpc::feedbackOf (kind, val (id::resonance)),
                    nominal = tbmpc::nominalHz (kind, val (id::cutoff));
        const float spread = val (id::peakSpread);
        const double ref = tbmpc::filterGain2 (kind, k, spread, 20.0 / nominal);
        int prev = 0;
        for (int x = 0; x < plot.w; ++x)
        {
            const double hz = 20.0 * std::pow (1000.0, double (x) / std::max (1, plot.w - 1));
            const double db = 10 * std::log10 (tbmpc::filterGain2 (kind, k, spread, hz / nominal) / ref + 1e-12);
            const int y = plot.y + int (std::clamp ((12 - db) / 60., 0., 1.) * (plot.h - 1));
            if (x) c_.line (plot.x + x - 1, prev, plot.x + x, y, 1.6f, cobalt);
            prev = y;
        }
    }
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
        lettering (c_, {r.x + 8, r.y, r.w - 18, r.h}, popupItems_[index], 16, light);
    }
    if (popupRects_.size () < popupItems_.size ())
    {
        const int scale = popupRect_.h - 12;
        c_.fill (popupRect_.x + popupRect_.w - 5,
                 popupRect_.y + 6 + scale * popupFirst_ / int (popupItems_.size ()), 2,
                 std::max (8, scale * int (popupRects_.size ()) / int (popupItems_.size ())), lilac);
    }
}

void PanelUI::edit (int param, Rect r, int buttonId, uint32_t mods, int clicks, int second)
{
    if (param < 0) return;
    const auto &pr = p_->params[size_t (param)];
    hover_ = lastTouched_ = param;
    if (p_->midiLearnTarget.load () == DesktopPlugin::kLearnArmedNoParam)
    {
        p_->midiLearnTarget.store (pr.id);
        return;
    }
    if (buttonId == 1)
    {
        openMenu (param, downX_, downY_);
        return;
    }
    if (clicks >= 2 || (mods & (kModAlt | kModCmd)))
    {
        resetParam (param);
        return;
    }
    if (tbmpc::paramTable[tbmpc::paramIndex (pr.id)].kind == tbmpc::Kind::button)
    {
        p_->setValueOnce (param, pr.maxVal);
        return;
    }
    dragParam_ = param;
    dragY_ = second;
    dragRect_ = r;
    start_ = norm (param);
    if (second >= 0) startY_ = norm (second);
}

void PanelUI::finishDrag ()
{
    if (moved_ && dragParam_ >= 0 && dragY_ < 0) p_->endEdit (dragParam_);
    dragParam_ = dragY_ = -1;
    moved_ = false;
}

void PanelUI::mouseDown (float x, float y, int buttonId, uint32_t mods, int clicks)
{
    finishDrag ();
    endWheelGesture ();
    dirty_ = true;
    downX_ = x;
    downY_ = y;
    if (popup_ != PopupKind::None)
    {
        for (size_t i = 0; i < popupRects_.size (); ++i)
            if (popupRects_[i].contains (x, y))
            {
                popupPick (popupFirst_ + int (i));
                return;
            }
        closePopup ();
        return;
    }
    for (size_t i = 0; i < size_t (Item::Count); ++i)
    {
        if (!items_[i].contains (x, y)) continue;
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
            break;
        default:
            break;
        }
        return;
    }
    if (score_.steps > 16)
        for (int i = 0; i < 3; ++i)
            if (banks_[i].contains (x, y))
            {
                if (i == 2)
                    follow_ = !follow_;
                else
                {
                    bank_ = i;
                    follow_ = false;
                }
                return;
            }
#ifdef SLINK_RAZOR_PANEL
    if (transport_.contains (x, y))
    {
        if (buttonId == 0 && p_->hostTransport && p_->hostTransport->request_toggle_play)
            p_->hostTransport->request_toggle_play (p_->realHost);
        return;
    }
#endif
    if (crown_.contains (x, y))
    {
        constexpr uint64_t shots[] = {0, 5000, 11000, 14000, 20000};
        const auto t = elapsed_ % 20000;
        for (auto next : shots)
            if (next > t)
            {
                elapsed_ = elapsed_ - t + next;
                break;
            }
        return;
    }
    const int step = hitStep (x, y);
    if (step >= 0)
    {
        inspected_ = step;
        edit (p_->findParamIndex (id::transpose), scoreBox_, buttonId, mods, clicks,
              p_->findParamIndex (id::density));
        return;
    }
    if (filter_.contains (x, y))
    {
        edit (p_->findParamIndex (id::cutoff), filter_, buttonId, mods, clicks,
              p_->findParamIndex (id::resonance));
        return;
    }
    for (const auto &control : controls_)
        if (control.rect.contains (x, y))
        {
            edit (control.param, control.rect, buttonId, mods, clicks);
            return;
        }
    const int param = hitParam (x, y);
    if (param >= 0)
        edit (param,
              seed_.contains (x, y)       ? seed_
              : root_.contains (x, y)     ? root_
              : scaleBox_.contains (x, y) ? scaleBox_
                                          : regen_,
              buttonId, mods, clicks);
}

void PanelUI::mouseMove (float x, float y, uint32_t mods)
{
    if (popup_ != PopupKind::None)
    {
        int next = -1;
        for (size_t i = 0; i < popupRects_.size (); ++i)
            if (popupRects_[i].contains (x, y)) next = popupFirst_ + int (i);
        dirty_ |= popupHover_ != next;
        popupHover_ = next;
        return;
    }
    if (dragParam_ >= 0)
    {
        const float dx = x - downX_, dy = downY_ - y;
        if (!moved_ && std::abs (dx) + std::abs (dy) < 3) return;
        if (!moved_)
        {
            if (dragY_ < 0) p_->beginEdit (dragParam_);
            moved_ = true;
        }
        const double sensitivity = (mods & kModShift || fine_) ? .12 : 1.;
        if (dragY_ >= 0)
        {
            p_->setValueOnce (dragParam_,
                              fromNorm (dragParam_, start_ + dx * sensitivity / std::max (40, dragRect_.w)));
            p_->setValueOnce (dragY_, fromNorm (dragY_, startY_ + dy * sensitivity / std::max (40, dragRect_.h)));
        }
        else
            setParam (dragParam_, fromNorm (dragParam_, start_ + (dy + dx * .25) * sensitivity / 130.));
        dirty_ = true;
        return;
    }
    const int over = hitParam (x, y);
    dirty_ |= hover_ != over;
    hover_ = over;
}

void PanelUI::mouseUp (float, float, int, uint32_t)
{
    if (dragParam_ >= 0 && dragY_ < 0 && !moved_)
    {
        const auto &pr = p_->params[dragParam_];
        if (tbmpc::paramTable[tbmpc::paramIndex (pr.id)].kind == tbmpc::Kind::choice)
            openChoices (dragParam_, dragRect_);
    }
    finishDrag ();
    dirty_ = true;
}

void PanelUI::wheel (float x, float y, float, float dy, bool precise, uint32_t mods)
{
    if (std::abs (dy) < .0001f) return;
    wheelAccum_ += dy * (precise ? .15 : 1.);
    const int steps = int (wheelAccum_);
    if (!steps) return;
    wheelAccum_ -= steps;
    dirty_ = true;
    if (popup_ != PopupKind::None)
    {
        popupFirst_ = std::clamp (popupFirst_ - steps, 0, int (popupItems_.size () - popupRects_.size ()));
        popupHover_ = -1;
        return;
    }
    int param = hitParam (x, y);
    if (param < 0 && hitStep (x, y) >= 0) param = p_->findParamIndex (id::transpose);
    if (param < 0) return;
    if (tbmpc::paramTable[tbmpc::paramIndex (p_->params[param].id)].kind == tbmpc::Kind::button) return;
    if ((fine_ || (mods & kModShift)) && !p_->params[param].isStepped ())
    {
        if (wheelParam_ != param)
        {
            endWheelGesture ();
            wheelParam_ = param;
            p_->beginEdit (param);
        }
        wheelAt_ = nowMs ();
        setParam (param, fromNorm (param, norm (param) + steps / 1600.));
    }
    else
        stepParam (param, steps);
}
void PanelUI::mouseExit ()
{
    hover_ = -1;
    dirty_ = true;
}
bool PanelUI::key (int k)
{
    dirty_ = true;
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
        return true;
    }
    if ((k == 0 || k == 1) && score_.steps)
    {
        inspected_ = std::clamp ((inspected_ >= 0 ? inspected_ : std::max (0, score_.current)) + (k == 0 ? -1 : 1),
                                 0, score_.steps - 1);
        bank_ = inspected_ / 16;
        follow_ = false;
        return true;
    }
    if ((k == 2 || k == 3) && lastTouched_ >= 0)
    {
        stepParam (lastTouched_, k == 2 ? 1 : -1);
        return true;
    }
    return false;
}

} // namespace hdslave_desktop
