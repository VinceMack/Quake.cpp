// input.hpp -- Keyboard, mouse, and peripheral input management
#pragma once

#include <cstdint>
#include <ostream>
#include <array>
#include <string>
#include <string_view>

struct usercmd_t;

namespace Keys {

inline constexpr int K_TAB = 9, K_ENTER = 13, K_ESCAPE = 27, K_SPACE = 32, K_BACKSPACE = 127;
inline constexpr int K_UPARROW = 128, K_DOWNARROW = 129, K_LEFTARROW = 130, K_RIGHTARROW = 131;
inline constexpr int K_ALT = 132, K_CTRL = 133, K_SHIFT = 134;
inline constexpr int K_F1 = 135, K_F2 = 136, K_F3 = 137, K_F4 = 138, K_F5 = 139, K_F6 = 140;
inline constexpr int K_F7 = 141, K_F8 = 142, K_F9 = 143, K_F10 = 144, K_F11 = 145, K_F12 = 146;
inline constexpr int K_INS = 147, K_DEL = 148, K_PGDN = 149, K_PGUP = 150, K_HOME = 151, K_END = 152;
inline constexpr int K_PAUSE = 255;
inline constexpr int K_MOUSE1 = 200, K_MOUSE2 = 201, K_MOUSE3 = 202;
inline constexpr int K_JOY1 = 203, K_JOY2 = 204, K_JOY3 = 205, K_JOY4 = 206;
inline constexpr int K_AUX1 = 207, K_AUX32 = 238;
inline constexpr int K_MWHEELUP = 239, K_MWHEELDOWN = 240;
inline constexpr int MAXCMDLINE = 256;

enum keydest_t { key_game, key_console, key_message, key_menu };

extern keydest_t key_dest;
extern std::array<std::array<char, MAXCMDLINE>, 32> key_lines;
extern int key_linepos, edit_line, key_count, key_lastpress;
extern std::array<char, 32> chat_buffer;
extern bool team_message;
extern std::array<std::string, 256> keybindings;
extern std::array<int, 256> key_repeats;

void Key_Event(int key, bool down);
void Key_Init();
void Key_WriteBindings(std::ostream& f);
void Key_SetBinding(int keynum, const char* binding);
[[nodiscard]] const char* Key_KeynumToString(int keynum);

} // namespace Keys

namespace Input {

void IN_Init();
void IN_Shutdown();
void IN_Commands();
void IN_Move(usercmd_t* cmd);
void IN_MouseMove(float xrel, float yrel);

} // namespace Input
