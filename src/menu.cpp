// menu.cpp -- menu system (main menu, options, multiplayer, etc)

#include <cstring>
#include <fstream>
#include <string>
#include <EASTL/string.h>
#include <string_view>
#include <array>
#include <algorithm>
#include <limits>
#include "quakedef.hpp"

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

namespace Vid {

void (*vid_menudrawfn)() = nullptr;
void (*vid_menukeyfn)(int key) = nullptr;

} // namespace Vid

namespace Menu {

MenuState m_state = MenuState::None;

void M_Menu_Main_f();
void M_Menu_SinglePlayer_f();
void M_Menu_Load_f();
void M_Menu_Save_f();
void M_Menu_MultiPlayer_f();
void M_Menu_Setup_f();
void M_Menu_Net_f();
void M_Menu_Options_f();
void M_Menu_Keys_f();
void M_Menu_Video_f();
void M_Menu_Help_f();
void M_Menu_Quit_f();
void M_Menu_SerialConfig_f();
void M_Menu_ModemConfig_f();
void M_Menu_LanConfig_f();
void M_Menu_GameOptions_f();
void M_Menu_Search_f();
void M_Menu_ServerList_f();

void M_Main_Draw();
void M_SinglePlayer_Draw();
void M_Load_Draw();
void M_Save_Draw();
void M_MultiPlayer_Draw();
void M_Setup_Draw();
void M_Net_Draw();
void M_Options_Draw();
void M_Keys_Draw();
void M_Video_Draw();
void M_Help_Draw();
void M_SerialConfig_Draw();
void M_ModemConfig_Draw();
void M_LanConfig_Draw();
void M_GameOptions_Draw();
void M_Search_Draw();
void M_ServerList_Draw();

void M_Main_Key(int key);
void M_SinglePlayer_Key(int key);
void M_Load_Key(int key);
void M_Save_Key(int key);
void M_MultiPlayer_Key(int key);
void M_Setup_Key(int key);
void M_Net_Key(int key);
void M_Options_Key(int key);
void M_Keys_Key(int key);
void M_Video_Key(int key);
void M_Help_Key(int key);
void M_SerialConfig_Key(int key);
void M_ModemConfig_Key(int key);
void M_LanConfig_Key(int key);
void M_GameOptions_Key(int key);
void M_Search_Key();
void M_ServerList_Key(int key);

bool m_entersound; // play after drawing a frame, so caching won't disrupt the sound
bool m_recursiveDraw;

MenuState m_return_state = MenuState::None;
bool m_return_onerror = false;
std::string m_return_reason;

int m_multiplayer_cursor = 0;
int m_net_cursor = 0;

inline bool StartingGame() { return m_multiplayer_cursor == 1; }
inline bool JoiningGame() { return m_multiplayer_cursor == 0; }
inline bool SerialConfig() { return m_net_cursor == 0; }
inline bool DirectConfig() { return m_net_cursor == 1; }
inline bool IPXConfig() { return m_net_cursor == 2; }
inline bool TCPIPConfig() { return m_net_cursor == 3; }

void M_ConfigureNetSubsystem();

/*
================
M_DrawCharacter

Draws one solid graphics character
================
*/
inline void M_DrawCharacter(int cx, int line, int num)
{
    Draw_Character(cx + ((vid.width - 320) >> 1), line, num);
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
    Draw_TransPic(x + ((vid.width - 320) >> 1), y, pic);
}

void M_DrawPic(int x, int y, qpic_t* pic)
{
    Draw_Pic(x + ((vid.width - 320) >> 1), y, pic);
}

std::array<byte, 256> identityTable;
std::array<byte, 256> translationTable;

void M_BuildTranslationTable(int top, int bottom)
{
    for (int j = 0; j < 256; j++) {
        identityTable[j] = static_cast<byte>(j);
    }
    translationTable = identityTable;

    if (top < 128) { // the artists made some backwards ranges.  sigh.
        std::copy_n(identityTable.begin() + top, 16, translationTable.begin() + TOP_RANGE);
    } else {
        for (int j = 0; j < 16; j++) {
            translationTable[TOP_RANGE + j] = identityTable[top + 15 - j];
        }
    }

    if (bottom < 128) {
        std::copy_n(identityTable.begin() + bottom, 16, translationTable.begin() + BOTTOM_RANGE);
    } else {
        for (int j = 0; j < 16; j++) {
            translationTable[BOTTOM_RANGE + j] = identityTable[bottom + 15 - j];
        }
    }
}

inline void M_DrawTransPicTranslate(int x, int y, qpic_t* pic)
{
    Draw_TransPicTranslate(x + ((vid.width - 320) >> 1), y, pic,
        translationTable.data());
}

inline void M_DrawTextBox(int x, int y, int width, int lines)
{
    qpic_t* p;
    int cx, cy;
    int n;

    // draw left side
    cx = x;
    cy = y;
    p = Draw_CachePic("gfx/box_tl.lmp");
    M_DrawTransPic(cx, cy, p);
    p = Draw_CachePic("gfx/box_ml.lmp");
    for (n = 0; n < lines; n++) {
        cy += 8;
        M_DrawTransPic(cx, cy, p);
    }
    p = Draw_CachePic("gfx/box_bl.lmp");
    M_DrawTransPic(cx, cy + 8, p);

    // draw middle
    cx += 8;
    while (width > 0) {
        cy = y;
        p = Draw_CachePic("gfx/box_tm.lmp");
        M_DrawTransPic(cx, cy, p);
        p = Draw_CachePic("gfx/box_mm.lmp");
        for (n = 0; n < lines; n++) {
            cy += 8;
            if (n == 1) {
                p = Draw_CachePic("gfx/box_mm2.lmp");
            }

            M_DrawTransPic(cx, cy, p);
        }
        p = Draw_CachePic("gfx/box_bm.lmp");
        M_DrawTransPic(cx, cy + 8, p);
        width -= 2;
        cx += 16;
    }

    // draw right side
    cy = y;
    p = Draw_CachePic("gfx/box_tr.lmp");
    M_DrawTransPic(cx, cy, p);
    p = Draw_CachePic("gfx/box_mr.lmp");
    for (n = 0; n < lines; n++) {
        cy += 8;
        M_DrawTransPic(cx, cy, p);
    }
    p = Draw_CachePic("gfx/box_br.lmp");
    M_DrawTransPic(cx, cy + 8, p);
}

//=============================================================================

int m_save_demonum;

/*
================
M_ToggleMenu_f
================
*/
void M_ToggleMenu_f()
{
    m_entersound = true;

    if (key_dest == key_menu) {
        if (m_state != MenuState::Main) {
            M_Menu_Main_f();

            return;
        }

        key_dest = key_game;
        m_state = MenuState::None;

        return;
    }

    if (key_dest == key_console) {
        ConsoleSystem::ToggleConsole_f();
    } else {
        M_Menu_Main_f();
    }
}

//=============================================================================
/* MAIN MENU */

int m_main_cursor;
constexpr int MAIN_ITEMS = 5;

void M_Menu_Main_f()
{
    if (key_dest != key_menu) {
        m_save_demonum = cls.demonum;
        cls.demonum = -1;
    }

    key_dest = key_menu;
    m_state = MenuState::Main;
    m_entersound = true;
}

void M_Main_Draw()
{
    int f;
    qpic_t* p;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/ttl_main.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);
    M_DrawTransPic(72, 32, Draw_CachePic("gfx/mainmenu.lmp"));

    f = (int)(host_time * 10) % 6;

    M_DrawTransPic(54, 32 + m_main_cursor * 20,
        Draw_CachePic(va("gfx/menudot%i.lmp", f + 1)));
}

void M_Main_Key(int key)
{
    switch (key) {
    case K_ESCAPE:
        key_dest = key_game;
        m_state = MenuState::None;
        cls.demonum = m_save_demonum;
        if (cls.demonum != -1 && !cls.demoplayback && cls.state != ca_connected) {
            CL_NextDemo();
        }

        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        if (++m_main_cursor >= MAIN_ITEMS) {
            m_main_cursor = 0;
        }

        break;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        if (--m_main_cursor < 0) {
            m_main_cursor = MAIN_ITEMS - 1;
        }

        break;

    case K_ENTER:
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

//=============================================================================
/* SINGLE PLAYER MENU */

int m_singleplayer_cursor;
constexpr int SINGLEPLAYER_ITEMS = 3;

void M_Menu_SinglePlayer_f()
{
    key_dest = key_menu;
    m_state = MenuState::SinglePlayer;
    m_entersound = true;
}

void M_SinglePlayer_Draw()
{
    int f;
    qpic_t* p;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/ttl_sgl.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);
    M_DrawTransPic(72, 32, Draw_CachePic("gfx/sp_menu.lmp"));

    f = (int)(host_time * 10) % 6;

    M_DrawTransPic(54, 32 + m_singleplayer_cursor * 20,
        Draw_CachePic(va("gfx/menudot%i.lmp", f + 1)));
}

void M_SinglePlayer_Key(int key)
{
    switch (key) {
    case K_ESCAPE:
        M_Menu_Main_f();
        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        if (++m_singleplayer_cursor >= SINGLEPLAYER_ITEMS) {
            m_singleplayer_cursor = 0;
        }

        break;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        if (--m_singleplayer_cursor < 0) {
            m_singleplayer_cursor = SINGLEPLAYER_ITEMS - 1;
        }

        break;

    case K_ENTER:
        m_entersound = true;

        switch (m_singleplayer_cursor) {
        case 0:
            if (sv.active) {
                if (!Screen::GetScreenSystem().ModalMessage(
                        "Are you sure you want to\nstart a new game?\n")) {
                    break;
                }
            }

            key_dest = key_game;
            if (sv.active) {
                Cmd::BufferAddText("disconnect\n");
            }

            Cmd::BufferAddText("maxplayers 1\n");
            Cmd::BufferAddText("map start\n");
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

//=============================================================================
/* LOAD/SAVE MENU */

int load_cursor; // 0 < load_cursor < MAX_SAVEGAMES

constexpr int MAX_SAVEGAMES = 12;
std::array<std::string, MAX_SAVEGAMES> m_filenames;
std::array<bool, MAX_SAVEGAMES> loadable;

void M_ScanSaves()
{
    for (int i = 0; i < MAX_SAVEGAMES; i++) {
        m_filenames[i] = "--- UNUSED SLOT ---";
        loadable[i] = false;
        char name[MAX_OSPATH];
        sprintf_s(name, sizeof(name), "%s/s%i.sav", com_gamedir, i);
        std::ifstream f(name);
        if (!f.is_open()) {
            continue;
        }

        int version = 0;
        if (!(f >> version)) {
            continue;
        }
        f.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        std::string comment;
        if (!std::getline(f, comment)) {
            continue;
        }

        if (!comment.empty() && comment.back() == '\r') {
            comment.pop_back();
        }

        for (char& c : comment) {
            if (c == '_') {
                c = ' ';
            }
        }

        if (comment.length() > SAVEGAME_COMMENT_LENGTH) {
            comment = comment.substr(0, SAVEGAME_COMMENT_LENGTH);
        }

        m_filenames[i] = comment;
        loadable[i] = true;
    }
}

void M_Menu_Load_f()
{
    m_entersound = true;
    m_state = MenuState::Load;
    key_dest = key_menu;
    M_ScanSaves();
}

void M_Menu_Save_f()
{
    if (!sv.active) {
        return;
    }

    if (cl.intermission) {
        return;
    }

    if (svs.maxclients != 1) {
        return;
    }

    m_entersound = true;
    m_state = MenuState::Save;
    key_dest = key_menu;
    M_ScanSaves();
}

void M_Load_Draw()
{
    int i;
    qpic_t* p;

    p = Draw_CachePic("gfx/p_load.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);

    for (i = 0; i < MAX_SAVEGAMES; i++) {
        M_Print(16, 32 + 8 * i, m_filenames[i]);
    }

    // line cursor
    M_DrawCharacter(8, 32 + load_cursor * 8, 12 + ((int)(realtime * 4) & 1));
}

void M_Save_Draw()
{
    int i;
    qpic_t* p;

    p = Draw_CachePic("gfx/p_save.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);

    for (i = 0; i < MAX_SAVEGAMES; i++) {
        M_Print(16, 32 + 8 * i, m_filenames[i]);
    }

    // line cursor
    M_DrawCharacter(8, 32 + load_cursor * 8, 12 + ((int)(realtime * 4) & 1));
}

void M_Load_Key(int k)
{
    switch (k) {
    case K_ESCAPE:
        M_Menu_SinglePlayer_f();
        break;

    case K_ENTER:
        S_LocalSound("misc/menu2.wav");
        if (!loadable[load_cursor]) {
            return;
        }

        m_state = MenuState::None;
        key_dest = key_game;

        // Host_Loadgame_f can't bring up the loading plaque because too much
        // stack space has been used, so do it now
        Screen::GetScreenSystem().BeginLoadingPlaque();

        // issue the load command
        Cmd::BufferAddText(va("load s%i\n", load_cursor));

        return;

    case K_UPARROW:
    case K_LEFTARROW:
        S_LocalSound("misc/menu1.wav");
        load_cursor--;
        if (load_cursor < 0) {
            load_cursor = MAX_SAVEGAMES - 1;
        }

        break;

    case K_DOWNARROW:
    case K_RIGHTARROW:
        S_LocalSound("misc/menu1.wav");
        load_cursor++;
        if (load_cursor >= MAX_SAVEGAMES) {
            load_cursor = 0;
        }

        break;
    }
}

void M_Save_Key(int k)
{
    switch (k) {
    case K_ESCAPE:
        M_Menu_SinglePlayer_f();
        break;

    case K_ENTER:
        m_state = MenuState::None;
        key_dest = key_game;
        Cmd::BufferAddText(va("save s%i\n", load_cursor));

        return;

    case K_UPARROW:
    case K_LEFTARROW:
        S_LocalSound("misc/menu1.wav");
        load_cursor--;
        if (load_cursor < 0) {
            load_cursor = MAX_SAVEGAMES - 1;
        }

        break;

    case K_DOWNARROW:
    case K_RIGHTARROW:
        S_LocalSound("misc/menu1.wav");
        load_cursor++;
        if (load_cursor >= MAX_SAVEGAMES) {
            load_cursor = 0;
        }

        break;
    }
}

//=============================================================================
/* MULTIPLAYER MENU */

constexpr int MULTIPLAYER_ITEMS = 3;

void M_Menu_MultiPlayer_f()
{
    key_dest = key_menu;
    m_state = MenuState::MultiPlayer;
    m_entersound = true;
}

void M_MultiPlayer_Draw()
{
    int f;
    qpic_t* p;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/p_multi.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);
    M_DrawTransPic(72, 32, Draw_CachePic("gfx/mp_menu.lmp"));

    f = (int)(host_time * 10) % 6;

    M_DrawTransPic(54, 32 + m_multiplayer_cursor * 20,
        Draw_CachePic(va("gfx/menudot%i.lmp", f + 1)));

    if (serialAvailable || ipxAvailable || tcpipAvailable) {
        return;
    }

    M_PrintWhite((320 / 2) - ((27 * 8) / 2), 148, "No Communications Available");
}

void M_MultiPlayer_Key(int key)
{
    switch (key) {
    case K_ESCAPE:
        M_Menu_Main_f();
        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        if (++m_multiplayer_cursor >= MULTIPLAYER_ITEMS) {
            m_multiplayer_cursor = 0;
        }

        break;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        if (--m_multiplayer_cursor < 0) {
            m_multiplayer_cursor = MULTIPLAYER_ITEMS - 1;
        }

        break;

    case K_ENTER:
        m_entersound = true;
        switch (m_multiplayer_cursor) {
        case 0:
            if (serialAvailable || ipxAvailable || tcpipAvailable) {
                M_Menu_Net_f();
            }

            break;

        case 1:
            if (serialAvailable || ipxAvailable || tcpipAvailable) {
                M_Menu_Net_f();
            }

            break;

        case 2:
            M_Menu_Setup_f();
            break;
        }
    }
}

//=============================================================================
/* SETUP MENU */

int setup_cursor = 4;
const std::array<int, 5> setup_cursor_table = { 40, 56, 80, 104, 140 };

eastl::string setup_hostname;
eastl::string setup_myname;
int setup_oldtop;
int setup_oldbottom;
int setup_top;
int setup_bottom;

constexpr int NUM_SETUP_CMDS = 5;

void M_Menu_Setup_f()
{
    key_dest = key_menu;
    m_state = MenuState::Setup;
    m_entersound = true;
    setup_myname = cl_name.string;
    setup_hostname = hostname.string;
    setup_top = setup_oldtop = ((int)cl_color.value) >> 4;
    setup_bottom = setup_oldbottom = ((int)cl_color.value) & 15;
}

void M_Setup_Draw()
{
    qpic_t* p;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/p_multi.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);

    M_Print(64, 40, "Hostname");
    M_DrawTextBox(160, 32, 16, 1);
    M_Print(168, 40, setup_hostname.c_str());

    M_Print(64, 56, "Your name");
    M_DrawTextBox(160, 48, 16, 1);
    M_Print(168, 56, setup_myname.c_str());

    M_Print(64, 80, "Shirt color");
    M_Print(64, 104, "Pants color");

    M_DrawTextBox(64, 140 - 8, 14, 1);
    M_Print(72, 140, "Accept Changes");

    p = Draw_CachePic("gfx/bigbox.lmp");
    M_DrawTransPic(160, 64, p);
    p = Draw_CachePic("gfx/menuplyr.lmp");
    M_BuildTranslationTable(setup_top * 16, setup_bottom * 16);
    M_DrawTransPicTranslate(172, 72, p);

    M_DrawCharacter(56, setup_cursor_table[setup_cursor],
        12 + ((int)(realtime * 4) & 1));

    if (setup_cursor == 0) {
        M_DrawCharacter(168 + 8 * (int)setup_hostname.length(),
            setup_cursor_table[setup_cursor],
            10 + ((int)(realtime * 4) & 1));
    }

    if (setup_cursor == 1) {
        M_DrawCharacter(168 + 8 * (int)setup_myname.length(),
            setup_cursor_table[setup_cursor],
            10 + ((int)(realtime * 4) & 1));
    }
}

void M_Setup_Key(int k)
{
    switch (k) {
    case K_ESCAPE:
        M_Menu_MultiPlayer_f();
        break;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        setup_cursor--;
        if (setup_cursor < 0) {
            setup_cursor = NUM_SETUP_CMDS - 1;
        }

        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        setup_cursor++;
        if (setup_cursor >= NUM_SETUP_CMDS) {
            setup_cursor = 0;
        }

        break;

    case K_LEFTARROW:
        if (setup_cursor < 2) {
            return;
        }

        S_LocalSound("misc/menu3.wav");
        if (setup_cursor == 2) {
            setup_top = setup_top - 1;
        }

        if (setup_cursor == 3) {
            setup_bottom = setup_bottom - 1;
        }

        break;
    case K_RIGHTARROW:
        if (setup_cursor < 2) {
            return;
        }

    forward:
        S_LocalSound("misc/menu3.wav");
        if (setup_cursor == 2) {
            setup_top = setup_top + 1;
        }

        if (setup_cursor == 3) {
            setup_bottom = setup_bottom + 1;
        }

        break;

    case K_ENTER:
        if (setup_cursor == 0 || setup_cursor == 1) {
            return;
        }

        if (setup_cursor == 2 || setup_cursor == 3) {
            goto forward;
        }

        // setup_cursor == 4 (OK)
        if (cl_name.string != setup_myname) {
            Cmd::BufferAddText(va("name \"%s\"\n", setup_myname.c_str()));
        }

        if (hostname.string != setup_hostname) {
            Cvar::Set("hostname", setup_hostname.c_str());
        }

        if (setup_top != setup_oldtop || setup_bottom != setup_oldbottom) {
            Cmd::BufferAddText(va("color %i %i\n", setup_top, setup_bottom));
        }

        m_entersound = true;
        M_Menu_MultiPlayer_f();
        break;

    case K_BACKSPACE:
        if (setup_cursor == 0) {
            if (!setup_hostname.empty()) {
                setup_hostname.pop_back();
            }
        }

        if (setup_cursor == 1) {
            if (!setup_myname.empty()) {
                setup_myname.pop_back();
            }
        }

        break;

    default:
        if (k < 32 || k > 127) {
            break;
        }

        if (setup_cursor == 0) {
            if (setup_hostname.length() < 15) {
                setup_hostname.push_back(static_cast<char>(k));
            }
        }

        if (setup_cursor == 1) {
            if (setup_myname.length() < 15) {
                setup_myname.push_back(static_cast<char>(k));
            }
        }
    }

    if (setup_top > 13) {
        setup_top = 0;
    }

    if (setup_top < 0) {
        setup_top = 13;
    }

    if (setup_bottom > 13) {
        setup_bottom = 0;
    }

    if (setup_bottom < 0) {
        setup_bottom = 13;
    }
}

//=============================================================================
/* NET MENU */

int m_net_items;
int m_net_saveHeight;

const std::array<std::string_view, 16> net_helpMessage = {
    /* .........1.........2.... */
    "                        ", " Two computers connected",
    "   through two modems.  ", "                        ",

    "                        ", " Two computers connected",
    " by a null-modem cable. ", "                        ",

    " Novell network LANs    ", " or Windows 95 DOS-box. ",
    "                        ", "(LAN=Local Area Network)",

    " Commonly used to play  ", " over the Internet, but ",
    " also used on a Local   ", " Area Network.          "
};

void M_Menu_Net_f()
{
    key_dest = key_menu;
    m_state = MenuState::Net;
    m_entersound = true;
    m_net_items = 4;

    if (m_net_cursor >= m_net_items) {
        m_net_cursor = 0;
    }

    m_net_cursor--;
    M_Net_Key(K_DOWNARROW);
}

void M_Net_Draw()
{
    int f;
    qpic_t* p;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/p_multi.lmp");
    int basex = (320 - p->width) / 2;
    M_DrawPic(basex, 4, p);

    f = 32;

    if (serialAvailable) {
        p = Draw_CachePic("gfx/netmen1.lmp");
    } else {
        p = Draw_CachePic("gfx/dim_modm.lmp");
    }

    if (p) {
        M_DrawTransPic(72, f, p);
    }

    f += 19;

    if (serialAvailable) {
        p = Draw_CachePic("gfx/netmen2.lmp");
    } else {
        p = Draw_CachePic("gfx/dim_drct.lmp");
    }

    if (p) {
        M_DrawTransPic(72, f, p);
    }

    f += 19;
    if (ipxAvailable) {
        p = Draw_CachePic("gfx/netmen3.lmp");
    } else {
        p = Draw_CachePic("gfx/dim_ipx.lmp");
    }

    M_DrawTransPic(72, f, p);

    f += 19;
    if (tcpipAvailable) {
        p = Draw_CachePic("gfx/netmen4.lmp");
    } else {
        p = Draw_CachePic("gfx/dim_tcp.lmp");
    }

    M_DrawTransPic(72, f, p);

    if (m_net_items == 5) // JDC, could just be removed
    {
        f += 19;
        p = Draw_CachePic("gfx/netmen5.lmp");
        M_DrawTransPic(72, f, p);
    }

    f = (320 - 26 * 8) / 2;
    M_DrawTextBox(f, 134, 24, 4);
    f += 8;
    M_Print(f, 142, net_helpMessage[m_net_cursor * 4 + 0]);
    M_Print(f, 150, net_helpMessage[m_net_cursor * 4 + 1]);
    M_Print(f, 158, net_helpMessage[m_net_cursor * 4 + 2]);
    M_Print(f, 166, net_helpMessage[m_net_cursor * 4 + 3]);

    f = (int)(host_time * 10) % 6;
    M_DrawTransPic(54, 32 + m_net_cursor * 20,
        Draw_CachePic(va("gfx/menudot%i.lmp", f + 1)));
}

void M_Net_Key(int k)
{
again:
    switch (k) {
    case K_ESCAPE:
        M_Menu_MultiPlayer_f();
        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        if (++m_net_cursor >= m_net_items) {
            m_net_cursor = 0;
        }

        break;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        if (--m_net_cursor < 0) {
            m_net_cursor = m_net_items - 1;
        }

        break;

    case K_ENTER:
        m_entersound = true;

        switch (m_net_cursor) {
        case 0:
            M_Menu_SerialConfig_f();
            break;

        case 1:
            M_Menu_SerialConfig_f();
            break;

        case 2:
            M_Menu_LanConfig_f();
            break;

        case 3:
            M_Menu_LanConfig_f();
            break;

        case 4:
            // multiprotocol
            break;
        }
    }

    if (m_net_cursor == 0 && !serialAvailable) {
        goto again;
    }

    if (m_net_cursor == 1 && !serialAvailable) {
        goto again;
    }

    if (m_net_cursor == 2 && !ipxAvailable) {
        goto again;
    }

    if (m_net_cursor == 3 && !tcpipAvailable) {
        goto again;
    }
}

//=============================================================================
/* OPTIONS MENU */

constexpr int OPTIONS_ITEMS = 13;
constexpr int SLIDER_RANGE = 10;

int options_cursor;

void M_Menu_Options_f()
{
    key_dest = key_menu;
    m_state = MenuState::Options;
    m_entersound = true;
}

void M_AdjustSliders(int dir)
{
    S_LocalSound("misc/menu3.wav");

    switch (options_cursor) {
    case 3: // screen size
        Screen::GetScreenSystem().GetViewsize().value += dir * 10;
        if (Screen::GetScreenSystem().GetViewsize().value < 30) {
            Screen::GetScreenSystem().GetViewsize().value = 30;
        }

        if (Screen::GetScreenSystem().GetViewsize().value > 120) {
            Screen::GetScreenSystem().GetViewsize().value = 120;
        }

        Cvar::SetValue("viewsize", Screen::GetScreenSystem().GetViewsize().value);
        break;
    case 4: // gamma
        v_gamma.value -= static_cast<float>(dir * 0.05);
        if (v_gamma.value < 0.5) {
            v_gamma.value = 0.5;
        }

        if (v_gamma.value > 1) {
            v_gamma.value = 1;
        }

        Cvar::SetValue("gamma", v_gamma.value);
        break;
    case 5: // mouse speed
        sensitivity.value += static_cast<float>(dir * 0.5);
        if (sensitivity.value < 1) {
            sensitivity.value = 1;
        }

        if (sensitivity.value > 11) {
            sensitivity.value = 11;
        }

        Cvar::SetValue("sensitivity", sensitivity.value);
        break;
    case 6: // music volume
        bgmvolume.value += static_cast<float>(dir * 0.1);
        if (bgmvolume.value < 0) {
            bgmvolume.value = 0;
        }

        if (bgmvolume.value > 1) {
            bgmvolume.value = 1;
        }

        Cvar::SetValue("bgmvolume", bgmvolume.value);
        break;
    case 7: // sfx volume
        volume.value += static_cast<float>(dir * 0.1);
        if (volume.value < 0) {
            volume.value = 0;
        }

        if (volume.value > 1) {
            volume.value = 1;
        }

        Cvar::SetValue("volume", volume.value);
        break;

    case 8: // allways run
        if (cl_forwardspeed.value > 200) {
            Cvar::SetValue("cl_forwardspeed", static_cast<float>(200));
            Cvar::SetValue("cl_backspeed", static_cast<float>(200));
        } else {
            Cvar::SetValue("cl_forwardspeed", static_cast<float>(400));
            Cvar::SetValue("cl_backspeed", static_cast<float>(400));
        }

        break;

    case 9: // invert mouse
        Cvar::SetValue("m_pitch", -m_pitch.value);
        break;

    case 10: // lookspring
        Cvar::SetValue("lookspring", static_cast<float>(!lookspring.value));
        break;

    case 11: // lookstrafe
        Cvar::SetValue("lookstrafe", static_cast<float>(!lookstrafe.value));
        break;
    }
}

inline void M_DrawSlider(int x, int y, float range)
{
    int i;

    if (range < 0) {
        range = 0;
    }

    if (range > 1) {
        range = 1;
    }

    M_DrawCharacter(x - 8, y, 128);
    for (i = 0; i < SLIDER_RANGE; i++) {
        M_DrawCharacter(x + i * 8, y, 129);
    }
    M_DrawCharacter(x + i * 8, y, 130);
    M_DrawCharacter(x + static_cast<int>((SLIDER_RANGE - 1) * 8 * range), y, 131);
}

inline void M_DrawCheckbox(int x, int y, int on)
{
    if (on) {
        M_Print(x, y, "on");
    } else {
        M_Print(x, y, "off");
    }
}

void M_Options_Draw()
{
    float r;
    qpic_t* p;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/p_option.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);

    M_Print(16, 32, "    Customize controls");
    M_Print(16, 40, "         Go to console");
    M_Print(16, 48, "     Reset to defaults");

    M_Print(16, 56, "           Screen size");
    r = (Screen::GetScreenSystem().GetViewsize().value - 30) / (120 - 30);
    M_DrawSlider(220, 56, r);

    M_Print(16, 64, "            Brightness");
    r = static_cast<float>((1.0 - v_gamma.value) / 0.5);
    M_DrawSlider(220, 64, r);

    M_Print(16, 72, "           Mouse Speed");
    r = (sensitivity.value - 1) / 10;
    M_DrawSlider(220, 72, r);

    M_Print(16, 80, "       CD Music Volume");
    r = bgmvolume.value;
    M_DrawSlider(220, 80, r);

    M_Print(16, 88, "          Sound Volume");
    r = volume.value;
    M_DrawSlider(220, 88, r);

    M_Print(16, 96, "            Always Run");
    M_DrawCheckbox(220, 96, cl_forwardspeed.value > 200);

    M_Print(16, 104, "          Invert Mouse");
    M_DrawCheckbox(220, 104, m_pitch.value < 0);

    M_Print(16, 112, "            Lookspring");
    M_DrawCheckbox(220, 112, static_cast<int>(lookspring.value));

    M_Print(16, 120, "            Lookstrafe");
    M_DrawCheckbox(220, 120, static_cast<int>(lookstrafe.value));

    if (vid_menudrawfn) {
        M_Print(16, 128, "         Video Options");
    }

    // cursor
    M_DrawCharacter(200, 32 + options_cursor * 8, 12 + ((int)(realtime * 4) & 1));
}

void M_Options_Key(int k)
{
    switch (k) {
    case K_ESCAPE:
        M_Menu_Main_f();
        break;

    case K_ENTER:
        m_entersound = true;
        switch (options_cursor) {
        case 0:
            M_Menu_Keys_f();
            break;
        case 1:
            m_state = MenuState::None;
            ConsoleSystem::ToggleConsole_f();
            break;
        case 2:
            Cmd::BufferAddText("exec default.cfg\n");
            break;
        case 12:
            M_Menu_Video_f();
            break;
        default:
            M_AdjustSliders(1);
            break;
        }

        return;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        options_cursor--;
        if (options_cursor < 0) {
            options_cursor = OPTIONS_ITEMS - 1;
        }

        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        options_cursor++;
        if (options_cursor >= OPTIONS_ITEMS) {
            options_cursor = 0;
        }

        break;

    case K_LEFTARROW:
        M_AdjustSliders(-1);
        break;

    case K_RIGHTARROW:
        M_AdjustSliders(1);
        break;
    }

    if (options_cursor == 12 && vid_menudrawfn == nullptr) {
        if (k == K_UPARROW) {
            options_cursor = 11;
        } else {
            options_cursor = 0;
        }
    }
}

//=============================================================================
/* KEYS MENU */

struct BindName {
    std::string_view command;
    std::string_view description;
};

const std::array<BindName, 18> bindnames = {{
    { "+attack", "attack" }, { "impulse 10", "change weapon" },
    { "+jump", "jump / swim up" }, { "+forward", "walk forward" },
    { "+back", "backpedal" }, { "+left", "turn left" },
    { "+right", "turn right" }, { "+speed", "run" },
    { "+moveleft", "step left" }, { "+moveright", "step right" },
    { "+strafe", "sidestep" }, { "+lookup", "look up" },
    { "+lookdown", "look down" }, { "centerview", "center view" },
    { "+mlook", "mouse look" }, { "+klook", "keyboard look" },
    { "+moveup", "swim up" }, { "+movedown", "swim down" }
}};

int keys_cursor;
bool bind_grab;

void M_Menu_Keys_f()
{
    key_dest = key_menu;
    m_state = MenuState::Keys;
    m_entersound = true;
}

void M_FindKeysForCommand(std::string_view command, std::array<int, 2>& twokeys)
{
    twokeys[0] = twokeys[1] = -1;
    int count = 0;

    for (int j = 0; j < 256; j++) {
        if (keybindings[j].empty()) {
            continue;
        }

        if (keybindings[j].compare(0, command.length(), command) == 0) {
            twokeys[count] = j;
            count++;
            if (count == 2) {
                break;
            }
        }
    }
}

void M_UnbindCommand(std::string_view command)
{
    for (int j = 0; j < 256; j++) {
        if (keybindings[j].empty()) {
            continue;
        }

        if (keybindings[j].compare(0, command.length(), command) == 0) {
            Key_SetBinding(j, "");
        }
    }
}

void M_Keys_Draw()
{
    int y;
    std::array<int, 2> keys;
    const char* name;
    qpic_t* p;

    p = Draw_CachePic("gfx/ttl_cstm.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);

    if (bind_grab) {
        M_Print(12, 32, "Press a key or button for this action");
    } else {
        M_Print(18, 32, "Enter to change, backspace to clear");
    }

    // search for known bindings
    for (size_t i = 0; i < bindnames.size(); i++) {
        y = 48 + 8 * static_cast<int>(i);

        M_Print(16, y, bindnames[i].description);

        M_FindKeysForCommand(bindnames[i].command, keys);

        if (keys[0] == -1) {
            M_Print(140, y, "???");
        } else {
            name = Key_KeynumToString(keys[0]);
            M_Print(140, y, name);
            int x = (int)strlen(name) * 8;
            if (keys[1] != -1) {
                M_Print(140 + x + 8, y, "or");
                M_Print(140 + x + 32, y, Key_KeynumToString(keys[1]));
            }
        }
    }

    if (bind_grab) {
        M_DrawCharacter(130, 48 + keys_cursor * 8, '=');
    } else {
        M_DrawCharacter(130, 48 + keys_cursor * 8, 12 + ((int)(realtime * 4) & 1));
    }
}

void M_Keys_Key(int k)
{
    char cmd[80];
    std::array<int, 2> keys;

    if (bind_grab) { // defining a key
        S_LocalSound("misc/menu1.wav");
        if (k == K_ESCAPE) {
            bind_grab = false;
        } else if (k != '`') {
            sprintf_s(cmd, sizeof(cmd), "bind \"%s\" \"%.70s\"\n", Key_KeynumToString(k),
                bindnames[keys_cursor].command.data());
            Cmd::BufferInsertText(cmd);
        }

        bind_grab = false;

        return;
    }

    switch (k) {
    case K_ESCAPE:
        M_Menu_Options_f();
        break;

    case K_LEFTARROW:
    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        keys_cursor--;
        if (keys_cursor < 0) {
            keys_cursor = static_cast<int>(bindnames.size()) - 1;
        }

        break;

    case K_DOWNARROW:
    case K_RIGHTARROW:
        S_LocalSound("misc/menu1.wav");
        keys_cursor++;
        if (static_cast<size_t>(keys_cursor) >= bindnames.size()) {
            keys_cursor = 0;
        }

        break;

    case K_ENTER: // go into bind mode
        M_FindKeysForCommand(bindnames[keys_cursor].command, keys);
        S_LocalSound("misc/menu2.wav");
        if (keys[1] != -1) {
            M_UnbindCommand(bindnames[keys_cursor].command);
        }

        bind_grab = true;
        break;

    case K_BACKSPACE: // delete bindings
    case K_DEL:       // delete bindings
        S_LocalSound("misc/menu2.wav");
        M_UnbindCommand(bindnames[keys_cursor].command);
        break;
    }
}

//=============================================================================
/* VIDEO MENU */

void M_Menu_Video_f()
{
    key_dest = key_menu;
    m_state = MenuState::Video;
    m_entersound = true;
}

void M_Video_Draw()
{
    if (vid_menudrawfn) {
        (*vid_menudrawfn)();
    }
}

void M_Video_Key(int key)
{
    if (vid_menukeyfn) {
        (*vid_menukeyfn)(key);
    }
}

//=============================================================================
/* HELP MENU */

int help_page;
constexpr int NUM_HELP_PAGES = 6;

void M_Menu_Help_f()
{
    key_dest = key_menu;
    m_state = MenuState::Help;
    m_entersound = true;
    help_page = 0;
}

void M_Help_Draw()
{
    M_DrawPic(0, 0, Draw_CachePic(va("gfx/help%i.lmp", help_page)));
}

void M_Help_Key(int key)
{
    switch (key) {
    case K_ESCAPE:
        M_Menu_Main_f();
        break;

    case K_UPARROW:
    case K_RIGHTARROW:
        m_entersound = true;
        if (++help_page >= NUM_HELP_PAGES) {
            help_page = 0;
        }

        break;

    case K_DOWNARROW:
    case K_LEFTARROW:
        m_entersound = true;
        if (--help_page < 0) {
            help_page = NUM_HELP_PAGES - 1;
        }

        break;
    }
}

//=============================================================================
/* QUIT MENU */

void M_Menu_Quit_f()
{
    key_dest = key_console;
    Host_Quit_f();
}

//=============================================================================
/* SERIAL CONFIG MENU */

int serialConfig_cursor;
const std::array<int, 6> serialConfig_cursor_table = { 48, 64, 80, 96, 112, 132 };
constexpr int NUM_SERIALCONFIG_CMDS = 6;

static const std::array<int, 4> ISA_uarts = { 0x3f8, 0x2f8, 0x3e8, 0x2e8 };
static const std::array<int, 4> ISA_IRQs = { 4, 3, 4, 3 };
static const std::array<int, 6> serialConfig_baudrate = { 9600, 14400, 19200, 28800, 38400, 57600 };

int serialConfig_comport;
int serialConfig_irq;
int serialConfig_baud;
std::string serialConfig_phone;

void M_Menu_SerialConfig_f()
{
    int n;
    int port;
    int baudrate;
    qboolean useModem;

    key_dest = key_menu;
    m_state = MenuState::SerialConfig;
    m_entersound = true;
    if (JoiningGame() && SerialConfig()) {
        serialConfig_cursor = 4;
    } else {
        serialConfig_cursor = 5;
    }

    (*GetComPortConfig)(0, &port, &serialConfig_irq, &baudrate, &useModem);

    // map uart's port to COMx
    for (n = 0; n < 4; n++) {
        if (ISA_uarts[n] == port) {
            break;
        }
    }
    if (n == 4) {
        n = 0;
        serialConfig_irq = 4;
    }

    serialConfig_comport = n + 1;

    // map baudrate to index
    for (n = 0; n < 6; n++) {
        if (serialConfig_baudrate[n] == baudrate) {
            break;
        }
    }
    if (n == 6) {
        n = 5;
    }

    serialConfig_baud = n;

    m_return_onerror = false;
    m_return_reason.clear();
}

void M_SerialConfig_Draw()
{
    qpic_t* p;
    int basex;
    const char* startJoin;
    const char* directModem;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/p_multi.lmp");
    basex = (320 - p->width) / 2;
    M_DrawPic(basex, 4, p);

    if (StartingGame()) {
        startJoin = "New Game";
    } else {
        startJoin = "Join Game";
    }

    if (SerialConfig()) {
        directModem = "Modem";
    } else {
        directModem = "Direct Connect";
    }

    M_Print(basex, 32, va("%s - %s", startJoin, directModem));
    basex += 8;

    M_Print(basex, serialConfig_cursor_table[0], "Port");
    M_DrawTextBox(160, 40, 4, 1);
    M_Print(168, serialConfig_cursor_table[0], va("COM%u", serialConfig_comport));

    M_Print(basex, serialConfig_cursor_table[1], "IRQ");
    M_DrawTextBox(160, serialConfig_cursor_table[1] - 8, 1, 1);
    M_Print(168, serialConfig_cursor_table[1], va("%u", serialConfig_irq));

    M_Print(basex, serialConfig_cursor_table[2], "Baud");
    M_DrawTextBox(160, serialConfig_cursor_table[2] - 8, 5, 1);
    M_Print(168, serialConfig_cursor_table[2],
        va("%u", serialConfig_baudrate[serialConfig_baud]));

    if (SerialConfig()) {
        M_Print(basex, serialConfig_cursor_table[3], "Modem Setup...");
        if (JoiningGame()) {
            M_Print(basex, serialConfig_cursor_table[4], "Phone number");
            M_DrawTextBox(160, serialConfig_cursor_table[4] - 8, 16, 1);
            M_Print(168, serialConfig_cursor_table[4], serialConfig_phone);
        }
    }

    if (JoiningGame()) {
        M_DrawTextBox(basex, serialConfig_cursor_table[5] - 8, 7, 1);
        M_Print(basex + 8, serialConfig_cursor_table[5], "Connect");
    } else {
        M_DrawTextBox(basex, serialConfig_cursor_table[5] - 8, 2, 1);
        M_Print(basex + 8, serialConfig_cursor_table[5], "OK");
    }

    M_DrawCharacter(basex - 8, serialConfig_cursor_table[serialConfig_cursor],
        12 + ((int)(realtime * 4) & 1));

    if (serialConfig_cursor == 4) {
        M_DrawCharacter(168 + 8 * (int)serialConfig_phone.length(),
            serialConfig_cursor_table[serialConfig_cursor],
            10 + ((int)(realtime * 4) & 1));
    }

    if (!m_return_reason.empty()) {
        M_PrintWhite(basex, 148, m_return_reason);
    }
}

void M_SerialConfig_Key(int key)
{
    switch (key) {
    case K_ESCAPE:
        M_Menu_Net_f();
        break;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        serialConfig_cursor--;
        if (serialConfig_cursor < 0) {
            serialConfig_cursor = NUM_SERIALCONFIG_CMDS - 1;
        }

        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        serialConfig_cursor++;
        if (serialConfig_cursor >= NUM_SERIALCONFIG_CMDS) {
            serialConfig_cursor = 0;
        }

        break;

    case K_LEFTARROW:
        if (serialConfig_cursor > 2) {
            break;
        }

        S_LocalSound("misc/menu3.wav");

        if (serialConfig_cursor == 0) {
            serialConfig_comport--;
            if (serialConfig_comport == 0) {
                serialConfig_comport = 4;
            }

            serialConfig_irq = ISA_IRQs[serialConfig_comport - 1];
        }

        if (serialConfig_cursor == 1) {
            serialConfig_irq--;
            if (serialConfig_irq == 6) {
                serialConfig_irq = 5;
            }

            if (serialConfig_irq == 1) {
                serialConfig_irq = 7;
            }
        }

        if (serialConfig_cursor == 2) {
            serialConfig_baud--;
            if (serialConfig_baud < 0) {
                serialConfig_baud = 5;
            }
        }

        break;

    case K_RIGHTARROW:
        if (serialConfig_cursor > 2) {
            break;
        }

    forward:
        S_LocalSound("misc/menu3.wav");

        if (serialConfig_cursor == 0) {
            serialConfig_comport++;
            if (serialConfig_comport > 4) {
                serialConfig_comport = 1;
            }

            serialConfig_irq = ISA_IRQs[serialConfig_comport - 1];
        }

        if (serialConfig_cursor == 1) {
            serialConfig_irq++;
            if (serialConfig_irq == 6) {
                serialConfig_irq = 7;
            }

            if (serialConfig_irq == 8) {
                serialConfig_irq = 2;
            }
        }

        if (serialConfig_cursor == 2) {
            serialConfig_baud++;
            if (serialConfig_baud > 5) {
                serialConfig_baud = 0;
            }
        }

        break;

    case K_ENTER:
        if (serialConfig_cursor < 3) {
            goto forward;
        }

        m_entersound = true;

        if (serialConfig_cursor == 3) {
            (*SetComPortConfig)(
                0, ISA_uarts[serialConfig_comport - 1], serialConfig_irq,
                serialConfig_baudrate[serialConfig_baud], SerialConfig());

            M_Menu_ModemConfig_f();
            break;
        }

        if (serialConfig_cursor == 4) {
            serialConfig_cursor = 5;
            break;
        }

        // serialConfig_cursor == 5 (OK/CONNECT)
        (*SetComPortConfig)(
            0, ISA_uarts[serialConfig_comport - 1], serialConfig_irq,
            serialConfig_baudrate[serialConfig_baud], SerialConfig());

        M_ConfigureNetSubsystem();

        if (StartingGame()) {
            M_Menu_GameOptions_f();
            break;
        }

        m_return_state = m_state;
        m_return_onerror = true;
        key_dest = key_game;
        m_state = MenuState::None;

        if (SerialConfig()) {
            Cmd::BufferAddText(va("connect \"%s\"\n", serialConfig_phone.c_str()));
        } else {
            Cmd::BufferAddText("connect\n");
        }

        break;

    case K_BACKSPACE:
        if (serialConfig_cursor == 4) {
            if (!serialConfig_phone.empty()) {
                serialConfig_phone.pop_back();
            }
        }

        break;

    default:
        if (key < 32 || key > 127) {
            break;
        }

        if (serialConfig_cursor == 4) {
            if (serialConfig_phone.length() < 15) {
                serialConfig_phone.push_back(static_cast<char>(key));
            }
        }
    }

    if (DirectConfig() && (serialConfig_cursor == 3 || serialConfig_cursor == 4)) {
        if (key == K_UPARROW) {
            serialConfig_cursor = 2;
        } else {
            serialConfig_cursor = 5;
        }
    }

    if (SerialConfig() && StartingGame() && serialConfig_cursor == 4) {
        if (key == K_UPARROW) {
            serialConfig_cursor = 3;
        } else {
            serialConfig_cursor = 5;
        }
    }
}

//=============================================================================
/* MODEM CONFIG MENU */

int modemConfig_cursor;
const std::array<int, 5> modemConfig_cursor_table = { 40, 56, 88, 120, 156 };
constexpr int NUM_MODEMCONFIG_CMDS = 5;

char modemConfig_dialing;
std::array<char, 16> modemConfig_clear;
std::array<char, 32> modemConfig_init;
std::array<char, 16> modemConfig_hangup;

void M_Menu_ModemConfig_f()
{
    key_dest = key_menu;
    m_state = MenuState::ModemConfig;
    m_entersound = true;
    (*GetModemConfig)(0, &modemConfig_dialing, modemConfig_clear.data(),
        modemConfig_init.data(), modemConfig_hangup.data());
}

void M_ModemConfig_Draw()
{
    qpic_t* p;
    int basex;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/p_multi.lmp");
    basex = (320 - p->width) / 2;
    M_DrawPic(basex, 4, p);
    basex += 8;

    if (modemConfig_dialing == 'P') {
        M_Print(basex, modemConfig_cursor_table[0], "Pulse Dialing");
    } else {
        M_Print(basex, modemConfig_cursor_table[0], "Touch Tone Dialing");
    }

    M_Print(basex, modemConfig_cursor_table[1], "Clear");
    M_DrawTextBox(basex, modemConfig_cursor_table[1] + 4, 16, 1);
    M_Print(basex + 8, modemConfig_cursor_table[1] + 12, modemConfig_clear.data());
    if (modemConfig_cursor == 1) {
        M_DrawCharacter(basex + 8 + 8 * (int)strlen(modemConfig_clear.data()),
            modemConfig_cursor_table[1] + 12,
            10 + ((int)(realtime * 4) & 1));
    }

    M_Print(basex, modemConfig_cursor_table[2], "Init");
    M_DrawTextBox(basex, modemConfig_cursor_table[2] + 4, 30, 1);
    M_Print(basex + 8, modemConfig_cursor_table[2] + 12, modemConfig_init.data());
    if (modemConfig_cursor == 2) {
        M_DrawCharacter(basex + 8 + 8 * (int)strlen(modemConfig_init.data()),
            modemConfig_cursor_table[2] + 12,
            10 + ((int)(realtime * 4) & 1));
    }

    M_Print(basex, modemConfig_cursor_table[3], "Hangup");
    M_DrawTextBox(basex, modemConfig_cursor_table[3] + 4, 16, 1);
    M_Print(basex + 8, modemConfig_cursor_table[3] + 12, modemConfig_hangup.data());
    if (modemConfig_cursor == 3) {
        M_DrawCharacter(basex + 8 + 8 * (int)strlen(modemConfig_hangup.data()),
            modemConfig_cursor_table[3] + 12,
            10 + ((int)(realtime * 4) & 1));
    }

    M_DrawTextBox(basex, modemConfig_cursor_table[4] - 8, 2, 1);
    M_Print(basex + 8, modemConfig_cursor_table[4], "OK");

    M_DrawCharacter(basex - 8, modemConfig_cursor_table[modemConfig_cursor],
        12 + ((int)(realtime * 4) & 1));
}

void M_ModemConfig_Key(int key)
{
    int l;

    switch (key) {
    case K_ESCAPE:
        M_Menu_SerialConfig_f();
        break;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        modemConfig_cursor--;
        if (modemConfig_cursor < 0) {
            modemConfig_cursor = NUM_MODEMCONFIG_CMDS - 1;
        }

        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        modemConfig_cursor++;
        if (modemConfig_cursor >= NUM_MODEMCONFIG_CMDS) {
            modemConfig_cursor = 0;
        }

        break;

    case K_LEFTARROW:
    case K_RIGHTARROW:
        if (modemConfig_cursor == 0) {
            if (modemConfig_dialing == 'P') {
                modemConfig_dialing = 'T';
            } else {
                modemConfig_dialing = 'P';
            }

            S_LocalSound("misc/menu1.wav");
        }

        break;

    case K_ENTER:
        if (modemConfig_cursor == 0) {
            if (modemConfig_dialing == 'P') {
                modemConfig_dialing = 'T';
            } else {
                modemConfig_dialing = 'P';
            }

            m_entersound = true;
        }

        if (modemConfig_cursor == 4) {
            (*SetModemConfig)(0, va("%c", modemConfig_dialing), modemConfig_clear.data(),
                modemConfig_init.data(), modemConfig_hangup.data());
            m_entersound = true;
            M_Menu_SerialConfig_f();
        }

        break;

    case K_BACKSPACE:
        if (modemConfig_cursor == 1) {
            if (strlen(modemConfig_clear.data())) {
                modemConfig_clear[strlen(modemConfig_clear.data()) - 1] = 0;
            }
        }

        if (modemConfig_cursor == 2) {
            if (strlen(modemConfig_init.data())) {
                modemConfig_init[strlen(modemConfig_init.data()) - 1] = 0;
            }
        }

        if (modemConfig_cursor == 3) {
            if (strlen(modemConfig_hangup.data())) {
                modemConfig_hangup[strlen(modemConfig_hangup.data()) - 1] = 0;
            }
        }

        break;

    default:
        if (key < 32 || key > 127) {
            break;
        }

        if (modemConfig_cursor == 1) {
            l = (int)strlen(modemConfig_clear.data());
            if (l < 15) {
                modemConfig_clear[l + 1] = 0;
                modemConfig_clear[l] = static_cast<char>(key);
            }
        }

        if (modemConfig_cursor == 2) {
            l = (int)strlen(modemConfig_init.data());
            if (l < 29) {
                modemConfig_init[l + 1] = 0;
                modemConfig_init[l] = static_cast<char>(key);
            }
        }

        if (modemConfig_cursor == 3) {
            l = (int)strlen(modemConfig_hangup.data());
            if (l < 15) {
                modemConfig_hangup[l + 1] = 0;
                modemConfig_hangup[l] = static_cast<char>(key);
            }
        }
    }
}

//=============================================================================
/* LAN CONFIG MENU */

int lanConfig_cursor = -1;
const std::array<int, 3> lanConfig_cursor_table = { 72, 92, 124 };
constexpr int NUM_LANCONFIG_CMDS = 3;

int lanConfig_port;
std::string lanConfig_portname;
std::string lanConfig_joinname;

void M_Menu_LanConfig_f()
{
    key_dest = key_menu;
    m_state = MenuState::LanConfig;
    m_entersound = true;
    if (lanConfig_cursor == -1) {
        if (JoiningGame() && TCPIPConfig()) {
            lanConfig_cursor = 2;
        } else {
            lanConfig_cursor = 1;
        }
    }

    if (StartingGame() && lanConfig_cursor == 2) {
        lanConfig_cursor = 1;
    }

    lanConfig_port = DEFAULTnet_hostport;
    lanConfig_portname = std::to_string(lanConfig_port);

    m_return_onerror = false;
    m_return_reason.clear();
}

void M_LanConfig_Draw()
{
    qpic_t* p;
    int basex;
    const char* startJoin;
    const char* protocol;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/p_multi.lmp");
    basex = (320 - p->width) / 2;
    M_DrawPic(basex, 4, p);

    if (StartingGame()) {
        startJoin = "New Game";
    } else {
        startJoin = "Join Game";
    }

    if (IPXConfig()) {
        protocol = "IPX";
    } else {
        protocol = "TCP/IP";
    }

    M_Print(basex, 32, va("%s - %s", startJoin, protocol));
    basex += 8;

    M_Print(basex, 52, "Address:");
    if (IPXConfig()) {
        M_Print(basex + 9 * 8, 52, my_ipx_address);
    } else {
        M_Print(basex + 9 * 8, 52, my_tcpip_address);
    }

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

    M_DrawCharacter(basex - 8, lanConfig_cursor_table[lanConfig_cursor],
        12 + ((int)(realtime * 4) & 1));

    if (lanConfig_cursor == 0) {
        M_DrawCharacter(basex + 9 * 8 + 8 * (int)lanConfig_portname.length(),
            lanConfig_cursor_table[0], 10 + ((int)(realtime * 4) & 1));
    }

    if (lanConfig_cursor == 2) {
        M_DrawCharacter(basex + 16 + 8 * (int)lanConfig_joinname.length(),
            lanConfig_cursor_table[2], 10 + ((int)(realtime * 4) & 1));
    }

    if (!m_return_reason.empty()) {
        M_PrintWhite(basex, 148, m_return_reason);
    }
}

void M_LanConfig_Key(int key)
{
    switch (key) {
    case K_ESCAPE:
        M_Menu_Net_f();
        break;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        lanConfig_cursor--;
        if (lanConfig_cursor < 0) {
            lanConfig_cursor = NUM_LANCONFIG_CMDS - 1;
        }

        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        lanConfig_cursor++;
        if (lanConfig_cursor >= NUM_LANCONFIG_CMDS) {
            lanConfig_cursor = 0;
        }

        break;

    case K_ENTER:
        if (lanConfig_cursor == 0) {
            break;
        }

        m_entersound = true;

        M_ConfigureNetSubsystem();

        if (lanConfig_cursor == 1) {
            if (StartingGame()) {
                M_Menu_GameOptions_f();
                break;
            }

            M_Menu_Search_f();
            break;
        }

        if (lanConfig_cursor == 2) {
            m_return_state = m_state;
            m_return_onerror = true;
            key_dest = key_game;
            m_state = MenuState::None;
            Cmd::BufferAddText(va("connect \"%s\"\n", lanConfig_joinname.c_str()));
            break;
        }

        break;

    case K_BACKSPACE:
        if (lanConfig_cursor == 0) {
            if (!lanConfig_portname.empty()) {
                lanConfig_portname.pop_back();
            }
        }

        if (lanConfig_cursor == 2) {
            if (!lanConfig_joinname.empty()) {
                lanConfig_joinname.pop_back();
            }
        }

        break;

    default:
        if (key < 32 || key > 127) {
            break;
        }

        if (lanConfig_cursor == 2) {
            if (lanConfig_joinname.length() < 21) {
                lanConfig_joinname.push_back(static_cast<char>(key));
            }
        }

        if (key < '0' || key > '9') {
            break;
        }

        if (lanConfig_cursor == 0) {
            if (lanConfig_portname.length() < 5) {
                lanConfig_portname.push_back(static_cast<char>(key));
            }
        }
    }

    if (StartingGame() && lanConfig_cursor == 2) {
        if (key == K_UPARROW) {
            lanConfig_cursor = 1;
        } else {
            lanConfig_cursor = 0;
        }
    }

    int l = Q_atoi(lanConfig_portname.c_str());
    if (l <= 65535) {
        lanConfig_port = l;
    }

    lanConfig_portname = std::to_string(lanConfig_port);
}

//=============================================================================
/* GAME OPTIONS MENU */

struct level_t {
    const char* name;
    const char* description;
};

static constexpr std::array<level_t, 38> levels = {{
    { "start", "Entrance" }, // 0

    { "e1m1", "Slipgate Complex" }, // 1
    { "e1m2", "Castle of the Damned" },
    { "e1m3", "The Necropolis" },
    { "e1m4", "The Grisly Grotto" },
    { "e1m5", "Gloom Keep" },
    { "e1m6", "The Door To Chthon" },
    { "e1m7", "The House of Chthon" },
    { "e1m8", "Ziggurat Vertigo" },

    { "e2m1", "The Installation" }, // 9
    { "e2m2", "Ogre Citadel" },
    { "e2m3", "Crypt of Decay" },
    { "e2m4", "The Ebon Fortress" },
    { "e2m5", "The Wizard's Manse" },
    { "e2m6", "The Dismal Oubliette" },
    { "e2m7", "Underearth" },

    { "e3m1", "Termination Central" }, // 16
    { "e3m2", "The Vaults of Zin" },
    { "e3m3", "The Tomb of Terror" },
    { "e3m4", "Satan's Dark Delight" },
    { "e3m5", "Wind Tunnels" },
    { "e3m6", "Chambers of Torment" },
    { "e3m7", "The Haunted Halls" },

    { "e4m1", "The Sewage System" }, // 23
    { "e4m2", "The Tower of Despair" },
    { "e4m3", "The Elder God Shrine" },
    { "e4m4", "The Palace of Hate" },
    { "e4m5", "Hell's Atrium" },
    { "e4m6", "The Pain Maze" },
    { "e4m7", "Azure Agony" },
    { "e4m8", "The Nameless City" },

    { "end", "Shub-Niggurath's Pit" }, // 31

    { "dm1", "Place of Two Deaths" }, // 32
    { "dm2", "Claustrophobopolis" },
    { "dm3", "The Abandoned Base" },
    { "dm4", "The Bad Place" },
    { "dm5", "The Cistern" },
    { "dm6", "The Dark Zone" }
}};

static constexpr std::array<level_t, 18> hipnoticlevels = {{
    { "start", "Command HQ" }, // 0

    { "hip1m1", "The Pumping Station" }, // 1
    { "hip1m2", "Storage Facility" },
    { "hip1m3", "The Lost Mine" },
    { "hip1m4", "Research Facility" },
    { "hip1m5", "Military Complex" },

    { "hip2m1", "Ancient Realms" }, // 6
    { "hip2m2", "The Black Cathedral" },
    { "hip2m3", "The Catacombs" },
    { "hip2m4", "The Crypt" },
    { "hip2m5", "Mortum's Keep" },
    { "hip2m6", "The Gremlin's Domain" },

    { "hip3m1", "Tur Torment" }, // 12
    { "hip3m2", "Pandemonium" },
    { "hip3m3", "Limbo" },
    { "hip3m4", "The Gauntlet" },

    { "hipend", "Armagon's Lair" }, // 16

    { "hipdm1", "The Edge of Oblivion" } // 17
}};

static constexpr std::array<level_t, 17> roguelevels = {{
    { "start", "Split Decision" }, { "r1m1", "Deviant's Domain" },
    { "r1m2", "Dread Portal" }, { "r1m3", "Judgement Call" },
    { "r1m4", "Cave of Death" }, { "r1m5", "Towers of Wrath" },
    { "r1m6", "Temple of Pain" }, { "r1m7", "Tomb of the Overlord" },
    { "r2m1", "Tempus Fugit" }, { "r2m2", "Elemental Fury I" },
    { "r2m3", "Elemental Fury II" }, { "r2m4", "Curse of Osiris" },
    { "r2m5", "Wizard's Keep" }, { "r2m6", "Blood Sacrifice" },
    { "r2m7", "Last Bastion" }, { "r2m8", "Source of Evil" },
    { "ctf1", "Division of Change" }
}};

struct episode_t {
    const char* description;
    int firstLevel;
    int levels;
};

static constexpr std::array<episode_t, 7> episodes = {{
    { "Welcome to Quake", 0, 1 }, { "Doomed Dimension", 1, 8 },
    { "Realm of Black Magic", 9, 7 }, { "Netherworld", 16, 7 },
    { "The Elder World", 23, 8 }, { "Final Level", 31, 1 },
    { "Deathmatch Arena", 32, 6 }
}};

static constexpr std::array<episode_t, 6> hipnoticepisodes = {{
    { "Scourge of Armagon", 0, 1 }, { "Fortress of the Dead", 1, 5 },
    { "Dominion of Darkness", 6, 6 }, { "The Rift", 12, 4 },
    { "Final Level", 16, 1 }, { "Deathmatch Arena", 17, 1 }
}};

static constexpr std::array<episode_t, 4> rogueepisodes = {{
    { "Introduction", 0, 1 },
    { "Hell's Fortress", 1, 7 },
    { "Corridors of Time", 8, 8 },
    { "Deathmatch Arena", 16, 1 }
}};

int startepisode;
int startlevel;
int maxplayers;
bool m_serverInfoMessage = false;
double m_serverInfoMessageTime;

void M_Menu_GameOptions_f()
{
    key_dest = key_menu;
    m_state = MenuState::GameOptions;
    m_entersound = true;
    if (maxplayers == 0) {
        maxplayers = svs.maxclients;
    }

    if (maxplayers < 2) {
        maxplayers = svs.maxclientslimit;
    }
}

const std::array<int, 9> gameoptions_cursor_table = { 40, 56, 64, 72, 80, 88, 96, 112, 120 };
constexpr int NUM_GAMEOPTIONS = 9;
int gameoptions_cursor;

void M_GameOptions_Draw()
{
    qpic_t* p;
    int x;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/p_multi.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);

    M_DrawTextBox(152, 32, 10, 1);
    M_Print(160, 40, "begin game");

    M_Print(0, 56, "      Max players");
    M_Print(160, 56, va("%i", maxplayers));

    M_Print(0, 64, "        Game Type");
    if (coop.value) {
        M_Print(160, 64, "Cooperative");
    } else {
        M_Print(160, 64, "Deathmatch");
    }

    M_Print(0, 72, "        Teamplay");
    if (rogue) {
        const char* msg;

        switch ((int)teamplay.value) {
        case 1:
            msg = "No Friendly Fire";
            break;
        case 2:
            msg = "Friendly Fire";
            break;
        case 3:
            msg = "Tag";
            break;
        case 4:
            msg = "Capture the Flag";
            break;
        case 5:
            msg = "One Flag CTF";
            break;
        case 6:
            msg = "Three Team CTF";
            break;
        default:
            msg = "Off";
            break;
        }
        M_Print(160, 72, msg);
    } else {
        const char* msg;

        switch ((int)teamplay.value) {
        case 1:
            msg = "No Friendly Fire";
            break;
        case 2:
            msg = "Friendly Fire";
            break;
        default:
            msg = "Off";
            break;
        }
        M_Print(160, 72, msg);
    }

    M_Print(0, 80, "            Skill");
    if (skill.value == 0) {
        M_Print(160, 80, "Easy difficulty");
    } else if (skill.value == 1) {
        M_Print(160, 80, "Normal difficulty");
    } else if (skill.value == 2) {
        M_Print(160, 80, "Hard difficulty");
    } else {
        M_Print(160, 80, "Nightmare difficulty");
    }

    M_Print(0, 88, "       Frag Limit");
    if (fraglimit.value == 0) {
        M_Print(160, 88, "none");
    } else {
        M_Print(160, 88, va("%i frags", (int)fraglimit.value));
    }

    M_Print(0, 96, "       Time Limit");
    if (timelimit.value == 0) {
        M_Print(160, 96, "none");
    } else {
        M_Print(160, 96, va("%i minutes", (int)timelimit.value));
    }

    M_Print(0, 112, "         Episode");
    if (hipnotic) {
        M_Print(160, 112, hipnoticepisodes[startepisode].description);
    }
    else if (rogue) {
        M_Print(160, 112, rogueepisodes[startepisode].description);
    } else {
        M_Print(160, 112, episodes[startepisode].description);
    }

    M_Print(0, 120, "           Level");
    if (hipnotic) {
        M_Print(
            160, 120,
            hipnoticlevels[hipnoticepisodes[startepisode].firstLevel + startlevel]
                .description);
        M_Print(
            160, 128,
            hipnoticlevels[hipnoticepisodes[startepisode].firstLevel + startlevel]
                .name);
    }
    else if (rogue) {
        M_Print(160, 120,
            roguelevels[rogueepisodes[startepisode].firstLevel + startlevel]
                .description);
        M_Print(
            160, 128,
            roguelevels[rogueepisodes[startepisode].firstLevel + startlevel].name);
    } else {
        M_Print(160, 120,
            levels[episodes[startepisode].firstLevel + startlevel].description);
        M_Print(160, 128,
            levels[episodes[startepisode].firstLevel + startlevel].name);
    }

    // line cursor
    M_DrawCharacter(144, gameoptions_cursor_table[gameoptions_cursor],
        12 + ((int)(realtime * 4) & 1));

    if (m_serverInfoMessage) {
        if ((realtime - m_serverInfoMessageTime) < 5.0) {
            x = (320 - 26 * 8) / 2;
            M_DrawTextBox(x, 138, 24, 4);
            x += 8;
            M_Print(x, 146, "  More than 4 players   ");
            M_Print(x, 154, " requires using command ");
            M_Print(x, 162, "line parameters; please ");
            M_Print(x, 170, "   see techinfo.txt.    ");
        } else {
            m_serverInfoMessage = false;
        }
    }
}

void M_NetStart_Change(int dir)
{
    int count;

    switch (gameoptions_cursor) {
    case 1:
        maxplayers += dir;
        if (maxplayers > svs.maxclientslimit) {
            maxplayers = svs.maxclientslimit;
            m_serverInfoMessage = true;
            m_serverInfoMessageTime = realtime;
        }

        if (maxplayers < 2) {
            maxplayers = 2;
        }

        break;

    case 2:
        Cvar::SetValue("coop", static_cast<float>(coop.value ? 0 : 1));
        break;

    case 3:
        if (rogue) {
            count = 6;
        } else {
            count = 2;
        }

        Cvar::SetValue("teamplay", static_cast<float>(teamplay.value + dir));
        if (teamplay.value > count) {
            Cvar::SetValue("teamplay", static_cast<float>(0));
        } else if (teamplay.value < 0) {
            Cvar::SetValue("teamplay", static_cast<float>(count));
        }

        break;

    case 4:
        Cvar::SetValue("skill", skill.value + dir);
        if (skill.value > 3) {
            Cvar::SetValue("skill", static_cast<float>(0));
        }

        if (skill.value < 0) {
            Cvar::SetValue("skill", static_cast<float>(3));
        }

        break;

    case 5:
        Cvar::SetValue("fraglimit", static_cast<float>(fraglimit.value + dir * 10));
        if (fraglimit.value > 100) {
            Cvar::SetValue("fraglimit", static_cast<float>(0));
        }

        if (fraglimit.value < 0) {
            Cvar::SetValue("fraglimit", static_cast<float>(100));
        }

        break;

    case 6:
        Cvar::SetValue("timelimit", timelimit.value + dir * 5);
        if (timelimit.value > 60) {
            Cvar::SetValue("timelimit", static_cast<float>(0));
        }

        if (timelimit.value < 0) {
            Cvar::SetValue("timelimit", static_cast<float>(60));
        }

        break;

    case 7:
        startepisode += dir;
        if (hipnotic) {
            count = 6;
        }
        else if (rogue) {
            count = 4;
        } else if (registered.value) {
            count = 7;
        } else {
            count = 2;
        }

        if (startepisode < 0) {
            startepisode = count - 1;
        }

        if (startepisode >= count) {
            startepisode = 0;
        }

        startlevel = 0;
        break;

    case 8:
        startlevel += dir;
        if (hipnotic) {
            count = hipnoticepisodes[startepisode].levels;
        }
        else if (rogue) {
            count = rogueepisodes[startepisode].levels;
        } else {
            count = episodes[startepisode].levels;
        }

        if (startlevel < 0) {
            startlevel = count - 1;
        }

        if (startlevel >= count) {
            startlevel = 0;
        }

        break;
    }
}

void M_GameOptions_Key(int key)
{
    switch (key) {
    case K_ESCAPE:
        M_Menu_Net_f();
        break;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        gameoptions_cursor--;
        if (gameoptions_cursor < 0) {
            gameoptions_cursor = NUM_GAMEOPTIONS - 1;
        }

        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        gameoptions_cursor++;
        if (gameoptions_cursor >= NUM_GAMEOPTIONS) {
            gameoptions_cursor = 0;
        }

        break;

    case K_LEFTARROW:
        if (gameoptions_cursor == 0) {
            break;
        }

        S_LocalSound("misc/menu3.wav");
        M_NetStart_Change(-1);
        break;

    case K_RIGHTARROW:
        if (gameoptions_cursor == 0) {
            break;
        }

        S_LocalSound("misc/menu3.wav");
        M_NetStart_Change(1);
        break;

    case K_ENTER:
        S_LocalSound("misc/menu2.wav");
        if (gameoptions_cursor == 0) {
            if (sv.active) {
                Cmd::BufferAddText("disconnect\n");
            }

            Cmd::BufferAddText("listen 0\n"); // so host_netport will be re-examined
            Cmd::BufferAddText(va("maxplayers %u\n", maxplayers));
            Screen::GetScreenSystem().BeginLoadingPlaque();

            if (hipnotic) {
                Cmd::BufferAddText(
                    va("map %s\n",
                        hipnoticlevels[hipnoticepisodes[startepisode].firstLevel + startlevel]
                            .name));
            } else if (rogue) {
                Cmd::BufferAddText(va(
                    "map %s\n",
                    roguelevels[rogueepisodes[startepisode].firstLevel + startlevel]
                        .name));
            } else {
                Cmd::BufferAddText(
                    va("map %s\n",
                        levels[episodes[startepisode].firstLevel + startlevel].name));
            }

            return;
        }

        M_NetStart_Change(1);
        break;
    }
}

//=============================================================================
/* SEARCH MENU */

bool searchComplete = false;
double searchCompleteTime;

void M_Menu_Search_f()
{
    key_dest = key_menu;
    m_state = MenuState::Search;
    m_entersound = false;
    slistSilent = true;
    slistLocal = false;
    searchComplete = false;
    NET_Slist_f();
}

void M_Search_Draw()
{
    qpic_t* p;
    int x;

    p = Draw_CachePic("gfx/p_multi.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);
    x = (320 / 2) - ((12 * 8) / 2) + 4;
    M_DrawTextBox(x - 8, 32, 12, 1);
    M_Print(x, 40, "Searching...");

    if (slistInProgress) {
        NET_Poll();

        return;
    }

    if (!searchComplete) {
        searchComplete = true;
        searchCompleteTime = realtime;
    }

    if (hostCacheCount) {
        M_Menu_ServerList_f();

        return;
    }

    M_PrintWhite((320 / 2) - ((22 * 8) / 2), 64, "No Quake servers found");
    if ((realtime - searchCompleteTime) < 3.0) {
        return;
    }

    M_Menu_LanConfig_f();
}

void M_Search_Key()
{
}

//=============================================================================
/* SLIST MENU */

int slist_cursor;
bool slist_sorted;

void M_Menu_ServerList_f()
{
    key_dest = key_menu;
    m_state = MenuState::SList;
    m_entersound = true;
    slist_cursor = 0;
    m_return_onerror = false;
    m_return_reason.clear();
    slist_sorted = false;
}

void M_ServerList_Draw()
{
    int n;
    char string[64];
    qpic_t* p;

    if (!slist_sorted) {
        if (hostCacheCount > 1) {
            int i, j;
            hostcache_t temp;
            for (i = 0; i < hostCacheCount; i++) {
                for (j = i + 1; j < hostCacheCount; j++) {
                    if (strcmp(hostcache[j].name, hostcache[i].name) < 0) {
                        Q_memcpy(&temp, &hostcache[j], sizeof(hostcache_t));
                        Q_memcpy(&hostcache[j], &hostcache[i], sizeof(hostcache_t));
                        Q_memcpy(&hostcache[i], &temp, sizeof(hostcache_t));
                    }
                }
            }
        }

        slist_sorted = true;
    }

    p = Draw_CachePic("gfx/p_multi.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);
    for (n = 0; n < hostCacheCount; n++) {
        if (hostcache[n].maxusers) {
            sprintf_s(string, sizeof(string), "%-15.15s %-15.15s %2u/%2u\n", hostcache[n].name,
                hostcache[n].map, hostcache[n].users, hostcache[n].maxusers);
        } else {
            sprintf_s(string, sizeof(string), "%-15.15s %-15.15s\n", hostcache[n].name,
                hostcache[n].map);
        }

        M_Print(16, 32 + 8 * n, string);
    }
    M_DrawCharacter(0, 32 + slist_cursor * 8, 12 + ((int)(realtime * 4) & 1));

    if (!m_return_reason.empty()) {
        M_PrintWhite(16, 148, m_return_reason);
    }
}

void M_ServerList_Key(int k)
{
    switch (k) {
    case K_ESCAPE:
        M_Menu_LanConfig_f();
        break;

    case K_SPACE:
        M_Menu_Search_f();
        break;

    case K_UPARROW:
    case K_LEFTARROW:
        S_LocalSound("misc/menu1.wav");
        slist_cursor--;
        if (slist_cursor < 0) {
            slist_cursor = hostCacheCount - 1;
        }

        break;

    case K_DOWNARROW:
    case K_RIGHTARROW:
        S_LocalSound("misc/menu1.wav");
        slist_cursor++;
        if (slist_cursor >= hostCacheCount) {
            slist_cursor = 0;
        }

        break;

    case K_ENTER:
        S_LocalSound("misc/menu2.wav");
        m_return_state = m_state;
        m_return_onerror = true;
        slist_sorted = false;
        key_dest = key_game;
        m_state = MenuState::None;
        Cmd::BufferAddText(va("connect \"%s\"\n", hostcache[slist_cursor].cname));
        break;

    default:
        break;
    }
}

//=============================================================================
/* Menu Subsystem */

void M_Init()
{
    Cmd::AddCommand("togglemenu", M_ToggleMenu_f);

    Cmd::AddCommand("menu_main", M_Menu_Main_f);
    Cmd::AddCommand("menu_singleplayer", M_Menu_SinglePlayer_f);
    Cmd::AddCommand("menu_load", M_Menu_Load_f);
    Cmd::AddCommand("menu_save", M_Menu_Save_f);
    Cmd::AddCommand("menu_multiplayer", M_Menu_MultiPlayer_f);
    Cmd::AddCommand("menu_setup", M_Menu_Setup_f);
    Cmd::AddCommand("menu_options", M_Menu_Options_f);
    Cmd::AddCommand("menu_keys", M_Menu_Keys_f);
    Cmd::AddCommand("menu_video", M_Menu_Video_f);
    Cmd::AddCommand("help", M_Menu_Help_f);
    Cmd::AddCommand("menu_quit", M_Menu_Quit_f);
}

void M_Draw()
{
    if (m_state == MenuState::None || key_dest != key_menu) {
        return;
    }

    if (!m_recursiveDraw) {
        Screen::GetScreenSystem().SetCopyeverything(1);

        if (Screen::GetScreenSystem().GetConCurrent()) {
            Draw_ConsoleBackground(vid.height);
            VID_UnlockBuffer();
            S_ExtraUpdate();
            VID_LockBuffer();
        } else {
            Draw_FadeScreen();
        }

        Screen::GetScreenSystem().SetFullupdate(0);
    } else {
        m_recursiveDraw = false;
    }

    switch (m_state) {
    case MenuState::None:
        break;

    case MenuState::Main:
        M_Main_Draw();
        break;

    case MenuState::SinglePlayer:
        M_SinglePlayer_Draw();
        break;

    case MenuState::Load:
        M_Load_Draw();
        break;

    case MenuState::Save:
        M_Save_Draw();
        break;

    case MenuState::MultiPlayer:
        M_MultiPlayer_Draw();
        break;

    case MenuState::Setup:
        M_Setup_Draw();
        break;

    case MenuState::Net:
        M_Net_Draw();
        break;

    case MenuState::Options:
        M_Options_Draw();
        break;

    case MenuState::Keys:
        M_Keys_Draw();
        break;

    case MenuState::Video:
        M_Video_Draw();
        break;

    case MenuState::Help:
        M_Help_Draw();
        break;

    case MenuState::SerialConfig:
        M_SerialConfig_Draw();
        break;

    case MenuState::ModemConfig:
        M_ModemConfig_Draw();
        break;

    case MenuState::LanConfig:
        M_LanConfig_Draw();
        break;

    case MenuState::GameOptions:
        M_GameOptions_Draw();
        break;

    case MenuState::Search:
        M_Search_Draw();
        break;

    case MenuState::SList:
        M_ServerList_Draw();
        break;
    }

    if (m_entersound) {
        S_LocalSound("misc/menu2.wav");
        m_entersound = false;
    }

    VID_UnlockBuffer();
    S_ExtraUpdate();
    VID_LockBuffer();
}

void M_Keydown(int key)
{
    switch (m_state) {
    case MenuState::None:
        return;

    case MenuState::Main:
        M_Main_Key(key);
        return;

    case MenuState::SinglePlayer:
        M_SinglePlayer_Key(key);
        return;

    case MenuState::Load:
        M_Load_Key(key);
        return;

    case MenuState::Save:
        M_Save_Key(key);
        return;

    case MenuState::MultiPlayer:
        M_MultiPlayer_Key(key);
        return;

    case MenuState::Setup:
        M_Setup_Key(key);
        return;

    case MenuState::Net:
        M_Net_Key(key);
        return;

    case MenuState::Options:
        M_Options_Key(key);
        return;

    case MenuState::Keys:
        M_Keys_Key(key);
        return;

    case MenuState::Video:
        M_Video_Key(key);
        return;

    case MenuState::Help:
        M_Help_Key(key);
        return;

    case MenuState::SerialConfig:
        M_SerialConfig_Key(key);
        return;

    case MenuState::ModemConfig:
        M_ModemConfig_Key(key);
        return;

    case MenuState::LanConfig:
        M_LanConfig_Key(key);
        return;

    case MenuState::GameOptions:
        M_GameOptions_Key(key);
        return;

    case MenuState::Search:
        M_Search_Key();
        break;

    case MenuState::SList:
        M_ServerList_Key(key);
        return;
    }
}

void M_ConfigureNetSubsystem()
{
    // enable/disable net systems to match desired config

    Cmd::BufferAddText("stopdemo\n");
    if (SerialConfig() || DirectConfig()) {
        Cmd::BufferAddText("com1 enable\n");
    }

    if (IPXConfig() || TCPIPConfig()) {
        net_hostport = lanConfig_port;
    }
}

} // namespace Menu
