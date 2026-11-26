#pragma once
#include "panel_renderer.h"
#include <vector>

namespace hdslave_desktop
{
// A decoded desktop plate is retained until its pose or output size changes.
class HeaderPlate
{
public:
    bool paint(Canvas& canvas, int index, Rect bounds);
private:
    int index_ = -1, width_ = 0, height_ = 0;
    std::vector<uint32_t> pixels_;
};
const uint8_t* desktopTypeMasks();
}
