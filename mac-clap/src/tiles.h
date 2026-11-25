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
            "Pattern",
            "Voice",
            "Tone",
            "Sequencer 1-8",
            "Sequencer 6-13",
            "Clock"
        };
        return tile < kTileCount ? kTitles[tile] : "";
    }

    template<bool overview>
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
        p.screen.overviewTitle = overview ? title (plugin, tile) : nullptr;

        melodee_screen_frame_t c = *frame;
        c.redraw_all = 1;
        hdui::Host h = hostOf (&c);
        h.shift = false; // Desktop Shift fine-tunes a drag; keep its control visible.
        p.screen.update (h.nowMs);
        const bool drew = p.screen.paint (h, c.pixels, (int) c.stride, true);
        if (knobs_out)
            screenControls (plugin, knobs_out);

        p.screen.overviewTitle = nullptr;
        p.screen.group = savedGroup;
        p.screen.row = savedRow;
        p.screen.pinnedTop = savedTop;
        p.screen.lastGroup = -1; // The cached pixels belong to this tile, not the restored page.
        return drew;
    }

    // Pixel geometry shared by hit testing and focus. The desktop Voice tile displays both extra LFO rows.
    struct ListHit { int row, x, y, w, h; };
    static ListHit listHit (int group, int top, bool blacklight, int x, int y)
    {
        const int rows = group == 1 ? Screen::desktopVoiceRows : Screen::visible;
        if (!blacklight)
        {
            const int originY = 62;
            const int height = group == 1 ? Screen::desktopVoiceRowH : Screen::rowH;
            if (x >= 0 && x < 160 && y >= originY && y < originY + rows * height)
            {
                const int slot = (y - originY) / height;
                return { top + slot, 2, originY + slot * height, 156, height - 1 };
            }
        }
        else
        {
            const int originY = 88;
            const int height = group == 1 ? Screen::desktopVoiceTicketH : 24;
            const int perColumn = rows / 2;
            if (x >= 88 && x < 316 && y >= originY && y < originY + perColumn * height)
            {
                const int slot = (y - originY) / height;
                const auto& fields = layout::groups[group];
                if (top + slot < fields.count && fields.fields[top+slot].id == hdui::noParam)
                    return { top + slot, 88, originY + slot * height, 228, height - 2 };
                const int col = (x - 88) / 116;
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

        if (x < 0 || x >= hdui::width || y < 0 || y >= hdui::height) return false;

        auto control = [&](clap_id id, int left, int upper, int right, int lower,
                           clap_id second = CLAP_INVALID_ID) {
            if (x < left || x >= right || y < upper || y >= lower) return false;
            out_spot->kind = paramTable[paramIndex (id)].kind == Kind::button
                                 ? HDSLAVE_HOTSPOT_BUTTON : HDSLAVE_HOTSPOT_PARAM;
            out_spot->param_id = id;
            out_spot->param_id_y = second;
            out_spot->x = left;
            out_spot->y = upper;
            out_spot->w = right - left;
            out_spot->h = lower - upper;
            return true;
        };

        const int row = p.screen.tileRows[tile];
        const auto& group = layout::groups[g];
        if (y >= 185)
        {
            if (g && group.fields[row].id == hdui::noParam)
            {
                out_spot->kind = HDSLAVE_HOTSPOT_ACTION;
                out_spot->custom_id = 1;
                out_spot->x = 0; out_spot->y = 185;
                out_spot->w = 320; out_spot->h = 33;
                return true;
            }
            for (int k = 0; k < 3; ++k)
            {
                if (g && row + k >= group.count) break;
                const auto id = g ? group.fields[row+k].id : layout::performance[k].id;
                if (id != hdui::noParam && control (id, 320*k/3, 185, 320*(k+1)/3, 218)) return true;
            }
            return false;
        }
        if (g == 0)
        {
            if (!blacklight)
            {
                // The root follows the variable-width scale name in the printed metadata.
                const char* scaleName = scaleNames[int (p.screen.liveValue (ParamId::scale))];
                const int rootX = 5 + hdui::font::glyphWidth * (int (std::strlen (scaleName)) + 2);
                return control (ParamId::seed, 4, 30, 106, 82)
                    || control (ParamId::seedMode, 106, 30, 166, 82)
                    || control (ParamId::regen, 234, 30, 320, 82)
                    || control (ParamId::scale, 0, 82, rootX, 98)
                    || control (ParamId::root, rootX, 82, 133, 98)
                    || control (ParamId::octave, 133, 82, 185, 98)
                    || control (ParamId::density, 185, 82, 247, 98)
                    || control (ParamId::clockDiv, 247, 82, 320, 98)
                    || control (ParamId::transpose, 0, 98, 320, 185, ParamId::density);
            }
            return control (ParamId::regen, 0, 0, 84, 106)
                || control (ParamId::seedMode, 0, 106, 84, 120)
                || control (ParamId::seed, 0, 120, 84, 146)
                || control (ParamId::scale, 0, 146, 84, 162)
                || control (ParamId::octave, 0, 162, 84, 171)
                || control (ParamId::density, 0, 171, 84, 185)
                || control (ParamId::clockDiv, 245, 21, 320, 43)
                || control (ParamId::transpose, 84, 43, 320, 185, ParamId::density);
        }
        else
        {
            const auto hit = listHit (g, top, blacklight, x, y);
            if (hit.row >= 0 && hit.row < group.count)
            {
                const uint32_t id = group.fields[hit.row].id;
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
            if (g == 1 || g == 2)
            {
                const bool hitPlot = blacklight
                    ? control (ParamId::cutoff, 84, 21, 320, 85, ParamId::resonance)
                    : control (ParamId::cutoff, 160, 60, 320, 185, ParamId::resonance);
                if (hitPlot) out_spot->invert_y = true;
                return hitPlot;
            }
            if (g == 4)
                return blacklight ? control (ParamId::swing, 84, 69, 320, 85)
                                  : control (ParamId::swing, 160, 140, 320, 185);
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
                p.screen.tileRows[tile] = rowIdx;
        }
        p.screen.row = p.screen.tileRows[tile];
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
    TilesAccess::paint<false>,
    TilesAccess::hit_test,
    TilesAccess::focus,
    TilesAccess::action,
    MELODEE_SCREEN_PAGE_HEIGHT,
    TilesAccess::paint<true>,
};

} // namespace tbmpc
