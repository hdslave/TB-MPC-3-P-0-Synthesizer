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
    friend class Smiley;
    bool prepare(const uint8_t* bank, uint32_t bytes, int count, int index, int dw, int dh);
    void draw(Canvas& canvas, Rect bounds, const HeaderPlate* next = nullptr, unsigned weight = 0) const;
    int index_ = -1, width_ = 0, height_ = 0;
    std::vector<uint32_t> pixels_;
};
class Smiley
{
public:
    void paint(Canvas& canvas, int position, Rect bounds);
private:
    HeaderPlate poses_[2];
};
const uint8_t* desktopTypeMasks();
}
