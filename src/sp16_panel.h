// SP-16 touch panel. The host paints the six encoder values below this canvas.
#pragma once
#include "screen.h"
#include "tb3po.h"
#include "sixteen_screen.h"
#include "sp16_graphics.h"
#include "../assets/sp16-thorn/index.h"
#include <array>

namespace tbmpc::sp16
{
using namespace ParamId;
struct Bank { const char* name; std::array<uint32_t, 6> ids; };
enum Page { performPage, voicePage, slidePage, lfoPage, patternPage, clockPage };
inline constexpr Bank banks[] = {
    {"Perform", {cutoff, resonance, envMod, decay, accent, volume}},
    {"Voice",   {waveform, tuning, filter, peakSpread, drive, accentDecay}},
    {"Slide",   {glide, slideTime, gate, octave, transpose, SIXTEEN_INVALID_PARAM_ID}},
    {"LFO",     {pitchLfoAmount, pitchLfoSpeed, cutoffLfoAmount, cutoffLfoSpeed,
                 SIXTEEN_INVALID_PARAM_ID, SIXTEEN_INVALID_PARAM_ID}},
    {"Pattern", {seedMode, seed, mutate, density, scale, root}},
    {"Clock",   {clockDiv, swing, reset, steps, mode, SIXTEEN_INVALID_PARAM_ID}}
};
inline constexpr uint32_t voiceControls[] = {filter, peakSpread, cutoff, resonance};
inline bool controlEnabled(uint32_t id, int filterMode, int slideMode, int playMode)
{
    return (id!=peakSpread || filterMode==filterTwinPeak) && (id!=slideTime || slideMode==1 || playMode==1);
}
using hdui::Ink;
constexpr Ink bg = 0xff080812, cell = 0xff100d1a, grid = 0xff302840;
constexpr Ink inactive = 0xff787583;
constexpr Ink paper = 0xfff1efff, muted = 0xffb9b1cb, steel = 0xffb6c8fa, ice = 0xff96b7ff;
constexpr int bodyTop = 100, tabTop = 247, tabLeft = 300, tabWidth = 79, stormX = 127;
constexpr int gridLeft = 8, gridWidth = 764, gridTop = 131, gridHeight = 113;

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

    // A vertical slice of a trace, with fractional coverage at each end.
    void trace(int x, double from, double to, Ink fg, int halfWidth=12) const
    {
        const int upper = int(std::lround(16*std::min(from,to))) - halfWidth;
        const int lower = int(std::lround(16*std::max(from,to))) + halfWidth;
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
    // Cut opposite corners and leave a one-pixel rim around the face.
    template <typename Face>
    void plateRows(int x,int y,int width,int height,Ink edge,Face face) const
    {
        constexpr int bevel = 4;
        for (int row=0; row<height; ++row)
        {
            const int left = std::max(0, bevel - (height-1-row));
            const int right = std::max(0, bevel-row);
            const int span = width-left-right;
            box(x+left,y+row,span,1,edge);
            if (row>0 && row<height-1)
                box(x+left+1,y+row,span-2,1,face(row));
        }
    }

    void plate(int x,int y,int width,int height,Ink upper,Ink lower,Ink edge) const
    {
        plateRows(x,y,width,height,edge,[&](int row) {
            auto channel = [&](int shift) {
                const int a=(upper>>shift)&255, b=(lower>>shift)&255;
                return unsigned(a+(b-a)*row/(height-1))<<shift;
            };
            return 0xff000000u|channel(16)|channel(8)|channel(0);
        });
    }

    void selectedPlate(int x,int y,int width,int height) const
    {
        plateRows(x,y,width,height,thorn::selectionEdge,[=](int row) {
            return thorn::selectionRamp[(2*row+1)*std::size(thorn::selectionRamp)/(2*height)];
        });
    }
};

}
#include "sp16_thorn.h"
namespace tbmpc::sp16
{
class Panel
{
    static constexpr int ribbonY = 103, ribbonHeight = 25;
    struct Card { int x, w; };

    int ribbonControls() const
    {
        if (bank == performPage) return 2;
        if (bank == voicePage) return int(std::size(voiceControls));
        return bank == slidePage || bank == clockPage ? 5 : 0;
    }
    Card ribbonCard(int index) const
    {
        if (bank == performPage || (bank == slidePage && index == 0))
            return {gridLeft+190*index,186};
        const int first = bank == slidePage ? gridLeft+190 : gridLeft;
        const int count = ribbonControls() - (bank == slidePage ? 1 : 0);
        const int slot = bank == slidePage ? index-1 : index;
        const int available = gridLeft+gridWidth-first+4;
        const int x = first + slot*available/count;
        return {x,first + (slot+1)*available/count - x-4};
    }
    static constexpr int slideChoiceX = 54, slideChoiceWidth = 66, slideChoiceGap = 3;

public:
    int bank = patternPage;
    ThornPage thorn;
    static constexpr int plotLeft=40, plotRight=732, plotTop=135, plotBottom=225;
    static constexpr int lfoTop=134, lfoBottom=226, lfoCardWidth=373;
    static constexpr int lfoCardLeft(int lane) { return 12+382*lane; }
    static constexpr int lfoLeft(int lane) { return lfoCardLeft(lane)+7; }
    static constexpr int lfoRight(int lane) { return lfoCardLeft(lane)+lfoCardWidth-8; }
    enum class Gesture { none, filterXY, lfoXY, parameter, steps };
    Gesture gesture = Gesture::none;
    StepLane lane = StepLane::number;
    int startX=0, startY=0, startStep=-1, startOffset=0, lfoLane=0;
    uint16_t gestureSeed=0;
    uint32_t dragParam=0;
    float startValue=0;
    bool paintOn=false;
    float voiceCutoff=0, voiceRes=0;
    uint32_t voiceMs=0;
    bool voiceInitialized=false;
    void release()
    {
        gesture=Gesture::none;
        thorn.capturedBank=-1;
        finger=-1;
    }
    int ribbonColumnAt(int x, int y) const
    {
        if (y < ribbonY || y >= ribbonY+ribbonHeight) return -1;
        for (int i=0; i<ribbonControls(); ++i)
        {
            const auto card = ribbonCard(i);
            if (x >= card.x && x < card.x+card.w) return i;
        }
        return -1;
    }
    int slideChoiceAt(int x, int y) const
    {
        if (bank != slidePage || ribbonColumnAt(x,y) != 0 || x < slideChoiceX)
            return -1;
        const int offset = x-slideChoiceX;
        const int index = offset/(slideChoiceWidth+slideChoiceGap);
        return index < 2 && offset%(slideChoiceWidth+slideChoiceGap) < slideChoiceWidth ? index : -1;
    }
    static StepLane patternLane(int y)
    {
        if (y<thorn::scoreRow::pitch) return StepLane::number;
        if (y<thorn::scoreRow::gate) return StepLane::pitch;
        if (y<thorn::scoreRow::accent) return StepLane::gate;
        return y<thorn::scoreRow::slide ? StepLane::accent : StepLane::slide;
    }
    int ribbonTop(int step) const { return gridTop + (step/16)*gridHeight/(shown.steps>16 ? 2 : 1); }
    int ribbonBottom(int row) const { return gridTop + (row+1)*gridHeight/(shown.steps>16 ? 2 : 1) - 1; }
    int ribbonPitchTop(int row) const { return ribbonBottom(row) - (shown.steps>16 ? 28 : 41); }
    bool localPlay = false;
    int finger = -1;
    bool dirty = true;
    PatternView shown {};
    std::array<float, paramCount> values {};

    void select(int page)
    {
        release();
        voiceInitialized=false;
        bank = page;
        dirty = true;
    }
    bool turn(int delta)
    {
        if (!delta) return false;
        select(std::clamp<int64_t>(int64_t(bank) + delta, 0, std::size(banks) - 1));
        return true;
    }
    static int tabAt(int x,int y)
    {
        if (y < tabTop+2 || y >= 283 || x < tabLeft || x >= tabLeft+6*tabWidth) return -1;
        return (x-tabLeft)/tabWidth;
    }

    int stepAt(int x, int y) const
    {
        if (bank == patternPage) return thorn.stepAt(shown,x,y);
        if (bank == voicePage || bank == lfoPage) return -1;
        if (x < gridLeft || x >= gridLeft + gridWidth || y < gridTop || y >= gridTop + gridHeight)
            return -1;
        const int rows = shown.steps > 16 ? 2 : 1;
        const int row = ((y - gridTop + 1) * rows - 1) / gridHeight;
        if (bank == clockPage && y >= ribbonTop(row*16)+14 && y < ribbonPitchTop(row))
            return -1;
        const int col = ((x - gridLeft + 1) * 16 - 1) / gridWidth;
        const int index = row * 16 + col;
        return index < shown.steps ? index : -1;
    }

    bool render(const sixteen_screen_surface_t& s, bool full, const PatternView& next, const std::atomic<float>* params)
    {
        const bool layoutChanged = full || dirty;
        const bool sequenceChanged = !(shown == next);
        shown = next;
        uint64_t changes=0;
        for (uint32_t i = 0; i < paramCount; ++i)
        {
            const float v = params[i].load(std::memory_order_relaxed);
            if (v != values[i]) changes |= uint64_t(1) << paramTable[i].id;
            values[i] = v;
        }
        dirty = false;
        Raster r{s.pixels, s.stride, s.height};
        const auto changed = [changes](std::initializer_list<uint32_t> ids) {
            for (const auto id : ids)
                if (changes & (uint64_t(1) << id)) return true;
            return false;
        };
        const bool footerChanged = layoutChanged || changed({root,scale});
        if (footerChanged) drawFooter(r);
        bool bodyChanged = layoutChanged;
        if (layoutChanged && bank!=patternPage) r.box(0,bodyTop,780,tabTop-bodyTop,bg);
        if (bank==voicePage)
        {
            const uint32_t dt=s.now_ms-voiceMs;
            // Follow the finger more tightly than an encoder or automation step.
            const float response=gesture==Gesture::filterXY ? 16.f : 28.f;
            const float weight=voiceInitialized ? -std::expm1(-float(dt)/response) : 1.f;
            auto track=[weight](float& display,float target) {
                const float before=display;
                display+=(target-display)*weight;
                if (std::abs(target-display)<.0005f) display=target;
                return before!=display;
            };
            bodyChanged |= changed({cutoff,resonance,filter,peakSpread});
            bodyChanged |= track(voiceCutoff,value(cutoff));
            bodyChanged |= track(voiceRes,value(resonance));
            voiceInitialized=true;
            voiceMs=s.now_ms;
            if (bodyChanged) drawVoice(r);
        }
        if (bank==lfoPage)
        {
            bodyChanged |= changed({pitchLfoAmount,pitchLfoSpeed,cutoffLfoAmount,cutoffLfoSpeed});
            if (bodyChanged) drawLfos(r);
        }
        if (bank==performPage || bank==slidePage || bank==clockPage)
        {
            bodyChanged |= sequenceChanged;
            if (bank==performPage) bodyChanged |= changed({cutoff,resonance,filter,peakSpread});
            if (bank==slidePage) bodyChanged |= changed({glide,slideTime,gate,octave,transpose,mode});
            if (bank==clockPage) bodyChanged |= changed({clockDiv,swing,reset,steps,mode});
            if (bodyChanged) drawPattern(r);
        }
        const int button=value(mode)>=.5f ? 3 : shown.running ? (localPlay?1:2) : 0;
        const bool headerChanged = layoutChanged || sequenceChanged || changed({seed,mode});
        const bool scoreChanged = bank==patternPage && (layoutChanged || sequenceChanged);
        return thorn.draw(r,shown,s.now_ms,full,headerChanged,scoreChanged,int(value(seed)),button)
            || bodyChanged || footerChanged;
    }

private:
    void drawFooter(const Raster& r) const
    {
        r.box(0,tabTop,stormX,285-tabTop,bg);
        r.box(stormX+168,tabTop,780-stormX-168,285-tabTop,bg);
        r.box(stormX,tabTop,168,253-tabTop,bg);
        r.box(stormX,278,168,285-278,bg);
        r.plate(7,252,115,27,0xff291d39,0xff141021,0xff685d80);
        char scaleName[64];
        formatParam(paramTable[paramIndex(scale)],value(scale),scaleName,sizeof(scaleName),{});
        char key[80];
        std::snprintf(key,sizeof(key),"%s / %s",rootChoices[int(value(root))],scaleName);
        r.label(13,259,120,key,paper,rave::small);
        r.box(stormX,252,168,1,0xff514f70);
        r.box(stormX,278,168,1,0xff514f70);
        for (int i=0; i<int(std::size(banks)); ++i)
        {
            const int x = tabLeft+i*tabWidth;
            const bool active = i==bank;
            if (active) r.selectedPlate(x,tabTop+2,tabWidth-2,33);
            else r.plate(x,tabTop+2,tabWidth-2,33,0xff2b203b,0xff100d1b,0xff675a79);
            if (i == slidePage)
            {
                const char* label = "SLIDE";
                r.text(x+(tabWidth-2-Raster::measure(label,rave::small))/2,tabTop+11,label,paper);
            }
            else ThornPage::bankLabel(r,i,x,tabTop+8);
            if (active) r.box(x+9,tabTop+30,tabWidth-20,2,thorn::selectionEdge);
        }
    }
    void drawSlideSwitch(const Raster& r) const
    {
        r.text(gridLeft+9,109,"Slide",muted);
        for (int i=0; i<2; ++i)
        {
            const int x = slideChoiceX+i*(slideChoiceWidth+slideChoiceGap);
            const bool active = i == int(value(glide));
            if (active) r.selectedPlate(x,ribbonY+2,slideChoiceWidth,ribbonHeight-4);
            else r.plate(x,ribbonY+2,slideChoiceWidth,ribbonHeight-4,0xff1d192d,0xff14101e,0xff514761);
            const char* label = glideChoices[i];
            r.text(x+(slideChoiceWidth-Raster::measure(label,rave::small))/2,109,label,active?paper:muted);
        }
    }

    void drawRibbonControls(const Raster& r) const
    {
        const TextContext ctx {value(resonance),value(filter),value(peakSpread)};
        for (int i=0; i<ribbonControls(); ++i)
        {
            const auto [x,width] = ribbonCard(i);
            const uint32_t id = banks[bank].ids[i];
            const bool enabled = controlEnabled(id,int(value(filter)),int(value(glide)),int(value(mode)));
            r.plate(x,ribbonY,width,ribbonHeight,enabled ? 0xff292036 : 0xff1c1b24,
                    enabled ? 0xff151121 : 0xff13121a,enabled ? 0xff514761 : 0xff3a3744);
            if (bank == slidePage && i == 0)
            {
                drawSlideSwitch(r);
                continue;
            }
            const auto& param = paramTable[paramIndex(id)];
            char text[64];
            formatParam(param,value(id),text,sizeof(text),ctx);
            const char* label = param.name;
            if (id == slideTime) label = "303 Time";
            if (id == gate) label = "Gate Length";
            if (id == clockDiv) label = "Division";
            r.text(x+9,109,label,enabled ? muted : inactive);
            r.right(x+width-10,109,text,enabled ? paper : inactive);
        }

        if (bank == performPage)
        {
            r.box(500,114,13,3,steel);
            r.text(519,109,"Gate",muted);
            accentMark(r,575,109,paper);
            r.text(590,109,"Accent",muted);
            slideMark(r,681,116,paper);
            r.text(699,109,"Slide",muted);
        }
    }

    void drawClockRuler(const Raster& r, int row, int top, int bottom, int focus) const
    {
        const int count = std::min(16,int(shown.steps)-row*16);
        const int middle = (top+bottom)/2;
        const double beats = clockDivBeats[int(value(clockDiv))];
        const double swingShare = value(swing);
        for (int col=0; col<count; col+=2)
        {
            const int end = stepX(std::min(count,col+2));
            const int x = stepX(col);
            const int straight = stepX(col+1);
            const bool paired = col+1 < count;
            const int split = paired ? x+int(std::lround((end-x)*swingShare)) : end;
            const double beat = (row*16+col)*beats;
            const bool bar = std::abs(beat/4-std::round(beat/4)) < 1e-6;
            const bool first = focus == row*16+col;
            const bool second = paired && focus == row*16+col+1;
            r.box(x+1,top+1,end-x-2,bottom-top-1,first || second ? 0xff1c2a58 : 0xff111225);
            r.box(x+2,top+(bar?1:3),bar?2:1,bottom-top-(bar?2:6),bar?paper:steel);
            if (paired) r.box(straight,top+2,1,bottom-top-4,0xff69588e);
            r.box(x+6,middle-3,split-x-8,6,first?0xff7295ee:0xff36497f);
            r.box(x+6,middle-3,split-x-8,1,first?paper:ice);
            if (paired)
            {
                r.box(split+3,middle-3,end-split-6,6,second?0xff8972d6:0xff4d376d);
                r.box(split+3,middle-3,end-split-6,1,second?paper:0xffb292eb);
                r.box(split,middle-4,2,8,second?paper:ice);
            }
        }
    }

    void drawPattern(const Raster& r) const
    {
        r.box(0,bodyTop,780,tabTop-bodyTop,bg);
        drawRibbonControls(r);
        if (shown.steps <= 0) return;
        const int rows = shown.steps > 16 ? 2 : 1;
        const auto bounds = std::minmax_element(shown.pitch, shown.pitch + shown.steps);
        const int low = *bounds.first, range = std::max(128, int(*bounds.second)-low);
        const bool showSlide = bank == slidePage, mode303 = value(glide) >= .5f;
        constexpr double previewSeconds = .06;
        const double glideSpeed = showSlide && mode303 ? previewSeconds/slideSeconds(value(slideTime)) : 0;
        for (int row = 0; row < rows; ++row)
        {
            const int numberTop = ribbonTop(row*16);
            const int top = numberTop+14;
            const int bottom = ribbonBottom(row);
            const int plotEnd = ribbonPitchTop(row);
            r.box(gridLeft, top, gridWidth, plotEnd-top, cell);
            const int focus = finger >= 0 ? finger : thorn.focusStep(shown);
            if (focus >= row*16 && focus < (row+1)*16)
            {
                const int col = focus%16;
                r.box(stepX(col), top, stepX(col+1)-stepX(col), plotEnd-top, 0xff302c62);
            }
            if (bank == clockPage) drawClockRuler(r,row,top,plotEnd,focus);
            else
            {
                for (int line=0; line<4; ++line)
                    r.box(gridLeft, top+3+line*(plotEnd-top-6)/3, gridWidth, 1, grid);
                for (int col=0; col<16; ++col)
                    r.box(stepX(col), top, 1, plotEnd-top, col%4 ? grid : 0xff655975);
            }
            const int pad=rows==1 ? 7 : bank==slidePage ? 4 : 2;
            auto pitchY = [&](int i) { return plotEnd-pad-(shown.pitch[i]-low)*(plotEnd-top-2*pad)/range; };
            for (int i=row*16; i<std::min((row+1)*16, int(shown.steps)); ++i)
            {
                const int col = i%16, x = stepX(col), w = stepX(col+1)-x, y = pitchY(i);
                char number[16];
                std::snprintf(number,sizeof(number),"%02d",i+1);
                if (focus==i) r.selectedPlate(x+1,numberTop,w-2,14);
                else r.box(x+1,numberTop,w-2,14,0xff181322);
                r.text(x+(w-Raster::measure(number,rave::small))/2,numberTop,number,focus==i?paper:muted);
                const bool hit = focus == i, gated = (shown.gates >> i) & 1u;
                if (bank != clockPage)
                {
                    if (hit) r.box(x, top, w, 2, thorn::selectionEdge);
                    const bool sliding = (shown.slides >> i) & 1u;
                    if (sliding && col < 15 && i+1 < shown.steps)
                    {
                        const int target = pitchY(i+1), startPitch = shown.pitch[i], endPitch = shown.pitch[i+1];
                        double previous = y;
                        for (int j=0; j<=12; ++j)
                        {
                            const double t = j/12.0;
                            double amount = t*t*(3-2*t);
                            if (showSlide && mode303)
                                amount = -std::expm1(-t*glideSpeed);
                            else if (showSlide && endPitch != startPitch)
                            {
                                const auto ticks = int64_t(t*previewSeconds*moduleTickRate);
                                amount = double(Tb3po::slideAfter(startPitch,endPitch,ticks)-startPitch)/(endPitch-startPitch);
                            }
                            const double next = y+(target-y)*amount;
                            r.trace(x+w-6+j,previous,next,showSlide ? ice : muted);
                            previous = next;
                        }
                    }
                    else if (sliding) slideMark(r, x+w-16, plotEnd-4, muted);
                    if (gated) r.box(x+5,y-1,w-10,4,hit ? paper : steel);
                    else r.frame(x+5,y-2,w-10,4,muted);
                    if ((shown.accents >> i) & 1u) accentMark(r,x+w/2-4,top+3,ice);
                    if (showSlide)
                    {
                        r.box(x+3,plotEnd-3,w-6,2,0xff292745);
                        if (gated)
                        {
                            const int length = int(std::lround((w-6)*(sliding ? 1.f : value(gate))));
                            r.box(x+3,plotEnd-3,length,2,sliding ? 0xffbf9af7 : ice);
                        }
                    }
                }
                r.plateRows(x+1,plotEnd,w-2,bottom-plotEnd+1,hit?thorn::selectionEdge:0xff292333,
                            [](int) { return 0xff000000u; });
                ThornPage::pitchLabel(r,shown.pitch[i],x+(w-42)/2,plotEnd+(rows==1?0:-3));

            }
        }
    }

    static int stepX(int col) { return gridLeft + col*gridWidth/16; }

    // One beat across each plot. Use the same detents and depth scales as the audio engine.
    void drawLfos(const Raster& r) const
    {
        for (int lane = 0; lane < 2; ++lane)
        {
            const int x=lfoCardLeft(lane);
            const auto amount = value(lane ? cutoffLfoAmount : pitchLfoAmount);
            const auto rate = value(lane ? cutoffLfoSpeed : pitchLfoSpeed);
            const auto* detent = lfoDetent(rate);
            const double cycles = detent ? detent->cyclesPerBeat : rate;
            const Ink color = lane ? 0xffd1afff : steel;
            r.plate(x,103,lfoCardWidth,141,0xff20192d,0xff0d0c15,0xff706580);
            ThornPage::plotTitle(r,3+lane,x+12,107);
            char depth[40];
            std::snprintf(depth, sizeof(depth), lane ? "%.2f oct" : "%.2f st",
                          amount * (lane ? cutoffLfoOctaves : 12 * pitchLfoOctaves));
            char speed[40];
            formatParam(paramTable[paramIndex(lane ? cutoffLfoSpeed : pitchLfoSpeed)],rate,speed,sizeof(speed),{});
            r.text(x+154,114,speed,muted);
            r.reading(x+275, 111, x+367, depth, paper);
            const int left=lfoLeft(lane), w=lfoRight(lane)-left;
            constexpr int mid=(lfoTop+lfoBottom)/2, amplitude=(lfoBottom-lfoTop)/2-4;
            r.box(left,lfoTop,w+1,lfoBottom-lfoTop+1,0xff110e1e);
            for (int k=0; k<=4; ++k) r.box(left+k*w/4,lfoTop,1,lfoBottom-lfoTop+1,grid);
            r.box(left,mid,w,1,grid);
            double prev = mid;
            for (int j=0; j<=w; ++j)
            {
                const double y = mid - amplitude*amount*std::sin(6.283185307179586*cycles*j/w);
                r.trace(left+j, prev+2, y+2, 0xff51448a);
                r.trace(left+j, prev, y, color);
                prev = y;
            }
            r.text(left,229,"0",muted);
            r.right(left+w,229,"1 beat",muted);
        }
    }

    void drawVoice(const Raster& r) const
    {
        const TextContext ctx {voiceRes, value(filter), value(peakSpread)};
        const int kind = int(ctx.filter);
        const double nominal = nominalHz(kind, voiceCutoff);
        const double feedback = feedbackOf(kind, voiceRes);
        // Fix the frequency ruler for each filter. Twin Peak needs space for its second peak.
        constexpr int lowerOctaves[] = {4, 2, 2};
        constexpr int upperOctaves[] = {2, 5, 3};
        const double minHz = std::ldexp(nominalHz(kind, 0), -lowerOctaves[kind]);
        const double span = cutoffOctaves + lowerOctaves[kind] + upperOctaves[kind];
        constexpr int w=plotRight-plotLeft, h=plotBottom-plotTop;
        constexpr int traceTop=plotTop+4, traceBottom=plotBottom-4;
        auto hzX = [&](double hz) { return plotLeft+w*std::log2(hz/minHz)/span; };
        auto dbY = [](double db) {
            return traceTop + (36-std::clamp(db,-36.,36.))*(traceBottom-traceTop)/72.;
        };
        char readout[48];
        for (int i=0; i<int(std::size(voiceControls)); ++i)
        {
            const auto [x,width] = ribbonCard(i);
            const auto id = voiceControls[i];
            const bool enabled = id!=peakSpread || kind==filterTwinPeak;
            const auto& param = paramTable[paramIndex(id)];
            const float displayed = id==cutoff ? voiceCutoff : id==resonance ? voiceRes : value(id);
            formatParam(param,displayed,readout,sizeof(readout),ctx);
            r.plate(x,ribbonY,width,ribbonHeight,enabled ? 0xff292036 : 0xff1c1b24,
                    enabled ? 0xff151121 : 0xff13121a,enabled ? 0xff635473 : 0xff3a3744);
            const char* label = id==cutoff ? "X  Cutoff" : id==resonance ? "Y  Resonance" : param.name;
            const Ink ink = id==cutoff ? ice : id==resonance ? 0xffcfa7ff : muted;
            const int end = x+width-10;
            r.text(x+10,109,label,enabled ? ink : inactive);
            r.reading(end-Raster::readingWidth(readout),106,end,readout,enabled ? paper : inactive);
        }

        r.plate(gridLeft,131,gridWidth,113,0xff241a32,0xff100c1a,0xff76678f);
        r.box(plotLeft-1,plotTop-1,w+3,h+3,0xff514461);
        for (int y=plotTop; y<=plotBottom; ++y)
            r.box(plotLeft,y,w+1,1,y%2 ? 0xff111020 : 0xff131123);
        for (int db : {36, 18, 0, -18, -36})
        {
            const int y=int(std::lround(dbY(db)));
            r.box(plotLeft,y,w+1,1,db==0?0xff584e77:0xff2a243c);
            std::snprintf(readout,sizeof(readout),"%+d",db);
            r.right(plotLeft-6,y-6,db==0 ? "0 dB" : readout,muted);
        }
        for (int n : {0,50,100})
        {
            const int y=plotBottom-n*h/100;
            std::snprintf(readout,sizeof(readout),"%d%%",n);
            r.text(plotRight+6,std::clamp(y-6,plotTop-3,plotBottom-8),readout,muted);
        }
        for (int hz : {5,10,20,50,100,200,500,1000,2000,5000,10000,20000,50000,100000})
        {
            const int x=int(std::lround(hzX(hz)));
            if (x<plotLeft+8 || x>plotRight-8) continue;
            for (int y=plotTop+2; y<plotBottom; y+=4) r.box(x,y,1,2,0xff383052);
            if (hz>=1000) std::snprintf(readout,sizeof(readout),"%dk",hz/1000);
            else std::snprintf(readout,sizeof(readout),"%d",hz);
            r.text(x-Raster::measure(readout,rave::small)/2,229,readout,steel);
        }
        r.text(12,229,"Hz",steel);

        const double ref = filterGain2(kind, feedback, ctx.spread, kind == filterTwinPeak ? std::exp2(ctx.spread*.5) : 1e-3);
        const double floorGain=ref*std::pow(10.,-3.6);
        const double ceilingGain=ref*std::pow(10.,3.6);
        auto responseY = [&](double relativeHz) {
            const double gain=filterGain2(kind,feedback,ctx.spread,relativeHz);
            if (gain<=floorGain) return double(traceBottom);
            if (gain>=ceilingGain) return double(traceTop);
            return dbY(10*std::log10(gain/ref));
        };
        const double cutoffFrequency=cutoffHz(voiceCutoff,ctx);
        const double edgeX=std::clamp(hzX(cutoffFrequency),double(plotLeft+3),double(plotRight-3));
        const double edgeY=responseY(cutoffFrequency/nominal);
        const double touchX=std::clamp(plotLeft+w*double(voiceCutoff),double(plotLeft+4),double(plotRight-4));
        const double touchY=plotBottom-h*double(voiceRes);
        const double railY=std::clamp(touchY,plotTop+.5,plotBottom-.5);
        const double reticleY=std::clamp(touchY,double(plotTop+5),double(plotBottom-5));
        for (int x=int(touchX)+4; x<=plotRight; x+=4)
            r.trace(x,railY,railY,0xff826ba9,5);

        double frequency=minHz/nominal;
        const double ratio=std::exp2(span/w);
        double previous=responseY(frequency);
        for (int x=plotLeft; x<=plotRight; ++x,frequency*=ratio)
        {
            const double y=responseY(frequency);
            const int depth=plotBottom-int(y);
            if (depth>6)
            {
                r.box(x,int(y)+3,1,depth/2,0xff232043);
                if ((x-plotLeft)%4==0) r.box(x,int(y)+3,1,depth-3,0xff38315c);
            }
            if (y==traceBottom && previous==traceBottom)
                r.trace(x,y,y,0xff555578,8);
            else
            {
                r.trace(x,previous,y,0xff5156a0,26);
                r.trace(x,previous,y,0xffdce8ff,11);
            }
            previous=y;
        }

        const int column=int(edgeX);
        const unsigned fraction=unsigned(std::lround((edgeX-column)*15));
        for (int y=int(edgeY)+4; y<plotBottom; y+=4)
            for (int dy=0; dy<2; ++dy)
            {
                r.blend(column,y+dy,0xff566fa5,15-fraction);
                r.blend(column+1,y+dy,0xff566fa5,fraction);
            }

        // The hollow reticle follows the pad. The brighter marker marks the cutoff on the response.
        for (int x=int(touchX)-3; x<=int(touchX)+4; ++x)
        {
            const double d=4-std::abs(x-touchX);
            if (d<=0) continue;
            r.trace(x,reticleY-d,reticleY-d,0xffbb9ce5,8);
            r.trace(x,reticleY+d,reticleY+d,0xffbb9ce5,8);
        }
        for (int x=column-2; x<=column+3; ++x)
        {
            const double half=2.5-std::abs(x-edgeX);
            if (half<=0) continue;
            r.trace(x,edgeY-half,edgeY+half,0xff7598f2,13);
            r.trace(x,edgeY-half*.6,edgeY+half*.6,paper,6);
        }
    }

    float value(uint32_t id) const { return values[paramIndex(id)]; }
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
