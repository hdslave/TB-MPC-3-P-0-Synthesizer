#pragma once
#include "panel_renderer.h"
#include <vector>

namespace hdslave_desktop
{
class FooterPrint
{
public:
    Rect paint(Canvas&, Rect bounds, uint64_t now, bool motion, bool full);
    void resetClock() { haveTick_ = false; }

private:
    std::vector<uint8_t> indices_;
    std::vector<uint32_t> pixels_;
    uint64_t elapsed_ = 0, lastTick_ = 0;
    int frame_ = -1;
    bool haveTick_ = false;
};
}
