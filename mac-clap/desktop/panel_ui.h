#pragma once
#include "desktop_wrapper.h"
#include "thorn_art.h"

namespace hdslave_desktop
{
enum Mods : uint32_t
{
    kModShift = 1,
    kModAlt = 2,
    kModCmd = 4,
    kModTouch = 8
};
enum class Item
{
    PresetPrev,
    PresetBox,
    PresetNext,
    Fine,
    Learn,
    ClearCc,
    Motion,
    Count
};

class PanelUI
{
  public:
    explicit PanelUI (DesktopPlugin *);
    ~PanelUI ();
    void setSize (int w, int h, int scale);
    Rect update (uint64_t now);
    void invalidateAll () { dirty_ = true; }
    void layoutChanged ()
    {
        layout ();
        invalidateAll ();
    }
    const uint32_t *pixels () const { return frame_.data (); }
    int physWidth () const { return w_ * scale_; }
    int physHeight () const { return h_ * scale_; }
    int scale () const { return scale_; }
    int width () const { return w_; }
    int height () const { return h_; }
    double lastRenderMs () const { return renderMs_; }
    void mouseDown (float x, float y, int button, uint32_t mods, int clicks);
    void mouseMove (float x, float y, uint32_t mods);
    void mouseUp (float x, float y, int button, uint32_t mods);
    void wheel (float x, float y, float dx, float dy, bool precise, uint32_t mods);
    void rotate (float x, float y, float degrees) { wheel (x, y, 0, degrees * .25f, true, 0); }
    void mouseExit ();
    bool key (int k);

  private:
    struct Control
    {
        Rect rect;
        int param;
        const char *label;
    };
    DesktopPlugin *p_;
    Canvas c_;
    ThornArt art_;
    std::vector<uint32_t> frame_;
    tbmpc::PatternView score_{};
    uint64_t motionMs_ = 0;
#ifndef SLINK_RAZOR_PANEL
    float accentLift_ = 0;
#endif
    std::vector<double> displayed_;
    int w_ = kDefaultGuiWidth, h_ = kDefaultGuiHeight, scale_ = 1;
    bool dirty_ = true, fine_ = false, motion_ = true, playing_ = false, follow_ = true;
    uint64_t updated_ = 0, elapsed_ = 0, pulseAt_ = 0;
    double renderMs_ = 0;
    int bank_ = 0, inspected_ = -1, lastTouched_ = -1, hover_ = -1;
    Rect items_[size_t (Item::Count)]{}, groupTitles_[4]{}, banks_[3]{};
    Rect brand_, crown_, counter_, regen_, seed_, root_, scaleBox_, scoreBox_, motionBounds_, filter_, deck_, status_;
    std::vector<Control> controls_;
    int dragParam_ = -1, dragY_ = -1, wheelParam_ = -1;
    bool moved_ = false;
    float downX_ = 0, downY_ = 0;
    double start_ = 0, startY_ = 0, wheelAccum_ = 0;
    uint64_t wheelAt_ = 0;
    Rect dragRect_{};

    enum class PopupKind
    {
        None,
        Presets,
        Choices,
        Menu
    };
    PopupKind popup_ = PopupKind::None;
    int popupParam_ = -1, popupHover_ = -1, popupSelected_ = -1, popupFirst_ = 0;
    std::vector<std::string> popupItems_;
    std::vector<double> popupValues_;
    std::vector<Rect> popupRects_;
    Rect popupRect_{};
    void openPresets ();
    void openChoices (int param, const Rect &anchor);
    void openMenu (int param, float x, float y);
    void closePopup ();
    void layoutPopup (const Rect &anchor);
    void popupPick (int idx);
    void drawPopup ();

    void layout ();
    Rect column (int slot) const;
    int hitStep (int x, int y) const;
    int hitParam (int x, int y) const;
#ifdef SLINK_RAZOR_PANEL
    Rect transport_, stormBox_, position_, contour_;
    bool cameraHeld_ = false;
    Rect faceBox () const;
    void drawRazorCrest ();
    void drawRazorFace ();
    int focusedStep () const;
    void layoutRazor (int y, int height);
    void drawRazorHeader ();
    void drawRazorFooter ();
    void drawRazorTrace (Rect r);
    void drawPitch (Rect box, int step, bool live);
#endif
    void drawHeader ();
    void drawScore ();
    void drawDeck ();
    void trace (Rect r, uint32_t ink);
    void edit (int param, Rect rect, int button, uint32_t mods, int clicks, int second = -1);
    void finishDrag ();
    void endWheelGesture ();
    double norm (int param) const;
    double fromNorm (int param, double n) const;
    std::string valueText (int param) const;
    void setParam (int param, double value);
    void stepParam (int param, int steps);
    void resetParam (int param);
};
}
