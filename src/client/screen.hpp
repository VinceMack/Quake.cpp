// screen.hpp -- 2D Screen Refresh, Layout Management, and Loading System
#pragma once

#include "core/types.hpp"
#include "core/cvar.hpp"
#include "render/draw2d.hpp"

#include <EASTL/fixed_string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>

namespace Screen {

class ScreenSystem {
public:
    ScreenSystem() = default;
    ~ScreenSystem() = default;

    void Init();
    void UpdateScreen();
    void SizeUp();
    void SizeDown();
    void CenterPrint(eastl::string_view str);
    void BeginLoadingPlaque();
    void EndLoadingPlaque();
    bool ModalMessage(eastl::string_view text);

    [[nodiscard]] vrect_t& GetVrect() { return vrect_; }
    [[nodiscard]] const vrect_t& GetVrect() const { return vrect_; }

    [[nodiscard]] cvar_t& GetFov() { return fov_; }
    [[nodiscard]] const cvar_t& GetFov() const { return fov_; }

    [[nodiscard]] cvar_t& GetViewsize() { return viewsize_; }
    [[nodiscard]] const cvar_t& GetViewsize() const { return viewsize_; }

    [[nodiscard]] float& GetCentertimeOff() { return centertime_off_; }
    void SetCentertimeOff(float val) { centertime_off_ = val; }

    [[nodiscard]] float& GetConCurrent() { return con_current_; }
    void SetConCurrent(float val) { con_current_ = val; }

    [[nodiscard]] float& GetConlines() { return conlines_; }
    void SetConlines(float val) { conlines_ = val; }

    [[nodiscard]] int& GetFullupdate() { return fullupdate_; }
    void SetFullupdate(int val) { fullupdate_ = val; }

    [[nodiscard]] int& GetClearnotify() { return clearnotify_; }
    void SetClearnotify(int val) { clearnotify_ = val; }

    [[nodiscard]] qboolean& GetDisabledForLoading() { return disabled_for_loading_; }
    void SetDisabledForLoading(qboolean val) { disabled_for_loading_ = val; }

    [[nodiscard]] qboolean& GetSkipupdate() { return skipupdate_; }
    void SetSkipupdate(qboolean val) { skipupdate_ = val; }

    [[nodiscard]] qboolean& GetBlockDrawing() { return block_drawing_; }
    void SetBlockDrawing(qboolean val) { block_drawing_ = val; }

    [[nodiscard]] int& GetCopytop() { return copytop_; }
    void SetCopytop(int val) { copytop_ = val; }

    [[nodiscard]] int& GetCopyeverything() { return copyeverything_; }
    void SetCopyeverything(int val) { copyeverything_ = val; }

    static void ScreenShot_f();
    static void SizeUp_f();
    static void SizeDown_f();

private:
    cvar_t viewsize_ = { "viewsize", "100", true, false, 0.0f, nullptr };
    cvar_t fov_ = { "fov", "90", false, false, 0.0f, nullptr };
    cvar_t conspeed_ = { "scr_conspeed", "300", false, false, 0.0f, nullptr };
    cvar_t centertime_ = { "scr_centertime", "2", false, false, 0.0f, nullptr };
    cvar_t showram_ = { "showram", "1", false, false, 0.0f, nullptr };
    cvar_t showturtle_ = { "showturtle", "0", false, false, 0.0f, nullptr };
    cvar_t showpause_ = { "showpause", "1", false, false, 0.0f, nullptr };
    cvar_t printspeed_ = { "scr_printspeed", "8", false, false, 0.0f, nullptr };

    vrect_t vrect_{};

    float con_current_ = 0.0f;
    float conlines_ = 0.0f;
    float centertime_off_ = 0.0f;
    float centertime_start_ = 0.0f;
    float oldscreensize_ = 0.0f;
    float oldfov_ = 0.0f;
    float disabled_time_ = 0.0f;

    int copytop_ = 0;
    int copyeverything_ = 0;
    int fullupdate_ = 0;
    int clearconsole_ = 0;
    int clearnotify_ = 0;

    int center_lines_ = 0;
    int erase_lines_ = 0;
    int erase_center_ = 0;

    qboolean initialized_ = false;
    qboolean disabled_for_loading_ = false;
    qboolean drawloading_ = false;
    qboolean skipupdate_ = false;
    qboolean block_drawing_ = false;
    qboolean drawdialog_ = false;

    eastl::fixed_string<char, 1024> centerstring_{};
    eastl::string_view notifystring_{};

    qpic_t* ram_pic_ = nullptr;
    qpic_t* net_pic_ = nullptr;
    qpic_t* turtle_pic_ = nullptr;

    vrect_t* pconupdate_ = nullptr;

    void EraseCenterString();
    void DrawCenterString();
    void CheckDrawCenterString();
    void CalcRefdef();
    void DrawRam();
    void DrawTurtle();
    void DrawNet();
    void DrawPause();
    void DrawLoading();
    void SetUpToDrawConsole();
    void DrawConsole();
    void DrawNotifyString();
    float CalcFov(float fov_x, float width, float height);
};

[[nodiscard]] ScreenSystem& GetScreenSystem();

} // namespace Screen
