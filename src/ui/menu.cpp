// menu.cpp -- Quake in-game menu system & screens
#include "quakedef.hpp"
#include "ui/menu.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <EASTL/sort.h>

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

// ============================================================================
// MENU SUBSYSTEM (Modernized & Table-Driven LoC Reduction)
// ============================================================================

namespace Menu {

struct CmdPair { const char* name; void (*fn)(); };

MenuState m_state = MenuState::None, m_return_state = MenuState::None;
bool m_return_onerror = false;
eastl::string m_return_reason;
bool m_entersound = false, m_recursiveDraw = false;

int m_multiplayer_cursor = 0, m_save_demonum = 0;
int m_main_cursor = 0, m_singleplayer_cursor = 0, load_cursor = 0;
int setup_cursor = 4, setup_oldtop = 0, setup_oldbottom = 0, setup_top = 0, setup_bottom = 0;
int options_cursor = 0, keys_cursor = 0;
bool bind_grab = false; int help_page = 0;

int lanConfig_cursor = -1, lanConfig_port = 0;
eastl::string lanConfig_portname, lanConfig_joinname, setup_hostname, setup_myname;

int startepisode = 0, startlevel = 0, maxplayers = 0, gameoptions_cursor = 0;
bool m_serverInfoMessage = false; double m_serverInfoMessageTime = 0.0;
bool searchComplete = false; double searchCompleteTime = 0.0;
int slist_cursor = 0; bool slist_sorted = false;

constexpr int MAX_SAVEGAMES = 12;
eastl::array<eastl::string, MAX_SAVEGAMES> m_filenames;
eastl::array<bool, MAX_SAVEGAMES> loadable;
eastl::array<byte, 256> identityTable{}, translationTable{};

inline bool StartingGame() { return m_multiplayer_cursor == 1; }
inline bool JoiningGame()  { return m_multiplayer_cursor == 0; }

void M_ConfigureNetSubsystem();

inline void M_DrawCharacter(int cx, int line, int num) { Draw_Character(cx + ((vid.width - 320) >> 1), line, num); }
void M_Print(int cx, int cy, eastl::string_view str) { for (char c : str) { M_DrawCharacter(cx, cy, static_cast<unsigned char>(c) + 128); cx += 8; } }
void M_PrintWhite(int cx, int cy, eastl::string_view str) { for (char c : str) { M_DrawCharacter(cx, cy, static_cast<unsigned char>(c)); cx += 8; } }
inline void M_DrawTransPic(int x, int y, qpic_t* pic) { Draw_TransPic(x + ((vid.width - 320) >> 1), y, pic); }
void M_DrawPic(int x, int y, qpic_t* pic) { Draw_Pic(x + ((vid.width - 320) >> 1), y, pic); }

void M_BuildTranslationTable(int top, int bottom) {
    for (int j = 0; j < 256; j++) identityTable[j] = static_cast<byte>(j);
    translationTable = identityTable;
    if (top < 128) eastl::copy_n(identityTable.begin() + top, 16, translationTable.begin() + TOP_RANGE);
    else for (int j = 0; j < 16; j++) translationTable[TOP_RANGE + j] = identityTable[top + 15 - j];
    if (bottom < 128) eastl::copy_n(identityTable.begin() + bottom, 16, translationTable.begin() + BOTTOM_RANGE);
    else for (int j = 0; j < 16; j++) translationTable[BOTTOM_RANGE + j] = identityTable[bottom + 15 - j];
}

inline void M_DrawTransPicTranslate(int x, int y, qpic_t* pic) { Draw_TransPicTranslate(x + ((vid.width - 320) >> 1), y, pic, translationTable.data()); }

inline void M_DrawTextBox(int x, int y, int width, int lines) {
    qpic_t* p = Draw_CachePic("gfx/box_tl.lmp"); int cx = x, cy = y; M_DrawTransPic(cx, cy, p);
    p = Draw_CachePic("gfx/box_ml.lmp"); for (int n = 0; n < lines; n++) { cy += 8; M_DrawTransPic(cx, cy, p); }
    M_DrawTransPic(cx, cy + 8, Draw_CachePic("gfx/box_bl.lmp")); cx += 8;
    while (width > 0) {
        cy = y; M_DrawTransPic(cx, cy, Draw_CachePic("gfx/box_tm.lmp"));
        for (int n = 0; n < lines; n++) { cy += 8; M_DrawTransPic(cx, cy, Draw_CachePic("gfx/box_mm2.lmp")); }
        M_DrawTransPic(cx, cy + 8, Draw_CachePic("gfx/box_bm.lmp")); width -= 2; cx += 16;
    }
    cy = y; M_DrawTransPic(cx, cy, Draw_CachePic("gfx/box_tr.lmp"));
    p = Draw_CachePic("gfx/box_mr.lmp"); for (int n = 0; n < lines; n++) { cy += 8; M_DrawTransPic(cx, cy, p); }
    M_DrawTransPic(cx, cy + 8, Draw_CachePic("gfx/box_br.lmp"));
}

static inline bool HandleNavKeys(int key, int& cursor, int max_items, const char* snd = "misc/menu1.wav") {
    if (key == K_DOWNARROW || key == K_RIGHTARROW) { S_LocalSound(snd); cursor = (cursor + 1) % max_items; return true; }
    if (key == K_UPARROW || key == K_LEFTARROW) { S_LocalSound(snd); cursor = (cursor - 1 + max_items) % max_items; return true; }
    return false;
}

static inline void DrawMenuHeader(const char* title_pic) {
    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    qpic_t* p = Draw_CachePic(title_pic); M_DrawPic((320 - p->width) / 2, 4, p);
}

static inline void DrawMenuDot(int x, int y, int cursor) {
    M_DrawTransPic(x, y + cursor * 20, Draw_CachePic(va("gfx/menudot%i.lmp", (int)(host_time * 10) % 6 + 1)));
}

static inline void DrawLineCursor(int x, int y_start, int cursor, int step = 8) {
    M_DrawCharacter(x, y_start + cursor * step, 12 + ((int)(realtime * 4) & 1));
}

void M_Menu_Main_f(); void M_Menu_SinglePlayer_f(); void M_Menu_Load_f(); void M_Menu_Save_f();
void M_Menu_MultiPlayer_f(); void M_Menu_Setup_f(); void M_Menu_Options_f();
void M_Menu_Keys_f(); void M_Menu_Help_f(); void M_Menu_Quit_f();
void M_Menu_LanConfig_f();
void M_Menu_GameOptions_f(); void M_Menu_Search_f(); void M_Menu_ServerList_f();

void M_ToggleMenu_f() {
    m_entersound = true;
    if (key_dest == key_menu) {
        if (m_state != MenuState::Main) { M_Menu_Main_f(); return; }
        key_dest = key_game; m_state = MenuState::None; return;
    }
    if (key_dest == key_console) ConsoleSystem::ToggleConsole_f(); else M_Menu_Main_f();
}

void M_Menu_Main_f() { if (key_dest != key_menu) { m_save_demonum = cls.demonum; cls.demonum = -1; } key_dest = key_menu; m_state = MenuState::Main; m_entersound = true; }
void M_Main_Draw() { DrawMenuHeader("gfx/ttl_main.lmp"); M_DrawTransPic(72, 32, Draw_CachePic("gfx/mainmenu.lmp")); DrawMenuDot(54, 32, m_main_cursor); }
void M_Main_Key(int key) {
    if (key == K_ESCAPE) { key_dest = key_game; m_state = MenuState::None; cls.demonum = m_save_demonum; if (cls.demonum != -1 && !cls.demoplayback && cls.state != ca_connected) CL_NextDemo(); return; }
    if (HandleNavKeys(key, m_main_cursor, 5)) return;
    if (key == K_ENTER) {
        m_entersound = true;
        switch (m_main_cursor) {
        case 0: M_Menu_SinglePlayer_f(); break; case 1: M_Menu_MultiPlayer_f(); break;
        case 2: M_Menu_Options_f(); break; case 3: M_Menu_Help_f(); break; case 4: M_Menu_Quit_f(); break;
        }
    }
}

void M_Menu_SinglePlayer_f() { key_dest = key_menu; m_state = MenuState::SinglePlayer; m_entersound = true; }
void M_SinglePlayer_Draw() { DrawMenuHeader("gfx/ttl_sgl.lmp"); M_DrawTransPic(72, 32, Draw_CachePic("gfx/sp_menu.lmp")); DrawMenuDot(54, 32, m_singleplayer_cursor); }
void M_SinglePlayer_Key(int key) {
    if (key == K_ESCAPE) { M_Menu_Main_f(); return; }
    if (HandleNavKeys(key, m_singleplayer_cursor, 3)) return;
    if (key == K_ENTER) {
        m_entersound = true;
        switch (m_singleplayer_cursor) {
        case 0:
            if (sv.active && !Screen::GetScreenSystem().ModalMessage("Are you sure you want to\nstart a new game?\n")) break;
            key_dest = key_game; if (sv.active) Cmd::BufferAddText("disconnect\n");
            Cmd::BufferAddText("maxplayers 1\nmap start\n"); break;
        case 1: M_Menu_Load_f(); break; case 2: M_Menu_Save_f(); break;
        }
    }
}

void M_ScanSaves() {
    for (int i = 0; i < MAX_SAVEGAMES; i++) {
        m_filenames[i] = "--- UNUSED SLOT ---"; loadable[i] = false;
        char name[MAX_OSPATH]; sprintf_s(name, sizeof(name), "%s/s%i.sav", com_gamedir, i);
        std::ifstream f(name); if (!f.is_open()) continue;
        int version = 0; if (!(f >> version)) continue;
        f.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::string temp_comment; if (!std::getline(f, temp_comment)) continue;
        eastl::string comment = temp_comment.c_str();
        if (!comment.empty() && comment.back() == '\r') comment.pop_back();
        for (char& c : comment) { if (c == '_') c = ' '; }
        if (comment.length() > SAVEGAME_COMMENT_LENGTH) comment = comment.substr(0, SAVEGAME_COMMENT_LENGTH);
        m_filenames[i] = comment; loadable[i] = true;
    }
}

void M_Menu_Load_f() { m_entersound = true; m_state = MenuState::Load; key_dest = key_menu; M_ScanSaves(); }
void M_Menu_Save_f() { if (!sv.active || cl.intermission || svs.maxclients != 1) return; m_entersound = true; m_state = MenuState::Save; key_dest = key_menu; M_ScanSaves(); }

static inline void DrawSaveLoadCommon(const char* pic) {
    qpic_t* p = Draw_CachePic(pic); M_DrawPic((320 - p->width) / 2, 4, p);
    for (int i = 0; i < MAX_SAVEGAMES; i++) M_Print(16, 32 + 8 * i, m_filenames[i]);
    DrawLineCursor(8, 32, load_cursor);
}

void M_Load_Draw() { DrawSaveLoadCommon("gfx/p_load.lmp"); }
void M_Save_Draw() { DrawSaveLoadCommon("gfx/p_save.lmp"); }

void M_Load_Key(int k) {
    if (k == K_ESCAPE) { M_Menu_SinglePlayer_f(); return; }
    if (HandleNavKeys(k, load_cursor, MAX_SAVEGAMES)) return;
    if (k == K_ENTER) {
        S_LocalSound("misc/menu2.wav"); if (!loadable[load_cursor]) return;
        m_state = MenuState::None; key_dest = key_game; Screen::GetScreenSystem().BeginLoadingPlaque();
        Cmd::BufferAddText(va("load s%i\n", load_cursor));
    }
}

void M_Save_Key(int k) {
    if (k == K_ESCAPE) { M_Menu_SinglePlayer_f(); return; }
    if (HandleNavKeys(k, load_cursor, MAX_SAVEGAMES)) return;
    if (k == K_ENTER) { m_state = MenuState::None; key_dest = key_game; Cmd::BufferAddText(va("save s%i\n", load_cursor)); }
}

void M_Menu_MultiPlayer_f() { key_dest = key_menu; m_state = MenuState::MultiPlayer; m_entersound = true; }
void M_MultiPlayer_Draw() {
    DrawMenuHeader("gfx/p_multi.lmp"); M_DrawTransPic(72, 32, Draw_CachePic("gfx/mp_menu.lmp")); DrawMenuDot(54, 32, m_multiplayer_cursor);
    if (!tcpipAvailable) M_PrintWhite((320 - 27 * 8) / 2, 148, "No Communications Available");
}
void M_MultiPlayer_Key(int key) {
    if (key == K_ESCAPE) { M_Menu_Main_f(); return; }
    if (HandleNavKeys(key, m_multiplayer_cursor, 3)) return;
    if (key == K_ENTER) {
        m_entersound = true;
        if (m_multiplayer_cursor == 2) M_Menu_Setup_f();
        else if (tcpipAvailable) M_Menu_LanConfig_f();
    }
}

constexpr auto setup_cursor_table = eastl::array{ 40, 56, 80, 104, 140 };

void M_Menu_Setup_f() {
    key_dest = key_menu; m_state = MenuState::Setup; m_entersound = true;
    setup_myname = cl_name.string; setup_hostname = hostname.string;
    setup_top = setup_oldtop = ((int)cl_color.value) >> 4;
    setup_bottom = setup_oldbottom = ((int)cl_color.value) & 15;
}

void M_Setup_Draw() {
    DrawMenuHeader("gfx/p_multi.lmp");
    M_Print(64, 40, "Hostname"); M_DrawTextBox(160, 32, 16, 1); M_Print(168, 40, setup_hostname.c_str());
    M_Print(64, 56, "Your name"); M_DrawTextBox(160, 48, 16, 1); M_Print(168, 56, setup_myname.c_str());
    M_Print(64, 80, "Shirt color"); M_Print(64, 104, "Pants color");
    M_DrawTextBox(64, 132, 14, 1); M_Print(72, 140, "Accept Changes");
    M_DrawTransPic(160, 64, Draw_CachePic("gfx/bigbox.lmp"));
    M_BuildTranslationTable(setup_top * 16, setup_bottom * 16);
    M_DrawTransPicTranslate(172, 72, Draw_CachePic("gfx/menuplyr.lmp"));
    M_DrawCharacter(56, setup_cursor_table[setup_cursor], 12 + ((int)(realtime * 4) & 1));
    if (setup_cursor == 0) M_DrawCharacter(168 + 8 * (int)setup_hostname.length(), setup_cursor_table[0], 10 + ((int)(realtime * 4) & 1));
    if (setup_cursor == 1) M_DrawCharacter(168 + 8 * (int)setup_myname.length(), setup_cursor_table[1], 10 + ((int)(realtime * 4) & 1));
}

void M_Setup_Key(int k) {
    if (k == K_ESCAPE) { M_Menu_MultiPlayer_f(); return; }
    if (k == K_UPARROW)   { S_LocalSound("misc/menu1.wav"); setup_cursor = (setup_cursor - 1 + 5) % 5; return; }
    if (k == K_DOWNARROW) { S_LocalSound("misc/menu1.wav"); setup_cursor = (setup_cursor + 1) % 5; return; }
    auto AdjColor = [](int dir, int& color) { S_LocalSound("misc/menu3.wav"); color = (color + dir + 14) % 14; };
    if (k == K_LEFTARROW) { if (setup_cursor == 2) AdjColor(-1, setup_top); if (setup_cursor == 3) AdjColor(-1, setup_bottom); return; }
    if (k == K_RIGHTARROW) { if (setup_cursor == 2) AdjColor(1, setup_top); if (setup_cursor == 3) AdjColor(1, setup_bottom); return; }
    if (k == K_ENTER) {
        if (setup_cursor == 2) AdjColor(1, setup_top);
        else if (setup_cursor == 3) AdjColor(1, setup_bottom);
        else if (setup_cursor == 4) {
            if (cl_name.string != setup_myname) Cmd::BufferAddText(va("name \"%s\"\n", setup_myname.c_str()));
            if (hostname.string != setup_hostname) Cvar::Set("hostname", setup_hostname.c_str());
            if (setup_top != setup_oldtop || setup_bottom != setup_oldbottom) Cmd::BufferAddText(va("color %i %i\n", setup_top, setup_bottom));
            m_entersound = true; M_Menu_MultiPlayer_f();
        }
        return;
    }
    if (k == K_BACKSPACE) {
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
void M_Menu_Options_f() { key_dest = key_menu; m_state = MenuState::Options; m_entersound = true; }

void M_AdjustSliders(int dir) {
    S_LocalSound("misc/menu3.wav");
    switch (options_cursor) {
    case 3: Cvar::SetValue("viewsize", eastl::clamp<float>(Screen::GetScreenSystem().GetViewsize().value + dir * 10, 30.0f, 120.0f)); break;
    case 4: Cvar::SetValue("gamma", eastl::clamp<float>(v_gamma.value - static_cast<float>(dir * 0.05), 0.5f, 1.0f)); break;
    case 5: Cvar::SetValue("sensitivity", eastl::clamp<float>(sensitivity.value + static_cast<float>(dir * 0.5), 1.0f, 11.0f)); break;
    case 6: Cvar::SetValue("bgmvolume", eastl::clamp<float>(bgmvolume.value + static_cast<float>(dir * 0.1), 0.0f, 1.0f)); break;
    case 7: Cvar::SetValue("volume", eastl::clamp<float>(volume.value + static_cast<float>(dir * 0.1), 0.0f, 1.0f)); break;
    case 8: Cvar::SetValue("cl_forwardspeed", (cl_forwardspeed.value > 200) ? 200.0f : 400.0f); Cvar::SetValue("cl_backspeed", (cl_forwardspeed.value > 200) ? 200.0f : 400.0f); break;
    case 9: Cvar::SetValue("m_pitch", -m_pitch.value); break;
    case 10: Cvar::SetValue("lookspring", static_cast<float>(!lookspring.value)); break;
    case 11: Cvar::SetValue("lookstrafe", static_cast<float>(!lookstrafe.value)); break;
    }
}

inline void M_DrawSlider(int x, int y, float range) {
    range = eastl::clamp(range, 0.0f, 1.0f); M_DrawCharacter(x - 8, y, 128);
    for (int i = 0; i < SLIDER_RANGE; i++) M_DrawCharacter(x + i * 8, y, 129);
    M_DrawCharacter(x + SLIDER_RANGE * 8, y, 130); M_DrawCharacter(x + static_cast<int>((SLIDER_RANGE - 1) * 8 * range), y, 131);
}
inline void M_DrawCheckbox(int x, int y, int on) { M_Print(x, y, on ? "on" : "off"); }

void M_Options_Draw() {
    DrawMenuHeader("gfx/p_option.lmp");
    M_Print(16, 32, "    Customize controls"); M_Print(16, 40, "         Go to console"); M_Print(16, 48, "     Reset to defaults");
    M_Print(16, 56, "           Screen size"); M_DrawSlider(220, 56, (Screen::GetScreenSystem().GetViewsize().value - 30) / (120 - 30));
    M_Print(16, 64, "            Brightness"); M_DrawSlider(220, 64, static_cast<float>((1.0 - v_gamma.value) / 0.5));
    M_Print(16, 72, "           Mouse Speed"); M_DrawSlider(220, 72, (sensitivity.value - 1) / 10);
    M_Print(16, 80, "       CD Music Volume"); M_DrawSlider(220, 80, bgmvolume.value);
    M_Print(16, 88, "          Sound Volume"); M_DrawSlider(220, 88, volume.value);
    M_Print(16, 96, "            Always Run"); M_DrawCheckbox(220, 96, cl_forwardspeed.value > 200);
    M_Print(16, 104, "          Invert Mouse"); M_DrawCheckbox(220, 104, m_pitch.value < 0);
    M_Print(16, 112, "            Lookspring"); M_DrawCheckbox(220, 112, static_cast<int>(lookspring.value));
    M_Print(16, 120, "            Lookstrafe"); M_DrawCheckbox(220, 120, static_cast<int>(lookstrafe.value));
    DrawLineCursor(200, 32, options_cursor);
}

void M_Options_Key(int k) {
    if (k == K_ESCAPE) { M_Menu_Main_f(); return; }
    if (k == K_ENTER) {
        m_entersound = true;
        switch (options_cursor) {
        case 0: M_Menu_Keys_f(); break; case 1: m_state = MenuState::None; ConsoleSystem::ToggleConsole_f(); break;
        case 2: Cmd::BufferAddText("exec default.cfg\n"); break;
        default: M_AdjustSliders(1); break;
        }
        return;
    }
    if (k == K_UPARROW)   { S_LocalSound("misc/menu1.wav"); options_cursor = (options_cursor - 1 + OPTIONS_ITEMS) % OPTIONS_ITEMS; }
    if (k == K_DOWNARROW) { S_LocalSound("misc/menu1.wav"); options_cursor = (options_cursor + 1) % OPTIONS_ITEMS; }
    if (k == K_LEFTARROW)  M_AdjustSliders(-1);
    if (k == K_RIGHTARROW) M_AdjustSliders(1);
}

struct BindName { eastl::string_view command; eastl::string_view description; };
constexpr auto bindnames = eastl::array<BindName, 18>{{
    { "+attack", "attack" }, { "impulse 10", "change weapon" }, { "+jump", "jump / swim up" },
    { "+forward", "walk forward" }, { "+back", "backpedal" }, { "+left", "turn left" },
    { "+right", "turn right" }, { "+speed", "run" }, { "+moveleft", "step left" },
    { "+moveright", "step right" }, { "+strafe", "sidestep" }, { "+lookup", "look up" },
    { "+lookdown", "look down" }, { "centerview", "center view" }, { "+mlook", "mouse look" },
    { "+klook", "keyboard look" }, { "+moveup", "swim up" }, { "+movedown", "swim down" }
}};

void M_Menu_Keys_f() { key_dest = key_menu; m_state = MenuState::Keys; m_entersound = true; }

void M_FindKeysForCommand(eastl::string_view command, eastl::array<int, 2>& twokeys) {
    twokeys[0] = twokeys[1] = -1; int count = 0;
    for (int j = 0; j < 256; j++) {
        if (!keybindings[j].empty() && keybindings[j] == command) { twokeys[count++] = j; if (count == 2) break; }
    }
}

void M_UnbindCommand(eastl::string_view command) {
    for (int j = 0; j < 256; j++) { if (!keybindings[j].empty() && keybindings[j] == command) Key_SetBinding(j, ""); }
}

void M_Keys_Draw() {
    qpic_t* p = Draw_CachePic("gfx/ttl_cstm.lmp"); M_DrawPic((320 - p->width) / 2, 4, p);
    M_Print(bind_grab ? 12 : 18, 32, bind_grab ? "Press a key or button for this action" : "Enter to change, backspace to clear");
    eastl::array<int, 2> keys;
    for (size_t i = 0; i < bindnames.size(); i++) {
        int y = 48 + 8 * static_cast<int>(i); M_Print(16, y, bindnames[i].description);
        M_FindKeysForCommand(bindnames[i].command, keys);
        if (keys[0] == -1) M_Print(140, y, "???");
        else {
            const char* name = Key_KeynumToString(keys[0]); M_Print(140, y, name);
            if (keys[1] != -1) { int x = static_cast<int>(strlen(name)) * 8; M_Print(140 + x + 8, y, "or"); M_Print(140 + x + 32, y, Key_KeynumToString(keys[1])); }
        }
    }
    if (bind_grab) M_DrawCharacter(130, 48 + keys_cursor * 8, '='); else DrawLineCursor(130, 48, keys_cursor);
}

void M_Keys_Key(int k) {
    eastl::array<int, 2> keys;
    if (bind_grab) {
        S_LocalSound("misc/menu1.wav");
        if (k != K_ESCAPE && k != '`') Cmd::BufferInsertText(va("bind \"%s\" \"%.70s\"\n", Key_KeynumToString(k), bindnames[keys_cursor].command.data()));
        bind_grab = false; return;
    }
    if (k == K_ESCAPE) { M_Menu_Options_f(); return; }
    if (HandleNavKeys(k, keys_cursor, static_cast<int>(bindnames.size()))) return;
    if (k == K_ENTER) {
        M_FindKeysForCommand(bindnames[keys_cursor].command, keys); S_LocalSound("misc/menu2.wav");
        if (keys[1] != -1) M_UnbindCommand(bindnames[keys_cursor].command);
        bind_grab = true;
    } else if (k == K_BACKSPACE || k == K_DEL) { S_LocalSound("misc/menu2.wav"); M_UnbindCommand(bindnames[keys_cursor].command); }
}

constexpr int NUM_HELP_PAGES = 6;
void M_Menu_Help_f() { key_dest = key_menu; m_state = MenuState::Help; m_entersound = true; help_page = 0; }
void M_Help_Draw() { M_DrawPic(0, 0, Draw_CachePic(va("gfx/help%i.lmp", help_page))); }
void M_Help_Key(int key) {
    if (key == K_ESCAPE) { M_Menu_Main_f(); return; }
    if (key == K_UPARROW || key == K_RIGHTARROW) { m_entersound = true; help_page = (help_page + 1) % NUM_HELP_PAGES; }
    if (key == K_DOWNARROW || key == K_LEFTARROW) { m_entersound = true; help_page = (help_page - 1 + NUM_HELP_PAGES) % NUM_HELP_PAGES; }
}

void M_Menu_Quit_f() { key_dest = key_console; Host_Quit_f(); }

constexpr auto lanConfig_cursor_table = eastl::array{ 72, 92, 124 };
constexpr int NUM_LANCONFIG_CMDS = 3;

void M_Menu_LanConfig_f() {
    key_dest = key_menu; m_state = MenuState::LanConfig; m_entersound = true;
    if (lanConfig_cursor == -1) lanConfig_cursor = JoiningGame() ? 2 : 1;
    if (StartingGame() && lanConfig_cursor == 2) lanConfig_cursor = 1;
    lanConfig_port = DEFAULTnet_hostport; lanConfig_portname = eastl::to_string(lanConfig_port);
    m_return_onerror = false; m_return_reason.clear();
}

void M_LanConfig_Draw() {
    DrawMenuHeader("gfx/p_multi.lmp"); int basex = (320 - Draw_CachePic("gfx/p_multi.lmp")->width) / 2;
    M_Print(basex, 32, va("%s - TCP/IP", StartingGame() ? "New Game" : "Join Game")); basex += 8;
    M_Print(basex, 52, "Address:"); M_Print(basex + 9 * 8, 52, my_tcpip_address);
    M_Print(basex, lanConfig_cursor_table[0], "Port"); M_DrawTextBox(basex + 8 * 8, lanConfig_cursor_table[0] - 8, 6, 1);
    M_Print(basex + 9 * 8, lanConfig_cursor_table[0], lanConfig_portname);
    if (JoiningGame()) {
        M_Print(basex, lanConfig_cursor_table[1], "Search for local games...");
        M_Print(basex, 108, "Join game at:"); M_DrawTextBox(basex + 8, lanConfig_cursor_table[2] - 8, 22, 1);
        M_Print(basex + 16, lanConfig_cursor_table[2], lanConfig_joinname);
    } else { M_DrawTextBox(basex, lanConfig_cursor_table[1] - 8, 2, 1); M_Print(basex + 8, lanConfig_cursor_table[1], "OK"); }
    M_DrawCharacter(basex - 8, lanConfig_cursor_table[lanConfig_cursor], 12 + ((int)(realtime * 4) & 1));
    if (lanConfig_cursor == 0) M_DrawCharacter(basex + 9 * 8 + 8 * (int)lanConfig_portname.length(), lanConfig_cursor_table[0], 10 + ((int)(realtime * 4) & 1));
    if (lanConfig_cursor == 2) M_DrawCharacter(basex + 16 + 8 * (int)lanConfig_joinname.length(), lanConfig_cursor_table[2], 10 + ((int)(realtime * 4) & 1));
    if (!m_return_reason.empty()) M_PrintWhite(basex, 148, m_return_reason.c_str());
}

void M_LanConfig_Key(int key) {
    if (key == K_ESCAPE) { M_Menu_MultiPlayer_f(); return; }
    if (key == K_UPARROW)   { S_LocalSound("misc/menu1.wav"); lanConfig_cursor = (lanConfig_cursor - 1 + NUM_LANCONFIG_CMDS) % NUM_LANCONFIG_CMDS; }
    if (key == K_DOWNARROW) { S_LocalSound("misc/menu1.wav"); lanConfig_cursor = (lanConfig_cursor + 1) % NUM_LANCONFIG_CMDS; }
    if (key == K_ENTER) {
        if (lanConfig_cursor == 0) return;
        m_entersound = true; M_ConfigureNetSubsystem();
        if (lanConfig_cursor == 1) { if (StartingGame()) M_Menu_GameOptions_f(); else M_Menu_Search_f(); return; }
        if (lanConfig_cursor == 2) {
            m_return_state = m_state; m_return_onerror = true; key_dest = key_game; m_state = MenuState::None;
            Cmd::BufferAddText(va("connect \"%s\"\n", lanConfig_joinname.c_str())); return;
        }
    }
    if (key == K_BACKSPACE) {
        if (lanConfig_cursor == 0 && !lanConfig_portname.empty()) lanConfig_portname.pop_back();
        if (lanConfig_cursor == 2 && !lanConfig_joinname.empty()) lanConfig_joinname.pop_back();
    }
    if (key >= 32 && key <= 127) {
        if (lanConfig_cursor == 2 && lanConfig_joinname.length() < 21) lanConfig_joinname.push_back(static_cast<char>(key));
        if (key >= '0' && key <= '9' && lanConfig_cursor == 0 && lanConfig_portname.length() < 5) lanConfig_portname.push_back(static_cast<char>(key));
    }
    if (StartingGame() && lanConfig_cursor == 2) lanConfig_cursor = (key == K_UPARROW) ? 1 : 0;
    int l = Q_atoi(lanConfig_portname.c_str()); if (l <= 65535) lanConfig_port = l;
    lanConfig_portname = eastl::to_string(lanConfig_port).c_str();
}

struct level_t { const char* name; const char* description; };
constexpr auto levels = eastl::array<level_t, 38>{{
    { "start", "Entrance" }, { "e1m1", "Slipgate Complex" }, { "e1m2", "Castle of the Damned" }, { "e1m3", "The Necropolis" },
    { "e1m4", "The Grisly Grotto" }, { "e1m5", "Gloom Keep" }, { "e1m6", "The Door To Chthon" }, { "e1m7", "The House of Chthon" },
    { "e1m8", "Ziggurat Vertigo" }, { "e2m1", "The Installation" }, { "e2m2", "Ogre Citadel" }, { "e2m3", "Crypt of Decay" },
    { "e2m4", "The Ebon Fortress" }, { "e2m5", "The Wizard's Manse" }, { "e2m6", "The Dismal Oubliette" }, { "e2m7", "Underearth" },
    { "e3m1", "Termination Central" }, { "e3m2", "The Vaults of Zin" }, { "e3m3", "The Tomb of Terror" }, { "e3m4", "Satan's Dark Delight" },
    { "e3m5", "Wind Tunnels" }, { "e3m6", "Chambers of Torment" }, { "e3m7", "The Haunted Halls" }, { "e4m1", "The Sewage System" },
    { "e4m2", "The Tower of Despair" }, { "e4m3", "The Elder God Shrine" }, { "e4m4", "The Palace of Hate" }, { "e4m5", "Hell's Atrium" },
    { "e4m6", "The Pain Maze" }, { "e4m7", "Azure Agony" }, { "e4m8", "The Nameless City" }, { "end", "Shub-Niggurath's Pit" },
    { "dm1", "Place of Two Deaths" }, { "dm2", "Claustrophobopolis" }, { "dm3", "The Abandoned Base" }, { "dm4", "The Bad Place" },
    { "dm5", "The Cistern" }, { "dm6", "The Dark Zone" }
}};

constexpr auto hipnoticlevels = eastl::array<level_t, 18>{{
    { "start", "Command HQ" }, { "hip1m1", "The Pumping Station" }, { "hip1m2", "Storage Facility" }, { "hip1m3", "The Lost Mine" },
    { "hip1m4", "Research Facility" }, { "hip1m5", "Military Complex" }, { "hip2m1", "Ancient Realms" }, { "hip2m2", "The Black Cathedral" },
    { "hip2m3", "The Catacombs" }, { "hip2m4", "The Crypt" }, { "hip2m5", "Mortum's Keep" }, { "hip2m6", "The Gremlin's Domain" },
    { "hip3m1", "Tur Torment" }, { "hip3m2", "Pandemonium" }, { "hip3m3", "Limbo" }, { "hip3m4", "The Gauntlet" },
    { "hipend", "Armagon's Lair" }, { "hipdm1", "The Edge of Oblivion" }
}};

constexpr auto roguelevels = eastl::array<level_t, 17>{{
    { "start", "Split Decision" }, { "r1m1", "Deviant's Domain" }, { "r1m2", "Dread Portal" }, { "r1m3", "Judgement Call" },
    { "r1m4", "Cave of Death" }, { "r1m5", "Towers of Wrath" }, { "r1m6", "Temple of Pain" }, { "r1m7", "Tomb of the Overlord" },
    { "r2m1", "Tempus Fugit" }, { "r2m2", "Elemental Fury I" }, { "r2m3", "Elemental Fury II" }, { "r2m4", "Curse of Osiris" },
    { "r2m5", "Wizard's Keep" }, { "r2m6", "Blood Sacrifice" }, { "r2m7", "Last Bastion" }, { "r2m8", "Source of Evil" },
    { "ctf1", "Division of Change" }
}};

struct episode_t { const char* description; int firstLevel; int levels; };
constexpr auto episodes = eastl::array<episode_t, 7>{{
    { "Welcome to Quake", 0, 1 }, { "Doomed Dimension", 1, 8 }, { "Realm of Black Magic", 9, 7 }, { "Netherworld", 16, 7 },
    { "The Elder World", 23, 8 }, { "Final Level", 31, 1 }, { "Deathmatch Arena", 32, 6 }
}};

constexpr auto hipnoticepisodes = eastl::array<episode_t, 6>{{
    { "Scourge of Armagon", 0, 1 }, { "Fortress of the Dead", 1, 5 }, { "Dominion of Darkness", 6, 6 }, { "The Rift", 12, 4 },
    { "Final Level", 16, 1 }, { "Deathmatch Arena", 17, 1 }
}};

constexpr auto rogueepisodes = eastl::array<episode_t, 4>{{
    { "Introduction", 0, 1 }, { "Hell's Fortress", 1, 7 }, { "Corridors of Time", 8, 8 }, { "Deathmatch Arena", 16, 1 }
}};

constexpr auto gameoptions_cursor_table = eastl::array{ 40, 56, 64, 72, 80, 88, 96, 112, 120 };
constexpr int NUM_GAMEOPTIONS = 9;

void M_Menu_GameOptions_f() {
    key_dest = key_menu; m_state = MenuState::GameOptions; m_entersound = true;
    if (maxplayers == 0) maxplayers = svs.maxclients;
    if (maxplayers < 2) maxplayers = svs.maxclientslimit;
}

void M_GameOptions_Draw() {
    DrawMenuHeader("gfx/p_multi.lmp");
    M_DrawTextBox(152, 32, 10, 1); M_Print(160, 40, "begin game");
    M_Print(0, 56, "      Max players"); M_Print(160, 56, va("%i", maxplayers));
    M_Print(0, 64, "        Game Type"); M_Print(160, 64, coop.value ? "Cooperative" : "Deathmatch");
    M_Print(0, 72, "        Teamplay");
    const char* team_msg = "Off";
    if (rogue) {
        constexpr auto rmsgs = eastl::array{ "Off", "No Friendly Fire", "Friendly Fire", "Tag", "Capture the Flag", "One Flag CTF", "Three Team CTF" };
        int idx = static_cast<int>(teamplay.value); if (idx >= 1 && idx <= 6) team_msg = rmsgs[idx];
    } else {
        if ((int)teamplay.value == 1) team_msg = "No Friendly Fire";
        else if ((int)teamplay.value == 2) team_msg = "Friendly Fire";
    }
    M_Print(160, 72, team_msg);
    M_Print(0, 80, "            Skill");
    constexpr auto skills = eastl::array{ "Easy difficulty", "Normal difficulty", "Hard difficulty", "Nightmare difficulty" };
    M_Print(160, 80, skills[eastl::clamp<int>(static_cast<int>(skill.value), 0, 3)]);
    M_Print(0, 88, "       Frag Limit"); M_Print(160, 88, (fraglimit.value == 0) ? "none" : va("%i frags", (int)fraglimit.value));
    M_Print(0, 96, "       Time Limit"); M_Print(160, 96, (timelimit.value == 0) ? "none" : va("%i minutes", (int)timelimit.value));
    M_Print(0, 112, "         Episode");
    const episode_t* ep_ptr = episodes.data(); const level_t* lvl_ptr = levels.data();
    if (hipnotic) { ep_ptr = hipnoticepisodes.data(); lvl_ptr = hipnoticlevels.data(); }
    else if (rogue) { ep_ptr = rogueepisodes.data(); lvl_ptr = roguelevels.data(); }
    M_Print(160, 112, ep_ptr[startepisode].description);
    M_Print(0, 120, "           Level");
    const auto& cur_lvl = lvl_ptr[ep_ptr[startepisode].firstLevel + startlevel];
    M_Print(160, 120, cur_lvl.description); M_Print(160, 128, cur_lvl.name);
    M_DrawCharacter(144, gameoptions_cursor_table[gameoptions_cursor], 12 + ((int)(realtime * 4) & 1));
    if (m_serverInfoMessage) {
        if ((realtime - m_serverInfoMessageTime) < 5.0) {
            int x = (320 - 26 * 8) / 2; M_DrawTextBox(x, 138, 24, 4); x += 8;
            M_Print(x, 146, "  More than 4 players   "); M_Print(x, 154, " requires using command ");
            M_Print(x, 162, "line parameters; please "); M_Print(x, 170, "   see techinfo.txt.    ");
        } else m_serverInfoMessage = false;
    }
}

void M_NetStart_Change(int dir) {
    switch (gameoptions_cursor) {
    case 1:
        maxplayers += dir;
        if (maxplayers > svs.maxclientslimit) { maxplayers = svs.maxclientslimit; m_serverInfoMessage = true; m_serverInfoMessageTime = realtime; }
        if (maxplayers < 2) maxplayers = 2;
        break;
    case 2: Cvar::SetValue("coop", static_cast<float>(coop.value ? 0 : 1)); break;
    case 3: {
        int count = rogue ? 6 : 2; float new_val = teamplay.value + dir;
        if (new_val > count) new_val = 0; else if (new_val < 0) new_val = static_cast<float>(count);
        Cvar::SetValue("teamplay", new_val); break;
    }
    case 4: { float new_sk = skill.value + dir; if (new_sk > 3) new_sk = 0; else if (new_sk < 0) new_sk = 3; Cvar::SetValue("skill", new_sk); break; }
    case 5: { float new_fl = fraglimit.value + dir * 10; if (new_fl > 100) new_fl = 0; else if (new_fl < 0) new_fl = 100; Cvar::SetValue("fraglimit", new_fl); break; }
    case 6: { float new_tl = timelimit.value + dir * 5; if (new_tl > 60) new_tl = 0; else if (new_tl < 0) new_tl = 60; Cvar::SetValue("timelimit", new_tl); break; }
    case 7: { int count = hipnotic ? 6 : (rogue ? 4 : (registered.value ? 7 : 2)); startepisode = (startepisode + dir + count) % count; startlevel = 0; break; }
    case 8: { int count = hipnotic ? hipnoticepisodes[startepisode].levels : (rogue ? rogueepisodes[startepisode].levels : episodes[startepisode].levels); startlevel = (startlevel + dir + count) % count; break; }
    }
}

void M_GameOptions_Key(int key) {
    if (key == K_ESCAPE) { M_Menu_LanConfig_f(); return; }
    if (key == K_UPARROW)   { S_LocalSound("misc/menu1.wav"); gameoptions_cursor = (gameoptions_cursor - 1 + NUM_GAMEOPTIONS) % NUM_GAMEOPTIONS; }
    if (key == K_DOWNARROW) { S_LocalSound("misc/menu1.wav"); gameoptions_cursor = (gameoptions_cursor + 1) % NUM_GAMEOPTIONS; }
    if (key == K_LEFTARROW && gameoptions_cursor > 0)  { S_LocalSound("misc/menu3.wav"); M_NetStart_Change(-1); }
    if (key == K_RIGHTARROW && gameoptions_cursor > 0) { S_LocalSound("misc/menu3.wav"); M_NetStart_Change(1); }
    if (key == K_ENTER) {
        S_LocalSound("misc/menu2.wav");
        if (gameoptions_cursor == 0) {
            if (sv.active) Cmd::BufferAddText("disconnect\n");
            Cmd::BufferAddText("listen 0\n"); Cmd::BufferAddText(va("maxplayers %u\n", maxplayers)); Screen::GetScreenSystem().BeginLoadingPlaque();
            const episode_t* ep_ptr = episodes.data(); const level_t* lvl_ptr = levels.data();
            if (hipnotic) { ep_ptr = hipnoticepisodes.data(); lvl_ptr = hipnoticlevels.data(); }
            else if (rogue) { ep_ptr = rogueepisodes.data(); lvl_ptr = roguelevels.data(); }
            Cmd::BufferAddText(va("map %s\n", lvl_ptr[ep_ptr[startepisode].firstLevel + startlevel].name)); return;
        }
        M_NetStart_Change(1);
    }
}

void M_Menu_Search_f() { key_dest = key_menu; m_state = MenuState::Search; m_entersound = false; slistSilent = true; slistLocal = false; searchComplete = false; NET_Slist_f(); }
void M_Search_Draw() {
    qpic_t* p = Draw_CachePic("gfx/p_multi.lmp"); M_DrawPic((320 - p->width) / 2, 4, p);
    int x = (320 / 2) - 44; M_DrawTextBox(x - 8, 32, 12, 1); M_Print(x, 40, "Searching...");
    if (slistInProgress) { NET_Poll(); return; }
    if (!searchComplete) { searchComplete = true; searchCompleteTime = realtime; }
    if (hostCacheCount) { M_Menu_ServerList_f(); return; }
    M_PrintWhite((320 - 22 * 8) / 2, 64, "No Quake servers found");
    if ((realtime - searchCompleteTime) >= 3.0) M_Menu_LanConfig_f();
}
void M_Search_Key() {}

void M_Menu_ServerList_f() { key_dest = key_menu; m_state = MenuState::SList; m_entersound = true; slist_cursor = 0; m_return_onerror = false; m_return_reason.clear(); slist_sorted = false; }
void M_ServerList_Draw() {
    if (!slist_sorted && hostCacheCount > 1) {
        eastl::sort(hostcache.begin(), hostcache.begin() + hostCacheCount, [](const hostcache_t& a, const hostcache_t& b) { return strcmp(a.name, b.name) < 0; });
        slist_sorted = true;
    }
    qpic_t* p = Draw_CachePic("gfx/p_multi.lmp"); M_DrawPic((320 - p->width) / 2, 4, p);
    for (int n = 0; n < hostCacheCount; n++) {
        char string[64];
        if (hostcache[n].maxusers) sprintf_s(string, sizeof(string), "%-15.15s %-15.15s %2u/%2u\n", hostcache[n].name, hostcache[n].map, hostcache[n].users, hostcache[n].maxusers);
        else sprintf_s(string, sizeof(string), "%-15.15s %-15.15s\n", hostcache[n].name, hostcache[n].map);
        M_Print(16, 32 + 8 * n, string);
    }
    DrawLineCursor(0, 32, slist_cursor);
    if (!m_return_reason.empty()) M_PrintWhite(16, 148, m_return_reason.c_str());
}

void M_ServerList_Key(int k) {
    if (k == K_ESCAPE) { M_Menu_LanConfig_f(); return; }
    if (k == K_SPACE)  { M_Menu_Search_f(); return; }
    if (HandleNavKeys(k, slist_cursor, hostCacheCount)) return;
    if (k == K_ENTER) {
        S_LocalSound("misc/menu2.wav"); m_return_state = m_state; m_return_onerror = true; slist_sorted = false;
        key_dest = key_game; m_state = MenuState::None; Cmd::BufferAddText(va("connect \"%s\"\n", hostcache[slist_cursor].cname));
    }
}

void M_Init() {
    constexpr CmdPair cmds[] = {
        {"togglemenu", M_ToggleMenu_f}, {"menu_main", M_Menu_Main_f}, {"menu_singleplayer", M_Menu_SinglePlayer_f},
        {"menu_load", M_Menu_Load_f}, {"menu_save", M_Menu_Save_f}, {"menu_multiplayer", M_Menu_MultiPlayer_f},
        {"menu_setup", M_Menu_Setup_f}, {"menu_options", M_Menu_Options_f}, {"menu_keys", M_Menu_Keys_f},
        {"help", M_Menu_Help_f}, {"menu_quit", M_Menu_Quit_f}
    };
    for (auto [name, fn] : cmds) Cmd::AddCommand(name, fn);
}

using MenuFn = void(*)();
using MenuKeyFn = void(*)(int);

// Indexed by MenuState; keep in the same order as the enum in menu.hpp.
constexpr MenuFn menu_draw_table[] = {
    nullptr, M_Main_Draw, M_SinglePlayer_Draw, M_Load_Draw, M_Save_Draw,
    M_MultiPlayer_Draw, M_Setup_Draw, M_Options_Draw,
    M_Keys_Draw, M_Help_Draw, nullptr,
    M_LanConfig_Draw, M_GameOptions_Draw, M_Search_Draw, M_ServerList_Draw
};

constexpr MenuKeyFn menu_key_table[] = {
    nullptr, M_Main_Key, M_SinglePlayer_Key, M_Load_Key, M_Save_Key,
    M_MultiPlayer_Key, M_Setup_Key, M_Options_Key,
    M_Keys_Key, M_Help_Key, nullptr,
    M_LanConfig_Key, M_GameOptions_Key, [](int) { M_Search_Key(); }, M_ServerList_Key
};
static_assert(std::size(menu_draw_table) == static_cast<size_t>(MenuState::SList) + 1);
static_assert(std::size(menu_key_table) == static_cast<size_t>(MenuState::SList) + 1);

void M_Draw() {
    if (m_state == MenuState::None || key_dest != key_menu) return;
    if (!m_recursiveDraw) {
        Screen::GetScreenSystem().SetCopyeverything(1);
        if (Screen::GetScreenSystem().GetConCurrent()) {
            Draw_ConsoleBackground(vid.height);
        } else Draw_FadeScreen();
        Screen::GetScreenSystem().SetFullupdate(0);
    } else m_recursiveDraw = false;

    const auto idx = static_cast<size_t>(m_state);
    if (idx < std::size(menu_draw_table) && menu_draw_table[idx]) menu_draw_table[idx]();

    if (m_entersound) { S_LocalSound("misc/menu2.wav"); m_entersound = false; }
}

void M_Keydown(int key) {
    const auto idx = static_cast<size_t>(m_state);
    if (idx < std::size(menu_key_table) && menu_key_table[idx]) menu_key_table[idx](key);
}

void M_ConfigureNetSubsystem() {
    Cmd::BufferAddText("stopdemo\n");
    net_hostport = lanConfig_port;
}

} // namespace Menu
