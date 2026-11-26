#pragma once

#include <clap/clap.h>
#include "../src/pattern_view.h"
#include "../../src/sixteen_screen.h"
#include "../../src/sp16_step_edits.h"

namespace tbmpc
{
// Private, main-thread bridge between the desktop editor and its embedded instrument.
inline constexpr char desktopPanelAccessId[] = "com.hdslave.slink.panel-access/3";
struct DesktopPanelAccess
{
    void (*open)(const void*);
    void (*close)(const void*);
    PatternView (*readPattern)(const clap_plugin_t*);
    bool (*renderArt)(const clap_plugin_t*, const sixteen_screen_surface_t&, bool full, int selected, int seed, int button);
    void (*pitchTile)(uint32_t* pixels, int cv, uint32_t background, bool active);
    void (*holdMotion)(const clap_plugin_t*, bool);
    void (*camera)(const clap_plugin_t*, bool cut);
    bool (*localPlay)(const clap_plugin_t*);
    void (*togglePlay)(const clap_plugin_t*);
    void (*press)(const clap_plugin_t*, clap_id);
    bool (*beginStep)(const clap_plugin_t*, int, sp16::StepLane);
    void (*moveStep)(const clap_plugin_t*, int, int semitones);
    void (*endStep)(const clap_plugin_t*);
};
} // namespace tbmpc
