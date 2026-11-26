// Shared Thorn Crown panel artwork. The sequencer owns all musical state.
#pragma once
#include "../assets/sp16-thorn/index.h"
#include "animation_phase.h"
#include <memory>
#include <new>

extern "C" const unsigned char slink_sp16_thorn_bank[];
namespace tbmpc::sp16
{
class ThornPage
{
public:
    int selected = 0, capturedBank = -1;
    bool heldCamera = false;

    static int columnX(int i) { return 65 + (i*693+8)/16 + (i/4)*5; }

    int first(const PatternView& v) const
    {
        if (capturedBank>=0) return capturedBank;
        return v.steps>16 && (v.running ? v.current : selected)>=16 ? 16 : 0;
    }
    int stepAt(const PatternView& v,int x,int y) const
    {
        if (y<103 || y>=245) return -1;
        for (int i=0; i<16; ++i)
        {
            const int step=first(v)+i;
            if (x>=columnX(i) && x<columnX(i)+42 && step<v.steps) return step;
        }
        return -1;
    }
    static void pitchLabel(const Raster& r, int cv, bool active, int x, int y)
    {
        const int pitch = std::clamp(int(std::lround(cv / 128.0)) + 256, 0, 512);
        image(r, active ? thorn::pitchSelected : thorn::pitch, pitch, x, y);
    }
    static void bankLabel(const Raster& r, int bank, int x, int y)
    {
        image(r, thorn::bankLabel, bank, x, y);
    }

    static void plotTitle(const Raster& r, int index, int x, int y)
    {
        image(r, thorn::plotTitle, index, x, y);
    }

    void open() { if (!media) media.reset(new (std::nothrow) Media); }
    void close() { media.reset(); }

    bool draw(const Raster& r,const PatternView& v,uint32_t now,bool full,bool updateHeader,bool updateScore,
              int seed,int button)
    {
        const uint32_t elapsed = initialized ? now-lastMs : 0;
        initialized = true;
        lastMs = now;
        if (!heldCamera) cameraMs += elapsed;
        const auto camera = slink::motion::loop(cameraMs,20,thorn::crest.count);
        const auto storm = slink::motion::loop(now,8,thorn::storm.count);
        const auto face = slink::motion::smile(now,thorn::face.count);

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
            const int focus=v.running?v.current:std::min(selected,int(v.steps)-1);
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
            const int focus = v.running ? v.current : std::min(selected,int(v.steps)-1);
            drawScore(r,v,offset,count,focus-offset);
        }
        const bool crestChanged = full || camera!=lastCamera;
        const bool faceChanged = full || face!=lastFace;
        const bool stormChanged = full || storm!=lastStorm;
        if (crestChanged) drawMedia(r,thorn::crest,camera,186,6,media ? &media->crest : nullptr);
        if (faceChanged) drawMedia(r,thorn::face,face,717,0,media ? &media->face : nullptr);
        if (stormChanged) drawMedia(r,thorn::storm,storm,stormX,253,media ? &media->storm : nullptr);
        lastCamera=camera; lastFace=face; lastStorm=storm;
        return updateHeader || updateScore || crestChanged || faceChanged || stormChanged;
    }

private:
    uint32_t lastMs = 0;
    uint64_t cameraMs = 0;
    bool initialized = false;
    slink::motion::Frame lastCamera{-1,-1,0}, lastFace{-1,-1,0}, lastStorm{-1,-1,0};

    template <int Width, int Height> struct Tile
    {
        std::array<Ink,Width*Height> pixels[2];
        int index[2]{-1,-1};
    };
    struct Media
    {
        Tile<thorn::crest.w,thorn::crest.h> crest;
        Tile<thorn::face.w,thorn::face.h> face;
        Tile<thorn::storm.w,thorn::storm.h> storm;
    };
    std::unique_ptr<Media> media;

    static void restore(const Raster& r,int x,int y,int w,int h)
    {
        const auto* plate = slink_sp16_thorn_bank+thorn::backdropOffsets[0];
        for (int row=y; row<y+h; ++row)
            std::memcpy(r.px+row*r.w+x,plate+4*(row*thorn::backdrop.w+x),4*w);
    }

    template <int W, int H>
    static void drawMedia(const Raster& r,const thorn::Images& clip,slink::motion::Frame frame,int x,int y,Tile<W,H>* cache)
    {
        if (!cache)
        {
            image(r,clip,frame.first,x,y);
            return;
        }
        const auto load = [&](int index,int slot) {
            if (cache->index[slot]==index) return;
            Raster tile{cache->pixels[slot].data(),W,H};
            image(tile,clip,index,0,0);
            cache->index[slot]=index;
        };
        const int slot = cache->index[1]==frame.first ? 1 : 0;
        load(frame.first,slot);
        if (frame.weight) load(frame.next,1-slot);
        const auto* a = cache->pixels[slot].data();
        const auto* b = cache->pixels[1-slot].data();
        for (int row=0; row<H; ++row)
        {
            auto* out = r.px+(y+row)*r.w+x;
            if (!frame.weight) std::memcpy(out,a+row*W,W*sizeof(Ink));
            else for (int col=0; col<W; ++col)
                out[col]=slink::motion::blend(a[row*W+col],b[row*W+col],frame.weight);
        }
    }

    static void drawScore(const Raster& r,const PatternView& v,int offset,int count,int col)
    {
        if (col>=0 && col<count) image(r,thorn::selected,0,columnX(col),103);
        for (int i=0; i<count; ++i)
        {
            const int index=offset+i,x=columnX(i),cx=x+21;
            const bool active=i==col;
            const Ink mark=active?0xffffffff:0xffd6d9ea, dash=active?0xffd6dcfa:0xff938aa8;
            image(r,thorn::number,index,x,104);
            pitchLabel(r,v.pitch[index],active,x,127);
            if ((v.gates>>index)&1u)
            {
                r.box(cx-5,174,10,10,mark);
                for (int cut=-3;cut<=3;cut+=3) r.box(cx+cut,181,1,2,active?0xff33499a:0xff5a5b84);
            }
            else r.frame(cx-5,174,10,10,mark);
            if ((v.accents>>index)&1u)
                for (int dy=-5;dy<=5;++dy) { int w=5-std::abs(dy);r.box(cx-w,206+dy,2*w+1,1,mark); }
            else r.box(cx-3,205,6,2,dash);
            if ((v.slides>>index)&1u)
            {
                for(int j=0;j<11;++j) r.box(cx-6+j,235-j*8/10,1,2,mark);
                for(int j=0;j<5;++j) r.box(cx+j,228-j/3,5-j,1+j/2,mark);
            }
            else r.box(cx-3,231,6,2,dash);
        }
    }

    // Each scanline is packet encoded. Score frames share a translucent palette.
    // The opaque animation frames carry their own palette before the packets.
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
                if (!img.paletteOffset)
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
