// menu.h -- menu system declarations
#pragma once

#include <string>

namespace Menu {

enum class MenuState {
    None,
    Main,
    SinglePlayer,
    Load,
    Save,
    MultiPlayer,
    Setup,
    Net,
    Options,
    Video,
    Keys,
    Help,
    Quit,
    SerialConfig,
    ModemConfig,
    LanConfig,
    GameOptions,
    Search,
    SList
};

void M_Init();
void M_Keydown(int key);
void M_Draw();
void M_ToggleMenu_f();
void M_Menu_Main_f();
void M_Menu_Quit_f();

void M_DrawPic(int x, int y, qpic_t* pic);

extern MenuState m_state;
extern MenuState m_return_state;
extern bool m_return_onerror;
extern std::string m_return_reason;

} // namespace Menu

