// Native Thorn Crown Pattern page. The sequencer owns all musical state.
#pragma once
#include "../assets/sp16-thorn/index.h"

extern "C" const unsigned char slink_sp16_thorn_bank[];
namespace tbmpc::sp16
{
class ThornPage
{
public:
    int selected = 0;
    bool menu = false, heldCamera = false;

    static int columnX(int i) { return 65 + (i*693+8)/16 + (i/4)*5; }

    int first(const PatternView& v) const
    {
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
    void enter(uint32_t now) { lastMs=now; }
    bool draw(const Raster& r,const PatternView& v,uint32_t now,bool full,int pulse,
              int seed,const char* scaleName,int button)
    {
        const uint32_t elapsed=now-lastMs;
        lastMs=now;
        if (!heldCamera) cameraMs+=elapsed;
        const int camera=int((uint64_t(cameraMs)*12/1000)%thorn::crest.count);
        const int storm=int((uint64_t(now)*12/1000)%thorn::storm.count);
        const double sway=.47+.32*std::sin(now*(6.28318530718/4800)+.18);
        const int face=std::clamp(int(std::lround(32*sway+2*pulse)),0,32);
        if (full)
        {
            const auto* plate=slink_sp16_thorn_bank+thorn::backdropOffsets[0];
            for (int y=0;y<thorn::backdrop.h;++y)
                std::memcpy(r.px+y*r.w,plate+4*y*thorn::backdrop.w,4*thorn::backdrop.w);
            const int offset=first(v), count=std::clamp(int(v.steps)-offset,0,16);
            const int focus=v.running?v.current:std::min(selected,int(v.steps)-1);
            const int col=focus-offset;
            if (col>=0 && col<count) image(r,thorn::selected,0,columnX(col),103);
            for (int i=0; i<count; ++i)
            {
                const int index=offset+i,x=columnX(i),cx=x+21;
                const bool active=i==col;
                const Ink mark=active?0xffffffff:0xffd6d9ea, dash=active?0xffd6dcfa:0xff938aa8;
                const int pitch=std::clamp(int(std::lround(v.pitch[index]/128.0))+256,0,512);
                image(r,thorn::number,index,x,104);
                image(r,active?thorn::pitchSelected:thorn::pitch,pitch,x,127);
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
            image(r,thorn::counter, std::clamp(focus+1,0,32),528,20);
            image(r,thorn::button,button,622,55);
            char text[16];
            std::snprintf(text,sizeof(text),"/%d",v.steps);
            r.right(619,77,text,muted);
            std::snprintf(text,sizeof(text),"%04X",unsigned(seed)&65535u);
            r.reading(12,71,70,text,paper,true);
            for (int i=0;i<20;++i)
                r.box(72+i*98/20,59,((seed>>(i%16))&1)?4:1,26,i%6?0xff919fbe:0xffced7ff);
            r.text(15,259,scaleName,0xffddd6eb);
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
                trace(625,17,77,23);trace(382,256,154,16);
                if(col>=0 && col<count) r.box(558+col*136/10,258,6,14,0xff8facff);
            }
        }
        const bool moving=full || camera!=lastCamera || storm!=lastStorm || face!=lastFace;
        if (full || camera!=lastCamera) image(r,thorn::crest,camera,186,6);
        if (full || face!=lastFace) image(r,thorn::face,face,717,0);
        if ((full || storm!=lastStorm) && !menu) image(r,thorn::storm,storm,201,253);
        lastCamera=camera;lastFace=face;lastStorm=storm;
        return moving;
    }

private:
    uint32_t lastMs=0,cameraMs=0;
    int lastFace=-1,lastCamera=-1,lastStorm=-1;

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
