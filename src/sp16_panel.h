// SP-16 touch panel. The host paints the six encoder values below this canvas.
#pragma once
#include "screen.h"
#include "sixteen_screen.h"
#include "sp16_graphics.h"
#include <array>

namespace tbmpc::sp16
{
using namespace ParamId;
struct Bank { const char* name; std::array<uint32_t, 6> ids; };
enum Page { performPage, voicePage, glidePage, lfoPage, patternPage, clockPage };
inline constexpr Bank banks[] = {
    {"Perform", {cutoff, resonance, envMod, decay, accent, volume}},
    {"Voice",   {waveform, tuning, filter, peakSpread, drive, volume}},
    {"Glide",   {glide, slideTime, accentDecay, gate, accent, decay}},
    {"LFO",     {pitchLfoAmount, pitchLfoSpeed, cutoffLfoAmount, cutoffLfoSpeed, tuning, transpose}},
    {"Pattern", {seedMode, seed, mutate, density, scale, root}},
    {"Clock",   {mode, clockDiv, swing, reset, steps, octave}}
};
using hdui::Ink;
constexpr Ink bg = 0xff080e15, cell = 0xff101923, grid = 0xff2c3947;
constexpr Ink paper = 0xffedf2f4, muted = 0xffa0b3c5, steel = 0xff9fbfdf, ice = 0xffbadcff;
constexpr int tabTop = 239, gridLeft = 12, gridWidth = 756, gridTop = 104, gridHeight = 125;

struct Raster : hdui::Raster
{
    Raster(uint32_t* pixels, int stride, int height) : hdui::Raster{pixels,stride,height} {}

    static unsigned glyph(char c) { return c >= 32 && c < 127 ? unsigned(c-32) : unsigned('?'-32); }
    static int measure(const char* s, const rave::Font& font)
    {
        int width = 0;
        for (; *s; ++s) width += font.widths[glyph(*s)];
        return width;
    }

    void label(int x, int y, int end, const char* s, Ink fg, const rave::Font& font) const
    {
        for (; *s; ++s)
        {
            unsigned index = glyph(*s);
            int advance = font.widths[index];
            if (x+advance > end) break;
            const auto* ink = font.ink + index*font.height*font.stride;
            for (int dy=0; dy<font.height; ++dy)
            {
                if (y+dy < 0 || y+dy >= h) continue;
                for (int dx=0; dx<std::min(advance+1, end-x); ++dx)
                {
                    const int packed = ink[dy*font.stride+dx/2];
                    const unsigned a = dx%2 ? packed&15 : packed>>4;
                    if (!a) continue;
                    blend(x+dx, y+dy, fg, a);
                }
            }
            x += advance;
        }
    }
    void blend(int x, int y, Ink fg, unsigned a) const
    {
        auto& dest = px[y*w+x];
        const unsigned b = 15-a;
        const unsigned red = (((fg>>16)&255)*a+((dest>>16)&255)*b+7)/15;
        const unsigned green = (((fg>>8)&255)*a+((dest>>8)&255)*b+7)/15;
        const unsigned blue = ((fg&255)*a+(dest&255)*b+7)/15;
        dest = 0xff000000u | (red<<16) | (green<<8) | blue;
    }

    // A vertical slice of a 1.5 px trace, with fractional coverage at each end.
    void trace(int x, double from, double to, Ink fg) const
    {
        const int upper = int(std::lround(16*std::min(from,to))) - 12;
        const int lower = int(std::lround(16*std::max(from,to))) + 12;
        for (int y = upper/16; y <= lower/16; ++y)
        {
            const int coverage = std::min(lower,(y+1)*16) - std::max(upper,y*16);
            blend(x, y, fg, unsigned(coverage*15+8)/16);
        }
    }
    static int readingWidth(const char* s, bool large=false) { return measure(s, large ? rave::large : rave::normal); }
    void reading(int x,int y,int end,const char* s,Ink fg,bool large=false) const
        { label(x,y,end,s,fg,large ? rave::large : rave::normal); }
    void text(int x,int y,const char* s,Ink fg) const { label(x,y,SIXTEEN_SCREEN_WIDTH,s,fg,rave::small); }
    void right(int end,int y,const char* s,Ink fg) const { label(end-measure(s,rave::small),y,end,s,fg,rave::small); }
    void indexed(int x,int y,int width,int height,const unsigned char* source) const
    {
        for (int j=0; j<height; ++j)
            for (int i=0; i<width; ++i)
                px[(y+j)*w+x+i] = rave::palette[*source++];
    }
};

}
#include "sp16_thorn.h"
namespace tbmpc::sp16
{
class Panel
{
public:
    int bank = patternPage;
    ThornPage thorn;
    bool localPlay = false;
    int finger = -1;
    bool dirty = true;
    PatternView shown {};
    std::array<float, paramCount> values {};

    bool turn(int delta)
    {
        if (!delta) return false;
        bank = std::clamp<int64_t>(int64_t(bank) + delta, 0, std::size(banks) - 1);
        dirty = true;
        return true;
    }

    int stepAt(int x, int y) const
    {
        if (bank == patternPage) return thorn.stepAt(shown,x,y);
        if (bank == voicePage || bank == lfoPage) return -1;
        if (x < gridLeft || x >= gridLeft + gridWidth || y < gridTop || y >= gridTop + gridHeight)
            return -1;
        const int rows = shown.steps > 16 ? 2 : 1;
        const int row = ((y - gridTop + 1) * rows - 1) / gridHeight;
        const int col = ((x - gridLeft + 1) * 16 - 1) / gridWidth;
        const int index = row * 16 + col;
        return index < shown.steps ? index : -1;
    }

    bool render(const sixteen_screen_surface_t& s, bool full, const PatternState& pattern, const std::atomic<float>* params)
    {
        const auto next = pattern.read();
        const bool graphPage = bank == voicePage || bank == lfoPage;
        bool changed = full || dirty || shown.seed != next.seed || (!graphPage && !(shown == next));
        const int face = animationFrame(next, s.now_ms);
        shown = next;
        for (uint32_t i = 0; i < paramCount; ++i)
        {
            const float v = params[i].load(std::memory_order_relaxed);
            changed |= v != values[i];
            values[i] = v;
        }
        Raster r{s.pixels, s.stride, s.height};
        if (bank == patternPage)
        {
            if (!wasThorn) { thorn.enter(s.now_ms); changed=true; thorn.menu=false; }
            wasThorn=true; dirty=false;
            char tuning[72];
            char name[48];
            formatParam(paramTable[paramIndex(scale)],value(scale),name,sizeof(name),{});
            std::snprintf(tuning,sizeof(tuning),"%s / %s",rootChoices[int(value(root))],name);
            const int button=value(mode)>=.5f?3:shown.running?(localPlay?1:2):0;
            const bool redraw=thorn.draw(r,shown,s.now_ms,changed,face,int(value(seed)),tuning,button);
            if (changed && thorn.menu) drawTabs(r);
            return redraw;
        }
        wasThorn=false;
        if (!changed && face == lastFace) return false;
        if (!changed)
        {
            paintFace(r, face);
            return true;
        }
        dirty = false;
        r.box(0, 67, SIXTEEN_SCREEN_WIDTH, s.height-67, bg);
        r.indexed(0, 0, 780, 67, rave::header);
        paintFace(r, face);
        char label[64];
        const unsigned dialled = unsigned(value(seed));
        const bool pending = shown.steps && dialled != unsigned(shown.seed);
        std::snprintf(label, sizeof(label), "%04X", dialled);
        r.text(510, 9, pending ? "SEED / PENDING" : value(seedMode) >= .5f ? "SEED / LOCKED" : "SEED / RANDOM", muted);
        r.reading(510, 26, 590, label, pending ? ice : paper, true);
        button(r, 594, 77, "Seed back");
        button(r, 676, 92, "Regen");

        if (bank == voicePage)
            drawVoice(r);
        else if (bank == lfoPage)
            drawLfos(r);
        else
            drawPattern(r);
        drawTabs(r);
        return true;
    }

private:
    bool wasThorn=false;
    void drawTabs(const Raster& r) const
    {
        r.box(0,tabTop,780,285-tabTop,bg);
        for (unsigned b = 0; b < std::size(banks); ++b)
        {
            const int x = int(b) * 130;
            const bool selected = int(b) == bank;
            if (selected) r.box(x+3, tabTop+4, 124, 34, ice);
            else if (x) r.box(x, tabTop+9, 1, 22, grid);
            const int w = Raster::readingWidth(banks[b].name, true);
            r.reading(x + (130-w)/2, tabTop + 9, x+128, banks[b].name, selected ? bg : muted, true);
        }
    }
    int lastFace = -1;
    uint32_t pulseAt = 0;
    bool pulsing = false;

    int animationFrame(const PatternView& v, uint32_t now)
    {
        if (!v.running || v.current < 0 || v.current >= v.steps)
        {
            pulsing = false;
            return 0;
        }
        const bool onset = !shown.running || shown.current != v.current || shown.seed != v.seed;
        const bool accent = ((v.gates & v.accents) >> v.current) & 1u;
        if (onset && (v.current%4 == 0 || accent) && (!pulsing || now-pulseAt >= 144))
        {
            pulsing = true;
            pulseAt = now;
        }
        if (!pulsing) return 0;
        const uint32_t age = now-pulseAt;
        if (age < 144) return 1 + int(age*5/144);
        pulsing = false;
        return 0;
    }

    void paintFace(const Raster& r, int frame)
    {
        r.indexed(421,0,68,67,rave::faces+frame*68*67);
        lastFace = frame;
    }

    void drawPattern(const Raster& r) const
    {
        char label[64];
        formatParam(paramTable[paramIndex(scale)], value(scale), label, sizeof(label), {});
        char info[100];
        std::snprintf(info, sizeof(info), "%s   /   %s", rootChoices[int(value(root))], label);
        r.reading(13, 72, 230, info, paper);
        const bool notes = value(mode) >= .5f;
        const char* status = notes ? "NOTES" : shown.running ? "PLAY" : "STOP";
        for (int j=0; j<5; ++j) r.box(145+j, 77+j/2, 1, 8-j, shown.running ? ice : muted);
        if (shown.running)
            std::snprintf(info, sizeof(info), "%02d / %02d", shown.current + 1, shown.steps);
        else
            std::snprintf(info, sizeof(info), "%s   /   %d steps", status, shown.steps);
        r.reading(162, 70, 410, info, muted);
        r.box(514, 79, 14, 3, steel);
        r.text(535, 75, "Gate", muted);
        accentMark(r, 588, 76, paper);
        r.text(602, 75, "Accent", muted);
        slideMark(r, 685, 80, paper);
        r.text(706, 75, "Slide", muted);

        if (shown.steps <= 0) return;
        const int rows = shown.steps > 16 ? 2 : 1;
        const auto bounds = std::minmax_element(shown.pitch, shown.pitch + shown.steps);
        const int low = *bounds.first, range = std::max(128, int(*bounds.second)-low);
        for (int row = 0; row < rows; ++row)
        {
            const int top = gridTop + row*gridHeight/rows;
            const int bottom = gridTop + (row+1)*gridHeight/rows - 1;
            const int plotEnd = bottom - (rows == 1 ? 35 : 20);
            r.box(gridLeft, top, gridWidth, plotEnd-top, cell);
            const int focus = finger >= 0 ? finger : shown.running ? shown.current : -1;
            if (focus >= row*16 && focus < (row+1)*16)
            {
                const int col = focus%16;
                r.box(stepX(col), top, stepX(col+1)-stepX(col), plotEnd-top, 0xff23374e);
            }
            for (int line=0; line<4; ++line)
                r.box(gridLeft, top+3+line*(plotEnd-top-6)/3, gridWidth, 1, grid);
            for (int col=0; col<16; ++col)
                r.box(stepX(col), top, 1, plotEnd-top, col%4 ? grid : 0xff526271);
            auto pitchY = [&](int i) { return plotEnd-7-(shown.pitch[i]-low)*(plotEnd-top-14)/range; };
            for (int i=row*16; i<std::min((row+1)*16, int(shown.steps)); ++i)
            {
                const int col = i%16, x = stepX(col), w = stepX(col+1)-x, y = pitchY(i);
                const bool hit = focus == i, gated = (shown.gates >> i) & 1u;
                if (hit) r.box(x, top, w, 2, ice);
                if ((shown.slides >> i) & 1u)
                {
                    if (col < 15 && i+1 < shown.steps)
                    {
                        double prev = y;
                        for (int j=0; j<=12; ++j)
                        {
                            const double t = j/12.0;
                            const double next = y+(pitchY(i+1)-y)*t*t*(3-2*t);
                            r.trace(x+w-6+j, prev, next, muted);
                            prev = next;
                        }
                    }
                    else slideMark(r, x+w-16, plotEnd-4, muted);
                }
                if (gated) r.box(x+5, y-1, w-10, 4, hit ? paper : steel);
                else r.frame(x+5, y-2, w-10, 4, muted);
                if ((shown.accents >> i) & 1u) accentMark(r, x+w/2-4, top+3, ice);
                char pitch[12];
                plots::noteName(int(std::lround(shown.pitch[i]/128.0)), pitch);
                r.reading(x+(w-Raster::readingWidth(pitch))/2, plotEnd, x+w, pitch, hit ? paper : muted);
                if (rows == 1 && (col%4 == 0 || hit))
                {
                    char number[16];
                    std::snprintf(number,sizeof(number),"%02d",i+1);
                    r.text(x+(w-Raster::measure(number,rave::small))/2, plotEnd+18, number, muted);
                }
            }
        }
    }

    static int stepX(int col) { return gridLeft + col*gridWidth/16; }

    // One beat across each plot. Use the same detents and depth scales as the audio engine.
    void drawLfos(const Raster& r) const
    {
        for (int lane = 0; lane < 2; ++lane)
        {
            const int x = 12 + lane * 382;
            const auto amount = value(lane ? cutoffLfoAmount : pitchLfoAmount);
            const auto rate = value(lane ? cutoffLfoSpeed : pitchLfoSpeed);
            const auto* detent = lfoDetent(rate);
            const double cycles = detent ? detent->cyclesPerBeat : rate;
            const Ink color = lane ? 0xff99b3c1 : paper;
            r.box(x, 76, 373, 152, cell);
            r.reading(x+12, 81, x+195, lane ? "Cutoff LFO" : "Pitch LFO", paper, true);
            char depth[40];
            std::snprintf(depth, sizeof(depth), lane ? "%.2f oct" : "%.2f st",
                          amount * (lane ? cutoffLfoOctaves : 12 * pitchLfoOctaves));
            r.reading(x+275, 85, x+367, depth, paper);
            const int left = x+12, w = 348, mid = 158;
            for (int k=0; k<=4; ++k) r.box(left + k*w/4, 114, 1, 88, grid);
            r.box(left, mid, w, 1, grid);
            double prev = mid;
            for (int j=0; j<=w; ++j)
            {
                const double y = mid - 41*amount*std::sin(6.283185307179586*cycles*j/w);
                r.trace(left+j, prev, y, color);
                prev = y;
            }
            r.text(left, 207, "0", muted);
            r.right(left+w, 207, "1 beat", muted);
        }
    }

    void drawVoice(const Raster& r) const
    {
        const TextContext ctx {value(resonance), value(filter), value(peakSpread)};
        const int kind = int(ctx.filter);
        const double nominal = nominalHz(kind, value(cutoff));
        const double feedback = feedbackOf(kind, ctx.resonance);
        r.box(12, 76, 756, 152, cell);
        r.reading(24, 81, 311, filterChoices[kind], paper, true);
        char readout[48];
        formatParam(paramTable[paramIndex(cutoff)], value(cutoff), readout, sizeof(readout), ctx);
        r.reading(640, 84, 756, readout, paper);
        const int left = 25, top = 110, bottom = 201, w = 728;
        for (int k = 0; k <= 3; ++k) r.box(left+k*w/3, top, 1, bottom-top, grid);
        r.box(left, top+30, w, 1, grid);
        const double ref = filterGain2(kind, feedback, ctx.spread, kind == filterTwinPeak ? std::exp2(ctx.spread*.5) : 1e-3);
        double prev = 0;
        for (int j=0; j<=w; j+=2)
        {
            const double hz = 20 * std::pow(1000.0, double(j)/w);
            const double db = 10 * std::log10(filterGain2(kind, feedback, ctx.spread, hz/nominal)/ref + 1e-12);
            const double y = top + (18 - std::clamp(db, -36.0, 18.0)) * (bottom-top)/54;
            if (!j) prev = y;
            const double halfway = (prev+y)*.5;
            if (j) r.trace(left+j-1, prev, halfway, steel);
            r.trace(left+j, halfway, y, steel);
            prev = y;
        }
        r.text(left, 207, "20 Hz", muted);
        r.text(left+w/3, 207, "200 Hz", muted);
        r.text(left+2*w/3, 207, "2 kHz", muted);
        r.right(left+w, 207, "20 kHz", muted);
    }

    float value(uint32_t id) const { return values[paramIndex(id)]; }
    static void button(const Raster& r, int x, int w, const char* text)
    {
        r.box(x, 9, w, 44, 0xff17232e);
        r.box(x, 9, w, 1, 0xff66839a);
        r.reading(x + (w - Raster::readingWidth(text)) / 2, 21, x+w, text, paper);
    }
    static void accentMark(const Raster& r, int x, int y, Ink color)
    {
        for (int j=0; j<5; ++j) r.box(x+4-j, y+j, 1+2*j, 1, color);
    }
    static void slideMark(const Raster& r, int x, int y, Ink color)
    {
        for (int k=0; k<11; ++k) r.box(x+k, y-k/3, 1, 2, color);
        r.box(x+8, y-4, 4, 2, color);
    }
};
}
