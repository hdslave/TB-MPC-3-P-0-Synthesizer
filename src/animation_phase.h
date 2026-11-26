#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace slink::motion
{
// Sample the timeline at 60 Hz. Rounding milliseconds avoids an early or
// late duplicate when a 60 Hz presenter reports 16, 33, 50 ms and so on.
inline uint64_t tick(uint64_t ms)
{
    return ms/1000*60 + (ms%1000*60+500)/1000;
}

struct Frame
{
    int first = 0, next = 0;
    unsigned weight = 0;
    bool operator==(const Frame& other) const
    {
        return first==other.first && next==other.next && weight==other.weight;
    }
    bool operator!=(const Frame& other) const { return !(*this==other); }
};

inline Frame loop(uint64_t ms, unsigned seconds, int frames)
{
    const uint64_t length = seconds*60;
    const uint64_t position = (tick(ms)%length)*frames*256/length;
    const int frame = int(position/256);
    return {frame,(frame+1)%frames,unsigned(position%256)};
}

inline Frame smile(uint64_t ms, int frames)
{
    const double phase = (tick(ms)%288)*6.283185307179586/288 + .18;
    const int position = int(std::lround(256*(frames-1)*(.47+.32*std::sin(phase))));
    const int frame = position/256;
    return {frame,std::min(frame+1,frames-1),unsigned(position%256)};
}

inline uint32_t blend(uint32_t a, uint32_t b, unsigned weight)
{
    const unsigned old = 256-weight;
    const unsigned rb = (((a&0xff00ff)*old+(b&0xff00ff)*weight)>>8)&0xff00ff;
    const unsigned g = (((a&0x00ff00)*old+(b&0x00ff00)*weight)>>8)&0x00ff00;
    return 0xff000000u | rb | g;
}
}
