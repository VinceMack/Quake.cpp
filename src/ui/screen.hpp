// screen.hpp -- 2D Screen Refresh, Layout Management, and Loading System
#pragma once

#include "core/types.hpp"
#include "core/cvar.hpp"
#include "render/draw2d.hpp"
#include "render/render_types.hpp"
#include "core/wad.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace Screen {

class ScreenSystem {
public:
    ScreenSystem() = default;
    ~ScreenSystem() = default;

    void Init();
    void UpdateScreen();
    void SizeUp();
    void SizeDown();
    void CenterPrint(std::string_view str);
    void BeginLoadingPlaque();
    void EndLoadingPlaque();
    bool ModalMessage(std::string_view text);

    static void ScreenShot_f();
    static void SizeUp_f();
    static void SizeDown_f();

    // State shared with the console, HUD, menu, view and renderer.
    cvar_t viewsize = { "viewsize", "100", true, false, 0.0f, nullptr };
    cvar_t fov = { "fov", "90", false, false, 0.0f, nullptr };
    vrect_t vrect { };        // the 3D view rectangle within the screen
    float con_current = 0.0f; // console height currently drawn
    float conlines = 0.0f;    // console height being animated toward
    float centertime_off = 0.0f;
    int copytop = 0; // dirty-rectangle bookkeeping for the software refresh
    int copyeverything = 0;
    int fullupdate = 0;
    int clearnotify = 0;
    qboolean disabled_for_loading = false;
    qboolean skipupdate = false;
    qboolean block_drawing = false;

private:
    cvar_t conspeed_ = { "scr_conspeed", "300", false, false, 0.0f, nullptr };
    cvar_t centertime_ = { "scr_centertime", "2", false, false, 0.0f, nullptr };
    cvar_t showram_ = { "showram", "1", false, false, 0.0f, nullptr };
    cvar_t showturtle_ = { "showturtle", "0", false, false, 0.0f, nullptr };
    cvar_t showpause_ = { "showpause", "1", false, false, 0.0f, nullptr };
    cvar_t printspeed_ = { "scr_printspeed", "8", false, false, 0.0f, nullptr };

    float centertime_start_ = 0.0f;
    float oldscreensize_ = 0.0f;
    float oldfov_ = 0.0f;
    float disabled_time_ = 0.0f;

    int clearconsole_ = 0;

    int center_lines_ = 0;
    int erase_lines_ = 0;
    int erase_center_ = 0;

    qboolean initialized_ = false;
    qboolean drawloading_ = false;
    qboolean drawdialog_ = false;

    std::string centerstring_;
    std::string_view notifystring_ { };

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
