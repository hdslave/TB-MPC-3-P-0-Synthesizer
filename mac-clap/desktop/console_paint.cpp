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
constexpr uint32_t ground = 0xff080812, glass = 0xff21192e, edge = 0xff685d80, light = 0xfff1efff,
                   muted = 0xffb9b1cb, steel = 0xffb6c8fa, ice = 0xff96b7ff, lilac = 0xffd1afff, quiet = 0xff938aa8;
constexpr uint32_t disabledLabel = 0xff84848e, disabledValue = 0xff9999a3;

// The hardware faceplates cut the upper-right and lower-left corners.
void faceplate(Canvas& c, Rect r, uint32_t top, uint32_t bottom, uint32_t outline, int cut = 4)
{
    const int x = r.x * c.s, y = r.y * c.s, w = r.w * c.s, h = r.h * c.s;
    const int bevel = std::min({cut * c.s, w / 2, h / 2});
    for (int j = 0; j < h; ++j)
    {
        const int left = x + std::max(0, bevel - (h - 1 - j));
        const int right = x + w - std::max(0, bevel - j);
        c.fillPhys(left, y + j, right, y + j + 1, outline);
        if (j >= c.s && j < h - c.s)
            c.fillPhys(left + c.s, y + j, right - c.s, y + j + 1, mix(top, bottom, float(j) / h));
    }
}
void plate(Canvas& c, Rect r, uint32_t fill = glass, uint32_t outline = edge)
{
    faceplate(c, r, fill, mix(fill, ground, .45f), outline);
}
void disabledPlate(Canvas& c, Rect r, int cut)
{
    faceplate(c, r, 0xff1a1a21, 0xff121219, 0xff3b3b45, cut);
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
    const int density = glyphScale <= 1.2f ? 1 : glyphScale <= 2.4f ? 2 : 4;
    const int priorLevels = density == 1 ? 0 : density == 2 ? 1 : 5;
    const int priorRows = face.height == 15 ? 0 : face.height == 20 ? 15 : 35;
    const int stride = 24 * density, rows = face.height * density;
    const auto* atlas = desktopTypeMasks();
    if (!atlas) return;
    atlas += 95 * 24 * (60 * priorLevels + priorRows * density * density);
    const int lineHeight = int(std::lround(face.height * glyphScale));
    float cursor = float(box.x * c.s);
    if (align) cursor += (box.w * c.s - textWidth(fitted, face) * glyphScale) * (align == 1 ? .5f : 1.f);
    const int y = box.y * c.s + (box.h * c.s - lineHeight) / 2;
    for (const char ch : fitted)
    {
        const auto glyph = glyphIndex(ch);
        const int advance = face.widths[glyph];
        const auto* mask = atlas + glyph * stride * rows;
        const int start = int(std::lround(cursor));
        cursor += advance * glyphScale;
        const int width = int(std::lround(cursor)) - start;
        const auto coverage = [&](int row, int col)
        {
            return int(mask[row * stride + col]);
        };
        // Sample at physical pixel centers so Retina text keeps soft edges.
        for (int dy = 0; dy < lineHeight; ++dy)
        {
            const float v = std::clamp((dy + .5f) * rows / lineHeight - .5f, 0.f, float(rows - 1));
            const int v0 = int(v), v1 = std::min(v0 + 1, rows - 1);
            for (int dx = 0; dx < width; ++dx)
            {
                const float u = std::clamp((dx + .5f) * advance * density / width - .5f, 0.f, float(advance * density - 1));
                const int u0 = int(u), u1 = std::min(u0 + 1, advance * density - 1);
                const float mixX = u - u0, mixY = v - v0;
                const float top = coverage(v0, u0) * (1 - mixX) + coverage(v0, u1) * mixX;
                const float bottom = coverage(v1, u0) * (1 - mixX) + coverage(v1, u1) * mixX;
                const unsigned alpha = unsigned((top * (1 - mixY) + bottom * mixY) + .5f);
                if (alpha) c.plot(start + dx, y + dy, color, alpha);
            }
        }
    }
}

void button(Canvas& c, Rect r, std::string_view text, bool selected = false)
{
    faceplate(c, r, selected ? 0xff4d75de : 0xff2b203b, selected ? 0xff202f7f : 0xff100d1b,
              selected ? 0xffb1c9ff : edge);
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
        if (!readings_[hover_].enabled)
            status = hover_ == int(id::peakSpread) ? "Select Twin Peak to edit Peak spread" :
                                                     "Select 303 slide or Notes mode to edit 303 time";
        else if (hover_ == int(id::regen)) status = "Roll a new pattern seed";
        else if (hover_ == int(id::seedBack)) status = "Click the seed number to recall a previously played seed";
        else if (hover_ == int(id::seed)) status = "Drag the barcode to set the pattern seed";
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
        status = "Slink " TBMPC_VERSION;
    controlText(c_, layout_.status, status, font::normal, muted, .9f);
}

Rect PanelUI::paintArt(bool full, bool refreshHeader, uint64_t now)
{
    constexpr int sw = SIXTEEN_SCREEN_WIDTH, sh = SIXTEEN_SCREEN_HEIGHT;
    sixteen_screen_surface_t surface{art_.data(), sw, sh, sw, uint32_t(now)};
    const int button = readings_[id::mode].value >= .5 ? 3 : localPlay_ ? 1 : pattern_.running ? 2 : 0;
    int cameraFrame = 0;
    if (!p_->panel->renderArt(p_->inner, surface, refreshHeader, selected_, int(readings_[id::seed].value), button, cameraFrame) && !full)
        return {};
    Rect damage;
    const auto changed = [&](Rect src) {
        bool touched = full;
        for (int y = src.y; y < src.y + src.h; ++y)
        {
            const size_t pos = size_t(y) * sw + src.x, bytes = size_t(src.w) * sizeof(uint32_t);
            if (!full && !std::memcmp(art_.data() + pos, previousArt_.data() + pos, bytes)) continue;
            std::memcpy(previousArt_.data() + pos, art_.data() + pos, bytes);
            touched = true;
        }
        return touched;
    };
    const auto blit = [&](Rect src, Rect dst) {
        c_.blitSharp(art_.data() + src.y * sw + src.x, src.w, src.h, dst, sw);
        damage = damage.unite(dst);
    };

    // These are adjacent regions of the live hardware framebuffer, including its frame,
    // glass, seed, count, pitch preview and transport. Update each at its own cadence.
    const Rect brand{0, 0, 182, 100}, camera{182, 0, 340, 100}, lcd{522, 0, 193, 100}, regen{715, 0, 65, 100};
    if (changed(brand))
    {
        const Rect dst = layout_.headRect(brand);
        if (brandPlate_.paint(c_,637,dst))
        {
            const unsigned seed=unsigned(readings_[id::seed].value)&65535u;
            char digits[5];
            std::snprintf(digits,sizeof(digits),"%04X",seed);
            controlText(c_,layout_.headRect({12,70,60,24}),digits,font::large,light,layout_.mast.h*.010f);
            for (int i=0;i<20;++i)
                c_.fill(layout_.headRect({72+i*98/20,59,((seed>>(i%16))&1)?4:1,26}),i%6?0xff919fbe:0xffced7ff);
            damage=damage.unite(dst);
        }
        else blit(brand,dst);
        c_.fill(layout_.logo, 0xff10101b);
        c_.blitScaled(desktopWordmark(), 680, 108, layout_.headRect({10, 12, 164, 26}), 680, screenInk);
    }
    if (changed(camera))
    {
        const Rect bay=layout_.headRect(camera);
        Rect dst=bay;
        if (bay.w>bay.h*9/2)
        {
            if (full) c_.fill(bay,ground);
            dst=fit(camera,bay);
        }
        if (!crestPlate_.paint(c_,cameraFrame,dst)) blit(camera,dst);
        damage=damage.unite(bay);
    }
    if (changed(lcd))
    {
        const Rect dst=layout_.headRect(lcd);
        if (lcdPlate_.paint(c_,601+focus(),dst))
        {
            transportPlate_.paint(c_,633+button,layout_.play);
            controlText(c_,layout_.headRect({597,77,23,13}),"/"+std::to_string(pattern_.steps),
                        font::small,muted,layout_.mast.h*.009f,2);
            const int first=focus()/16*16, count=std::min(16,int(pattern_.steps)-first);
            if (count>0)
            {
                const auto limits=std::minmax_element(pattern_.pitch+first,pattern_.pitch+first+count);
                const int span=std::max(128,int(*limits.second)-*limits.first);
                int previous=0;
                for (int i=0;i<count;++i)
                {
                    const int n=first+i, x=625+i*77/16;
                    const int y=38-(pattern_.pitch[n]-*limits.first)*19/span;
                    if (i && ((pattern_.slides>>(n-1))&1u))
                        c_.fill(layout_.headRect({x-1,std::min(y,previous),1,std::abs(y-previous)+1}),0xff7689c0);
                    c_.fill(layout_.headRect({x,y,3,2}),n==focus()?0xffcabaff:0xff7c8cc6);
                    previous=y;
                }
            }
            damage=damage.unite(dst);
        }
        else blit(lcd,dst);
    }
    if (changed(regen))
    {
        blit(regen,layout_.headRect(regen));
        regenPlate_.paint(c_,638,layout_.headRect({720,63,53,26}));
    }
    return damage;
}

void PanelUI::paintRibbon()
{
    for (int i = 0; i < 2; ++i)
    {
        const int slot = ribbonParams[bank_][i];
        const bool enabled = readings_[slot].enabled;
        const Rect r = layout_.ribbon[i];
        if (enabled) faceplate(c_, r, 0xff292036, 0xff151121, hover_ == slot ? ice : 0xff554962);
        else disabledPlate(c_, r, 4);
        const int pad = std::clamp(r.w / 22, 6, 12);
        const int valueW = std::min(r.w * 54 / 100, textWidth(readings_[slot].text, font::normal) + 2);
        const char* label = slot == int(id::pitchLfoAmount) ? "Pitch depth" :
                            slot == int(id::cutoffLfoAmount) ? "Cutoff depth" : fieldName(slot);
        controlText(c_, {r.x + pad, r.y, r.w - 2 * pad - valueW - 3, r.h}, label, font::normal,
                    enabled ? muted : disabledLabel, .92f);
        controlText(c_, {r.x + r.w - pad - valueW, r.y, valueW, r.h}, readings_[slot].text, font::normal,
                    enabled ? light : disabledValue, .96f, 2);
    }
    const Rect r = layout_.legend;
    faceplate(c_, r, 0xff241c34, 0xff141020, 0xff4a405c);
    constexpr const char* names[]{"Gate", "Accent", "Slide"};
    const int pad = 5, gap = 14, symbol = 13;
    int total = gap * 2;
    for (const char* name : names) total += symbol + pad + textWidth(name, font::normal);
    int x = r.x + (r.w - total) / 2;
    for (int i = 0; i < 3; ++i)
    {
        const int w = textWidth(names[i], font::normal);
        mark(c_, i + 2, x + symbol / 2, r.y + r.h / 2, 5, i == 0 ? steel : light);
        controlText(c_, {x + symbol + pad, r.y, w + 1, r.h}, names[i], font::normal, light);
        x += symbol + pad + w + gap;
    }
}

void PanelUI::paintFooter()
{
    const auto& l = layout_;
    faceplate(c_, l.keyScale, 0xff291d39, 0xff141021, edge);
    const Rect root = l.keyFields[0], scale = l.keyFields[1];
    controlText(c_, {root.x + 6, root.y, root.w - 13, root.h}, readings_[id::root].text, font::normal, light, .94f);
    controlText(c_, {scale.x - 9, scale.y, 9, scale.h}, "/", font::normal, muted, .92f);
    controlText(c_, scale.inset(3), readings_[id::scale].text, font::normal, light, .94f);
    c_.hline(l.storm.x, l.storm.y - 1, l.storm.w, 0xff514f70);
    c_.hline(l.storm.x, l.storm.y + l.storm.h, l.storm.w, 0xff514f70);
}

void PanelUI::paintScore()
{
    const auto& l = layout_;
    c_.fill(l.score, ground);
    const Rect pitch = l.lane(1);
    const int noteH = std::clamp(pitch.h * 53 / 100, 24, 64), noteY = pitch.y + pitch.h - noteH;
    constexpr const char* labels[]{"STEP", "PITCH", "GATE", "ACCENT", "SLIDE"};
    for (int j = 0; j < 5; ++j)
    {
        const Rect row = l.lane(j), label{l.score.x, row.y, l.gutter - 4, row.h - (j == 1 ? noteH : 0) - 1};
        faceplate(c_, label, 0xff32263f, 0xff191329, 0xff736d88);
        controlText(c_, {label.x + 6, label.y, label.w - 9, label.h}, labels[j], font::normal, light,
                    label.h < 20 ? .84f : 1.f);
    }
    const Rect noteLabel{l.score.x, noteY, l.gutter - 4, noteH - 1};
    faceplate(c_, noteLabel, 0xffdee3f0, 0xff909bb6, 0xff736d88);
    controlText(c_, {noteLabel.x + 6, noteY, noteLabel.w - 9, noteLabel.h}, "NOTE", font::large, 0xff19172b);

    const int n = std::clamp(int(pattern_.steps), 0, 32), count = columns(), active = focus();
    const int high = pitch.y + 6, low = noteY - 5;
    const bool chromeNotes = noteH >= 25 && l.column(0, count).w >= (count == 16 ? 41 : 55);
    const auto limits = std::minmax_element(pattern_.pitch, pattern_.pitch + n);
    const auto pitchY = [&](int i) {
        const int range = std::max(128, int(*limits.second) - int(*limits.first));
        return low - (pattern_.pitch[i] - *limits.first) * (low - high) / range;
    };
    for (int i = 0; i < count; ++i)
    {
        const Rect col = l.column(i, count), ruler = l.lane(0);
        const bool live = i < n, selected = live && i == active;
        const int left = col.x + (i && i % 4 == 0 ? 3 : 1), w = col.x + col.w - left - 1, mid = left + w / 2;
        const uint32_t upper = selected ? 0xff527be7 : !live ? 0xff191622 : (i / 4) % 2 ? 0xff30213f : 0xff262139;
        const uint32_t lower = selected ? 0xff263b98 : !live ? 0xff100d17 : (i / 4) % 2 ? 0xff1a112c : 0xff141224;
        const uint32_t lip = selected ? 0xffa4bfff : 0xff52465f;
        c_.gradient({left, ruler.y, w, ruler.h - 1}, upper, lower);
        c_.hline(left, ruler.y, w, i == hoverStep_ ? light : lip);
        if (selected)
            for (int j = 0; j < 5; ++j) c_.fill(left + 1, ruler.y + 1 + j, 5 - j, 1, light);
        else if (i % 4 == 0) c_.hline(left + 2, ruler.y + 2, 6, muted);
        char number[4];
        std::snprintf(number, sizeof(number), "%02d", i + 1);
        controlText(c_, {left, ruler.y, w, ruler.h}, number, font::normal, live ? light : quiet, ruler.h < 19 ? .85f : 1.f, 1);

        c_.fill(col.x, pitch.y, col.w, noteY - pitch.y, selected ? 0xff293b80 : 0xff100d1a);
        c_.fill(col.x, pitch.y, 1, noteY - pitch.y, i % 4 ? 0xff302840 : 0xff655975);
        for (int line = 0; line < 4; ++line)
            c_.hline(col.x + 1, high - 2 + line * (low - high + 3) / 3, col.w - 1, 0xff302840);
        if (selected) c_.fill(col.x + 1, pitch.y, col.w - 1, 2, ice);
        if (live && ((pattern_.accents >> i) & 1u)) mark(c_, 3, mid, pitch.y + 5, 3, ice);

        const Rect key{left, noteY, w, noteH - 1};
        const uint32_t bed = selected ? 0xff101629 : 0xff07070c;
        faceplate(c_, key, bed, bed, selected ? 0xff8ca8fc : 0xff342c43, 4);
        if (live && chromeNotes)
        {
            constexpr auto& glyph = tbmpc::thorn::pitch;
            std::array<uint32_t, glyph.w * glyph.h> tile;
            p_->panel->pitchTile(tile.data(), pattern_.pitch[i], bed, selected);
            // Trim the transparent margin around the hardware glyphs, including the sharp and octave.
            const Rect ink{2, 3, glyph.w - 4, glyph.h - 6};
            c_.blitSharp(tile.data() + ink.y * glyph.w + ink.x, ink.w, ink.h, fit(ink, key.inset(2)), glyph.w);
        }
        else if (live)
        {
            const auto name = noteName(pattern_.pitch[i]);
            const float zoom = std::min(1.f, float(key.w - 4) / textWidth(name, font::normal));
            controlText(c_, key, name, font::normal, selected ? light : 0xffd1d3e6, zoom, 1);
        }

        const int size = std::clamp(std::min(w / 7, l.lane(2).h / 4), 3, 7);
        for (int j = 2; j < 5; ++j)
        {
            const Rect row = l.lane(j);
            c_.gradient({left, row.y, w, row.h - 1}, upper, lower);
            c_.hline(left, row.y, w, lip);
            if (!live) continue;
            const int cy = row.y + row.h / 2;
            const bool on = ((j == 2 ? pattern_.gates : j == 3 ? pattern_.accents : pattern_.slides) >> i) & 1u;
            const uint32_t ink = selected ? 0xffffffff : 0xffd6d9ea;
            if (j == 2)
            {
                const Rect gate{mid - size, cy - size, 2 * size, 2 * size};
                if (on)
                {
                    c_.fill(gate, ink);
                    for (int k = -1; k <= 1; ++k) c_.fill(mid + k * (size/2), cy + size - 2, 1, 2, lower);
                }
                else c_.frame(gate, selected ? light : muted);
            }
            else if (!on) c_.fill(mid - 2, cy, 5, 1, selected ? light : quiet);
            else if (j == 3)
                for (int k = -size; k <= size; ++k) c_.fill(mid - size + std::abs(k), cy + k, 2 * (size - std::abs(k)) + 1, 1, ink);
            else mark(c_, 4, mid, cy, size, ink);
        }
    }
    // The performance ribbon and the editable switches share the same columns and clock.
    for (int i = 0; i < n; ++i)
    {
        const Rect col = l.column(i, count);
        const int pad = std::clamp(col.w / 9, 3, 7), y = pitchY(i), x = col.x + pad, end = col.x + col.w - pad;
        if (i && ((pattern_.slides >> (i - 1)) & 1u))
        {
            const Rect prev = l.column(i - 1, count);
            const int x0 = prev.x + prev.w - std::clamp(prev.w / 9, 3, 7), y0 = pitchY(i - 1);
            float lastX = float(x0), lastY = float(y0);
            for (int j = 1; j <= 6; ++j)
            {
                const float t = j / 6.f, xx = x0 + (x - x0) * t, yy = y0 + (y - y0) * t * t * (3 - 2 * t);
                c_.line(lastX, lastY, xx, yy, 1.35f, muted);
                lastX = xx; lastY = yy;
            }
        }
        if ((pattern_.gates >> i) & 1u) c_.fill(x, y - 1, end - x, 4, i == active ? light : steel);
        else c_.frame({x, y - 2, end - x, 4}, muted);
    }
}

void PanelUI::paintSection(size_t index)
{
    const auto& section = sections[index];
    const Rect group = layout_.groups[index];
    c_.fill(group, ground);
    const int hh = layout_.headerHeight;
    const bool active = int(index) == bank_;
    const Rect heading = layout_.heading(index);
    faceplate(c_, heading, active ? 0xff323358 : 0xff292034, active ? 0xff1b203d : 0xff161121,
              active ? 0xff8099d7 : 0xff544a65);
    controlText(c_, {heading.x + 9, heading.y, heading.w - 17, heading.h}, section.name, font::normal,
                active ? light : 0xffd7d4e6, hh < 22 ? .84f : .97f);
    if (active) c_.fill(heading.x + 7, heading.y + heading.h - 2, heading.w - 14, 1, ice);
    for (int j = 0; j < 2 * section.columns; ++j)
        paintSlot(section.slots[j], section.ink);
}

void PanelUI::paintSlot(int slot, uint32_t ink)
{
    const Rect r = layout_.slots[slot];
    const bool enabled = slot >= filterPlot || readings_[slot].enabled;
    const bool hot = enabled &&
        (slot == hover_ || (slot < int(id::count) && paramIndex_[slot] == dragParam_) || slot == dragPlot_);
    c_.fill(r, ground);
    const Rect key{r.x + 1, r.y + 1, r.w - 2, r.h - 2};
    if (enabled)
        faceplate(c_, key, hot ? 0xff362e49 : 0xff251d32, hot ? 0xff1e1c31 : 0xff13101e, hot ? ice : 0xff51465f, 3);
    else
        disabledPlate(c_, key, 3);
    if (slot >= filterPlot)
    {
        paintPlot(slot, ink);
        return;
    }

    const Reading& reading = readings_[slot];
    const auto& param = tbmpc::paramTable[tbmpc::paramIndex(slot)];
    const int pad = std::clamp(r.w / 15, 5, 11), left = r.x + pad;
    const bool choice = param.kind == tbmpc::Kind::choice;
    const bool compact = r.h < 40;
    const bool shallow = r.h < 34;
    const int labelH = shallow ? std::clamp(r.h / 3, 9, 11) : std::clamp(r.h * 38 / 100, 12, 24);
    const int top = r.y + (shallow ? 1 : std::max(2, (r.h - 60) / 4));
    const int badgeW = reading.cc >= 0 ? 34 : 0;
    const char* label = fieldName(slot);
    const int labelW = r.w - 2 * pad - badgeW;
    const auto& face = textWidth(label, font::normal) <= labelW && labelH >= 18 ? font::normal : font::small;
    const float labelZoom = std::min(.96f, float(labelH + 1) / face.height);
    controlText(c_, {left, top, labelW, labelH}, label, face, enabled ? muted : disabledLabel, labelZoom);
    if (reading.cc >= 0)
    {
        char badge[10];
        std::snprintf(badge, sizeof(badge), "CC %d", reading.cc);
        controlText(c_, {r.x + r.w - pad - badgeW, top + 1, badgeW, labelH}, badge, font::small,
                    enabled ? lilac : disabledLabel, .82f, 2);
    }
    const int valueY = top + labelH, valueH = std::min(r.y + r.h - (choice ? 3 : 5) - valueY, 33);
    if (slot == int(id::glide))
    {
        const int usable = r.w - 2 * pad;
        for (int i = 0; i < 2; ++i)
        {
            const Rect option{left + i * usable / 2, valueY + 1, usable / 2 - 2, valueH - 1};
            const bool chosen = int(reading.value) == i;
            faceplate(c_, option, chosen ? 0xff4d75de : 0xff292034, chosen ? 0xff202f7f : 0xff141021,
                      chosen ? 0xffb1c9ff : edge, 3);
            controlText(c_, option, tbmpc::glideChoices[i], font::normal, chosen ? light : muted,
                        std::min(.92f, float(option.h + 2) / font::normal.height), 1);
        }
        return;
    }
    const int valueW = r.w - 2 * pad - (choice ? 10 : 0);
    const bool big = valueH >= 22 && textWidth(reading.text, font::large) <= valueW;
    const auto& valueFace = big ? font::large : font::normal;
    const float zoom = std::min({1.f, compact ? float(valueH + 2) / valueFace.height : 1.f,
                                 std::max(.72f, float(valueW) / std::max(1, textWidth(reading.text, valueFace)))});
    controlText(c_, {left, valueY, valueW, valueH}, reading.text, valueFace, enabled ? light : disabledValue, zoom);
    if (choice)
    {
        const int x = r.x + r.w - pad - 3, y = valueY + valueH / 2;
        c_.line(x - 3, y - 1, x, y + 2, 1.4f, ink);
        c_.line(x, y + 2, x + 3, y - 1, 1.4f, ink);
    }
    else
    {
        const int rail = r.w - 2 * pad, y = r.y + r.h - 5;
        c_.fill(left, y, rail, 2, enabled ? 0xff393247 : 0xff32323b);
        const uint32_t fill = enabled ? (hot ? ice : 0xff7389c9) : 0xff61616c;
        c_.fill(left, y, int(std::lround(norm(paramIndex_[slot]) * rail)), 2, fill);
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
        if (filter && i % 4 == 0) c_.fill(plot.x + i, int(y), 1, bottom - int(y) + 1, 0xff3b375f);
        if (i) c_.line(plot.x + i - 1, previous, plot.x + i, float(y), 1.5f, color);
        previous = float(y);
    }
    const float xVal = filter ? cutoff : float(std::log(rate / lfoMinRate) / std::log(lfoMaxRate / lfoMinRate));
    const int x = std::clamp(plot.x + int(std::lround(width * xVal)), plot.x + 3, plot.x + width - 3);
    const int y = std::clamp(bottom - int(std::lround(height * (filter ? res : amount))), plot.y + 3, bottom - 3);
    c_.line(x - 3, y, x, y - 3, 1.3f, light);
    c_.line(x, y - 3, x + 3, y, 1.3f, light);
    c_.line(x + 3, y, x, y + 3, 1.3f, color);
    c_.line(x, y + 3, x - 3, y, 1.3f, color);
    c_.resetClip();
}
} // namespace hdslave_desktop
