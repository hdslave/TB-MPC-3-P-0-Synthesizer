#pragma once

#include "panel_renderer.h"

namespace hdslave_desktop
{
// Monochrome moving artwork, redrawn directly at the editor's backing scale.
void drawAcidMotion (Canvas& canvas, Rect bounds, double seconds, float accent);
}
