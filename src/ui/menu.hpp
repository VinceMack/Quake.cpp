// menu.hpp -- Quake in-game menu system & screens
#pragma once

#include <cstdint>
#include <EASTL/string.h>
#include "sys_render.hpp"

namespace Menu {

enum class MenuState {
    None, Main, SinglePlayer, Load, Save, MultiPlayer, Setup, Options,
    Keys, Help, Quit, LanConfig, GameOptions, Search, SList
};

void M_Init();
void M_Keydown(int key);
void M_Draw();
void M_ToggleMenu_f();
void M_Menu_Main_f();
void M_Menu_Quit_f();
void M_DrawPic(int x, int y, qpic_t* pic);

extern MenuState m_state, m_return_state;
extern bool m_return_onerror;
extern eastl::string m_return_reason;

} // namespace Menu
