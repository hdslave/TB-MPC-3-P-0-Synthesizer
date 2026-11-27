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
            image(r,thorn::counter, std::clamp(focus+1,0,32),528,20);
            image(r,thorn::button,button,622,55);
            char text[16];
            std::snprintf(text,sizeof(text),"/%d",v.steps);
            r.right(619,77,text,muted);
            std::snprintf(text,sizeof(text),"%04X",unsigned(seed)&65535u);
            r.reading(12,71,70,text,paper,true);
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
        if (stormChanged) image(r,thorn::storm,storm,stormX,253);
        lastCamera=camera; lastFace=face; lastStorm=storm;
        return updateHeader || updateScore || crestChanged || faceChanged || stormChanged;
    }

private:
    uint32_t lastMs = 0;
    uint64_t cameraMs = 0;
    bool initialized = false;
    int lastCamera=-1, lastFace=-1, lastStorm=-1;

    static void restore(const Raster& r,int x,int y,int w,int h)
    {
        const auto* plate = slink_sp16_thorn_bank+thorn::backdropOffsets[0];
        for (int row=y; row<y+h; ++row)
            std::memcpy(r.px+row*r.w+x,plate+4*(row*thorn::backdrop.w+x),4*w);
    }

    static void drawPitchRail(const Raster& r,const PatternView& v,int offset,int count,int col)
    {
        if (count<=0) return;
        constexpr int top=thorn::scoreRow::pitch, bottom=thorn::scoreRow::note;
        const auto limits=std::minmax_element(v.pitch,v.pitch+v.steps);
        const int span=std::max(128,int(*limits.second)-int(*limits.first));
        int previousEnd=0, previousY=0;
        for (int i=0; i<count; ++i)
        {
            const int step=offset+i, x=columnX(i), left=x+4, right=x+38;
            const int y=bottom-4-(int(v.pitch[step])-*limits.first)*(bottom-top-9)/span;
            const Ink ink=i==col ? paper : steel;
            if (i && ((v.slides>>(step-1))&1u))
            {
                double last=previousY;
                for (int px=previousEnd; px<=left; ++px)
                {
                    const double t=double(px-previousEnd)/(left-previousEnd);
                    const double next=previousY+(y-previousY)*t*t*(3-2*t);
                    r.trace(px,last,next,muted,9);
                    last=next;
                }
            }
            if ((v.gates>>step)&1u) r.box(left,y-1,right-left+1,3,ink);
            else r.frame(left,y-1,right-left+1,3,ink);
            if ((v.accents>>step)&1u)
            {
                r.box(x+20,top+2,2,1,steel);
                r.box(x+19,top+3,4,1,steel);
            }
            previousEnd=right;
            previousY=y;
        }
    }

    static void drawScore(const Raster& r,const PatternView& v,int offset,int count,int col)
    {
        namespace row=thorn::scoreRow;
        if (col>=0 && col<count) image(r,thorn::selected,0,columnX(col),row::step);
        drawPitchRail(r,v,offset,count,col);
        constexpr int gateY=(row::gate+row::accent)/2, accentY=(row::accent+row::slide)/2;
        constexpr int slideY=(row::slide+row::end)/2;
        for (int i=0; i<count; ++i)
        {
            const int index=offset+i,x=columnX(i),cx=x+21;
            const bool active=i==col;
            const Ink mark=active?0xffffffff:0xffd6d9ea, dash=active?0xffd6dcfa:0xff938aa8;
            image(r,thorn::number,index,x,row::step);
            pitchLabel(r,v.pitch[index],x,(row::note+row::gate-thorn::pitch.h)/2);
            if ((v.gates>>index)&1u)
            {
                r.box(cx-5,gateY-5,10,10,mark);
                for (int cut=-3;cut<=3;cut+=3) r.box(cx+cut,gateY+2,1,2,active?0xff33499a:0xff5a5b84);
            }
            else r.frame(cx-5,gateY-5,10,10,mark);
            if ((v.accents>>index)&1u)
                for (int dy=-5;dy<=5;++dy) { int w=5-std::abs(dy);r.box(cx-w,accentY+dy,2*w+1,1,mark); }
            else r.box(cx-3,accentY-1,6,2,dash);
            if ((v.slides>>index)&1u)
            {
                for(int j=0;j<11;++j) r.box(cx-6+j,slideY+3-j*8/10,1,2,mark);
                for(int j=0;j<5;++j) r.box(cx+j,slideY-4-j/3,5-j,1+j/2,mark);
            }
            else r.box(cx-3,slideY-1,6,2,dash);
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
