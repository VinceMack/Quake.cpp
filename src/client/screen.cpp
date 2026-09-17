// screen.cpp -- 2D Screen Refresh, Layout Management, and Loading System Implementation
#include "quakedef.hpp"
#include "client/screen.hpp"
#include "ui/console.hpp"
#include "ui/menu.hpp"
#include "ui/hud.hpp"
#include "render/draw2d.hpp"

#include <cmath>

namespace Screen {

ScreenSystem& GetScreenSystem()
{
    static ScreenSystem instance;
    return instance;
}

void ScreenSystem::CenterPrint(std::string_view str)
{
    constexpr size_t kMaxCenterString = 1023;
    centerstring_.assign(str.data(), std::min(str.length(), kMaxCenterString));
    centertime_off_ = centertime_.value;
    centertime_start_ = static_cast<float>(Client::cl.time);
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
    if (erase_center_++ > Vid::vid.numpages) {
        erase_lines_ = 0;
        return;
    }
    if (center_lines_ <= 4) {
        y = static_cast<int>(Vid::vid.height * 0.35f);
    } else {
        y = 48;
    }
    copytop_ = 1;
    Draw::Draw_TileClear(0, y, Vid::vid.width, 8 * erase_lines_);
}

void ScreenSystem::DrawCenterString()
{
    int remaining = 0;
    if (Client::cl.intermission) {
        remaining = static_cast<int>(printspeed_.value * (Client::cl.time - centertime_start_));
    } else {
        remaining = 9999;
    }
    erase_center_ = 0;
    const char* start = centerstring_.c_str();
    int y = (center_lines_ <= 4) ? static_cast<int>(Vid::vid.height * 0.35f) : 48;
    do {
        int l = 0;
        for (l = 0; l < 40; l++) {
            if (start[l] == '\n' || start[l] == '\0') {
                break;
            }
        }
        int x = (Vid::vid.width - l * 8) / 2;
        for (int j = 0; j < l; j++, x += 8) {
            Draw::Draw_Character(x, y, start[j]);
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
        start++;
    } while (true);
}

void ScreenSystem::CheckDrawCenterString()
{
    copytop_ = 1;
    if (center_lines_ > erase_lines_) {
        erase_lines_ = center_lines_;
    }
    centertime_off_ -= static_cast<float>(Host::host_frametime);
    if (centertime_off_ <= 0.0f && !Client::cl.intermission) {
        return;
    }
    if (Keys::key_dest != Keys::key_game) {
        return;
    }
    DrawCenterString();
}

float ScreenSystem::CalcFov(float fov_x, float width, float height)
{
    if (fov_x < 1.0f || fov_x > 179.0f) {
        Common::Sys_Error("Bad fov: %f", fov_x);
    }
    float x = width / static_cast<float>(std::tan(fov_x / 360.0f * M_PI));
    float a = static_cast<float>(std::atan(height / x) * 360.0f / M_PI);
    return a;
}

void ScreenSystem::CalcRefdef()
{
    vrect_t vrect{};
    fullupdate_ = 0;
    Vid::vid.recalc_refdef = 0;
    Sbar::Sbar_Changed();
    if (viewsize_.value < 30.0f) {
        Cvar::Set("viewsize", "30");
    }
    if (viewsize_.value > 120.0f) {
        Cvar::Set("viewsize", "120");
    }
    if (fov_.value < 10.0f) {
        Cvar::Set("fov", "10");
    }
    if (fov_.value > 170.0f) {
        Cvar::Set("fov", "170");
    }
    Render::r_refdef.fov_x = fov_.value;
    Render::r_refdef.fov_y = CalcFov(Render::r_refdef.fov_x, static_cast<float>(Render::r_refdef.vrect.width), static_cast<float>(Render::r_refdef.vrect.height));
    float size = Client::cl.intermission ? 120.0f : viewsize_.value;
    if (size >= 120.0f) {
        sb_lines = 0;
    } else if (size >= 110.0f) {
        sb_lines = 24;
    } else {
        sb_lines = 24 + 16 + 8;
    }
    vrect.x = 0;
    vrect.y = 0;
    vrect.width = Vid::vid.width;
    vrect.height = Vid::vid.height;
    Render::R_SetVrect(&vrect, &vrect_, sb_lines);
    if (con_current_ > static_cast<float>(Vid::vid.height)) {
        con_current_ = static_cast<float>(Vid::vid.height);
    }
    Render::R_ViewChanged(&vrect, sb_lines, Vid::vid.aspect);
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
    Vid::vid.recalc_refdef = 1;
}

void ScreenSystem::SizeDown_f()
{
    auto& sys = GetScreenSystem();
    Cvar::SetValue("viewsize", sys.viewsize_.value - 10.0f);
    Vid::vid.recalc_refdef = 1;
}

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
    ram_pic_ = Draw::Draw_PicFromWad("ram");
    net_pic_ = Draw::Draw_PicFromWad("net");
    turtle_pic_ = Draw::Draw_PicFromWad("turtle");
    initialized_ = true;
}

void ScreenSystem::DrawRam()
{
    if (!showram_.value || !Render::r_cache_thrash) {
        return;
    }
    Draw::Draw_Pic(vrect_.x + 32, vrect_.y, ram_pic_);
}

void ScreenSystem::DrawTurtle()
{
    static int count = 0;
    if (!showturtle_.value) {
        return;
    }
    if (Host::host_frametime < 0.1) {
        count = 0;
        return;
    }
    count++;
    if (count < 3) {
        return;
    }
    Draw::Draw_Pic(vrect_.x, vrect_.y, turtle_pic_);
}

void ScreenSystem::DrawNet()
{
    if (Host::realtime - Client::cl.last_received_message < 0.3 || Client::cls.demoplayback) {
        return;
    }
    Draw::Draw_Pic(vrect_.x + 64, vrect_.y, net_pic_);
}

void ScreenSystem::DrawPause()
{
    if (!showpause_.value || !Client::cl.paused) {
        return;
    }
    qpic_t* pic = Draw::Draw_CachePic("gfx/pause.lmp");
    Draw::Draw_Pic((Vid::vid.width - pic->width) / 2, (Vid::vid.height - 48 - pic->height) / 2, pic);
}

void ScreenSystem::DrawLoading()
{
    if (!drawloading_) {
        return;
    }
    qpic_t* pic = Draw::Draw_CachePic("gfx/loading.lmp");
    Draw::Draw_Pic((Vid::vid.width - pic->width) / 2, (Vid::vid.height - 48 - pic->height) / 2, pic);
}

void ScreenSystem::SetUpToDrawConsole()
{
    Console::GetConsoleSystem().CheckResize();
    if (drawloading_) {
        return;
    }
    Console::GetConsoleSystem().SetForcedUp(!Client::cl.worldmodel || Client::cls.signon != SIGNONS);
    if (Console::GetConsoleSystem().IsForcedUp()) {
        conlines_ = static_cast<float>(Vid::vid.height);
        con_current_ = conlines_;
    } else if (Keys::key_dest == Keys::key_console) {
        conlines_ = static_cast<float>(Vid::vid.height / 2);
    } else {
        conlines_ = 0.0f;
    }
    if (conlines_ < con_current_) {
        con_current_ -= static_cast<float>(conspeed_.value * Host::host_frametime);
        if (conlines_ > con_current_) {
            con_current_ = conlines_;
        }
    } else if (conlines_ > con_current_) {
        con_current_ += static_cast<float>(conspeed_.value * Host::host_frametime);
        if (conlines_ < con_current_) {
            con_current_ = conlines_;
        }
    }
    if (clearconsole_++ < Vid::vid.numpages) {
        copytop_ = 1;
        Draw::Draw_TileClear(0, static_cast<int>(con_current_), Vid::vid.width,
            Vid::vid.height - static_cast<int>(con_current_));
        Sbar::Sbar_Changed();
    } else if (clearnotify_++ < Vid::vid.numpages) {
        copytop_ = 1;
        Draw::Draw_TileClear(0, 0, Vid::vid.width, Console::GetConsoleSystem().GetNotifyLines());
    } else {
        Console::GetConsoleSystem().SetNotifyLines(0);
    }
}

void ScreenSystem::DrawConsole()
{
    if (con_current_) {
        copyeverything_ = 1;
        Console::GetConsoleSystem().DrawConsole(static_cast<int>(con_current_), true);
        clearconsole_ = 0;
    } else {
        if (Keys::key_dest == Keys::key_game || Keys::key_dest == Keys::key_message) {
            Console::GetConsoleSystem().DrawNotify();
        }
    }
}

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
    std::vector<uint8_t> buffer;
    buffer.reserve(sizeof(pcx_header_t) + width * height * 2 + 1024);
    buffer.resize(sizeof(pcx_header_t));
    auto* pcx = reinterpret_cast<pcx_header_t*>(buffer.data());
    pcx->manufacturer = 0x0a;
    pcx->version = 5;
    pcx->encoding = 1;
    pcx->bits_per_pixel = 8;
    pcx->xmin = 0;
    pcx->ymin = 0;
    pcx->xmax = Common::LittleShort(static_cast<short>(width - 1));
    pcx->ymax = Common::LittleShort(static_cast<short>(height - 1));
    pcx->hres = Common::LittleShort(static_cast<short>(width));
    pcx->vres = Common::LittleShort(static_cast<short>(height));
    pcx->color_planes = 1;
    pcx->bytes_per_line = Common::LittleShort(static_cast<short>(width));
    pcx->palette_type = Common::LittleShort(2);
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
    Common::COM_WriteFile(filename, buffer.data(), static_cast<int>(buffer.size()));
}

void ScreenSystem::ScreenShot_f()
{
    int i = 0;
    std::string pcxname = "quake00.pcx";
    char checkname[MAX_OSPATH];
    for (i = 0; i <= 99; i++) {
        pcxname[5] = static_cast<char>(i / 10 + '0');
        pcxname[6] = static_cast<char>(i % 10 + '0');
        sprintf_s(checkname, sizeof(checkname), "%s/%s", Common::com_gamedir, pcxname.c_str());
        if (!Common::Sys_FileExists(checkname)) {
            break;
        }
    }
    if (i == 100) {
        Console::Con_Printf("SCR_ScreenShot_f: Couldn't create a PCX file\n");
        return;
    }
    WritePCXfile(pcxname.c_str(), Vid::vid.buffer, Vid::vid.width, Vid::vid.height, Vid::vid.rowbytes, Host::host_basepal);
    Console::Con_Printf("Wrote %s\n", pcxname.c_str());
}

void ScreenSystem::BeginLoadingPlaque()
{
    Audio::S_StopAllSounds(true);
    if (Client::cls.state != ca_connected || Client::cls.signon != SIGNONS) {
        return;
    }
    Console::GetConsoleSystem().ClearNotify();
    centertime_off_ = 0.0f;
    con_current_ = 0.0f;
    drawloading_ = true;
    fullupdate_ = 0;
    Sbar::Sbar_Changed();
    UpdateScreen();
    drawloading_ = false;
    disabled_for_loading_ = true;
    disabled_time_ = static_cast<float>(Host::realtime);
    fullupdate_ = 0;
}

void ScreenSystem::EndLoadingPlaque()
{
    disabled_for_loading_ = false;
    fullupdate_ = 0;
    Console::GetConsoleSystem().ClearNotify();
}

void ScreenSystem::DrawNotifyString()
{
    const char* start = notifystring_.data();
    if (!start) {
        return;
    }
    int y = static_cast<int>(Vid::vid.height * 0.35f);
    do {
        int l = 0;
        for (l = 0; l < 40; l++) {
            if (start[l] == '\n' || !start[l]) {
                break;
            }
        }
        int x = (Vid::vid.width - l * 8) / 2;
        for (int j = 0; j < l; j++, x += 8) {
            Draw::Draw_Character(x, y, start[j]);
        }
        y += 8;
        while (*start && *start != '\n') {
            start++;
        }
        if (!*start) {
            break;
        }
        start++;
    } while (true);
}

bool ScreenSystem::ModalMessage(std::string_view text)
{
    if (Client::cls.state == ca_dedicated) {
        return true;
    }
    notifystring_ = text;
    fullupdate_ = 0;
    drawdialog_ = true;
    UpdateScreen();
    drawdialog_ = false;
    Audio::S_ClearBuffer();
    do {
        Keys::key_count = -1;
        Common::Sys_SendKeyEvents();
    } while (Keys::key_lastpress != 'y' && Keys::key_lastpress != 'n' && Keys::key_lastpress != Keys::K_ESCAPE);
    fullupdate_ = 0;
    UpdateScreen();
    return Keys::key_lastpress == 'y';
}

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
        if (Host::realtime - disabled_time_ > 60) {
            disabled_for_loading_ = false;
            Console::Con_Printf("load failed.\n");
        } else {
            return;
        }
    }
    if (Client::cls.state == ca_dedicated || !initialized_ || !Console::GetConsoleSystem().IsInitialized()) {
        return;
    }
    if (viewsize_.value != oldscr_viewsize) {
        oldscr_viewsize = viewsize_.value;
        Vid::vid.recalc_refdef = 1;
    }
    if (oldfov_ != fov_.value) {
        oldfov_ = fov_.value;
        Vid::vid.recalc_refdef = true;
    }
    if (oldlcd_x != View::lcd_x.value) {
        oldlcd_x = View::lcd_x.value;
        Vid::vid.recalc_refdef = true;
    }
    if (oldscreensize_ != viewsize_.value) {
        oldscreensize_ = viewsize_.value;
        Vid::vid.recalc_refdef = true;
    }
    if (Vid::vid.recalc_refdef) {
        CalcRefdef();
    }
    if (fullupdate_++ < Vid::vid.numpages) {
        copyeverything_ = 1;
        Draw::Draw_TileClear(0, 0, Vid::vid.width, Vid::vid.height);
        Sbar::Sbar_Changed();
    }
    pconupdate_ = nullptr;
    SetUpToDrawConsole();
    EraseCenterString();
    View::V_RenderView();
    if (drawdialog_) {
        Sbar::Sbar_Draw();
        Draw::Draw_FadeScreen();
        DrawNotifyString();
        copyeverything_ = true;
    } else if (drawloading_) {
        DrawLoading();
        Sbar::Sbar_Draw();
    } else if (Client::cl.intermission == 1 && Keys::key_dest == Keys::key_game) {
        Sbar::Sbar_IntermissionOverlay();
    } else if (Client::cl.intermission == 2 && Keys::key_dest == Keys::key_game) {
        Sbar::Sbar_FinaleOverlay();
        CheckDrawCenterString();
    } else if (Client::cl.intermission == 3 && Keys::key_dest == Keys::key_game) {
        CheckDrawCenterString();
    } else {
        DrawRam();
        DrawNet();
        DrawTurtle();
        DrawPause();
        CheckDrawCenterString();
        Sbar::Sbar_Draw();
        DrawConsole();
        Menu::M_Draw();
    }
    if (pconupdate_) {
        Render::D_UpdateRects(pconupdate_);
    }
    View::V_UpdatePalette();
    if (copyeverything_) {
        vrect.x = 0;
        vrect.y = 0;
        vrect.width = Vid::vid.width;
        vrect.height = Vid::vid.height;
        vrect.pnext = nullptr;
        Vid::VID_Update(&vrect);
    } else if (copytop_) {
        vrect.x = 0;
        vrect.y = 0;
        vrect.width = Vid::vid.width;
        vrect.height = Vid::vid.height - sb_lines;
        vrect.pnext = nullptr;
        Vid::VID_Update(&vrect);
    } else {
        vrect.x = vrect_.x;
        vrect.y = vrect_.y;
        vrect.width = vrect_.width;
        vrect.height = vrect_.height;
        vrect.pnext = nullptr;
        Vid::VID_Update(&vrect);
    }
}

} // namespace Screen
