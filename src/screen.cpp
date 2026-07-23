// screen.cpp -- master for refresh, status bar, console, chat, notify, etc

#include "quakedef.hpp"
#include "r_local.hpp"
#include "screen.hpp"

#include <EASTL/fixed_string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <cmath>
#include <cstdio>

using namespace Client;
using namespace Common;
using namespace Console;
using namespace Render;
using namespace Draw;
using namespace Host;
using namespace Input;
using namespace Keys;
using namespace Math;
using namespace Menu;
using namespace Model;
using namespace Net;
using namespace VM;
using namespace Sbar;
using namespace Screen;
using namespace Server;
using namespace Audio;
using namespace Vid;
using namespace View;
using namespace Wad;
using namespace Cvar;
using namespace Cmd;

namespace Screen {

ScreenSystem& GetScreenSystem()
{
    static ScreenSystem instance;
    return instance;
}

/*
===============================================================================

CENTER PRINTING

===============================================================================
*/

void ScreenSystem::CenterPrint(eastl::string_view str)
{
    if (str.length() >= centerstring_.capacity()) {
        centerstring_.assign(str.data(), centerstring_.capacity() - 1);
    } else {
        centerstring_.assign(str.data(), str.length());
    }

    centertime_off_ = centertime_.value;
    centertime_start_ = static_cast<float>(cl.time);

    // count the number of lines for centering
    center_lines_ = 1;
    for (char ch : centerstring_) {
        if (ch == '\n') {
            center_lines_++;
        }
    }
}

void ScreenSystem::EraseCenterString()
{
    int y = 0;

    if (erase_center_++ > vid.numpages) {
        erase_lines_ = 0;
        return;
    }

    if (center_lines_ <= 4) {
        y = static_cast<int>(vid.height * 0.35f);
    } else {
        y = 48;
    }

    copytop_ = 1;
    Draw_TileClear(0, y, vid.width, 8 * erase_lines_);
}

void ScreenSystem::DrawCenterString()
{
    int remaining = 0;

    if (cl.intermission) {
        remaining = static_cast<int>(printspeed_.value * (cl.time - centertime_start_));
    } else {
        remaining = 9999;
    }

    erase_center_ = 0;
    const char* start = centerstring_.c_str();

    int y = (center_lines_ <= 4) ? static_cast<int>(vid.height * 0.35f) : 48;

    do {
        int l = 0;
        for (l = 0; l < 40; l++) {
            if (start[l] == '\n' || start[l] == '\0') {
                break;
            }
        }

        int x = (vid.width - l * 8) / 2;
        for (int j = 0; j < l; j++, x += 8) {
            Draw_Character(x, y, start[j]);
            if (!remaining--) {
                return;
            }
        }

        y += 8;

        while (*start && *start != '\n') {
            start++;
        }

        if (!*start) {
            break;
        }

        start++; // skip the \n
    } while (true);
}

void ScreenSystem::CheckDrawCenterString()
{
    copytop_ = 1;
    if (center_lines_ > erase_lines_) {
        erase_lines_ = center_lines_;
    }

    centertime_off_ -= static_cast<float>(host_frametime);

    if (centertime_off_ <= 0.0f && !cl.intermission) {
        return;
    }

    if (key_dest != key_game) {
        return;
    }

    DrawCenterString();
}

/*
===============================================================================

CALCULATE REFDEF

===============================================================================
*/

float ScreenSystem::CalcFov(float fov_x, float width, float height)
{
    if (fov_x < 1.0f || fov_x > 179.0f) {
        Sys_Error("Bad fov: %f", fov_x);
    }

    float x = width / static_cast<float>(std::tan(fov_x / 360.0f * M_PI));
    float a = static_cast<float>(std::atan(height / x) * 360.0f / M_PI);
    return a;
}

void ScreenSystem::CalcRefdef()
{
    vrect_t vrect{};

    fullupdate_ = 0; // force a background redraw
    vid.recalc_refdef = 0;

    // force the status bar to redraw
    Sbar_Changed();

    // bound viewsize
    if (viewsize_.value < 30.0f) {
        Cvar::Set("viewsize", "30");
    }
    if (viewsize_.value > 120.0f) {
        Cvar::Set("viewsize", "120");
    }

    // bound field of view
    if (fov_.value < 10.0f) {
        Cvar::Set("fov", "10");
    }
    if (fov_.value > 170.0f) {
        Cvar::Set("fov", "170");
    }

    r_refdef.fov_x = fov_.value;
    r_refdef.fov_y = CalcFov(r_refdef.fov_x, static_cast<float>(r_refdef.vrect.width), static_cast<float>(r_refdef.vrect.height));

    float size = cl.intermission ? 120.0f : viewsize_.value;

    if (size >= 120.0f) {
        sb_lines = 0; // no status bar at all
    } else if (size >= 110.0f) {
        sb_lines = 24; // no inventory
    } else {
        sb_lines = 24 + 16 + 8;
    }

    vrect.x = 0;
    vrect.y = 0;
    vrect.width = vid.width;
    vrect.height = vid.height;

    R_SetVrect(&vrect, &vrect_, sb_lines);

    if (con_current_ > static_cast<float>(vid.height)) {
        con_current_ = static_cast<float>(vid.height);
    }

    R_ViewChanged(&vrect, sb_lines, vid.aspect);
}

void ScreenSystem::SizeUp()
{
    SizeUp_f();
}

void ScreenSystem::SizeDown()
{
    SizeDown_f();
}

void ScreenSystem::SizeUp_f()
{
    auto& sys = GetScreenSystem();
    Cvar::SetValue("viewsize", sys.viewsize_.value + 10.0f);
    vid.recalc_refdef = 1;
}

void ScreenSystem::SizeDown_f()
{
    auto& sys = GetScreenSystem();
    Cvar::SetValue("viewsize", sys.viewsize_.value - 10.0f);
    vid.recalc_refdef = 1;
}

/*
===============================================================================

INITIALIZATION

===============================================================================
*/

void ScreenSystem::Init()
{
    Cvar::Register(&fov_);
    Cvar::Register(&viewsize_);
    Cvar::Register(&conspeed_);
    Cvar::Register(&showram_);
    Cvar::Register(&showturtle_);
    Cvar::Register(&showpause_);
    Cvar::Register(&centertime_);
    Cvar::Register(&printspeed_);

    Cmd::AddCommand("screenshot", ScreenShot_f);
    Cmd::AddCommand("sizeup", SizeUp_f);
    Cmd::AddCommand("sizedown", SizeDown_f);

    ram_pic_ = Draw_PicFromWad("ram");
    net_pic_ = Draw_PicFromWad("net");
    turtle_pic_ = Draw_PicFromWad("turtle");

    initialized_ = true;
}

void ScreenSystem::DrawRam()
{
    if (!showram_.value || !r_cache_thrash) {
        return;
    }

    Draw_Pic(vrect_.x + 32, vrect_.y, ram_pic_);
}

void ScreenSystem::DrawTurtle()
{
    static int count = 0;

    if (!showturtle_.value) {
        return;
    }

    if (host_frametime < 0.1) {
        count = 0;
        return;
    }

    count++;
    if (count < 3) {
        return;
    }

    Draw_Pic(vrect_.x, vrect_.y, turtle_pic_);
}

void ScreenSystem::DrawNet()
{
    if (realtime - cl.last_received_message < 0.3 || cls.demoplayback) {
        return;
    }

    Draw_Pic(vrect_.x + 64, vrect_.y, net_pic_);
}

void ScreenSystem::DrawPause()
{
    if (!showpause_.value || !cl.paused) {
        return;
    }

    qpic_t* pic = Draw_CachePic("gfx/pause.lmp");
    Draw_Pic((vid.width - pic->width) / 2, (vid.height - 48 - pic->height) / 2, pic);
}

void ScreenSystem::DrawLoading()
{
    if (!drawloading_) {
        return;
    }

    qpic_t* pic = Draw_CachePic("gfx/loading.lmp");
    Draw_Pic((vid.width - pic->width) / 2, (vid.height - 48 - pic->height) / 2, pic);
}

/*
===============================================================================

CONSOLE DRAWING SETUP

===============================================================================
*/

void ScreenSystem::SetUpToDrawConsole()
{
    GetConsoleSystem().CheckResize();

    if (drawloading_) {
        return;
    }

    GetConsoleSystem().SetForcedUp(!cl.worldmodel || cls.signon != SIGNONS);

    if (GetConsoleSystem().IsForcedUp()) {
        conlines_ = static_cast<float>(vid.height);
        con_current_ = conlines_;
    } else if (key_dest == key_console) {
        conlines_ = static_cast<float>(vid.height / 2);
    } else {
        conlines_ = 0.0f;
    }

    if (conlines_ < con_current_) {
        con_current_ -= static_cast<float>(conspeed_.value * host_frametime);
        if (conlines_ > con_current_) {
            con_current_ = conlines_;
        }
    } else if (conlines_ > con_current_) {
        con_current_ += static_cast<float>(conspeed_.value * host_frametime);
        if (conlines_ < con_current_) {
            con_current_ = conlines_;
        }
    }

    if (clearconsole_++ < vid.numpages) {
        copytop_ = 1;
        Draw_TileClear(0, static_cast<int>(con_current_), vid.width,
            vid.height - static_cast<int>(con_current_));
        Sbar_Changed();
    } else if (clearnotify_++ < vid.numpages) {
        copytop_ = 1;
        Draw_TileClear(0, 0, vid.width, GetConsoleSystem().GetNotifyLines());
    } else {
        GetConsoleSystem().SetNotifyLines(0);
    }
}

void ScreenSystem::DrawConsole()
{
    if (con_current_) {
        copyeverything_ = 1;
        GetConsoleSystem().DrawConsole(static_cast<int>(con_current_), true);
        clearconsole_ = 0;
    } else {
        if (key_dest == key_game || key_dest == key_message) {
            GetConsoleSystem().DrawNotify();
        }
    }
}

/*
===============================================================================

SCREENSHOTS

===============================================================================
*/

#pragma pack(push, 1)
struct pcx_header_t {
    uint8_t manufacturer = 0x0a;
    uint8_t version = 5;
    uint8_t encoding = 1;
    uint8_t bits_per_pixel = 8;
    uint16_t xmin = 0;
    uint16_t ymin = 0;
    uint16_t xmax = 0;
    uint16_t ymax = 0;
    uint16_t hres = 0;
    uint16_t vres = 0;
    uint8_t palette[48]{};
    uint8_t reserved = 0;
    uint8_t color_planes = 1;
    uint16_t bytes_per_line = 0;
    uint16_t palette_type = 2;
    uint8_t filler[58]{};
};
#pragma pack(pop)

static void WritePCXfile(const char* filename,
    const byte* data,
    int width,
    int height,
    int rowbytes,
    const byte* palette)
{
    eastl::vector<uint8_t> buffer;
    buffer.reserve(sizeof(pcx_header_t) + width * height * 2 + 1024);
    buffer.resize(sizeof(pcx_header_t));

    auto* pcx = reinterpret_cast<pcx_header_t*>(buffer.data());
    pcx->manufacturer = 0x0a;
    pcx->version = 5;
    pcx->encoding = 1;
    pcx->bits_per_pixel = 8;
    pcx->xmin = 0;
    pcx->ymin = 0;
    pcx->xmax = LittleShort(static_cast<short>(width - 1));
    pcx->ymax = LittleShort(static_cast<short>(height - 1));
    pcx->hres = LittleShort(static_cast<short>(width));
    pcx->vres = LittleShort(static_cast<short>(height));
    pcx->color_planes = 1;
    pcx->bytes_per_line = LittleShort(static_cast<short>(width));
    pcx->palette_type = LittleShort(2);

    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            byte val = *data++;
            if ((val & 0xc0) != 0xc0) {
                buffer.push_back(val);
            } else {
                buffer.push_back(0xc1);
                buffer.push_back(val);
            }
        }
        data += rowbytes - width;
    }

    buffer.push_back(0x0c);
    for (int i = 0; i < 768; i++) {
        buffer.push_back(*palette++);
    }

    COM_WriteFile(filename, buffer.data(), static_cast<int>(buffer.size()));
}

void ScreenSystem::ScreenShot_f()
{
    int i = 0;
    eastl::fixed_string<char, 80> pcxname = "quake00.pcx";
    char checkname[MAX_OSPATH];

    for (i = 0; i <= 99; i++) {
        pcxname[5] = static_cast<char>(i / 10 + '0');
        pcxname[6] = static_cast<char>(i % 10 + '0');
        sprintf_s(checkname, sizeof(checkname), "%s/%s", com_gamedir, pcxname.c_str());
        if (Sys_FileTime(checkname) == -1) {
            break;
        }
    }

    if (i == 100) {
        Con_Printf("SCR_ScreenShot_f: Couldn't create a PCX file\n");
        return;
    }

    D_EnableBackBufferAccess();

    WritePCXfile(pcxname.c_str(), vid.buffer, vid.width, vid.height, vid.rowbytes, host_basepal);

    D_DisableBackBufferAccess();

    Con_Printf("Wrote %s\n", pcxname.c_str());
}

/*
===============================================================================

LOADING PLAQUE & MODAL DIALOGS

===============================================================================
*/

void ScreenSystem::BeginLoadingPlaque()
{
    S_StopAllSounds(true);

    if (cls.state != ca_connected || cls.signon != SIGNONS) {
        return;
    }

    GetConsoleSystem().ClearNotify();
    centertime_off_ = 0.0f;
    con_current_ = 0.0f;

    drawloading_ = true;
    fullupdate_ = 0;
    Sbar_Changed();
    UpdateScreen();
    drawloading_ = false;

    disabled_for_loading_ = true;
    disabled_time_ = static_cast<float>(realtime);
    fullupdate_ = 0;
}

void ScreenSystem::EndLoadingPlaque()
{
    disabled_for_loading_ = false;
    fullupdate_ = 0;
    GetConsoleSystem().ClearNotify();
}

void ScreenSystem::DrawNotifyString()
{
    const char* start = notifystring_.data();
    if (!start) {
        return;
    }

    int y = static_cast<int>(vid.height * 0.35f);

    do {
        int l = 0;
        for (l = 0; l < 40; l++) {
            if (start[l] == '\n' || !start[l]) {
                break;
            }
        }

        int x = (vid.width - l * 8) / 2;
        for (int j = 0; j < l; j++, x += 8) {
            Draw_Character(x, y, start[j]);
        }

        y += 8;

        while (*start && *start != '\n') {
            start++;
        }

        if (!*start) {
            break;
        }

        start++; // skip the \n
    } while (true);
}

bool ScreenSystem::ModalMessage(eastl::string_view text)
{
    if (cls.state == ca_dedicated) {
        return true;
    }

    notifystring_ = text;

    fullupdate_ = 0;
    drawdialog_ = true;
    UpdateScreen();
    drawdialog_ = false;

    S_ClearBuffer();

    do {
        key_count = -1;
        Sys_SendKeyEvents();
    } while (key_lastpress != 'y' && key_lastpress != 'n' && key_lastpress != K_ESCAPE);

    fullupdate_ = 0;
    UpdateScreen();

    return key_lastpress == 'y';
}

/*
===============================================================================

MAIN SCREEN UPDATE LOOP

===============================================================================
*/

void ScreenSystem::UpdateScreen()
{
    static float oldscr_viewsize = 0.0f;
    static float oldlcd_x = 0.0f;
    vrect_t vrect{};

    if (skipupdate_ || block_drawing_) {
        return;
    }

    copytop_ = 0;
    copyeverything_ = 0;

    if (disabled_for_loading_) {
        if (realtime - disabled_time_ > 60) {
            disabled_for_loading_ = false;
            Con_Printf("load failed.\n");
        } else {
            return;
        }
    }

    if (cls.state == ca_dedicated || !initialized_ || !GetConsoleSystem().IsInitialized()) {
        return;
    }

    if (viewsize_.value != oldscr_viewsize) {
        oldscr_viewsize = viewsize_.value;
        vid.recalc_refdef = 1;
    }

    if (oldfov_ != fov_.value) {
        oldfov_ = fov_.value;
        vid.recalc_refdef = true;
    }

    if (oldlcd_x != lcd_x.value) {
        oldlcd_x = lcd_x.value;
        vid.recalc_refdef = true;
    }

    if (oldscreensize_ != viewsize_.value) {
        oldscreensize_ = viewsize_.value;
        vid.recalc_refdef = true;
    }

    if (vid.recalc_refdef) {
        CalcRefdef();
    }

    D_EnableBackBufferAccess();

    if (fullupdate_++ < vid.numpages) {
        copyeverything_ = 1;
        Draw_TileClear(0, 0, vid.width, vid.height);
        Sbar_Changed();
    }

    pconupdate_ = nullptr;

    SetUpToDrawConsole();
    EraseCenterString();

    D_DisableBackBufferAccess();

    VID_LockBuffer();
    V_RenderView();
    VID_UnlockBuffer();

    D_EnableBackBufferAccess();

    if (drawdialog_) {
        Sbar_Draw();
        Draw_FadeScreen();
        DrawNotifyString();
        copyeverything_ = true;
    } else if (drawloading_) {
        DrawLoading();
        Sbar_Draw();
    } else if (cl.intermission == 1 && key_dest == key_game) {
        Sbar_IntermissionOverlay();
    } else if (cl.intermission == 2 && key_dest == key_game) {
        Sbar_FinaleOverlay();
        CheckDrawCenterString();
    } else if (cl.intermission == 3 && key_dest == key_game) {
        CheckDrawCenterString();
    } else {
        DrawRam();
        DrawNet();
        DrawTurtle();
        DrawPause();
        CheckDrawCenterString();
        Sbar_Draw();
        DrawConsole();
        M_Draw();
    }

    D_DisableBackBufferAccess();

    if (pconupdate_) {
        D_UpdateRects(pconupdate_);
    }

    V_UpdatePalette();

    if (copyeverything_) {
        vrect.x = 0;
        vrect.y = 0;
        vrect.width = vid.width;
        vrect.height = vid.height;
        vrect.pnext = nullptr;
        VID_Update(&vrect);
    } else if (copytop_) {
        vrect.x = 0;
        vrect.y = 0;
        vrect.width = vid.width;
        vrect.height = vid.height - sb_lines;
        vrect.pnext = nullptr;
        VID_Update(&vrect);
    } else {
        vrect.x = vrect_.x;
        vrect.y = vrect_.y;
        vrect.width = vrect_.width;
        vrect.height = vrect_.height;
        vrect.pnext = nullptr;
        VID_Update(&vrect);
    }
}

} // namespace Screen
