#include "panel_ui.h"
#include "../../src/sp16_graphics.h"
#include "../../assets/sp16-thorn/index.h"
#include <cstdio>
#include <cstring>
#include <string_view>

#ifdef _WIN32
const uint32_t* desktopWordmark();
#else
extern "C" const uint32_t slink_desktop_wordmark[];
static const uint32_t* desktopWordmark() { return slink_desktop_wordmark; }
#endif

namespace hdslave_desktop
{
namespace
{
constexpr uint32_t ground = 0xff090910, glass = 0xff171423, edge = 0xff504762, light = 0xfff2f2ff,
                   muted = 0xffb9b6cb;
constexpr uint32_t lime = 0xffc3f58a, ice = 0xffa0b8ff, lilac = 0xffd4a2fc, quiet = 0xff817b94;
constexpr uint32_t laneInk[]{ice, ice, lime, lilac, 0xff8edddc};
void plate(Canvas& c, Rect r, uint32_t fill = glass, uint32_t outline = edge)
{
    c.fillChamfer(r, 4, outline);
    c.fillChamfer(r.inset(1), 3, fill);
}
// Use the same antialiased Barlow lettering as the panel for desktop control cards.
namespace font = tbmpc::rave;

unsigned glyphIndex(char ch)
{
    return ch >= ' ' && ch <= '~' ? unsigned(ch - ' ') : unsigned('?' - ' ');
}

int textWidth(std::string_view text, const font::Font& face)
{
    int width = 0;
    for (char ch : text)
        width += face.widths[glyphIndex(ch)];
    return width;
}

void controlText(Canvas& c, Rect box, std::string_view text, const font::Font& face, uint32_t color,
                 float zoom = 1.f, int align = 0)
{
    std::string fitted(text);
    const int available = int(box.w / zoom);
    if (textWidth(fitted, face) > available)
    {
        const int ellipsisWidth = textWidth("...", face);
        while (!fitted.empty() && textWidth(fitted, face) + ellipsisWidth > available)
            fitted.pop_back();
        fitted += "...";
    }
    const float glyphScale = zoom * c.s;
    const int lineHeight = int(std::lround(face.height * glyphScale));
    float cursor = float(box.x * c.s);
    if (align) cursor += (box.w * c.s - textWidth(fitted, face) * glyphScale) * (align == 1 ? .5f : 1.f);
    const int y = box.y * c.s + (box.h * c.s - lineHeight) / 2;
    for (const char ch : fitted)
    {
        const auto glyph = glyphIndex(ch);
        const int advance = face.widths[glyph];
        const auto* mask = face.ink + glyph * face.height * face.stride;
        const int start = int(std::lround(cursor));
        cursor += advance * glyphScale;
        const int width = int(std::lround(cursor)) - start;
        const auto coverage = [&](int row, int col)
        {
            const auto packed = mask[row * face.stride + col / 2];
            return int(col % 2 ? packed & 15 : packed >> 4);
        };
        // Sample at physical pixel centers so Retina text keeps soft edges.
        for (int dy = 0; dy < lineHeight; ++dy)
        {
            const float v = std::clamp((dy + .5f) * face.height / lineHeight - .5f, 0.f, float(face.height - 1));
            const int v0 = int(v), v1 = std::min(v0 + 1, face.height - 1);
            for (int dx = 0; dx < width; ++dx)
            {
                const float u = std::clamp((dx + .5f) * advance / width - .5f, 0.f, float(advance - 1));
                const int u0 = int(u), u1 = std::min(u0 + 1, advance - 1);
                const float mixX = u - u0, mixY = v - v0;
                const float top = coverage(v0, u0) * (1 - mixX) + coverage(v0, u1) * mixX;
                const float bottom = coverage(v1, u0) * (1 - mixX) + coverage(v1, u1) * mixX;
                const unsigned alpha = unsigned((top * (1 - mixY) + bottom * mixY) * 17.f + .5f);
                if (alpha) c.plot(start + dx, y + dy, color, alpha);
            }
        }
    }
}

void button(Canvas& c, Rect r, std::string_view text, bool selected = false)
{
    plate(c, r, selected ? 0xff29304a : 0xff191624, selected ? 0xff8594bf : edge);
    controlText(c, r.inset(2), text, font::normal, selected ? light : muted, .9f, 1);
}

Rect fit(Rect src, Rect box)
{
    const double scale = std::min(double(box.w) / src.w, double(box.h) / src.h);
    const int w = int(std::lround(src.w * scale)), h = int(std::lround(src.h * scale));
    return {box.x + (box.w - w) / 2, box.y + (box.h - h) / 2, w, h};
}
void screenInk(uint32_t& bed, uint32_t ink)
{
    const auto channel = [&](int shift) {
        const uint32_t paper = (bed >> shift) & 255, shine = (ink >> shift) & 255;
        return (paper + ((255 - paper) * shine + 127) / 255) << shift;
    };
    bed = 0xff000000u | channel(16) | channel(8) | channel(0);
}
const char* fieldName(int slot)
{
    switch (slot)
    {
    case id::glide:
        return "Slide mode";
    case id::slideTime:
        return "303 time";
    case id::gate:
        return "Gate length";
    case id::clockDiv:
        return "Division";
    case id::envMod:
        return "Env mod";
    case id::peakSpread:
        return "Peak spread";
    case id::accentDecay:
        return "Accent decay";
    case id::seedMode:
        return "Seed mode";
    case id::pitchLfoAmount:
    case id::cutoffLfoAmount:
        return "Depth";
    case id::pitchLfoSpeed:
    case id::cutoffLfoSpeed:
        return "Rate";
    default:
        return tbmpc::paramTable[tbmpc::paramIndex(slot)].name;
    }
}
std::string noteName(int pitch)
{
    const int midi = tbmpc::noteAtZeroVolts + int(std::lround(pitch / 128.0));
    return std::string(tbmpc::rootChoices[(midi % 12 + 12) % 12]) +
           std::to_string(int(std::floor(midi / 12.0)) - 1);
}
void mark(Canvas& c, int lane, int x, int y, int size, uint32_t color)
{
    if (lane == 2) c.fill(x - size, y - 2, 2 * size + 1, 4, color);
    if (lane == 3)
        for (int j = 0; j <= size; ++j)
            c.fill(x - j, y - size / 2 + j, 2 * j + 1, 1, color);
    if (lane == 4)
    {
        c.line(x - size, y + 3, x + size, y - 3, 1.7f, color);
        c.fill(x + size - 3, y - 3, 4, 2, color);
    }
}
} // namespace

void PanelUI::drawPopup()
{
    plate(c_, popupRect_, 0xff191526);
    for (size_t i = 0; i < popupRects_.size(); ++i)
    {
        const auto r = popupRects_[i];
        const int index = popupFirst_ + int(i);
        if (index == popupHover_ || index == popupSelected_) c_.fill(r, 0xff374073);
        controlText(c_, {r.x + 8, r.y, r.w - 23, r.h}, popupItems_[index], font::normal, light);
    }
    if (popupRects_.size() < popupItems_.size())
    {
        const int scale = popupRect_.h - 12;
        c_.fill(popupRect_.x + popupRect_.w - 5, popupRect_.y + 6 + scale * popupFirst_ / int(popupItems_.size()),
                2, std::max(8, scale * int(popupRects_.size()) / int(popupItems_.size())), lilac);
    }
}

void PanelUI::paintToolbar()
{
    const auto& t = layout_.tools;
    c_.fill(layout_.toolbar, ground);
    const int preset = p_->currentPresetIndex;
    button(c_, t[size_t(Item::PresetPrev)], "<");
    button(c_, t[size_t(Item::PresetBox)], preset < 0 ? "Factory presets" : p_->presets[size_t(preset)].name);
    button(c_, t[size_t(Item::PresetNext)], ">");
    button(c_, t[size_t(Item::Fine)], "Fine", fine_);
    button(c_, t[size_t(Item::Learn)], "MIDI learn", lastLearn_ != CLAP_INVALID_ID);
    button(c_, t[size_t(Item::ClearCc)], "Clear CC");
    button(c_, t[size_t(Item::Motion)], motion_ ? "Motion on" : "Motion off", motion_);
    std::string status;
    if (lastLearn_ == DesktopPlugin::kLearnArmedNoParam)
        status = "Choose a control";
    else if (lastLearn_ != CLAP_INVALID_ID)
        status = "Move a MIDI controller";
    else if (hover_ == crestHover)
        status = "Click to hold or resume the crest   /   Shift-click to cut";
    else if (hover_ >= 0 && hover_ < int(id::count))
    {
        if (hover_ == int(id::regen)) status = "Roll a new pattern seed";
        else if (hover_ == int(id::seedBack)) status = "Recall a previously played seed";
        else status = std::string(tbmpc::paramTable[tbmpc::paramIndex(hover_)].name) + "   /   " + readings_[hover_].text;
        if (readings_[hover_].cc >= 0) status += "   /   CC " + std::to_string(readings_[hover_].cc);
    }
    else if (hover_ >= filterPlot)
        status = hover_ == filterPlot ? "Across: cutoff     Up: resonance" : "Across: rate     Up: depth";
    else if (hoverStep_ >= 0)
    {
        constexpr const char* tips[]{"Click or scrub to audition", "Drag up or down to tune",
                                     "Drag across to paint gates", "Drag across to paint accents",
                                     "Drag across to paint slides"};
        status = tips[hoverLane_];
    }
    else
        status = "Thorn Crown";
    controlText(c_, layout_.status, status, font::normal, muted, .9f);
}

void PanelUI::paintMast()
{
    const Rect mast = layout_.mast;
    const auto seed = layout_.slots[id::seed], back = layout_.slots[id::seedBack];
    const Rect regen = layout_.slots[id::regen];
    c_.gradient(mast, 0xff222438, 0xff101020);
    c_.hline(mast.x, mast.y, mast.w, 0xff51516a);

    // Etched contours open around the signature and gather at the seed barcode.
    const int span = seed.x - mast.x;
    constexpr float tau = 6.2831853f;
    for (int i = 0; i < 8; ++i)
    {
        float lastX = 0, lastY = 0;
        for (int j = 0; j <= 64; ++j)
        {
            const float t = float(j) / 64, fan = std::sin(t * tau * .5f);
            const float wave = std::sin(t * tau + i * .16f) * .19f + std::sin(t * tau * 2 - i * .18f) * .10f;
            const float x = mast.x + t * (span - 1);
            const float y = mast.y + mast.h * (.63f + wave * fan + (i - 3.5f) * (.005f * (1 - t) + .043f * fan));
            if (j) c_.line(lastX, lastY, x, y, .72f, i == 5 ? 0xff9686bb : 0xff6976a0, i == 5 ? 118 : 74);
            lastX = x;
            lastY = y;
        }
    }

    const int seedNumber = int(readings_[id::seed].value);
    char hex[8];
    std::snprintf(hex, sizeof(hex), "%04X", seedNumber & 65535);
    const int pad = 8, labelH = seed.h / 3, valueW = seed.w * 43 / 100;
    const uint32_t seedInk = hover_ == int(id::seed) ? lime : ice;
    controlText(c_, {seed.x + pad, seed.y, seed.w - pad, labelH}, "PATTERN SEED", font::small, seedInk);
    controlText(c_, {seed.x + pad, seed.y + labelH, valueW, seed.h - labelH}, hex, font::large, light,
                seed.h >= 50 ? 1.28f : 1.05f);
    const int bx = seed.x + pad + valueW, by = seed.y + labelH + 4, barWidth = seed.x + seed.w - bx - 2;
    for (int i = 0; i < 20; ++i)
    {
        const int left = bx + i * barWidth / 20, right = bx + (i + 1) * barWidth / 20;
        const int w = (seedNumber >> (i % 16)) & 1 ? std::max(1, right - left - 1) : 1;
        c_.fill(left, by, w, seed.y + seed.h - by - 5, i % 5 ? 0xff98a8c3 : seedInk);
    }
    const bool recallHot = hover_ == int(id::seedBack), regenHot = hover_ == int(id::regen);
    const Rect recall = back.inset(2);
    c_.fillChamfer(recall, 7, recallHot ? 0xff353044 : 0xff242232);
    c_.frameChamfer(recall, 7, recallHot ? ice : 0xff534d68);
    controlText(c_, recall, "Recall", font::normal, recallHot ? light : muted, 1.f, 1);
    const int cx = layout_.face.x + layout_.face.w / 2, cy = regen.y + regen.h / 2;
    const int radius = regen.h / 2;
    const Rect key{cx, regen.y + 4, regen.x + regen.w - cx, regen.h - 8};
    const uint32_t cap = regenHot ? 0xff4c405d : 0xff322b45, rim = regenHot ? lilac : 0xff756583;
    c_.fillChamfer(key, 8, cap);
    c_.frameChamfer(key, 8, rim);
    c_.disc(cx, cy, radius, rim);
    c_.disc(cx, cy, radius - 1, 0xff272337);
    const int nameX = layout_.face.x + layout_.face.w;
    controlText(c_, {nameX, key.y, key.x + key.w - nameX - 4, key.h}, "Regen", font::normal, light, 1.f, 1);
}

// The hardware plates have dark opaque grounds. Use their light as screen ink on the desktop metal.
void PanelUI::chrome(Rect src, Rect dst, int fadeX, int fadeY, int intensity)
{
    const size_t area = size_t(src.w) * src.h;
    if (chromeInk_.size() < area) chromeInk_.resize(area);
    auto feather = [](int p, int length, int fade) {
        return std::min({p, length - 1 - p, fade}) * 256 / fade;
    };
    std::array<int, tbmpc::thorn::crest.w> horizontal;
    for (int x = 0; x < src.w; ++x) horizontal[x] = feather(x, src.w, fadeX);
    for (int y = 0; y < src.h; ++y)
    {
        const auto* original = art_.data() + (src.y + y) * SIXTEEN_SCREEN_WIDTH + src.x;
        auto* ink = chromeInk_.data() + y * src.w;
        const int gain = feather(y, src.h, fadeY) * intensity;
        for (int x = 0; x < src.w; ++x)
        {
            const int weight = (gain * horizontal[x]) >> 16;
            const auto channel = [&](int shift) {
                const int value = int((original[x] >> shift) & 255);
                return uint32_t(std::min(255, std::max(0, value - 17) * weight / 238)) << shift;
            };
            ink[x] = 0xff000000u | channel(16) | channel(8) | channel(0);
        }
    }
    c_.blitScaled(chromeInk_.data(), src.w, src.h, dst, src.w, screenInk);
}

Rect PanelUI::paintArt(bool repaintMast, bool repaintScore, uint64_t now)
{
    constexpr int sw = SIXTEEN_SCREEN_WIDTH, sh = SIXTEEN_SCREEN_HEIGHT;
    sixteen_screen_surface_t surface{art_.data(), sw, sh, sw, uint32_t(now)};
    const bool changed = p_->panel->renderArt(p_->inner, surface, repaintMast, selected_);
    if (!changed && !repaintMast && !repaintScore) return {};
    Rect damage;
    const auto changedPlate = [&](Rect src, bool force)
    {
        if (!changed) return force;
        bool touched = force;
        for (int y = src.y; y < src.y + src.h; ++y)
        {
            const size_t offset = size_t(y) * sw + src.x;
            const size_t bytes = size_t(src.w) * sizeof(uint32_t);
            if (!force && !std::memcmp(art_.data() + offset, previousArt_.data() + offset, bytes)) continue;
            std::memcpy(previousArt_.data() + offset, art_.data() + offset, bytes);
            touched = true;
        }
        return touched;
    };
    const Rect mast = layout_.mast;
    const int stride = mast.w * c_.s;
    if (repaintMast)
    {
        const Rect wordmark{0, 0, 680, 108};
        c_.blitScaled(desktopWordmark(), wordmark.w, wordmark.h, fit(wordmark, layout_.logo), wordmark.w, screenInk);
        mastBacking_.resize(size_t(stride) * mast.h * c_.s);
        for (int y = 0; y < mast.h * c_.s; ++y)
            std::copy_n(c_.px + (mast.y * c_.s + y) * c_.pw + mast.x * c_.s, stride,
                        mastBacking_.data() + size_t(y) * stride);
    }
    const auto inlay = [&](Rect src, Rect dst, int fadeX, int fadeY, int gain) {
        if (!changedPlate(src, repaintMast)) return;
        // Restore the engraving underneath each moving silhouette before laying down the next exposure.
        if (!repaintMast)
            for (int y = 0; y < dst.h * c_.s; ++y)
                std::copy_n(mastBacking_.data() + ((dst.y - mast.y) * c_.s + y) * stride + (dst.x - mast.x) * c_.s,
                            dst.w * c_.s, c_.px + (dst.y * c_.s + y) * c_.pw + dst.x * c_.s);
        chrome(src, dst, fadeX, fadeY, gain);
        damage = damage.unite(dst);
    };
    inlay({186, 6, 332, 90}, layout_.crest, 42, 10, 262);
    const Rect face{717, 0, 61, 58};
    inlay(face, fit(face, layout_.face), 2, 2, 252);
    if (changedPlate({127, 253, 168, 25}, repaintScore) && pattern_.steps)
    {
        const Rect cap = layout_.column(focus(), columns()).intersect(layout_.lane(0));
        paintPlayhead(cap);
        damage = damage.unite(cap);
    }
    return damage;
}

void PanelUI::paintPlayhead(Rect cap)
{
    const Rect head{cap.x + 1, cap.y, cap.w - 2, cap.h};
    c_.gradient(head, 0xff49405e, 0xff26243c);
    const Rect storm{129, 255, 164, 21};
    // The moving chrome belongs to the one focused column. Keep the number on a quiet face.
    const Rect foil = head.inset(1);
    chrome(fit(foil, storm), foil, 4, 2, 201);
    char number[4];
    std::snprintf(number, sizeof(number), "%02d", focus() + 1);
    const int badgeW = std::min(head.w - 4, textWidth(number, font::normal) + 6);
    const Rect badge{head.x + (head.w - badgeW) / 2, head.y + 1, badgeW, head.h - 3};
    c_.fillChamfer(badge, 4, 0xff211e34, 225);
    controlText(c_, badge, number, font::normal, light, .9f, 1);
    c_.fill(head.x, head.y + head.h - 2, head.w, 2, lime);
    if (hoverStep_ == focus()) c_.hline(head.x, head.y, head.w, lilac);
}

void PanelUI::paintScore()
{
    const auto& l = layout_;
    const Rect box = l.score, heading = l.caption;
    plate(c_, box, 0xff11101c);
    c_.gradient(heading, 0xff191a2a, 0xff141321);
    c_.hline(box.x, heading.y, box.w, 0xff46445a);
    const int titleX = box.x + 14;
    const float titleZoom = heading.h >= 41 ? 1.13f : .97f;
    const int titleW = int(std::ceil(textWidth("Step sequencer", font::large) * titleZoom));
    controlText(c_, {titleX, heading.y, titleW + 1, heading.h}, "Step sequencer", font::large, light, titleZoom);
    const int active = focus(), n = std::clamp(int(pattern_.steps), 0, 32), count = columns();
    int railStart = titleX + titleW + 20;
    if (n)
    {
        const std::string key = std::string(tbmpc::rootChoices[int(readings_[id::root].value)]) + " / " +
                                readings_[id::scale].text;
        const int keyW = textWidth(key, font::normal);
        const int mid = heading.y + heading.h / 2;
        c_.line(railStart - 8, mid + 6, railStart - 4, mid - 6, 1.f, 0xff69657b);
        controlText(c_, {railStart + 4, heading.y, keyW, heading.h}, key, font::normal, muted, .92f);
        railStart += keyW + 20;
        char current[4], length[8];
        std::snprintf(current, sizeof(current), "%02d", active + 1);
        std::snprintf(length, sizeof(length), "/ %02d", n);
        const Rect r = l.counter;
        if (heading.h >= 41)
            controlText(c_, {r.x + 3, r.y - 2, r.w / 2, r.h}, current, font::large, 0xff40384f, titleZoom, 2);
        controlText(c_, {r.x, r.y, r.w / 2, r.h}, current, font::large, light, titleZoom, 2);
        controlText(c_, {r.x + r.w / 2 + 6, r.y + 1, r.w / 2 - 6, r.h - 1}, length, font::normal, muted, .9f);
    }
    const int railY = heading.y + heading.h / 2;
    c_.hline(railStart, railY + 3, l.counter.x - railStart - 6, 0xff423e53);
    c_.line(l.counter.x - 7, railY + 3, l.counter.x - 1, railY - 3, 1, 0xff77738e);
    c_.hline(l.counter.x, railY - 3, 7, 0xff77738e);

    const Rect play = l.play;
    const bool notes = readings_[id::mode].value >= .5, sync = pattern_.running && !localPlay_;
    const char* action = notes ? "Notes" : localPlay_ ? "Stop" : sync ? "Sync" : "Play";
    const uint32_t keyInk = notes ? muted : 0xff19221b;
    c_.fillChamfer(play, 7, notes ? 0xff373344 : sync ? 0xffa5cbea : lime);
    const int nameW = textWidth(action, font::normal), icon = play.x + (play.w - nameW - 19) / 2;
    const int mid = play.y + play.h / 2;
    if (localPlay_ || notes)
        c_.fillChamfer({icon, mid - 4, 9, 9}, 1, keyInk);
    else if (sync)
    {
        c_.line(icon, mid + 4, icon + 5, mid - 4, 2.f, keyInk);
        c_.line(icon + 5, mid + 4, icon + 10, mid - 4, 2.f, keyInk);
    }
    else
        for (int i = 0; i < 7; ++i) c_.fill(icon + i, mid - (6 - i), 1, 2 * (6 - i) + 1, keyInk);
    controlText(c_, {icon + 18, play.y, nameW + 2, play.h}, action, font::normal, keyInk);
    constexpr const char* labels[]{"Step", "Pitch", "Gate", "Accent", "Slide"};
    for (int lane = 0; lane < 5; ++lane)
    {
        const Rect r = l.lane(lane);
        c_.fill(box.x + 2, r.y, l.gutter - 3, r.h, 0xff1b1828);
        c_.hline(box.x + 2, r.y, box.w - 4, 0xff473d56);
        if (lane >= 2) mark(c_, lane, box.x + 14, r.y + r.h / 2, 4, laneInk[lane]);
        controlText(c_, {box.x + (lane >= 2 ? 26 : 11), r.y, l.gutter - (lane >= 2 ? 30 : 15), r.h}, labels[lane],
                    font::normal, lane < 2 ? light : laneInk[lane], r.h < 19 ? .85f : 1.f);
    }
    const Rect pitch = l.lane(1);
    const bool chromeNotes = count == 16 && pitch.h >= 65 && l.column(0, count).w >= 45;
    const int labelH = chromeNotes ? std::clamp(pitch.h * 47 / 100, 23, 46) : std::clamp(pitch.h / 3, 13, 25);
    const int low = pitch.y + pitch.h - labelH - 5, high = pitch.y + 5;
    int pmin = 0, pmax = 0;
    if (n)
    {
        const auto limits = std::minmax_element(pattern_.pitch, pattern_.pitch + n);
        pmin = *limits.first;
        pmax = *limits.second;
    }
    auto pitchY = [&](int step)
    {
        return pmax == pmin ? (low + high) / 2
                            : low - int(double(pattern_.pitch[step] - pmin) * (low - high) / (pmax - pmin));
    };
    for (int i = 0; i < count; ++i)
    {
        const Rect col = l.column(i, count);
        const bool live = i < n, hit = live && i == active;
        const uint32_t bed = !live ? 0xff111019 : hit ? 0xff2a2943 : (i / 4) % 2 ? 0xff171320 : 0xff13121e;
        c_.fill(col, bed);
        c_.fill(col.x, col.y, i % 4 ? 1 : 2, col.h, i % 4 ? 0xff312a3d : 0xff615770);
        const Rect ruler = l.lane(0);
        if (!hit)
        {
            if (i == hoverStep_ && live) c_.hline(col.x + 2, ruler.y, col.w - 3, lilac);
            char number[4];
            std::snprintf(number, sizeof(number), "%02d", i + 1);
            controlText(c_, {col.x, ruler.y, col.w, ruler.h}, number, font::normal, live ? muted : quiet, .85f, 1);
        }
        for (int lane = 1; lane < 5; ++lane)
            c_.hline(col.x + 1, l.lane(lane).y, col.w - 1, 0xff382f45);
        if (hit) c_.fill(col.x + 1, pitch.y, 1, col.y + col.h - pitch.y, mix(bed, lime, .43f));
        if (!live) continue;
        const int mid = col.x + col.w / 2, markSize = std::clamp(col.w / 9, 3, 6);
        const uint32_t bits = uint32_t(1) << i;
        for (int lane = 2; lane < 5; ++lane)
        {
            const Rect row = l.lane(lane);
            const bool on = ((lane == 2   ? pattern_.gates
                              : lane == 3 ? pattern_.accents
                                          : pattern_.slides) &
                             bits) != 0;
            if (on)
            {
                const uint32_t color = laneInk[lane];
                const int pad = std::max(3, col.w / 8);
                const Rect cell{col.x + pad, row.y + 2, col.w - 2 * pad, row.h - 4};
                c_.fillChamfer(cell, 2, mix(bed, color, hit ? .26f : .15f));
                mark(c_, lane, mid, row.y + row.h / 2, markSize, color);
            }
            else
                c_.fill(mid - 2, row.y + row.h / 2, 4, 1, quiet);
        }
        const Rect note{col.x + 2, low + 3, col.w - 4, labelH};
        if (chromeNotes)
        {
            constexpr auto& glyph = tbmpc::thorn::pitch;
            std::array<uint32_t, glyph.w * glyph.h> tile;
            p_->panel->pitchTile(tile.data(), pattern_.pitch[i], bed);
            c_.blitSharp(tile.data(), glyph.w, glyph.h, fit({0, 0, glyph.w, glyph.h}, note));
        }
        else
        {
            const auto name = noteName(pattern_.pitch[i]);
            const float zoom = std::min(.92f, float(note.w) / textWidth(name, font::normal));
            controlText(c_, note, name, font::normal, hit ? light : 0xffd1d3e6, zoom, 1);
        }
    }
    // Draw the pitch ribbon after the cells so a slide crosses the column divider.
    for (int i = 0; i < n; ++i)
    {
        const Rect col = l.column(i, count);
        const int pad = std::clamp(col.w / 8, 3, 8), y = pitchY(i), x = col.x + pad, end = col.x + col.w - pad;
        const bool gated = (pattern_.gates >> i) & 1u;
        if (i && ((pattern_.slides >> (i - 1)) & 1u))
        {
            const Rect prev = l.column(i - 1, count);
            c_.line(prev.x + prev.w - std::clamp(prev.w / 8, 3, 8), pitchY(i - 1), x, y, 1.6f, laneInk[4]);
        }
        const uint32_t color = i == active ? 0xffe6ffbe : ice;
        if (gated)
        {
            c_.fillChamfer({x, y - 2, end - x, 5}, 1, mix(color, ground, .4f));
            c_.gradient({x + 1, y - 2, end - x - 2, 3}, mix(color, light, .45f), color);
        }
        else
            c_.frame({x, y - 2, end - x, 4}, quiet);
    }
}

void PanelUI::paintSection(size_t index)
{
    const auto& section = sections[index];
    const Rect group = layout_.groups[index];
    plate(c_, group, 0xff171520);
    const int hh = layout_.headerHeight;
    c_.gradient({group.x + 4, group.y + 1, group.w - 8, hh - 1}, mix(glass, section.ink, .12f), 0xff1b1826);
    const int mid = group.y + hh / 2, left = group.x + 19, right = group.x + group.w - 9;
    const float zoom = hh < 25 ? .78f : .88f;
    const int labelW = int(std::ceil(textWidth(section.name, font::large) * zoom)) + 1;
    c_.line(group.x + 5, mid + 5, group.x + 11, mid - 5, 2.1f, section.ink);
    controlText(c_, {left, group.y, labelW, hh}, section.name, font::large, light, zoom);
    const int rail = left + labelW + 9;
    const uint32_t etch = mix(glass, section.ink, .25f);
    c_.line(rail, mid + 3, rail + 5, mid - 2, 1.f, etch);
    c_.hline(rail + 5, mid - 2, right - rail - 5, etch);
    for (int j = 0; j < 2 * section.columns; ++j)
        paintSlot(section.slots[j], section.ink);
}

void PanelUI::paintSlot(int slot, uint32_t ink)
{
    const Rect r = layout_.slots[slot];
    const bool hot =
        slot == hover_ || (slot < int(id::count) && paramIndex_[slot] == dragParam_) || slot == dragPlot_;
    const uint32_t base = hot ? 0xff282336 : 0xff191622;
    c_.fill({r.x + 1, r.y + 1, r.w - 1, r.h - 1}, base);
    c_.hline(r.x, r.y, r.w, hot ? ink : 0xff393141);
    c_.fill(r.x, r.y, 1, r.h, 0xff393141);
    if (slot >= filterPlot)
    {
        paintPlot(slot, ink);
        return;
    }

    const Reading& reading = readings_[slot];
    const auto& param = tbmpc::paramTable[tbmpc::paramIndex(slot)];
    const int pad = std::clamp(r.w / 15, 5, 11), left = r.x + pad;
    const bool choice = param.kind == tbmpc::Kind::choice;
    const int labelH = std::clamp(r.h * 41 / 100, 15, 27), top = r.y + std::max(1, (r.h - 63) / 3);
    const bool dial = (slot == int(id::cutoff) || slot == int(id::resonance) || slot == int(id::envMod) ||
                       slot == int(id::decay) || slot == int(id::accent) || slot == int(id::volume)) &&
                      reading.cc < 0;
    const float radius = std::clamp(r.h * .31f, 10.f, 22.f);
    const int dialW = dial ? int(2 * radius) + 3 : 0, badgeW = reading.cc >= 0 ? 34 : 0;
    const char* label = fieldName(slot);
    const int labelW = r.w - 2 * pad - std::max(dialW, badgeW);
    const float labelZoom = textWidth(label, font::normal) <= labelW && labelH >= 18 ? .90f : .98f;
    const auto& face = textWidth(label, font::normal) <= labelW && labelH >= 18 ? font::normal : font::small;
    controlText(c_, {left, top + 1, labelW, labelH}, label, face, 0xffc0b9cd, labelZoom);
    if (reading.cc >= 0)
    {
        char badge[10];
        std::snprintf(badge, sizeof(badge), "CC %d", reading.cc);
        controlText(c_, {r.x + r.w - pad - badgeW, top + 1, badgeW, labelH}, badge, font::small, lilac, .82f, 2);
    }
    if (dial)
        c_.knob(r.x + r.w - pad - radius, r.y + r.h * .53f, radius, float(norm(paramIndex_[slot])), false, ink,
                0xff474051, 0xff282532, 0xff7b748c, hot);
    const int valueY = top + labelH - 1, valueH = std::min(r.y + r.h - 4 - valueY, 33);
    if (slot == int(id::glide))
    {
        const int usable = r.w - 2 * pad;
        for (int i = 0; i < 2; ++i)
        {
            const Rect option{left + i * usable / 2, valueY + 1, usable / 2 - 2, valueH - 1};
            const bool chosen = int(reading.value) == i;
            plate(c_, option, chosen ? 0xff453153 : 0xff1e1b27, chosen ? ink : edge);
            controlText(c_, option, tbmpc::glideChoices[i], font::normal, chosen ? light : muted, .92f, 1);
        }
        return;
    }
    const int valueW = r.w - 2 * pad - dialW - (choice ? 10 : 0);
    const bool big = valueH >= 22 && textWidth(reading.text, font::large) <= valueW;
    const auto& valueFace = big ? font::large : font::normal;
    const float zoom =
        std::min(1.f, std::max(.72f, float(valueW) / std::max(1, textWidth(reading.text, valueFace))));
    controlText(c_, {left, valueY, valueW, valueH}, reading.text, valueFace, light, zoom);
    if (choice)
    {
        const int x = r.x + r.w - pad - 3, y = valueY + valueH / 2;
        c_.line(x - 3, y - 1, x, y + 2, 1.4f, ink);
        c_.line(x, y + 2, x + 3, y - 1, 1.4f, ink);
    }
    else if (!dial)
    {
        const int rail = r.w - 2 * pad, y = r.y + r.h - 4;
        c_.fill(left, y, rail, 2, 0xff393241);
        c_.fill(left, y, int(std::lround(norm(paramIndex_[slot]) * rail)), 2, ink);
    }
}

void PanelUI::paintPlot(int slot, uint32_t groupInk)
{
    using namespace tbmpc;
    const bool filter = slot == filterPlot, pitch = slot == pitchPlot;
    const Rect r = layout_.slots[slot], plot = layout_.plotArea(slot);
    const auto value = [&](int param) { return float(readings_[param].value); };
    const uint32_t color = filter ? groupInk : pitch ? ice : lilac;
    const char* title = filter ? "Filter response" : pitch ? "Pitch LFO" : "Cutoff LFO";
    controlText(c_, {r.x + 6, r.y + 2, r.w - 12, plot.y - r.y - 2}, title, font::small, color);
    c_.gradient(plot, 0xff100d1b, 0xff161324);
    const int width = plot.w - 1, height = plot.h - 1;
    const int bottom = plot.y + height;
    for (int i = 1; i < 4; ++i)
        c_.fill(plot.x + width * i / 4, plot.y, 1, height, 0xff302a3d);
    c_.hline(plot.x, plot.y + height / 2, plot.w, 0xff443a54);
    const float cutoff = value(id::cutoff), res = value(id::resonance);
    const TextContext ctx{res, value(id::filter), value(id::peakSpread)};
    const int kind = int(ctx.filter);
    const float rate = value(pitch ? id::pitchLfoSpeed : id::cutoffLfoSpeed);
    const auto* detent = lfoDetent(rate);
    const double cycles = detent ? detent->cyclesPerBeat : rate;
    const float amount = value(pitch ? id::pitchLfoAmount : id::cutoffLfoAmount);
    double hz = 1, ratio = 1, reference = 1;
    const float feedback = feedbackOf(kind, res);
    if (filter)
    {
        hz = cutoffHz(0, ctx) / nominalHz(kind, cutoff);
        ratio = std::exp2(double(cutoffOctaves) / width);
        reference =
            filterGain2(kind, feedback, ctx.spread, kind == filterTwinPeak ? std::exp2(ctx.spread * .5) : 1e-3);
    }
    c_.setClip(plot);
    float previous = 0;
    for (int i = 0; i <= width; ++i)
    {
        double y;
        if (filter)
        {
            const double db = 10 * std::log10(filterGain2(kind, feedback, ctx.spread, hz) / reference + 1e-12);
            y = plot.y + (18 - std::clamp(db, -36., 18.)) * height / 54.;
            hz *= ratio;
        }
        else
            y = plot.y + height * (.5 - .45 * amount * std::sin(6.28318530718 * cycles * i / width));
        if (i) c_.line(plot.x + i - 1, previous, plot.x + i, float(y), 1.5f, color);
        previous = float(y);
    }
    const float xVal = filter ? cutoff : float(std::log(rate / lfoMinRate) / std::log(lfoMaxRate / lfoMinRate));
    const int x = std::clamp(plot.x + int(std::lround(width * xVal)), plot.x + 3, plot.x + width - 3);
    const int y = std::clamp(bottom - int(std::lround(height * (filter ? res : amount))), plot.y + 3, bottom - 3);
    c_.disc(x, y, 3, 0xff252030);
    c_.ring(x, y, 1.6f, 3, color);
    c_.resetClip();
}
} // namespace hdslave_desktop
