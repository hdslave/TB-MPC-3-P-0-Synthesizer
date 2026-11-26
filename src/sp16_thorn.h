// Shared Thorn Crown panel artwork. The sequencer owns all musical state.
#pragma once
#include "../assets/sp16-thorn/index.h"

#if defined(SLINK_DESKTOP) && defined(_WIN32)
const unsigned char* desktopThornBank();
#else
extern "C" const unsigned char slink_sp16_thorn_bank[];
#endif
namespace tbmpc::sp16
{
class ThornPage
{
public:
    int selected = 0, capturedBank = -1;
    bool heldCamera = false;
#ifdef SLINK_DESKTOP
    bool heldMotion = false;
    void nextCameraCut()
    {
        constexpr uint32_t cuts[]{0,5000,11000,14000,20000};
        const auto phase=cameraMs%20000;
        for (const auto cut:cuts)
            if (cut>phase) { cameraMs+=cut-phase; break; }
    }
#endif

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

    bool draw(const Raster& r,const PatternView& v,uint32_t now,bool full,
              int seed,int button,bool score)
    {
        const uint32_t elapsed=lastCamera < 0 ? 0 : now-lastMs;
        lastMs=now;
        const auto* bytes=bank();
        if (!bytes) return false;
#ifdef SLINK_DESKTOP
        if (!heldMotion)
        {
            motionMs+=elapsed;
            if (!heldCamera) cameraMs+=elapsed;
        }
        now=motionMs;
#else
        if (!heldCamera) cameraMs+=elapsed;
#endif
        const int camera=int(uint64_t(cameraMs % 20000)*thorn::crest.count/20000);
        const int storm=int(uint64_t(now % 8000)*thorn::storm.count/8000);
        const double sway=.47+.32*std::sin((now%4800)*(6.28318530718/4800)+.18);
        const int face=int(std::lround(256*(thorn::face.count-1)*sway));
        if (full)
        {
            const auto* plate=bytes+thorn::backdropOffsets[0];
            const int height = score ? tabTop : bodyTop;
            for (int y=0; y<height; ++y)
                std::memcpy(r.px+y*r.w,plate+4*y*thorn::backdrop.w,4*thorn::backdrop.w);
            const int offset=first(v), count=std::clamp(int(v.steps)-offset,0,16);
            const int focus=v.running?v.current:std::min(selected,int(v.steps)-1);
            const int col=focus-offset;
            if (score) drawScore(r,v,offset,count,col);
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
        const bool moving=full || camera!=lastCamera || storm!=lastStorm || face!=lastFace;
        if (full || camera!=lastCamera) image(r,thorn::crest,camera,186,6);
        if (full || face!=lastFace) drawFace(r,face);
        if (full || storm!=lastStorm) image(r,thorn::storm,storm,stormX,253);
        lastCamera=camera;lastFace=face;lastStorm=storm;
        return moving;
    }

private:
    uint32_t lastMs=0,cameraMs=0;
#ifdef SLINK_DESKTOP
    uint32_t motionMs=0;
#endif
    int lastFace=-1,lastCamera=-1,lastStorm=-1;

    static const unsigned char* bank()
    {
#if defined(SLINK_DESKTOP) && defined(_WIN32)
        return desktopThornBank();
#else
        return slink_sp16_thorn_bank;
#endif
    }

    static constexpr int facePixels=thorn::face.w*thorn::face.h;
    std::array<Ink,facePixels> poses[2];
    int cached[2] {-1,-1};
    void drawFace(const Raster& r,int position)
    {
        const int low=position/256, fraction=position%256;
        for (int frame=low; frame<=low+1; ++frame)
        {
            const int slot=frame%2;
            if (cached[slot]==frame) continue;
            Raster tile {poses[slot].data(),thorn::face.w,thorn::face.h};
            image(tile,thorn::face,frame,0,0);
            cached[slot]=frame;
        }
        for (int y=0;y<thorn::face.h;++y)
        {
            auto* out=r.px+y*r.w+717;
            for (int x=0;x<thorn::face.w;++x)
            {
                const int i=y*thorn::face.w+x;
                const Ink a=poses[low%2][i], b=poses[(low+1)%2][i];
                auto mix=[&](unsigned mask) {
                    return ((((a&mask)*(256-fraction)+(b&mask)*fraction)>>8)&mask);
                };
                out[x]=0xff000000u|mix(0xff00ff)|mix(0x00ff00);
            }
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
        const auto* bytes=bank();
        if (!bytes) return;
        const auto* data=bytes+img.offsets[frame];
        const auto* palette=reinterpret_cast<const uint32_t*>(
            img.paletteOffset ? bytes+img.paletteOffset : data);
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
