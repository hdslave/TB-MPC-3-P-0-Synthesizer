#pragma once

#include <clap/clap.h>
#include "../../src/sixteen_screen.h"

namespace tbmpc
{
inline constexpr char desktopPanelAccessId[] = "com.hdslave.slink.panel-access/1";
struct DesktopPanelAccess
{
    const sixteen_plugin_screen_t* screen;
    void (*holdMotion)(const clap_plugin_t*, bool);
    void (*cameraCut)(const clap_plugin_t*);
    bool (*localPlay)(const clap_plugin_t*);
};
}
