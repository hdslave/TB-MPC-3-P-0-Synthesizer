#pragma once
#include "desktop_wrapper.h"
#include "console_layout.h"
#include "footer_storm.h"
#include "header_plate.h"
#include <array>

namespace hdslave_desktop
{
enum Mods : uint32_t
{
    kModShift = 1,
    kModAlt = 2,
    kModCmd = 4,
    kModTouch = 8
};
class PanelUI
{
  public:
    explicit PanelUI(DesktopPlugin*);
    ~PanelUI();
    void setVisible(bool visible);
    void setSize(int w, int h, int scale);
    Rect update(uint64_t now);
    void invalidateAll() { dirty_ = true; }
    void layoutChanged()
    {
        layout();
        invalidateAll();
    }
    const uint32_t* pixels() const { return frame_.data(); }
    int physWidth() const { return w_ * scale_; }
    int physHeight() const { return h_ * scale_; }
    int scale() const { return scale_; }
    int width() const { return w_; }
    int height() const { return h_; }
    double lastRenderMs() const { return renderMs_; }
    void mouseDown(float x, float y, int button, uint32_t mods, int clicks);
    void mouseMove(float x, float y, uint32_t mods);
    void mouseUp(float x, float y, int button, uint32_t mods);
    void wheel(float x, float y, float dx, float dy, bool precise, uint32_t mods);
    void rotate(float x, float y, float degrees) { wheel(x, y, 0, degrees * .25f, true, 0); }
    void mouseExit();
    bool key(int k);

  private:
    DesktopPlugin* p_;
    Canvas c_;
    ConsoleLayout layout_;
    FooterStorm footerStorm_;
    HeaderPlate crestPlate_, lcdPlate_, transportPlate_, brandPlate_, regenPlate_;
    std::vector<uint32_t> frame_, art_, previousArt_;
    struct Reading
    {
        double value = 0;
        std::string text;
        int cc = -1;
        bool enabled = true;
    };
    std::array<Reading, id::count> readings_{};
    std::array<int, id::count> paramIndex_{};
    tbmpc::PatternView pattern_{};
    uint32_t lastLearn_ = CLAP_INVALID_ID;
    int w_ = kDefaultGuiWidth, h_ = kDefaultGuiHeight, scale_ = 1;
    bool visible_ = false, dirty_ = true, fine_ = false, motion_ = true, localPlay_ = false;
    double renderMs_ = 0;
    static constexpr int crestHover = -2;
    int lastTouched_ = -1, hover_ = -1, hoverStep_ = -1, hoverLane_ = -1, selected_ = 0, paintedFocus_ = -1;
    int bank_ = 0, paintedBank_ = -1;
    int paintedHover_ = -1, paintedHoverStep_ = -1, paintedHoverLane_ = -1, paintedPreset_ = -2;
    int dragParam_ = -1, wheelParam_ = -1, dragPlot_ = -1, startStep_ = -1;
    tbmpc::sp16::StepLane dragLane_ = tbmpc::sp16::StepLane::number;
    bool moved_ = false;
    float downX_ = 0, downY_ = 0;
    double start_ = 0, wheelAccum_ = 0;
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
    void openPresets();
    void openChoices(int param, const Rect& anchor);
    void openMenu(int param, float x, float y);
    void closePopup();
    void layoutPopup(const Rect& anchor);
    void popupPick(int idx);
    void drawPopup();

    void layout() { layout_.arrange(w_, h_); }
    int columns() const { return pattern_.steps > 16 ? 32 : 16; }
    int focus() const;
    void paintToolbar();
    void paintRibbon();
    void paintFooter();
    void paintScore();
    void paintSection(size_t group);
    void paintSlot(int slot, uint32_t ink);
    void paintPlot(int slot, uint32_t ink);
    void movePlot(float x, float y);
    void setHover(float x, float y);
    Rect paintArt(bool full, bool refreshHeader, uint64_t now);
    void edit(int param, Rect anchor, int button, uint32_t mods, int clicks);
    void finishDrag();
    void endWheelGesture();
    double norm(int param) const;
    double fromNorm(int param, double n) const;
    std::string valueText(int param) const;
    bool canEdit(int param) const;
    void setParam(int param, double value);
    void stepParam(int param, int steps);
    void resetParam(int param);
};
} // namespace hdslave_desktop
