// Shared Thorn Crown panel artwork. The sequencer owns all musical state.
#pragma once
#include "animation_phase.h"

extern "C" const unsigned char slink_sp16_thorn_bank[];
namespace tbmpc::sp16
{
class ThornPage
{
public:
    int selected = 0, capturedBank = -1;
    bool heldCamera = false;

    static int columnX(int i) { return 65 + (i*693+8)/16 + (i/4)*5; }

    int focusStep(const PatternView& v) const
    {
        return v.running ? v.current : std::min(selected,int(v.steps)-1);
    }

    int first(const PatternView& v) const
    {
        if (capturedBank>=0) return capturedBank;
        return v.steps>16 && focusStep(v)>=16 ? 16 : 0;
    }
    int stepAt(const PatternView& v,int x,int y) const
    {
        if (y<thorn::scoreRow::step || y>=thorn::scoreRow::end) return -1;
        for (int i=0; i<16; ++i)
        {
            const int step=first(v)+i;
            if (x>=columnX(i) && x<columnX(i)+42 && step<v.steps) return step;
        }
        return -1;
    }
    static void pitchLabel(const Raster& r, int cv, int x, int y)
    {
        const int pitch = std::clamp(int(std::lround(cv / 128.0)) + 256, 0, 512);
        image(r, thorn::pitch, pitch, x, y);
    }
    static void bankLabel(const Raster& r, int bank, int x, int y)
    {
        image(r, thorn::bankLabel, bank, x, y);
    }

    static void plotTitle(const Raster& r, int index, int x, int y)
    {
        image(r, thorn::plotTitle, index, x, y);
    }

    bool draw(const Raster& r,const PatternView& v,uint32_t now,bool full,bool updateHeader,bool updateScore,
              int seed,int button)
    {
        const uint32_t elapsed = initialized ? now-lastMs : 0;
        initialized = true;
        lastMs = now;
        if (!heldCamera) cameraMs += elapsed;
        // The bank contains one native-size image for each 60 Hz sample.
        const auto wallTick=slink::motion::tick(now);
        const int camera=int(slink::motion::tick(cameraMs)%thorn::crest.count);
        const int storm=int(wallTick%thorn::storm.count);
        const int face=int(wallTick%thorn::face.count);

        if (updateHeader)
        {
            if (full) restore(r,0,0,780,bodyTop);
            else
            {
                // Leave the crest and smiley on the persistent surface.
                restore(r,0,0,186,bodyTop);
                restore(r,186,0,332,6);
                restore(r,186,96,332,bodyTop-96);
                restore(r,518,0,199,bodyTop);
                restore(r,717,58,63,bodyTop-58);
                restore(r,778,0,2,58);
            }
            const int offset=first(v), count=std::clamp(int(v.steps)-offset,0,16);
            const int focus=focusStep(v);
            const int col=focus-offset;
            r.tile(controls::counter,std::clamp(focus,0,31),528,20);
            r.tile(controls::stepTitle,0,533,10);
            r.tile(controls::length,std::clamp(int(v.steps)-1,0,31),599,78);
            drawActions(r,button,focus,v.steps);
            r.tile(controls::seedTitle,0,14,55);
            for (int i=0; i<4; ++i)
                r.tile(controls::hexDigit,(unsigned(seed) >> ((3-i)*4)) & 15,14+11*i,70);
            for (int i=0;i<20;++i)
                r.box(72+i*98/20,59,((seed>>(i%16))&1)?4:1,26,i%6?0xff919fbe:0xffced7ff);
            if (count)
            {
                auto trace=[&](int x,int y,int w,int h) {
                    const auto limits=std::minmax_element(v.pitch+offset,v.pitch+offset+count);
                    const int span=std::max(128,int(*limits.second)-*limits.first);
                    int prev=0;
                    for(int i=0;i<count;++i){
                        int xx=x+i*w/16, yy=y+h-2-(v.pitch[offset+i]-*limits.first)*(h-4)/span;
                        if(i && ((v.slides>>(offset+i-1))&1u)) r.trace(xx-1,prev,yy,0xff7689c0);
                        r.box(xx,yy,std::max(2,w/16-1),2,i==col?0xffcabaff:0xff7c8cc6);
                        prev=yy;
                    }
                };
                trace(625,17,77,23);
            }
        }
        if (updateScore)
        {
            restore(r,0,bodyTop,780,tabTop-bodyTop);
            const int offset = first(v), count = std::clamp(int(v.steps)-offset,0,16);
            const int focus = focusStep(v);
            drawScore(r,v,offset,count,focus-offset);
        }
        const bool crestChanged = full || camera!=lastCamera;
        const bool faceChanged = full || face!=lastFace;
        const bool stormChanged = full || storm!=lastStorm;
        if (crestChanged) image(r,thorn::crest,camera,186,6);
        if (faceChanged) image(r,thorn::face,face,717,0);
        if (stormChanged)
        {
            r.box(stormX,footerTop,thorn::storm.w,1,bg);
            r.box(stormX,footerBottom-1,thorn::storm.w,1,bg);
            image(r,thorn::storm,storm,stormX,footerTop+1);
            r.tile(controls::stormRim,0,stormX,footerTop);
        }
        lastCamera=camera; lastFace=face; lastStorm=storm;
        return updateHeader || updateScore || crestChanged || faceChanged || stormChanged;
    }

private:
    uint32_t lastMs = 0;
    uint64_t cameraMs = 0;
    bool initialized = false;
    int lastCamera=-1, lastFace=-1, lastStorm=-1;

    static void drawActions(const Raster& r,int mode,int focus,int steps)
    {
        const auto& t=transportButton;
        r.tile(controls::transport,mode,t.x,t.y);
        const int slot=focus%16;
        const int visible=std::min(16,steps-(focus/16)*16);
        for (int i=0; i<visible; ++i)
        {
            const int x=t.x+11+int(std::lround(i*3.72+(i/4)*.92));
            if (i==slot) r.box(x,t.y+22,3,2,0xffd6ddff);
            else if (i<slot && (mode==1 || mode==2)) r.box(x,t.y+23,3,1,0xff8f9edc);
        }
        r.tile(controls::regen,0,regenButton.x,regenButton.y);
    }

    static void restore(const Raster& r,int x,int y,int w,int h)
    {
        const auto* plate = slink_sp16_thorn_bank+thorn::backdropOffsets[0];
        for (int row=y; row<y+h; ++row)
            std::memcpy(r.px+row*r.w+x,plate+4*(row*thorn::backdrop.w+x),4*w);
    }

    static void drawPitchRail(const Raster& r,const PatternView& v,int offset,int count,int col)
    {
        constexpr int top=thorn::scoreRow::pitch, bottom=thorn::scoreRow::note;
        r.box(columnX(0),top,columnX(15)+42-columnX(0),bottom-top,0xff0b0b19);
        if (count<=0) return;
        const auto limits=std::minmax_element(v.pitch,v.pitch+v.steps);
        const int span=std::max(128,int(*limits.second)-int(*limits.first));
        const int mid=(top+bottom)/2;
        int previousEnd=0, previousY=0;
        for (int i=0; i<count; ++i)
        {
            const int step=offset+i, x=columnX(i), left=x+4, right=x+37;
            const int y=bottom-4-(int(v.pitch[step])-*limits.first)*(bottom-top-8)/span;
            const bool focus=i==col, gated=(v.gates>>step)&1u;
            const Ink edge=focus ? 0xffe1edff : 0xffb0c8ff;
            if (focus) r.box(x,top,42,bottom-top-1,0xff22223f);
            r.box(x,mid,42,1,focus ? 0xff3a3658 : 0xff211e36);
            r.box(x,mid-1,1,3,0xff49415e);
            if (i%4==0) r.box(x,top,1,bottom-top-1,0xff514760);
            if (i && ((v.slides>>(step-1))&1u))
            {
                double last=previousY;
                for (int px=previousEnd; px<=left; ++px)
                {
                    const double t=double(px-previousEnd)/(left-previousEnd);
                    const double next=previousY+(y-previousY)*t*t*(3-2*t);
                    r.trace(px,last,next,0xffb69ce8,10);
                    last=next;
                }
            }
            if (gated)
            {
                r.box(left,y+1,right-left-2,1,0xff51549a);
                r.box(left+1,y,right-left,1,edge);
                r.box(left+2,y-1,((v.accents>>step)&1u) ? 12 : 4,1,edge);
            }
            else
                for (int px=left; px<right; px+=5) r.box(px,y,3,1,0xff8886b0);
            previousEnd=right;
            previousY=y;
        }
    }

    static void drawSwitches(const Raster& r,int x,uint32_t stepBit,const PatternView& v,bool focus)
    {
        namespace row=thorn::scoreRow;
        const int cx=x+21;
        const int gateY=(row::gate+row::accent)/2;
        // The three switches share one dark strip per step. Separators register across each beat.
        const Ink base=focus ? 0xff20203c : 0xff100e1c;
        r.box(x,row::gate,42,row::end-row::gate,base);
        for (int y : {row::gate,row::accent,row::slide})
        {
            r.box(x,y,42,1,focus ? 0xff695d94 : 0xff342c46);
            r.box(x,y,1,3,focus ? steel : 0xff574d6d);
        }
        if (!stepBit) return;

        // The gate is a square wave, low for rests and high between its two contacts.
        const Ink gateInk=focus ? paper : steel;
        r.box(cx-15,gateY+3,30,1,(v.gates&stepBit) ? 0xff484361 : 0xff696078);
        if (v.gates&stepBit)
        {
            r.box(cx-9,gateY-4,1,8,gateInk);
            r.box(cx-8,gateY-4,16,2,gateInk);
            r.box(cx+8,gateY-4,1,8,gateInk);
        }
        else
            r.box(cx-1,gateY+2,3,2,0xff9b92b4);

        const int state=focus ? 2 : 0;
        r.tile(controls::accentMark,state+bool(v.accents&stepBit),x+6,row::accent+4);
        r.tile(controls::slideMark,state+bool(v.slides&stepBit),x+6,row::slide+3);
    }

    static void drawScore(const Raster& r,const PatternView& v,int offset,int count,int col)
    {
        namespace row=thorn::scoreRow;
        if (col>=0 && col<count) image(r,thorn::selected,0,columnX(col),row::step);
        r.tile(controls::lane,0,5,102);
        drawPitchRail(r,v,offset,count,col);
        for (int i=0; i<16; ++i)
        {
            const int index=offset+i, x=columnX(i);
            const bool active=i==col && i<count;
            // The original numbered cap and its downbeat slash come from the restored plate.
            r.tile(controls::noteKey,active ? 1 : 0,x,row::note);
            drawSwitches(r,x,i<count ? (1u<<index) : 0u,v,active);
            if (i>=count) continue;
            r.tile(controls::stepNumber,index,x,row::step);
            pitchLabel(r,v.pitch[index],x,(row::note+row::gate-thorn::pitch.h)/2);
        }
    }

    // Each scanline is packet encoded. A group may share a palette across its frames.
    static void image(const Raster& r,const thorn::Images& img,int frame,int x,int y)
    {
        const auto* data=slink_sp16_thorn_bank+img.offsets[frame];
        const auto* palette=reinterpret_cast<const uint32_t*>(
            img.paletteOffset ? slink_sp16_thorn_bank+img.paletteOffset : data);
        if (!img.paletteOffset) data+=1024;
        for (int row=0;row<img.h;++row)
        {
            auto* dest=r.px+(row+y)*r.w+x;
            for(int col=0;col<img.w;)
            {
                const int code=*data++,len=(code&127)+1;
                const bool repeat=(code&128)!=0;
                if (!img.alpha)
                {
                    if (repeat) std::fill_n(dest+col,len,palette[*data]);
                    else for (int i=0;i<len;++i) dest[col+i]=palette[data[i]];
                }
                else if (!repeat || (palette[*data]>>24)!=0)
                {
                    for (int i=0;i<len;++i)
                    {
                        const Ink color=palette[data[repeat?0:i]];
                        const unsigned alpha=color>>24;
                        if (alpha==255) dest[col+i]=color;
                        else if (alpha) r.blend(x+col+i,y+row,color,(alpha+8)/17);
                    }
                }
                data+=repeat?1:len;col+=len;
            }
        }
    }
};
}
