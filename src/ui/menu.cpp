// menu.cpp -- Quake in-game menu system & screens
#include "ui/menu.hpp"
#include "platform/crt_compat.hpp"
#include "core/wad.hpp"
#include "render/software/sw_vid.hpp"
#include "audio/audio_main.hpp"
#include "ui/screen.hpp"
#include "core/string_utils.hpp"
#include "core/cvar.hpp"
#include "render/draw2d.hpp"
#include "quakedef.hpp"
#include "network/socket.hpp"
#include "core/types.hpp"
#include "core/filesystem.hpp"
#include "core/cmd.hpp"
#include "world/model.hpp"
#include "render/render_types.hpp"
#include "client/view.hpp"
#include "ui/console.hpp"
#include "server/server_types.hpp"
#include "host/host.hpp"
#include "client/input.hpp"
#include "client/cl_demo.hpp"
#include "network/net_main.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <cstring>

// ============================================================================
// MENU SUBSYSTEM (Modernized & Table-Driven LoC Reduction)
// ============================================================================

namespace Menu {

struct CmdPair {
    const char* name;
    void (*fn)();
};

MenuState m_state = MenuState::None, m_return_state = MenuState::None;
bool m_return_onerror = false;
std::string m_return_reason;
bool m_entersound = false, m_recursiveDraw = false;

int m_multiplayer_cursor = 0, m_save_demonum = 0;
int m_main_cursor = 0, m_singleplayer_cursor = 0, load_cursor = 0;
int setup_cursor = 4, setup_oldtop = 0, setup_oldbottom = 0, setup_top = 0, setup_bottom = 0;
int options_cursor = 0, keys_cursor = 0;
bool bind_grab = false;
int help_page = 0;

int lanConfig_cursor = -1, lanConfig_port = 0;
std::string lanConfig_portname, lanConfig_joinname, setup_hostname, setup_myname;

int startepisode = 0, startlevel = 0, maxplayers = 0, gameoptions_cursor = 0;
bool m_serverInfoMessage = false;
double m_serverInfoMessageTime = 0.0;
bool searchComplete = false;
double searchCompleteTime = 0.0;
int slist_cursor = 0;
bool slist_sorted = false;

constexpr int MAX_SAVEGAMES = 12;
std::array<std::string, MAX_SAVEGAMES> m_filenames;
std::array<bool, MAX_SAVEGAMES> loadable;
std::array<byte, 256> identityTable { }, translationTable { };

inline bool StartingGame()
{
    return m_multiplayer_cursor == 1;
}
inline bool JoiningGame()
{
    return m_multiplayer_cursor == 0;
}

void M_ConfigureNetSubsystem();

inline void M_DrawCharacter(int cx, int line, int num)
{
    Draw::Draw_Character(cx + ((Vid::vid.width - 320) >> 1), line, num);
}
void M_Print(int cx, int cy, std::string_view str)
{
    for (char c : str) {
        M_DrawCharacter(cx, cy, static_cast<unsigned char>(c) + 128);
        cx += 8;
    }
}
void M_PrintWhite(int cx, int cy, std::string_view str)
{
    for (char c : str) {
        M_DrawCharacter(cx, cy, static_cast<unsigned char>(c));
        cx += 8;
    }
}
inline void M_DrawTransPic(int x, int y, qpic_t* pic)
{
    Draw::Draw_TransPic(x + ((Vid::vid.width - 320) >> 1), y, pic);
}
void M_DrawPic(int x, int y, qpic_t* pic)
{
    Draw::Draw_Pic(x + ((Vid::vid.width - 320) >> 1), y, pic);
}

void M_BuildTranslationTable(int top, int bottom)
{
    for (int j = 0; j < 256; j++) identityTable[j] = static_cast<byte>(j);
    translationTable = identityTable;
    if (top < 128)
        std::copy_n(identityTable.begin() + top, 16, translationTable.begin() + TOP_RANGE);
    else
        for (int j = 0; j < 16; j++) translationTable[TOP_RANGE + j] = identityTable[top + 15 - j];
    if (bottom < 128)
        std::copy_n(identityTable.begin() + bottom, 16, translationTable.begin() + BOTTOM_RANGE);
    else
        for (int j = 0; j < 16; j++) translationTable[BOTTOM_RANGE + j] = identityTable[bottom + 15 - j];
}

inline void M_DrawTransPicTranslate(int x, int y, qpic_t* pic)
{
    Draw::Draw_TransPicTranslate(x + ((Vid::vid.width - 320) >> 1), y, pic, translationTable.data());
}

inline void M_DrawTextBox(int x, int y, int width, int lines)
{
    qpic_t* p = Draw::Draw_CachePic("gfx/box_tl.lmp");
    int cx = x, cy = y;
    M_DrawTransPic(cx, cy, p);
    p = Draw::Draw_CachePic("gfx/box_ml.lmp");
    for (int n = 0; n < lines; n++) {
        cy += 8;
        M_DrawTransPic(cx, cy, p);
    }
    M_DrawTransPic(cx, cy + 8, Draw::Draw_CachePic("gfx/box_bl.lmp"));
    cx += 8;
    while (width > 0) {
        cy = y;
        M_DrawTransPic(cx, cy, Draw::Draw_CachePic("gfx/box_tm.lmp"));
        for (int n = 0; n < lines; n++) {
            cy += 8;
            M_DrawTransPic(cx, cy, Draw::Draw_CachePic("gfx/box_mm2.lmp"));
        }
        M_DrawTransPic(cx, cy + 8, Draw::Draw_CachePic("gfx/box_bm.lmp"));
        width -= 2;
        cx += 16;
    }
    cy = y;
    M_DrawTransPic(cx, cy, Draw::Draw_CachePic("gfx/box_tr.lmp"));
    p = Draw::Draw_CachePic("gfx/box_mr.lmp");
    for (int n = 0; n < lines; n++) {
        cy += 8;
        M_DrawTransPic(cx, cy, p);
    }
    M_DrawTransPic(cx, cy + 8, Draw::Draw_CachePic("gfx/box_br.lmp"));
}

static inline bool HandleNavKeys(int key, int& cursor, int max_items, const char* snd = "misc/menu1.wav")
{
    if (key == Keys::K_DOWNARROW || key == Keys::K_RIGHTARROW) {
        Audio::S_LocalSound(snd);
        cursor = (cursor + 1) % max_items;
        return true;
    }
    if (key == Keys::K_UPARROW || key == Keys::K_LEFTARROW) {
        Audio::S_LocalSound(snd);
        cursor = (cursor - 1 + max_items) % max_items;
        return true;
    }
    return false;
}

static inline void DrawMenuHeader(const char* title_pic)
{
    M_DrawTransPic(16, 4, Draw::Draw_CachePic("gfx/qplaque.lmp"));
    qpic_t* p = Draw::Draw_CachePic(title_pic);
    M_DrawPic((320 - p->width) / 2, 4, p);
}

static inline void DrawMenuDot(int x, int y, int cursor)
{
    M_DrawTransPic(
        x, y + cursor * 20, Draw::Draw_CachePic(Common::va("gfx/menudot%i.lmp", (int)(Host::host_time * 10) % 6 + 1)));
}

static inline void DrawLineCursor(int x, int y_start, int cursor, int step = 8)
{
    M_DrawCharacter(x, y_start + cursor * step, 12 + ((int)(Host::realtime * 4) & 1));
}

void M_Menu_Main_f();
void M_Menu_SinglePlayer_f();
void M_Menu_Load_f();
void M_Menu_Save_f();
void M_Menu_MultiPlayer_f();
void M_Menu_Setup_f();
void M_Menu_Options_f();
void M_Menu_Keys_f();
void M_Menu_Help_f();
void M_Menu_Quit_f();
void M_Menu_LanConfig_f();
void M_Menu_GameOptions_f();
void M_Menu_Search_f();
void M_Menu_ServerList_f();

void M_ToggleMenu_f()
{
    m_entersound = true;
    if (Keys::key_dest == Keys::key_menu) {
        if (m_state != MenuState::Main) {
            M_Menu_Main_f();
            return;
        }
        Keys::key_dest = Keys::key_game;
        m_state = MenuState::None;
        return;
    }
    if (Keys::key_dest == Keys::key_console)
        Console::ConsoleSystem::ToggleConsole_f();
    else
        M_Menu_Main_f();
}

void M_Menu_Main_f()
{
    if (Keys::key_dest != Keys::key_menu) {
        m_save_demonum = Client::cls.demonum;
        Client::cls.demonum = -1;
    }
    Keys::key_dest = Keys::key_menu;
    m_state = MenuState::Main;
    m_entersound = true;
}
void M_Main_Draw()
{
    DrawMenuHeader("gfx/ttl_main.lmp");
    M_DrawTransPic(72, 32, Draw::Draw_CachePic("gfx/mainmenu.lmp"));
    DrawMenuDot(54, 32, m_main_cursor);
}
void M_Main_Key(int key)
{
    if (key == Keys::K_ESCAPE) {
        Keys::key_dest = Keys::key_game;
        m_state = MenuState::None;
        Client::cls.demonum = m_save_demonum;
        if (Client::cls.demonum != -1 && !Client::cls.demoplayback && Client::cls.state != ca_connected)
            Client::CL_NextDemo();
        return;
    }
    if (HandleNavKeys(key, m_main_cursor, 5)) return;
    if (key == Keys::K_ENTER) {
        m_entersound = true;
        switch (m_main_cursor) {
        case 0:
            M_Menu_SinglePlayer_f();
            break;
        case 1:
            M_Menu_MultiPlayer_f();
            break;
        case 2:
            M_Menu_Options_f();
            break;
        case 3:
            M_Menu_Help_f();
            break;
        case 4:
            M_Menu_Quit_f();
            break;
        }
    }
}

void M_Menu_SinglePlayer_f()
{
    Keys::key_dest = Keys::key_menu;
    m_state = MenuState::SinglePlayer;
    m_entersound = true;
}
void M_SinglePlayer_Draw()
{
    DrawMenuHeader("gfx/ttl_sgl.lmp");
    M_DrawTransPic(72, 32, Draw::Draw_CachePic("gfx/sp_menu.lmp"));
    DrawMenuDot(54, 32, m_singleplayer_cursor);
}
void M_SinglePlayer_Key(int key)
{
    if (key == Keys::K_ESCAPE) {
        M_Menu_Main_f();
        return;
    }
    if (HandleNavKeys(key, m_singleplayer_cursor, 3)) return;
    if (key == Keys::K_ENTER) {
        m_entersound = true;
        switch (m_singleplayer_cursor) {
        case 0:
            if (Server::sv.active
                && !Screen::GetScreenSystem().ModalMessage("Are you sure you want to\nstart a new game?\n"))
                break;
            Keys::key_dest = Keys::key_game;
            if (Server::sv.active) Cmd::BufferAddText("disconnect\n");
            Cmd::BufferAddText("maxplayers 1\nmap start\n");
            break;
        case 1:
            M_Menu_Load_f();
            break;
        case 2:
            M_Menu_Save_f();
            break;
        }
    }
}

void M_ScanSaves()
{
    for (int i = 0; i < MAX_SAVEGAMES; i++) {
        m_filenames[i] = "--- UNUSED SLOT ---";
        loadable[i] = false;
        char name[MAX_OSPATH];
        sprintf_s(name, sizeof(name), "%s/s%i.sav", Common::com_gamedir, i);
        std::ifstream f(name);
        if (!f.is_open()) continue;
        int version = 0;
        if (!(f >> version)) continue;
        f.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::string temp_comment;
        if (!std::getline(f, temp_comment)) continue;
        std::string comment = temp_comment.c_str();
        if (!comment.empty() && comment.back() == '\r') comment.pop_back();
        for (char& c : comment) {
            if (c == '_') c = ' ';
        }
        if (comment.length() > SAVEGAME_COMMENT_LENGTH) comment = comment.substr(0, SAVEGAME_COMMENT_LENGTH);
        m_filenames[i] = comment;
        loadable[i] = true;
    }
}

void M_Menu_Load_f()
{
    m_entersound = true;
    m_state = MenuState::Load;
    Keys::key_dest = Keys::key_menu;
    M_ScanSaves();
}
void M_Menu_Save_f()
{
    if (!Server::sv.active || Client::cl.intermission || Server::svs.maxclients != 1) return;
    m_entersound = true;
    m_state = MenuState::Save;
    Keys::key_dest = Keys::key_menu;
    M_ScanSaves();
}

static inline void DrawSaveLoadCommon(const char* pic)
{
    qpic_t* p = Draw::Draw_CachePic(pic);
    M_DrawPic((320 - p->width) / 2, 4, p);
    for (int i = 0; i < MAX_SAVEGAMES; i++) M_Print(16, 32 + 8 * i, m_filenames[i]);
    DrawLineCursor(8, 32, load_cursor);
}

void M_Load_Draw()
{
    DrawSaveLoadCommon("gfx/p_load.lmp");
}
void M_Save_Draw()
{
    DrawSaveLoadCommon("gfx/p_save.lmp");
}

void M_Load_Key(int k)
{
    if (k == Keys::K_ESCAPE) {
        M_Menu_SinglePlayer_f();
        return;
    }
    if (HandleNavKeys(k, load_cursor, MAX_SAVEGAMES)) return;
    if (k == Keys::K_ENTER) {
        Audio::S_LocalSound("misc/menu2.wav");
        if (!loadable[load_cursor]) return;
        m_state = MenuState::None;
        Keys::key_dest = Keys::key_game;
        Screen::GetScreenSystem().BeginLoadingPlaque();
        Cmd::BufferAddText(Common::va("load s%i\n", load_cursor));
    }
}

void M_Save_Key(int k)
{
    if (k == Keys::K_ESCAPE) {
        M_Menu_SinglePlayer_f();
        return;
    }
    if (HandleNavKeys(k, load_cursor, MAX_SAVEGAMES)) return;
    if (k == Keys::K_ENTER) {
        m_state = MenuState::None;
        Keys::key_dest = Keys::key_game;
        Cmd::BufferAddText(Common::va("save s%i\n", load_cursor));
    }
}

void M_Menu_MultiPlayer_f()
{
    Keys::key_dest = Keys::key_menu;
    m_state = MenuState::MultiPlayer;
    m_entersound = true;
}
void M_MultiPlayer_Draw()
{
    DrawMenuHeader("gfx/p_multi.lmp");
    M_DrawTransPic(72, 32, Draw::Draw_CachePic("gfx/mp_menu.lmp"));
    DrawMenuDot(54, 32, m_multiplayer_cursor);
    if (!Net::tcpipAvailable) M_PrintWhite((320 - 27 * 8) / 2, 148, "No Communications Available");
}
void M_MultiPlayer_Key(int key)
{
    if (key == Keys::K_ESCAPE) {
        M_Menu_Main_f();
        return;
    }
    if (HandleNavKeys(key, m_multiplayer_cursor, 3)) return;
    if (key == Keys::K_ENTER) {
        m_entersound = true;
        if (m_multiplayer_cursor == 2)
            M_Menu_Setup_f();
        else if (Net::tcpipAvailable)
            M_Menu_LanConfig_f();
    }
}

constexpr auto setup_cursor_table = std::array { 40, 56, 80, 104, 140 };

void M_Menu_Setup_f()
{
    Keys::key_dest = Keys::key_menu;
    m_state = MenuState::Setup;
    m_entersound = true;
    setup_myname = Client::cl_name.string;
    setup_hostname = Net::hostname.string;
    setup_top = setup_oldtop = ((int)Client::cl_color.value) >> 4;
    setup_bottom = setup_oldbottom = ((int)Client::cl_color.value) & 15;
}

void M_Setup_Draw()
{
    DrawMenuHeader("gfx/p_multi.lmp");
    M_Print(64, 40, "Hostname");
    M_DrawTextBox(160, 32, 16, 1);
    M_Print(168, 40, setup_hostname.c_str());
    M_Print(64, 56, "Your name");
    M_DrawTextBox(160, 48, 16, 1);
    M_Print(168, 56, setup_myname.c_str());
    M_Print(64, 80, "Shirt color");
    M_Print(64, 104, "Pants color");
    M_DrawTextBox(64, 132, 14, 1);
    M_Print(72, 140, "Accept Changes");
    M_DrawTransPic(160, 64, Draw::Draw_CachePic("gfx/bigbox.lmp"));
    M_BuildTranslationTable(setup_top * 16, setup_bottom * 16);
    M_DrawTransPicTranslate(172, 72, Draw::Draw_CachePic("gfx/menuplyr.lmp"));
    M_DrawCharacter(56, setup_cursor_table[setup_cursor], 12 + ((int)(Host::realtime * 4) & 1));
    if (setup_cursor == 0)
        M_DrawCharacter(
            168 + 8 * (int)setup_hostname.length(), setup_cursor_table[0], 10 + ((int)(Host::realtime * 4) & 1));
    if (setup_cursor == 1)
        M_DrawCharacter(
            168 + 8 * (int)setup_myname.length(), setup_cursor_table[1], 10 + ((int)(Host::realtime * 4) & 1));
}

void M_Setup_Key(int k)
{
    if (k == Keys::K_ESCAPE) {
        M_Menu_MultiPlayer_f();
        return;
    }
    if (k == Keys::K_UPARROW) {
        Audio::S_LocalSound("misc/menu1.wav");
        setup_cursor = (setup_cursor - 1 + 5) % 5;
        return;
    }
    if (k == Keys::K_DOWNARROW) {
        Audio::S_LocalSound("misc/menu1.wav");
        setup_cursor = (setup_cursor + 1) % 5;
        return;
    }
    auto AdjColor = [](int dir, int& color) {
        Audio::S_LocalSound("misc/menu3.wav");
        color = (color + dir + 14) % 14;
    };
    if (k == Keys::K_LEFTARROW) {
        if (setup_cursor == 2) AdjColor(-1, setup_top);
        if (setup_cursor == 3) AdjColor(-1, setup_bottom);
        return;
    }
    if (k == Keys::K_RIGHTARROW) {
        if (setup_cursor == 2) AdjColor(1, setup_top);
        if (setup_cursor == 3) AdjColor(1, setup_bottom);
        return;
    }
    if (k == Keys::K_ENTER) {
        if (setup_cursor == 2)
            AdjColor(1, setup_top);
        else if (setup_cursor == 3)
            AdjColor(1, setup_bottom);
        else if (setup_cursor == 4) {
            if (Client::cl_name.string != setup_myname)
                Cmd::BufferAddText(Common::va("name \"%s\"\n", setup_myname.c_str()));
            if (Net::hostname.string != setup_hostname) Cvar::Set("hostname", setup_hostname.c_str());
            if (setup_top != setup_oldtop || setup_bottom != setup_oldbottom)
                Cmd::BufferAddText(Common::va("color %i %i\n", setup_top, setup_bottom));
            m_entersound = true;
            M_Menu_MultiPlayer_f();
        }
        return;
    }
    if (k == Keys::K_BACKSPACE) {
        if (setup_cursor == 0 && !setup_hostname.empty()) setup_hostname.pop_back();
        if (setup_cursor == 1 && !setup_myname.empty()) setup_myname.pop_back();
        return;
    }
    if (k >= 32 && k <= 127) {
        if (setup_cursor == 0 && setup_hostname.length() < 15) setup_hostname.push_back(static_cast<char>(k));
        if (setup_cursor == 1 && setup_myname.length() < 15) setup_myname.push_back(static_cast<char>(k));
    }
}

constexpr int OPTIONS_ITEMS = 12, SLIDER_RANGE = 10;
void M_Menu_Options_f()
{
    Keys::key_dest = Keys::key_menu;
    m_state = MenuState::Options;
    m_entersound = true;
}

void M_AdjustSliders(int dir)
{
    Audio::S_LocalSound("misc/menu3.wav");
    switch (options_cursor) {
    case 3:
        Cvar::SetValue(
            "viewsize", std::clamp<float>(Screen::GetScreenSystem().GetViewsize().value + dir * 10, 30.0f, 120.0f));
        break;
    case 4:
        Cvar::SetValue("gamma", std::clamp<float>(View::v_gamma.value - static_cast<float>(dir * 0.05), 0.5f, 1.0f));
        break;
    case 5:
        Cvar::SetValue(
            "sensitivity", std::clamp<float>(Client::sensitivity.value + static_cast<float>(dir * 0.5), 1.0f, 11.0f));
        break;
    case 6:
        Cvar::SetValue(
            "bgmvolume", std::clamp<float>(Audio::bgmvolume.value + static_cast<float>(dir * 0.1), 0.0f, 1.0f));
        break;
    case 7:
        Cvar::SetValue("volume", std::clamp<float>(Audio::volume.value + static_cast<float>(dir * 0.1), 0.0f, 1.0f));
        break;
    case 8:
        Cvar::SetValue("cl_forwardspeed", (Client::cl_forwardspeed.value > 200) ? 200.0f : 400.0f);
        Cvar::SetValue("cl_backspeed", (Client::cl_forwardspeed.value > 200) ? 200.0f : 400.0f);
        break;
    case 9:
        Cvar::SetValue("m_pitch", -Client::m_pitch.value);
        break;
    case 10:
        Cvar::SetValue("lookspring", static_cast<float>(!Client::lookspring.value));
        break;
    case 11:
        Cvar::SetValue("lookstrafe", static_cast<float>(!Client::lookstrafe.value));
        break;
    }
}

inline void M_DrawSlider(int x, int y, float range)
{
    range = std::clamp(range, 0.0f, 1.0f);
    M_DrawCharacter(x - 8, y, 128);
    for (int i = 0; i < SLIDER_RANGE; i++) M_DrawCharacter(x + i * 8, y, 129);
    M_DrawCharacter(x + SLIDER_RANGE * 8, y, 130);
    M_DrawCharacter(x + static_cast<int>((SLIDER_RANGE - 1) * 8 * range), y, 131);
}
inline void M_DrawCheckbox(int x, int y, int on)
{
    M_Print(x, y, on ? "on" : "off");
}

void M_Options_Draw()
{
    DrawMenuHeader("gfx/p_option.lmp");
    M_Print(16, 32, "    Customize controls");
    M_Print(16, 40, "         Go to console");
    M_Print(16, 48, "     Reset to defaults");
    M_Print(16, 56, "           Screen size");
    M_DrawSlider(220, 56, (Screen::GetScreenSystem().GetViewsize().value - 30) / (120 - 30));
    M_Print(16, 64, "            Brightness");
    M_DrawSlider(220, 64, static_cast<float>((1.0 - View::v_gamma.value) / 0.5));
    M_Print(16, 72, "           Mouse Speed");
    M_DrawSlider(220, 72, (Client::sensitivity.value - 1) / 10);
    M_Print(16, 80, "       CD Music Volume");
    M_DrawSlider(220, 80, Audio::bgmvolume.value);
    M_Print(16, 88, "          Sound Volume");
    M_DrawSlider(220, 88, Audio::volume.value);
    M_Print(16, 96, "            Always Run");
    M_DrawCheckbox(220, 96, Client::cl_forwardspeed.value > 200);
    M_Print(16, 104, "          Invert Mouse");
    M_DrawCheckbox(220, 104, Client::m_pitch.value < 0);
    M_Print(16, 112, "            Lookspring");
    M_DrawCheckbox(220, 112, static_cast<int>(Client::lookspring.value));
    M_Print(16, 120, "            Lookstrafe");
    M_DrawCheckbox(220, 120, static_cast<int>(Client::lookstrafe.value));
    DrawLineCursor(200, 32, options_cursor);
}

void M_Options_Key(int k)
{
    if (k == Keys::K_ESCAPE) {
        M_Menu_Main_f();
        return;
    }
    if (k == Keys::K_ENTER) {
        m_entersound = true;
        switch (options_cursor) {
        case 0:
            M_Menu_Keys_f();
            break;
        case 1:
            m_state = MenuState::None;
            Console::ConsoleSystem::ToggleConsole_f();
            break;
        case 2:
            Cmd::BufferAddText("exec default.cfg\n");
            break;
        default:
            M_AdjustSliders(1);
            break;
        }
        return;
    }
    if (k == Keys::K_UPARROW) {
        Audio::S_LocalSound("misc/menu1.wav");
        options_cursor = (options_cursor - 1 + OPTIONS_ITEMS) % OPTIONS_ITEMS;
    }
    if (k == Keys::K_DOWNARROW) {
        Audio::S_LocalSound("misc/menu1.wav");
        options_cursor = (options_cursor + 1) % OPTIONS_ITEMS;
    }
    if (k == Keys::K_LEFTARROW) M_AdjustSliders(-1);
    if (k == Keys::K_RIGHTARROW) M_AdjustSliders(1);
}

struct BindName {
    std::string_view command;
    std::string_view description;
};
constexpr auto bindnames = std::array<BindName, 18> { { { "+attack", "attack" }, { "impulse 10", "change weapon" },
    { "+jump", "jump / swim up" }, { "+forward", "walk forward" }, { "+back", "backpedal" }, { "+left", "turn left" },
    { "+right", "turn right" }, { "+speed", "run" }, { "+moveleft", "step left" }, { "+moveright", "step right" },
    { "+strafe", "sidestep" }, { "+lookup", "look up" }, { "+lookdown", "look down" }, { "centerview", "center view" },
    { "+mlook", "mouse look" }, { "+klook", "keyboard look" }, { "+moveup", "swim up" },
    { "+movedown", "swim down" } } };

void M_Menu_Keys_f()
{
    Keys::key_dest = Keys::key_menu;
    m_state = MenuState::Keys;
    m_entersound = true;
}

void M_FindKeysForCommand(std::string_view command, std::array<int, 2>& twokeys)
{
    twokeys[0] = twokeys[1] = -1;
    int count = 0;
    for (int j = 0; j < 256; j++) {
        if (!Keys::keybindings[j].empty() && Keys::keybindings[j] == command) {
            twokeys[count++] = j;
            if (count == 2) break;
        }
    }
}

void M_UnbindCommand(std::string_view command)
{
    for (int j = 0; j < 256; j++) {
        if (!Keys::keybindings[j].empty() && Keys::keybindings[j] == command) Keys::Key_SetBinding(j, "");
    }
}

void M_Keys_Draw()
{
    qpic_t* p = Draw::Draw_CachePic("gfx/ttl_cstm.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);
    M_Print(bind_grab ? 12 : 18, 32,
        bind_grab ? "Press a key or button for this action" : "Enter to change, backspace to clear");
    std::array<int, 2> keys;
    for (size_t i = 0; i < bindnames.size(); i++) {
        int y = 48 + 8 * static_cast<int>(i);
        M_Print(16, y, bindnames[i].description);
        M_FindKeysForCommand(bindnames[i].command, keys);
        if (keys[0] == -1)
            M_Print(140, y, "???");
        else {
            const char* name = Keys::Key_KeynumToString(keys[0]);
            M_Print(140, y, name);
            if (keys[1] != -1) {
                int x = static_cast<int>(strlen(name)) * 8;
                M_Print(140 + x + 8, y, "or");
                M_Print(140 + x + 32, y, Keys::Key_KeynumToString(keys[1]));
            }
        }
    }
    if (bind_grab)
        M_DrawCharacter(130, 48 + keys_cursor * 8, '=');
    else
        DrawLineCursor(130, 48, keys_cursor);
}

void M_Keys_Key(int k)
{
    std::array<int, 2> keys;
    if (bind_grab) {
        Audio::S_LocalSound("misc/menu1.wav");
        if (k != Keys::K_ESCAPE && k != '`')
            Cmd::BufferInsertText(Common::va(
                "bind \"%s\" \"%.70s\"\n", Keys::Key_KeynumToString(k), bindnames[keys_cursor].command.data()));
        bind_grab = false;
        return;
    }
    if (k == Keys::K_ESCAPE) {
        M_Menu_Options_f();
        return;
    }
    if (HandleNavKeys(k, keys_cursor, static_cast<int>(bindnames.size()))) return;
    if (k == Keys::K_ENTER) {
        M_FindKeysForCommand(bindnames[keys_cursor].command, keys);
        Audio::S_LocalSound("misc/menu2.wav");
        if (keys[1] != -1) M_UnbindCommand(bindnames[keys_cursor].command);
        bind_grab = true;
    } else if (k == Keys::K_BACKSPACE || k == Keys::K_DEL) {
        Audio::S_LocalSound("misc/menu2.wav");
        M_UnbindCommand(bindnames[keys_cursor].command);
    }
}

constexpr int NUM_HELP_PAGES = 6;
void M_Menu_Help_f()
{
    Keys::key_dest = Keys::key_menu;
    m_state = MenuState::Help;
    m_entersound = true;
    help_page = 0;
}
void M_Help_Draw()
{
    M_DrawPic(0, 0, Draw::Draw_CachePic(Common::va("gfx/help%i.lmp", help_page)));
}
void M_Help_Key(int key)
{
    if (key == Keys::K_ESCAPE) {
        M_Menu_Main_f();
        return;
    }
    if (key == Keys::K_UPARROW || key == Keys::K_RIGHTARROW) {
        m_entersound = true;
        help_page = (help_page + 1) % NUM_HELP_PAGES;
    }
    if (key == Keys::K_DOWNARROW || key == Keys::K_LEFTARROW) {
        m_entersound = true;
        help_page = (help_page - 1 + NUM_HELP_PAGES) % NUM_HELP_PAGES;
    }
}

void M_Menu_Quit_f()
{
    Keys::key_dest = Keys::key_console;
    Host::Host_Quit_f();
}

constexpr auto lanConfig_cursor_table = std::array { 72, 92, 124 };
constexpr int NUM_LANCONFIG_CMDS = 3;

void M_Menu_LanConfig_f()
{
    Keys::key_dest = Keys::key_menu;
    m_state = MenuState::LanConfig;
    m_entersound = true;
    if (lanConfig_cursor == -1) lanConfig_cursor = JoiningGame() ? 2 : 1;
    if (StartingGame() && lanConfig_cursor == 2) lanConfig_cursor = 1;
    lanConfig_port = Net::DEFAULTnet_hostport;
    lanConfig_portname = std::to_string(lanConfig_port);
    m_return_onerror = false;
    m_return_reason.clear();
}

void M_LanConfig_Draw()
{
    DrawMenuHeader("gfx/p_multi.lmp");
    int basex = (320 - Draw::Draw_CachePic("gfx/p_multi.lmp")->width) / 2;
    M_Print(basex, 32, Common::va("%s - TCP/IP", StartingGame() ? "New Game" : "Join Game"));
    basex += 8;
    M_Print(basex, 52, "Address:");
    M_Print(basex + 9 * 8, 52, Net::my_tcpip_address);
    M_Print(basex, lanConfig_cursor_table[0], "Port");
    M_DrawTextBox(basex + 8 * 8, lanConfig_cursor_table[0] - 8, 6, 1);
    M_Print(basex + 9 * 8, lanConfig_cursor_table[0], lanConfig_portname);
    if (JoiningGame()) {
        M_Print(basex, lanConfig_cursor_table[1], "Search for local games...");
        M_Print(basex, 108, "Join game at:");
        M_DrawTextBox(basex + 8, lanConfig_cursor_table[2] - 8, 22, 1);
        M_Print(basex + 16, lanConfig_cursor_table[2], lanConfig_joinname);
    } else {
        M_DrawTextBox(basex, lanConfig_cursor_table[1] - 8, 2, 1);
        M_Print(basex + 8, lanConfig_cursor_table[1], "OK");
    }
    M_DrawCharacter(basex - 8, lanConfig_cursor_table[lanConfig_cursor], 12 + ((int)(Host::realtime * 4) & 1));
    if (lanConfig_cursor == 0)
        M_DrawCharacter(basex + 9 * 8 + 8 * (int)lanConfig_portname.length(), lanConfig_cursor_table[0],
            10 + ((int)(Host::realtime * 4) & 1));
    if (lanConfig_cursor == 2)
        M_DrawCharacter(basex + 16 + 8 * (int)lanConfig_joinname.length(), lanConfig_cursor_table[2],
            10 + ((int)(Host::realtime * 4) & 1));
    if (!m_return_reason.empty()) M_PrintWhite(basex, 148, m_return_reason.c_str());
}

void M_LanConfig_Key(int key)
{
    if (key == Keys::K_ESCAPE) {
        M_Menu_MultiPlayer_f();
        return;
    }
    if (key == Keys::K_UPARROW) {
        Audio::S_LocalSound("misc/menu1.wav");
        lanConfig_cursor = (lanConfig_cursor - 1 + NUM_LANCONFIG_CMDS) % NUM_LANCONFIG_CMDS;
    }
    if (key == Keys::K_DOWNARROW) {
        Audio::S_LocalSound("misc/menu1.wav");
        lanConfig_cursor = (lanConfig_cursor + 1) % NUM_LANCONFIG_CMDS;
    }
    if (key == Keys::K_ENTER) {
        if (lanConfig_cursor == 0) return;
        m_entersound = true;
        M_ConfigureNetSubsystem();
        if (lanConfig_cursor == 1) {
            if (StartingGame())
                M_Menu_GameOptions_f();
            else
                M_Menu_Search_f();
            return;
        }
        if (lanConfig_cursor == 2) {
            m_return_state = m_state;
            m_return_onerror = true;
            Keys::key_dest = Keys::key_game;
            m_state = MenuState::None;
            Cmd::BufferAddText(Common::va("connect \"%s\"\n", lanConfig_joinname.c_str()));
            return;
        }
    }
    if (key == Keys::K_BACKSPACE) {
        if (lanConfig_cursor == 0 && !lanConfig_portname.empty()) lanConfig_portname.pop_back();
        if (lanConfig_cursor == 2 && !lanConfig_joinname.empty()) lanConfig_joinname.pop_back();
    }
    if (key >= 32 && key <= 127) {
        if (lanConfig_cursor == 2 && lanConfig_joinname.length() < 21)
            lanConfig_joinname.push_back(static_cast<char>(key));
        if (key >= '0' && key <= '9' && lanConfig_cursor == 0 && lanConfig_portname.length() < 5)
            lanConfig_portname.push_back(static_cast<char>(key));
    }
    if (StartingGame() && lanConfig_cursor == 2) lanConfig_cursor = (key == Keys::K_UPARROW) ? 1 : 0;
    int l = Common::Q_atoi(lanConfig_portname.c_str());
    if (l <= 65535) lanConfig_port = l;
    lanConfig_portname = std::to_string(lanConfig_port).c_str();
}

struct level_t {
    const char* name;
    const char* description;
};
constexpr auto levels = std::array<level_t, 38> { { { "start", "Entrance" }, { "e1m1", "Slipgate Complex" },
    { "e1m2", "Castle of the Damned" }, { "e1m3", "The Necropolis" }, { "e1m4", "The Grisly Grotto" },
    { "e1m5", "Gloom Keep" }, { "e1m6", "The Door To Chthon" }, { "e1m7", "The House of Chthon" },
    { "e1m8", "Ziggurat Vertigo" }, { "e2m1", "The Installation" }, { "e2m2", "Ogre Citadel" },
    { "e2m3", "Crypt of Decay" }, { "e2m4", "The Ebon Fortress" }, { "e2m5", "The Wizard's Manse" },
    { "e2m6", "The Dismal Oubliette" }, { "e2m7", "Underearth" }, { "e3m1", "Termination Central" },
    { "e3m2", "The Vaults of Zin" }, { "e3m3", "The Tomb of Terror" }, { "e3m4", "Satan's Dark Delight" },
    { "e3m5", "Wind Tunnels" }, { "e3m6", "Chambers of Torment" }, { "e3m7", "The Haunted Halls" },
    { "e4m1", "The Sewage System" }, { "e4m2", "The Tower of Despair" }, { "e4m3", "The Elder God Shrine" },
    { "e4m4", "The Palace of Hate" }, { "e4m5", "Hell's Atrium" }, { "e4m6", "The Pain Maze" },
    { "e4m7", "Azure Agony" }, { "e4m8", "The Nameless City" }, { "end", "Shub-Niggurath's Pit" },
    { "dm1", "Place of Two Deaths" }, { "dm2", "Claustrophobopolis" }, { "dm3", "The Abandoned Base" },
    { "dm4", "The Bad Place" }, { "dm5", "The Cistern" }, { "dm6", "The Dark Zone" } } };

constexpr auto hipnoticlevels = std::array<level_t, 18> { { { "start", "Command HQ" },
    { "hip1m1", "The Pumping Station" }, { "hip1m2", "Storage Facility" }, { "hip1m3", "The Lost Mine" },
    { "hip1m4", "Research Facility" }, { "hip1m5", "Military Complex" }, { "hip2m1", "Ancient Realms" },
    { "hip2m2", "The Black Cathedral" }, { "hip2m3", "The Catacombs" }, { "hip2m4", "The Crypt" },
    { "hip2m5", "Mortum's Keep" }, { "hip2m6", "The Gremlin's Domain" }, { "hip3m1", "Tur Torment" },
    { "hip3m2", "Pandemonium" }, { "hip3m3", "Limbo" }, { "hip3m4", "The Gauntlet" }, { "hipend", "Armagon's Lair" },
    { "hipdm1", "The Edge of Oblivion" } } };

constexpr auto roguelevels = std::array<level_t, 17> { { { "start", "Split Decision" }, { "r1m1", "Deviant's Domain" },
    { "r1m2", "Dread Portal" }, { "r1m3", "Judgement Call" }, { "r1m4", "Cave of Death" },
    { "r1m5", "Towers of Wrath" }, { "r1m6", "Temple of Pain" }, { "r1m7", "Tomb of the Overlord" },
    { "r2m1", "Tempus Fugit" }, { "r2m2", "Elemental Fury I" }, { "r2m3", "Elemental Fury II" },
    { "r2m4", "Curse of Osiris" }, { "r2m5", "Wizard's Keep" }, { "r2m6", "Blood Sacrifice" },
    { "r2m7", "Last Bastion" }, { "r2m8", "Source of Evil" }, { "ctf1", "Division of Change" } } };

struct episode_t {
    const char* description;
    int firstLevel;
    int levels;
};
constexpr auto episodes = std::array<episode_t, 7> { { { "Welcome to Quake", 0, 1 }, { "Doomed Dimension", 1, 8 },
    { "Realm of Black Magic", 9, 7 }, { "Netherworld", 16, 7 }, { "The Elder World", 23, 8 }, { "Final Level", 31, 1 },
    { "Deathmatch Arena", 32, 6 } } };

constexpr auto hipnoticepisodes = std::array<episode_t, 6> { { { "Scourge of Armagon", 0, 1 },
    { "Fortress of the Dead", 1, 5 }, { "Dominion of Darkness", 6, 6 }, { "The Rift", 12, 4 }, { "Final Level", 16, 1 },
    { "Deathmatch Arena", 17, 1 } } };

constexpr auto rogueepisodes = std::array<episode_t, 4> { { { "Introduction", 0, 1 }, { "Hell's Fortress", 1, 7 },
    { "Corridors of Time", 8, 8 }, { "Deathmatch Arena", 16, 1 } } };

constexpr auto gameoptions_cursor_table = std::array { 40, 56, 64, 72, 80, 88, 96, 112, 120 };
constexpr int NUM_GAMEOPTIONS = 9;

void M_Menu_GameOptions_f()
{
    Keys::key_dest = Keys::key_menu;
    m_state = MenuState::GameOptions;
    m_entersound = true;
    if (maxplayers == 0) maxplayers = Server::svs.maxclients;
    if (maxplayers < 2) maxplayers = Server::svs.maxclientslimit;
}

void M_GameOptions_Draw()
{
    DrawMenuHeader("gfx/p_multi.lmp");
    M_DrawTextBox(152, 32, 10, 1);
    M_Print(160, 40, "begin game");
    M_Print(0, 56, "      Max players");
    M_Print(160, 56, Common::va("%i", maxplayers));
    M_Print(0, 64, "        Game Type");
    M_Print(160, 64, Server::coop.value ? "Cooperative" : "Deathmatch");
    M_Print(0, 72, "        Teamplay");
    const char* team_msg = "Off";
    if (Common::rogue) {
        constexpr auto rmsgs = std::array { "Off", "No Friendly Fire", "Friendly Fire", "Tag", "Capture the Flag",
            "One Flag CTF", "Three Team CTF" };
        int idx = static_cast<int>(Server::teamplay.value);
        if (idx >= 1 && idx <= 6) team_msg = rmsgs[idx];
    } else {
        if ((int)Server::teamplay.value == 1)
            team_msg = "No Friendly Fire";
        else if ((int)Server::teamplay.value == 2)
            team_msg = "Friendly Fire";
    }
    M_Print(160, 72, team_msg);
    M_Print(0, 80, "            Skill");
    constexpr auto skills
        = std::array { "Easy difficulty", "Normal difficulty", "Hard difficulty", "Nightmare difficulty" };
    M_Print(160, 80, skills[std::clamp<int>(static_cast<int>(Server::skill.value), 0, 3)]);
    M_Print(0, 88, "       Frag Limit");
    M_Print(160, 88, (Server::fraglimit.value == 0) ? "none" : Common::va("%i frags", (int)Server::fraglimit.value));
    M_Print(0, 96, "       Time Limit");
    M_Print(160, 96, (Server::timelimit.value == 0) ? "none" : Common::va("%i minutes", (int)Server::timelimit.value));
    M_Print(0, 112, "         Episode");
    const episode_t* ep_ptr = episodes.data();
    const level_t* lvl_ptr = levels.data();
    if (Common::hipnotic) {
        ep_ptr = hipnoticepisodes.data();
        lvl_ptr = hipnoticlevels.data();
    } else if (Common::rogue) {
        ep_ptr = rogueepisodes.data();
        lvl_ptr = roguelevels.data();
    }
    M_Print(160, 112, ep_ptr[startepisode].description);
    M_Print(0, 120, "           Level");
    const auto& cur_lvl = lvl_ptr[ep_ptr[startepisode].firstLevel + startlevel];
    M_Print(160, 120, cur_lvl.description);
    M_Print(160, 128, cur_lvl.name);
    M_DrawCharacter(144, gameoptions_cursor_table[gameoptions_cursor], 12 + ((int)(Host::realtime * 4) & 1));
    if (m_serverInfoMessage) {
        if ((Host::realtime - m_serverInfoMessageTime) < 5.0) {
            int x = (320 - 26 * 8) / 2;
            M_DrawTextBox(x, 138, 24, 4);
            x += 8;
            M_Print(x, 146, "  More than 4 players   ");
            M_Print(x, 154, " requires using command ");
            M_Print(x, 162, "line parameters; please ");
            M_Print(x, 170, "   see techinfo.txt.    ");
        } else
            m_serverInfoMessage = false;
    }
}

void M_NetStart_Change(int dir)
{
    switch (gameoptions_cursor) {
    case 1:
        maxplayers += dir;
        if (maxplayers > Server::svs.maxclientslimit) {
            maxplayers = Server::svs.maxclientslimit;
            m_serverInfoMessage = true;
            m_serverInfoMessageTime = Host::realtime;
        }
        if (maxplayers < 2) maxplayers = 2;
        break;
    case 2:
        Cvar::SetValue("coop", static_cast<float>(Server::coop.value ? 0 : 1));
        break;
    case 3: {
        int count = Common::rogue ? 6 : 2;
        float new_val = Server::teamplay.value + dir;
        if (new_val > count)
            new_val = 0;
        else if (new_val < 0)
            new_val = static_cast<float>(count);
        Cvar::SetValue("teamplay", new_val);
        break;
    }
    case 4: {
        float new_sk = Server::skill.value + dir;
        if (new_sk > 3)
            new_sk = 0;
        else if (new_sk < 0)
            new_sk = 3;
        Cvar::SetValue("skill", new_sk);
        break;
    }
    case 5: {
        float new_fl = Server::fraglimit.value + dir * 10;
        if (new_fl > 100)
            new_fl = 0;
        else if (new_fl < 0)
            new_fl = 100;
        Cvar::SetValue("fraglimit", new_fl);
        break;
    }
    case 6: {
        float new_tl = Server::timelimit.value + dir * 5;
        if (new_tl > 60)
            new_tl = 0;
        else if (new_tl < 0)
            new_tl = 60;
        Cvar::SetValue("timelimit", new_tl);
        break;
    }
    case 7: {
        int count = Common::hipnotic ? 6 : (Common::rogue ? 4 : (registered.value ? 7 : 2));
        startepisode = (startepisode + dir + count) % count;
        startlevel = 0;
        break;
    }
    case 8: {
        int count = Common::hipnotic
            ? hipnoticepisodes[startepisode].levels
            : (Common::rogue ? rogueepisodes[startepisode].levels : episodes[startepisode].levels);
        startlevel = (startlevel + dir + count) % count;
        break;
    }
    }
}

void M_GameOptions_Key(int key)
{
    if (key == Keys::K_ESCAPE) {
        M_Menu_LanConfig_f();
        return;
    }
    if (key == Keys::K_UPARROW) {
        Audio::S_LocalSound("misc/menu1.wav");
        gameoptions_cursor = (gameoptions_cursor - 1 + NUM_GAMEOPTIONS) % NUM_GAMEOPTIONS;
    }
    if (key == Keys::K_DOWNARROW) {
        Audio::S_LocalSound("misc/menu1.wav");
        gameoptions_cursor = (gameoptions_cursor + 1) % NUM_GAMEOPTIONS;
    }
    if (key == Keys::K_LEFTARROW && gameoptions_cursor > 0) {
        Audio::S_LocalSound("misc/menu3.wav");
        M_NetStart_Change(-1);
    }
    if (key == Keys::K_RIGHTARROW && gameoptions_cursor > 0) {
        Audio::S_LocalSound("misc/menu3.wav");
        M_NetStart_Change(1);
    }
    if (key == Keys::K_ENTER) {
        Audio::S_LocalSound("misc/menu2.wav");
        if (gameoptions_cursor == 0) {
            if (Server::sv.active) Cmd::BufferAddText("disconnect\n");
            Cmd::BufferAddText("listen 0\n");
            Cmd::BufferAddText(Common::va("maxplayers %u\n", maxplayers));
            Screen::GetScreenSystem().BeginLoadingPlaque();
            const episode_t* ep_ptr = episodes.data();
            const level_t* lvl_ptr = levels.data();
            if (Common::hipnotic) {
                ep_ptr = hipnoticepisodes.data();
                lvl_ptr = hipnoticlevels.data();
            } else if (Common::rogue) {
                ep_ptr = rogueepisodes.data();
                lvl_ptr = roguelevels.data();
            }
            Cmd::BufferAddText(Common::va("map %s\n", lvl_ptr[ep_ptr[startepisode].firstLevel + startlevel].name));
            return;
        }
        M_NetStart_Change(1);
    }
}

void M_Menu_Search_f()
{
    Keys::key_dest = Keys::key_menu;
    m_state = MenuState::Search;
    m_entersound = false;
    Net::slistSilent = true;
    Net::slistLocal = false;
    searchComplete = false;
    Net::NET_Slist_f();
}
void M_Search_Draw()
{
    qpic_t* p = Draw::Draw_CachePic("gfx/p_multi.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);
    int x = (320 / 2) - 44;
    M_DrawTextBox(x - 8, 32, 12, 1);
    M_Print(x, 40, "Searching...");
    if (Net::slistInProgress) {
        Net::NET_Poll();
        return;
    }
    if (!searchComplete) {
        searchComplete = true;
        searchCompleteTime = Host::realtime;
    }
    if (Net::hostCacheCount) {
        M_Menu_ServerList_f();
        return;
    }
    M_PrintWhite((320 - 22 * 8) / 2, 64, "No Quake servers found");
    if ((Host::realtime - searchCompleteTime) >= 3.0) M_Menu_LanConfig_f();
}
void M_Search_Key() { }

void M_Menu_ServerList_f()
{
    Keys::key_dest = Keys::key_menu;
    m_state = MenuState::SList;
    m_entersound = true;
    slist_cursor = 0;
    m_return_onerror = false;
    m_return_reason.clear();
    slist_sorted = false;
}
void M_ServerList_Draw()
{
    if (!slist_sorted && Net::hostCacheCount > 1) {
        std::sort(Net::hostcache.begin(), Net::hostcache.begin() + Net::hostCacheCount,
            [](const Net::hostcache_t& a, const Net::hostcache_t& b) { return strcmp(a.name, b.name) < 0; });
        slist_sorted = true;
    }
    qpic_t* p = Draw::Draw_CachePic("gfx/p_multi.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);
    for (int n = 0; n < Net::hostCacheCount; n++) {
        char string[64];
        if (Net::hostcache[n].maxusers)
            sprintf_s(string, sizeof(string), "%-15.15s %-15.15s %2u/%2u\n", Net::hostcache[n].name,
                Net::hostcache[n].map, Net::hostcache[n].users, Net::hostcache[n].maxusers);
        else
            sprintf_s(string, sizeof(string), "%-15.15s %-15.15s\n", Net::hostcache[n].name, Net::hostcache[n].map);
        M_Print(16, 32 + 8 * n, string);
    }
    DrawLineCursor(0, 32, slist_cursor);
    if (!m_return_reason.empty()) M_PrintWhite(16, 148, m_return_reason.c_str());
}

void M_ServerList_Key(int k)
{
    if (k == Keys::K_ESCAPE) {
        M_Menu_LanConfig_f();
        return;
    }
    if (k == Keys::K_SPACE) {
        M_Menu_Search_f();
        return;
    }
    if (HandleNavKeys(k, slist_cursor, Net::hostCacheCount)) return;
    if (k == Keys::K_ENTER) {
        Audio::S_LocalSound("misc/menu2.wav");
        m_return_state = m_state;
        m_return_onerror = true;
        slist_sorted = false;
        Keys::key_dest = Keys::key_game;
        m_state = MenuState::None;
        Cmd::BufferAddText(Common::va("connect \"%s\"\n", Net::hostcache[slist_cursor].cname));
    }
}

void M_Init()
{
    constexpr CmdPair cmds[] = { { "togglemenu", M_ToggleMenu_f }, { "menu_main", M_Menu_Main_f },
        { "menu_singleplayer", M_Menu_SinglePlayer_f }, { "menu_load", M_Menu_Load_f }, { "menu_save", M_Menu_Save_f },
        { "menu_multiplayer", M_Menu_MultiPlayer_f }, { "menu_setup", M_Menu_Setup_f },
        { "menu_options", M_Menu_Options_f }, { "menu_keys", M_Menu_Keys_f }, { "help", M_Menu_Help_f },
        { "menu_quit", M_Menu_Quit_f } };
    for (auto [name, fn] : cmds) Cmd::AddCommand(name, fn);
}

using MenuFn = void (*)();
using MenuKeyFn = void (*)(int);

// Indexed by MenuState; keep in the same order as the enum in menu.hpp.
constexpr MenuFn menu_draw_table[] = { nullptr, M_Main_Draw, M_SinglePlayer_Draw, M_Load_Draw, M_Save_Draw,
    M_MultiPlayer_Draw, M_Setup_Draw, M_Options_Draw, M_Keys_Draw, M_Help_Draw, nullptr, M_LanConfig_Draw,
    M_GameOptions_Draw, M_Search_Draw, M_ServerList_Draw };

constexpr MenuKeyFn menu_key_table[] = { nullptr, M_Main_Key, M_SinglePlayer_Key, M_Load_Key, M_Save_Key,
    M_MultiPlayer_Key, M_Setup_Key, M_Options_Key, M_Keys_Key, M_Help_Key, nullptr, M_LanConfig_Key, M_GameOptions_Key,
    [](int) { M_Search_Key(); }, M_ServerList_Key };
static_assert(std::size(menu_draw_table) == static_cast<size_t>(MenuState::SList) + 1);
static_assert(std::size(menu_key_table) == static_cast<size_t>(MenuState::SList) + 1);

void M_Draw()
{
    if (m_state == MenuState::None || Keys::key_dest != Keys::key_menu) return;
    if (!m_recursiveDraw) {
        Screen::GetScreenSystem().SetCopyeverything(1);
        if (Screen::GetScreenSystem().GetConCurrent()) {
            Draw::Draw_ConsoleBackground(Vid::vid.height);
        } else
            Draw::Draw_FadeScreen();
        Screen::GetScreenSystem().SetFullupdate(0);
    } else
        m_recursiveDraw = false;

    const auto idx = static_cast<size_t>(m_state);
    if (idx < std::size(menu_draw_table) && menu_draw_table[idx]) menu_draw_table[idx]();

    if (m_entersound) {
        Audio::S_LocalSound("misc/menu2.wav");
        m_entersound = false;
    }
}

void M_Keydown(int key)
{
    const auto idx = static_cast<size_t>(m_state);
    if (idx < std::size(menu_key_table) && menu_key_table[idx]) menu_key_table[idx](key);
}

void M_ConfigureNetSubsystem()
{
    Cmd::BufferAddText("stopdemo\n");
    Net::net_hostport = lanConfig_port;
}

} // namespace Menu
