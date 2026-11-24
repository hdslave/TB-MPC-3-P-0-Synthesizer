#pragma once

#include "hdslave_tiles.h"
#include "screen.h"

namespace tbmpc
{

struct TilesAccess
{
    // 6 tiles (3x2 grid): PATTERN, VOICE, TONE, TB-3PO (1-8), TB-3PO (6-13), CLOCK & THEME
    static constexpr int kTileCount = 6;
    static constexpr int kTileGroup[kTileCount] = { 0, 1, 2, 3, 3, 4 };
    static constexpr int kTileTop[kTileCount]   = { 0, 0, 0, 0, 5, 0 };

    static uint32_t CLAP_ABI count (const clap_plugin_t*, int* out_cols, int* out_rows)
    {
        if (out_cols) *out_cols = 3;
        if (out_rows) *out_rows = 2;
        return kTileCount;
    }

    static const char* CLAP_ABI title (const clap_plugin_t*, uint32_t tile)
    {
        static constexpr const char* kTitles[kTileCount] = {
            "01 // PATTERN & SEED",
            "02 // 303 VOICE",
            "03 // TONE & FILTER",
            "04 // TB-3PO SEQ (1-8)",
            "05 // TB-3PO SEQ (6-13)",
            "06 // CLOCK & THEME"
        };
        return tile < kTileCount ? kTitles[tile] : "";
    }

    static bool CLAP_ABI paint (const clap_plugin_t* plugin, uint32_t tile, const melodee_screen_frame_t* frame, clap_id knobs_out[3])
    {
        if (tile >= kTileCount || !frame || !frame->pixels) return false;
        auto& p = of (plugin);
        const int savedGroup = p.screen.group;
        const int savedRow = p.screen.row;
        const int savedTop = p.screen.pinnedTop;

        p.screen.group = kTileGroup[tile];
        p.screen.pinnedTop = kTileTop[tile];
        p.screen.row = std::clamp (p.screen.tileRows[tile], kTileTop[tile],
                                   std::max (0, p.screen.fields().count - 1));
        p.screen.lastGroup = -1;

        melodee_screen_frame_t c = *frame;
        c.redraw_all = 1;
        const hdui::Host h = hostOf (&c);
        p.screen.update (h.nowMs);
        const bool drew = p.screen.paint (h, c.pixels, (int) c.stride, true);
        if (knobs_out)
            screenControls (plugin, knobs_out);

        p.screen.group = savedGroup;
        p.screen.row = savedRow;
        p.screen.pinnedTop = savedTop;
        return drew;
    }

    // Pixel geometry shared by hit testing and focus. The desktop Voice tile displays both extra LFO rows.
    struct ListHit { int row, x, y, w, h; };
    static ListHit listHit (int group, int top, bool blacklight, int x, int y)
    {
        const int rows = group == 1 ? Screen::desktopVoiceRows : Screen::visible;
        if (!blacklight)
        {
            const int originY = group == 1 ? 62 : 63;
            const int height = group == 1 ? Screen::desktopVoiceRowH : Screen::rowH;
            if (x >= 0 && x < 160 && y >= originY && y < originY + rows * height)
            {
                const int slot = (y - originY) / height;
                return { top + slot, 2, originY + slot * height, 156, height - 1 };
            }
        }
        else
        {
            const int originY = group == 1 ? 88 : 82;
            const int height = group == 1 ? Screen::desktopVoiceTicketH : 24;
            const int perColumn = rows / 2;
            if (x >= 88 && x < 316 && y >= originY && y < originY + perColumn * height)
            {
                const int col = (x - 88) / 116, slot = (y - originY) / height;
                return { top + col * perColumn + slot, 88 + col * 116, originY + slot * height, 112, height - 2 };
            }
        }
        return { -1, 0, 0, 0, 0 };
    }

    static bool CLAP_ABI hit_test (const clap_plugin_t* plugin, uint32_t tile, int x, int y, hdslave_tile_hotspot_t* out_spot)
    {
        if (!out_spot || tile >= kTileCount) return false;
        *out_spot = {};
        out_spot->param_id = CLAP_INVALID_ID;
        out_spot->param_id_y = CLAP_INVALID_ID;

        auto& p = of (plugin);
        const int g = kTileGroup[tile];
        const int top = kTileTop[tile];
        const bool blacklight = p.screen.theme == Screen::Theme::blacklight;

        if (g == 0)
        {
            // PATTERN screen
            if (!blacklight)
            {
                // Seed hex area (x: 0..175, y: 26..60)
                if (y >= 26 && y < 61)
                {
                    if (x < 95)
                    {
                        out_spot->kind = HDSLAVE_HOTSPOT_PARAM;
                        out_spot->param_id = ParamId::seed;
                        out_spot->x = 4; out_spot->y = 26; out_spot->w = 90; out_spot->h = 34;
                        return true;
                    }
                    if (x < 175)
                    {
                        out_spot->kind = HDSLAVE_HOTSPOT_PARAM;
                        out_spot->param_id = ParamId::seedMode;
                        out_spot->x = 95; out_spot->y = 26; out_spot->w = 80; out_spot->h = 34;
                        return true;
                    }
                    // Acid smiley / Regen trigger (x: 175..320)
                    out_spot->kind = HDSLAVE_HOTSPOT_BUTTON;
                    out_spot->param_id = ParamId::regen;
                    out_spot->x = 175; out_spot->y = 26; out_spot->w = 141; out_spot->h = 34;
                    return true;
                }
                // Status bar (y: 61..76): Scale / Root, Oct, Den, Div
                if (y >= 61 && y < 77)
                {
                    if (x < 75)
                    {
                        out_spot->kind = HDSLAVE_HOTSPOT_PARAM;
                        out_spot->param_id = ParamId::scale;
                        out_spot->x = 4; out_spot->y = 61; out_spot->w = 70; out_spot->h = 15;
                    }
                    else if (x < 115)
                    {
                        out_spot->kind = HDSLAVE_HOTSPOT_PARAM;
                        out_spot->param_id = ParamId::root;
                        out_spot->x = 75; out_spot->y = 61; out_spot->w = 40; out_spot->h = 15;
                    }
                    else if (x < 168)
                    {
                        out_spot->kind = HDSLAVE_HOTSPOT_PARAM;
                        out_spot->param_id = ParamId::octave;
                        out_spot->x = 115; out_spot->y = 61; out_spot->w = 53; out_spot->h = 15;
                    }
                    else if (x < 235)
                    {
                        out_spot->kind = HDSLAVE_HOTSPOT_PARAM;
                        out_spot->param_id = ParamId::density;
                        out_spot->x = 168; out_spot->y = 61; out_spot->w = 67; out_spot->h = 15;
                    }
                    else
                    {
                        out_spot->kind = HDSLAVE_HOTSPOT_PARAM;
                        out_spot->param_id = ParamId::clockDiv;
                        out_spot->x = 235; out_spot->y = 61; out_spot->w = 81; out_spot->h = 15;
                    }
                    return true;
                }
                // 16-step pattern score (y: 77..183)
                if (y >= 77 && y < 183)
                {
                    out_spot->kind = HDSLAVE_HOTSPOT_PARAM;
                    out_spot->param_id = ParamId::transpose;
                    out_spot->param_id_y = ParamId::density;
                    out_spot->x = 4; out_spot->y = 77; out_spot->w = 312; out_spot->h = 105;
                    return true;
                }
            }
            else
            {
                // Blacklight theme PATTERN layout
                if (x < 84 && y >= 79 && y < 183)
                {
                    if (y < 115)
                    {
                        out_spot->kind = HDSLAVE_HOTSPOT_PARAM;
                        out_spot->param_id = ParamId::seed;
                        out_spot->x = 2; out_spot->y = 79; out_spot->w = 80; out_spot->h = 36;
                    }
                    else if (y < 145)
                    {
                        out_spot->kind = HDSLAVE_HOTSPOT_PARAM;
                        out_spot->param_id = ParamId::scale;
                        out_spot->x = 2; out_spot->y = 115; out_spot->w = 80; out_spot->h = 30;
                    }
                    else
                    {
                        out_spot->kind = HDSLAVE_HOTSPOT_PARAM;
                        out_spot->param_id = ParamId::density;
                        out_spot->x = 2; out_spot->y = 145; out_spot->w = 80; out_spot->h = 36;
                    }
                    return true;
                }
                if (x >= 84 && y >= 79 && y < 183)
                {
                    out_spot->kind = HDSLAVE_HOTSPOT_PARAM;
                    out_spot->param_id = ParamId::transpose;
                    out_spot->param_id_y = ParamId::density;
                    out_spot->x = 84; out_spot->y = 79; out_spot->w = 232; out_spot->h = 102;
                    return true;
                }
            }
        }
        else
        {
            const auto& grp = layout::groups[g];
            const auto hit = listHit (g, top, blacklight, x, y);
            if (hit.row >= 0 && hit.row < grp.count)
            {
                const uint32_t id = grp.fields[hit.row].id;
                out_spot->x = (int16_t) hit.x;
                out_spot->y = (int16_t) hit.y;
                out_spot->w = (int16_t) hit.w;
                out_spot->h = (int16_t) hit.h;
                if (id == hdui::noParam)
                {
                    out_spot->kind = HDSLAVE_HOTSPOT_ACTION;
                    out_spot->custom_id = 1; // Theme switch
                    return true;
                }
                const auto& param = paramTable[paramIndex (id)];
                out_spot->kind = param.kind == Kind::button ? HDSLAVE_HOTSPOT_BUTTON : HDSLAVE_HOTSPOT_PARAM;
                out_spot->param_id = id;
                return true;
            }
            if (!blacklight)
            {
                // Right plot panel (x: 160..320, y: 61..183)
                if (x >= 160 && x < 320 && y >= 61 && y < 183)
                {
                    if (g == 1 || g == 2)
                    {
                        // 2D Filter Response plot (Cutoff & Resonance)
                        out_spot->kind = HDSLAVE_HOTSPOT_PARAM;
                        out_spot->param_id = ParamId::cutoff;
                        out_spot->param_id_y = ParamId::resonance;
                        out_spot->x = 164; out_spot->y = 65; out_spot->w = 152; out_spot->h = 114;
                        out_spot->invert_y = true;
                        return true;
                    }
                    if (g == 4 && y >= 140)
                    {
                        out_spot->kind = HDSLAVE_HOTSPOT_PARAM;
                        out_spot->param_id = ParamId::swing;
                        out_spot->x = 164; out_spot->y = 140; out_spot->w = 152; out_spot->h = 40;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    static void CLAP_ABI focus (const clap_plugin_t* plugin, uint32_t tile, int x, int y, bool)
    {
        if (tile >= kTileCount) return;
        auto& p = of (plugin);
        const int g = kTileGroup[tile];
        const int top = kTileTop[tile];
        p.screen.group = g;
        if (g > 0)
        {
            const bool blacklight = p.screen.theme == Screen::Theme::blacklight;
            const int rowIdx = listHit (g, top, blacklight, x, y).row;
            if (rowIdx >= 0 && rowIdx < layout::groups[g].count)
            {
                p.screen.tileRows[tile] = rowIdx;
                p.screen.row = rowIdx;
            }
        }
    }

    static void CLAP_ABI action (const clap_plugin_t* plugin, uint32_t, int32_t custom_id, int32_t, bool)
    {
        auto& p = of (plugin);
        if (custom_id == 1)
        {
            p.screen.theme = p.screen.theme == Screen::Theme::pressPlate
                                 ? Screen::Theme::blacklight
                                 : Screen::Theme::pressPlate;
            if (p.hostScreen)
                p.hostScreen->redraw (p.host, 0);
        }
    }
};

const clap_plugin_hdslave_tiles_t tilesExtension {
    TilesAccess::count,
    TilesAccess::title,
    TilesAccess::paint,
    TilesAccess::hit_test,
    TilesAccess::focus,
    TilesAccess::action,
    184,
};

} // namespace tbmpc
