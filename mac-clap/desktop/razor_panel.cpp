#include "panel_ui.h"
#include "../src/params.h"
#include <cstdio>

namespace hdslave_desktop
{
namespace
{
constexpr uint32_t paper = 0xfff2f2ff, secondary = 0xffb4b6cf, rim = 0xff655e79, midnight = 0xff100e1c;

void housing (Canvas &c, Rect r, uint32_t fill)
{
    c.fillChamfer (r, 7, rim);
    c.fillChamfer (r.inset (1), 6, fill);
    c.hline (r.x + 8, r.y, r.w / 2, 0xffafb7d4);
}

void badge (Canvas &c, Rect r, bool bright)
{
    c.fillChamfer (r, 5, bright ? 0xffafc2ff : rim);
    c.fillChamfer (r.inset (1), 4, bright ? 0xff344b9f : 0xff221b35);
    c.hline (r.x + 4, r.y + 2, r.w - 9, bright ? 0xff7194ff : 0xff4d4263);
}

void cover (Canvas &c, const ThornImage &image, Rect box)
{
    if (image.pixels.empty () || box.empty ()) return;
    const float zoom = std::max (float (box.w) / image.width, float (box.h) / image.height);
    const int w = int (std::ceil (image.width * zoom)), h = int (std::ceil (image.height * zoom));
    const auto saved = c.clip;
    c.setClip (box);
    c.blitSharp (image.pixels.data (), image.width, image.height,
                 {box.x + (box.w - w) / 2, box.y + (box.h - h) / 2, w, h});
    c.clip = saved;
}
}

void PanelUI::layoutRazor (int y, int h)
{
    const int x = scoreBox_.x, w = scoreBox_.w;
    const int gap = std::max (5, w / 180);
    const int crest = x + w * 23 / 100, step = x + w * 67 / 100, face = x + w * 92 / 100;
    brand_ = {x, y, crest - x - gap, h};
    crown_ = {crest, y, step - crest - gap, h};
    counter_ = {step, y, face - step - gap, h};
    regen_ = {face, y, x + w - face, h};
    const int split = y + h * 54 / 100;
    seed_ = {x + 6, split, brand_.w / 2 - 6, y + h - split - 3};
    transport_ = {counter_.x + counter_.w * 56 / 100, y + h * 59 / 100,
                  counter_.w * 41 / 100, h * 33 / 100};
    const int matrixTop = y + h + gap;
    scoreBox_.h += scoreBox_.y - matrixTop;
    scoreBox_.y = matrixTop;
    for (int i = 0; i < 3; ++i)
        banks_[i] = {status_.x + status_.w - (3 - i) * 57, status_.y + 2, 54, status_.h - 4};

    const Rect strip = motionBounds_;
    const int keyEnd = strip.x + strip.w * 23 / 100;
    root_ = {strip.x, strip.y, std::max (34, strip.w / 23), strip.h};
    scaleBox_ = {root_.x + root_.w, strip.y, keyEnd - root_.x - root_.w, strip.h};
    ribbon_ = {keyEnd + gap, strip.y, strip.w * 34 / 100 - gap, strip.h};
    const int contourX = ribbon_.x + ribbon_.w + gap, railX = strip.x + strip.w * 79 / 100;
    contour_ = {contourX, strip.y, railX - contourX - gap, strip.h};
    position_ = {railX, strip.y, x + w - railX, strip.h};
}

void PanelUI::drawPitch (Rect box, int step, bool live)
{
    const int midi = tbmpc::noteAtZeroVolts + int (std::lround (score_.pitch[step] / 128.0));
    const char *pitch = tbmpc::rootChoices[(midi % 12 + 12) % 12];
    const std::string octave = std::to_string (int (std::floor (midi / 12.0)) - 1);
    const int letterW = std::min (int (box.h * .96f), box.w * 3 / (3 + int (octave.size ())));
    const int figureW = std::max (1, std::min (letterW / 3 + 1, (box.w - letterW) / int (octave.size ())));
    const int x = box.x + (box.w - letterW - figureW * int (octave.size ())) / 2;
    const int mid = box.y + box.h / 2;
    art_.notes.fit (c_, {x, box.y, letterW, box.h}, pitch[0] - 'A', 7);
    const auto &fig = live ? art_.pearl : art_.steel;
    if (pitch[1] == '#')
        fig.fit (c_, {x + letterW, mid - box.h / 3, figureW, box.h / 3}, 11, 12);
    for (size_t i = 0; i < octave.size (); ++i)
        fig.fit (c_, {x + letterW + int (i) * figureW, mid, figureW, box.h * 2 / 5},
                 octave[i] == '-' ? 10 : octave[i] - '0', 12);
}

void PanelUI::drawRazorHeader ()
{
    const int n = score_.steps ? score_.steps : int (p_->displayValue (p_->findParamIndex (tbmpc::ParamId::steps)));
    const int current = score_.current >= 0 && score_.current < score_.steps ? score_.current : -1;
    housing (c_, brand_, midnight);
    art_.logo.fit (c_, {brand_.x + 7, brand_.y + 5, brand_.w - 14, seed_.y - brand_.y - 12});
    c_.hline (brand_.x + 8, seed_.y, brand_.w - 16, rim);
    const int seed = int (p_->displayValue (p_->findParamIndex (tbmpc::ParamId::seed)));
    char hex[12];
    std::snprintf (hex, sizeof hex, "%04X", seed);
    Rect box = seed_.inset (3);
    lettering (c_, {box.x, box.y, box.w, box.h / 3}, score_.steps && score_.seed != seed ? "NEXT SEED" : "SEED",
               14, secondary);
    lettering (c_, {box.x, box.y + box.h / 3, box.w, box.h * 2 / 3}, hex, 34, paper);
    const int detail = inspected_ >= 0 ? inspected_ : current;
    if (detail >= 0)
    {
        Rect key{seed_.x + seed_.w + 4, box.y + 2, brand_.x + brand_.w - seed_.x - seed_.w - 13, box.h - 4};
        c_.gradient (key, 0xffd5d9e6, 0xff9199b4);
        char number[8];
        std::snprintf (number, sizeof number, "%02d", detail + 1);
        lettering (c_, {key.x + 4, key.y, key.w / 3 - 3, key.h / 2}, "STEP", 13, 0xff323448);
        lettering (c_, {key.x + 4, key.y + key.h / 2 - 2, key.w / 3 - 3, key.h / 2}, number, 20, midnight,
                   Face::italic);
        drawPitch ({key.x + key.w / 3, key.y, key.w * 2 / 3 - 2, key.h}, detail, false);
    }

    housing (c_, crown_, 0xff07070d);
    cover (c_, art_.camera, crown_.inset (3));
    housing (c_, counter_, 0xff1d172c);
    art_.glass.fit (c_, counter_.inset (2));
    const Rect r = counter_.inset (5);
    lettering (c_, {r.x + 4, r.y, r.w / 3, r.h / 6}, "STEP", 16, secondary);
    const Rect digits{r.x, r.y + r.h / 8, r.w * 53 / 100, r.h * 7 / 8};
    if (current >= 0)
        art_.counter.fit (c_, digits, current, 32);
    else
        lettering (c_, digits, "--", 72, secondary, Face::italic, 1);
    lettering (c_, {digits.x + digits.w - 18, r.y + r.h * 4 / 5, 39, r.h / 5}, "/" + std::to_string (n),
               18, secondary);
    trace ({transport_.x + 3, r.y + r.h / 8, transport_.w - 6, r.h / 4}, 0xff9cabe3);
    const bool command = p_->hostTransport && p_->hostTransport->request_toggle_play;
    badge (c_, transport_, command || playing_);
    if (command)
        art_.labels.fit (c_, transport_.inset (5), playing_ ? 6 : 5, 8);
    else
        lettering (c_, transport_.inset (3), playing_ ? "HOST PLAY" : "HOST STOP", 25, paper, Face::italic, 1);

    const int bh = regen_.h * 29 / 100;
    Rect face{regen_.x, regen_.y, regen_.w, regen_.h - bh - 5};
    const double hit = playing_ && score_.running && updated_ >= pulseAt_
                           ? std::max (0., 1. - (updated_ - pulseAt_) / 300.) : 0.;
    // The motion clock holds both the face and crest when the artwork is paused.
    const double angle = .47 + .34 * std::sin (motionMs_ * .00131) + (motion_ ? .18 * hit : 0.);
    if (motion_) facePose_ = std::clamp (int (std::lround (32 * angle)), 0, 32);
    art_.face.fit (c_, face, facePose_, 33);
    Rect regen{regen_.x + 2, regen_.y + regen_.h - bh - 2, regen_.w - 4, bh};
    badge (c_, regen, hover_ == p_->findParamIndex (tbmpc::ParamId::regen));
    art_.labels.fit (c_, regen.inset (4), 7, 8);

    if (score_.steps > 16)
    {
        const char *names[] = {"01 - 16", "17 - 32", "Follow"};
        for (int i = 0; i < 3; ++i)
        {
            const bool on = i == 2 ? follow_ : i == bank_;
            badge (c_, banks_[i], on);
            lettering (c_, banks_[i].inset (2), names[i], 14, paper, Face::regular, 1);
        }
    }
}

void PanelUI::drawRazorFooter ()
{
    const Rect key = root_.unite (scaleBox_);
    housing (c_, key, 0xff21192d);
    lettering (c_, root_.inset (3), valueText (p_->findParamIndex (tbmpc::ParamId::root)), 21, paper, Face::italic, 1);
    c_.line (scaleBox_.x - 4, key.y + key.h * .72f, scaleBox_.x + 2, key.y + key.h * .28f, 1.4f, secondary);
    lettering (c_, scaleBox_.inset (6), valueText (p_->findParamIndex (tbmpc::ParamId::scale)), 21, paper);
    Rect r = contour_.inset (4);
    for (int x : {r.x, r.x + r.w - 1})
        for (int y : {r.y, r.y + r.h - 5})
            c_.fill (x, y, 1, 5, rim);
    trace (r.inset (4), 0xff8a9ace);
    r = position_.inset (6);
    const int cy = r.y + r.h / 2;
    c_.hline (r.x, cy, r.w, rim);
    for (int i = 0; i < std::clamp (score_.steps - bank_ * 16, 0, 16); ++i)
    {
        const int x = r.x + i * (r.w - 6) / 15, h = r.h / (i % 4 == 0 ? 2 : 3);
        if (playing_ && score_.running && score_.current == bank_ * 16 + i)
            c_.fill (x, r.y + 1, 6, r.h - 2, 0xff94aaff);
        else
            c_.fill (x, cy - h / 2, i % 4 ? 1 : 2, h, secondary);
    }
}
}
