// keys.hpp -- keycode definitions and key event declarations
#pragma once
#include <ostream>
#include <array>
#include <string>

namespace Keys {

// normal keys should be passed as lowercased ascii
inline constexpr int K_TAB = 9;
inline constexpr int K_ENTER = 13;
inline constexpr int K_ESCAPE = 27;
inline constexpr int K_SPACE = 32;

inline constexpr int K_BACKSPACE = 127;
inline constexpr int K_UPARROW = 128;
inline constexpr int K_DOWNARROW = 129;
inline constexpr int K_LEFTARROW = 130;
inline constexpr int K_RIGHTARROW = 131;

inline constexpr int K_ALT = 132;
inline constexpr int K_CTRL = 133;
inline constexpr int K_SHIFT = 134;
inline constexpr int K_F1 = 135;
inline constexpr int K_F2 = 136;
inline constexpr int K_F3 = 137;
inline constexpr int K_F4 = 138;
inline constexpr int K_F5 = 139;
inline constexpr int K_F6 = 140;
inline constexpr int K_F7 = 141;
inline constexpr int K_F8 = 142;
inline constexpr int K_F9 = 143;
inline constexpr int K_F10 = 144;
inline constexpr int K_F11 = 145;
inline constexpr int K_F12 = 146;
inline constexpr int K_INS = 147;
inline constexpr int K_DEL = 148;
inline constexpr int K_PGDN = 149;
inline constexpr int K_PGUP = 150;
inline constexpr int K_HOME = 151;
inline constexpr int K_END = 152;

inline constexpr int K_PAUSE = 255;

// mouse buttons generate virtual keys
inline constexpr int K_MOUSE1 = 200;
inline constexpr int K_MOUSE2 = 201;
inline constexpr int K_MOUSE3 = 202;

// joystick buttons
inline constexpr int K_JOY1 = 203;
inline constexpr int K_JOY2 = 204;
inline constexpr int K_JOY3 = 205;
inline constexpr int K_JOY4 = 206;

// aux keys are for multi-buttoned joysticks to generate so they can use
// the normal binding process
inline constexpr int K_AUX1 = 207;
inline constexpr int K_AUX2 = 208;
inline constexpr int K_AUX3 = 209;
inline constexpr int K_AUX4 = 210;
inline constexpr int K_AUX5 = 211;
inline constexpr int K_AUX6 = 212;
inline constexpr int K_AUX7 = 213;
inline constexpr int K_AUX8 = 214;
inline constexpr int K_AUX9 = 215;
inline constexpr int K_AUX10 = 216;
inline constexpr int K_AUX11 = 217;
inline constexpr int K_AUX12 = 218;
inline constexpr int K_AUX13 = 219;
inline constexpr int K_AUX14 = 220;
inline constexpr int K_AUX15 = 221;
inline constexpr int K_AUX16 = 222;
inline constexpr int K_AUX17 = 223;
inline constexpr int K_AUX18 = 224;
inline constexpr int K_AUX19 = 225;
inline constexpr int K_AUX20 = 226;
inline constexpr int K_AUX21 = 227;
inline constexpr int K_AUX22 = 228;
inline constexpr int K_AUX23 = 229;
inline constexpr int K_AUX24 = 230;
inline constexpr int K_AUX25 = 231;
inline constexpr int K_AUX26 = 232;
inline constexpr int K_AUX27 = 233;
inline constexpr int K_AUX28 = 234;
inline constexpr int K_AUX29 = 235;
inline constexpr int K_AUX30 = 236;
inline constexpr int K_AUX31 = 237;
inline constexpr int K_AUX32 = 238;

// JACK: Intellimouse(c) Mouse Wheel Support
inline constexpr int K_MWHEELUP = 239;
inline constexpr int K_MWHEELDOWN = 240;

inline constexpr int MAXCMDLINE = 256;

extern std::array<std::array<char, MAXCMDLINE>, 32> key_lines;
extern int key_linepos;
extern int edit_line;
extern std::array<char, 32> chat_buffer;
extern bool team_message;

enum keydest_t {
    key_game,
    key_console,
    key_message,
    key_menu
};

extern keydest_t key_dest;
extern std::array<std::string, 256> keybindings;
extern std::array<int, 256> key_repeats;
extern int key_count;
extern int key_lastpress;

void Key_Event(int key, bool down);
void Key_Init();
void Key_WriteBindings(std::ostream& f);
void Key_SetBinding(int keynum, const char* binding);
const char* Key_KeynumToString(int keynum);

} // namespace Keys
