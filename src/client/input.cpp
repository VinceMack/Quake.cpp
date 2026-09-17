// input.cpp -- Unified keyboard and mouse input handling
#include "quakedef.hpp"
#include "client/input.hpp"
#include "ui/console.hpp"
#include "ui/menu.hpp"

#include <SDL.h>
#include <array>
#include <string>
#include <string_view>
#include <algorithm>
#include <ostream>

// ============================================================================
// KEYBOARD SUBSYSTEM
// ============================================================================

namespace Keys {

std::array<std::array<char, MAXCMDLINE>, 32> key_lines;
int key_linepos, edit_line = 0, history_line = 0;
std::array<char, 32> chat_buffer;
bool team_message = false;
int shift_down = false, key_lastpress;
keydest_t key_dest;
int key_count;

std::array<std::string, 256> keybindings;
std::array<bool, 256> consolekeys, menubound, keydown;
std::array<int, 256> keyshift, key_repeats;

struct keyname_t { const char* name; int keynum; };
constexpr keyname_t keynames[] = {
    { "TAB", K_TAB }, { "ENTER", K_ENTER }, { "ESCAPE", K_ESCAPE }, { "SPACE", K_SPACE },
    { "BACKSPACE", K_BACKSPACE }, { "UPARROW", K_UPARROW }, { "DOWNARROW", K_DOWNARROW },
    { "LEFTARROW", K_LEFTARROW }, { "RIGHTARROW", K_RIGHTARROW },
    { "ALT", K_ALT }, { "CTRL", K_CTRL }, { "SHIFT", K_SHIFT },
    { "F1", K_F1 }, { "F2", K_F2 }, { "F3", K_F3 }, { "F4", K_F4 }, { "F5", K_F5 },
    { "F6", K_F6 }, { "F7", K_F7 }, { "F8", K_F8 }, { "F9", K_F9 }, { "F10", K_F10 },
    { "F11", K_F11 }, { "F12", K_F12 },
    { "INS", K_INS }, { "DEL", K_DEL }, { "PGDN", K_PGDN }, { "PGUP", K_PGUP },
    { "HOME", K_HOME }, { "END", K_END },
    { "MOUSE1", K_MOUSE1 }, { "MOUSE2", K_MOUSE2 }, { "MOUSE3", K_MOUSE3 },
    { "JOY1", K_JOY1 }, { "JOY2", K_JOY2 }, { "JOY3", K_JOY3 }, { "JOY4", K_JOY4 },
    { "AUX1", K_AUX1 }, { "AUX2", K_AUX1 + 1 }, { "AUX3", K_AUX1 + 2 }, { "AUX4", K_AUX1 + 3 },
    { "AUX5", K_AUX1 + 4 }, { "AUX6", K_AUX1 + 5 }, { "AUX7", K_AUX1 + 6 }, { "AUX8", K_AUX1 + 7 },
    { "AUX9", K_AUX1 + 8 }, { "AUX10", K_AUX1 + 9 }, { "AUX11", K_AUX1 + 10 }, { "AUX12", K_AUX1 + 11 },
    { "AUX13", K_AUX1 + 12 }, { "AUX14", K_AUX1 + 13 }, { "AUX15", K_AUX1 + 14 }, { "AUX16", K_AUX1 + 15 },
    { "AUX17", K_AUX1 + 16 }, { "AUX18", K_AUX1 + 17 }, { "AUX19", K_AUX1 + 18 }, { "AUX20", K_AUX1 + 19 },
    { "AUX21", K_AUX1 + 20 }, { "AUX22", K_AUX1 + 21 }, { "AUX23", K_AUX1 + 22 }, { "AUX24", K_AUX1 + 23 },
    { "AUX25", K_AUX1 + 24 }, { "AUX26", K_AUX1 + 25 }, { "AUX27", K_AUX1 + 26 }, { "AUX28", K_AUX1 + 27 },
    { "AUX29", K_AUX1 + 28 }, { "AUX30", K_AUX1 + 29 }, { "AUX31", K_AUX1 + 30 }, { "AUX32", K_AUX32 },
    { "PAUSE", K_PAUSE }, { "MWHEELUP", K_MWHEELUP }, { "MWHEELDOWN", K_MWHEELDOWN },
    { "SEMICOLON", ';' }, { nullptr, 0 }
};

void Key_Console(int key) {
    if (key == K_ENTER) {
        Cmd::BufferAddText(key_lines[edit_line].data() + 1); Cmd::BufferAddText("\n");
        Console::Con_Printf("%s\n", key_lines[edit_line].data());
        edit_line = (edit_line + 1) & 31; history_line = edit_line;
        key_lines[edit_line][0] = ']'; key_linepos = 1;
        if (Client::cls.state == ca_disconnected) Screen::GetScreenSystem().UpdateScreen();
        return;
    }
    if (key == K_TAB) {
        std::string_view cmd_view = Cmd::CompleteCommand(key_lines[edit_line].data() + 1);
        if (cmd_view.empty()) cmd_view = Cvar::CompleteVariable(key_lines[edit_line].data() + 1);
        if (!cmd_view.empty()) {
            std::string cmd_str(cmd_view.data(), cmd_view.length());
            Common::Q_strcpy(key_lines[edit_line].data() + 1, cmd_str.c_str());
            key_linepos = Common::Q_strlen(cmd_str.c_str()) + 1;
            key_lines[edit_line][key_linepos++] = ' '; key_lines[edit_line][key_linepos] = 0;
            return;
        }
    }
    if (key == K_BACKSPACE || key == K_LEFTARROW) { if (key_linepos > 1) key_linepos--; return; }
    if (key == K_UPARROW) {
        do { history_line = (history_line - 1) & 31; } while (history_line != edit_line && !key_lines[history_line][1]);
        if (history_line == edit_line) history_line = (edit_line + 1) & 31;
        Common::Q_strcpy(key_lines[edit_line].data(), key_lines[history_line].data());
        key_linepos = Common::Q_strlen(key_lines[edit_line].data()); return;
    }
    if (key == K_DOWNARROW) {
        if (history_line == edit_line) return;
        do { history_line = (history_line + 1) & 31; } while (history_line != edit_line && !key_lines[history_line][1]);
        if (history_line == edit_line) { key_lines[edit_line][0] = ']'; key_linepos = 1; }
        else { Common::Q_strcpy(key_lines[edit_line].data(), key_lines[history_line].data()); key_linepos = Common::Q_strlen(key_lines[edit_line].data()); }
        return;
    }
    auto& con = Console::GetConsoleSystem();
    if (key == K_PGUP || key == K_MWHEELUP) { con.SetBackscroll(std::min(con.GetBackscroll() + 2, con.GetTotalLines() - (int)(Vid::vid.height >> 3) - 1)); return; }
    if (key == K_PGDN || key == K_MWHEELDOWN) { con.SetBackscroll(std::max(0, con.GetBackscroll() - 2)); return; }
    if (key == K_HOME) { con.SetBackscroll(con.GetTotalLines() - (Vid::vid.height >> 3) - 1); return; }
    if (key == K_END)  { con.SetBackscroll(0); return; }
    if (key >= 32 && key <= 127 && key_linepos < MAXCMDLINE - 1) {
        key_lines[edit_line][key_linepos++] = static_cast<char>(key);
        key_lines[edit_line][key_linepos] = 0;
    }
}

void Key_Message(int key) {
    static int chat_bufferlen = 0;
    if (key == K_ENTER) {
        Cmd::BufferAddText(team_message ? "say_team \"" : "say \"");
        Cmd::BufferAddText(chat_buffer.data()); Cmd::BufferAddText("\"\n");
        key_dest = key_game; chat_bufferlen = 0; chat_buffer[0] = 0; return;
    }
    if (key == K_ESCAPE) { key_dest = key_game; chat_bufferlen = 0; chat_buffer[0] = 0; return; }
    if (key == K_BACKSPACE) { if (chat_bufferlen) { chat_bufferlen--; chat_buffer[chat_bufferlen] = 0; } return; }
    if (key >= 32 && key <= 127 && chat_bufferlen < 31) { chat_buffer[chat_bufferlen++] = static_cast<char>(key); chat_buffer[chat_bufferlen] = 0; }
}

int Key_StringToKeynum(std::string_view str) {
    if (str.empty()) return -1;
    if (str.size() == 1) return str[0];
    const auto it = std::find_if(std::begin(keynames), std::end(keynames), [str](const keyname_t& kn) {
        return kn.name && Common::Q_strcasecmp(str, kn.name) == 0;
    });
    return (it != std::end(keynames)) ? it->keynum : -1;
}

const char* Key_KeynumToString(int keynum) {
    static char tinystr[2];
    if (keynum == -1) return "<KEY NOT FOUND>";
    if (keynum > 32 && keynum < 127) { tinystr[0] = static_cast<char>(keynum); tinystr[1] = 0; return tinystr; }
    const auto it = std::find_if(std::begin(keynames), std::end(keynames), [keynum](const keyname_t& kn) {
        return kn.name && kn.keynum == keynum;
    });
    return (it != std::end(keynames)) ? it->name : "<UNKNOWN KEYNUM>";
}

void Key_SetBinding(int keynum, const char* binding) { if (keynum >= 0 && keynum < 256) keybindings[keynum] = binding; }

void Key_Unbind_f() {
    if (Cmd::Argc() != 2) { Console::Con_Printf("unbind <key> : remove commands from a key\n"); return; }
    int b = Key_StringToKeynum(Cmd::Argv(1));
    if (b == -1) Console::Con_Printf("\"%s\" isn't a valid key\n", Cmd::Argv(1)); else Key_SetBinding(b, "");
}

void Key_Unbindall_f() { for (auto& kb : keybindings) kb.clear(); }

void Key_Bind_f() {
    int c = Cmd::Argc();
    if (c != 2 && c != 3) { Console::Con_Printf("bind <key> [command] : attach a command to a key\n"); return; }
    int b = Key_StringToKeynum(Cmd::Argv(1));
    if (b == -1) { Console::Con_Printf("\"%s\" isn't a valid key\n", Cmd::Argv(1)); return; }
    if (c == 2) {
        if (!keybindings[b].empty()) Console::Con_Printf("\"%s\" = \"%s\"\n", Cmd::Argv(1), keybindings[b].c_str());
        else Console::Con_Printf("\"%s\" is not bound\n", Cmd::Argv(1));
        return;
    }
    char cmd[1024] = "";
    for (int i = 2; i < c; i++) { if (i > 2) Common::Q_strcat(cmd, " "); Common::Q_strcat(cmd, Cmd::Argv(i)); }
    Key_SetBinding(b, cmd);
}

void Key_WriteBindings(std::ostream& f) {
    for (int i = 0; i < 256; i++) { if (!keybindings[i].empty()) f << "bind \"" << Key_KeynumToString(i) << "\" \"" << keybindings[i].c_str() << "\"\n"; }
}

void Key_Init() {
    for (int i = 0; i < 32; i++) { key_lines[i][0] = ']'; key_lines[i][1] = 0; }
    key_linepos = 1;
    for (int i = 32; i < 128; i++) consolekeys[i] = true;
    for (int k : { K_ENTER, K_TAB, K_LEFTARROW, K_RIGHTARROW, K_UPARROW, K_DOWNARROW, K_BACKSPACE, K_PGUP, K_PGDN, K_SHIFT, K_MWHEELUP, K_MWHEELDOWN }) consolekeys[k] = true;
    consolekeys['`'] = consolekeys['~'] = false;
    for (int i = 0; i < 256; i++) keyshift[i] = i;
    for (int i = 'a'; i <= 'z'; i++) keyshift[i] = i - 'a' + 'A';
    const char *s_from = "1234567890-=/;'`\\", *s_to = "!@#$%^&*()_+:\"~|";
    for (size_t i = 0; s_from[i]; i++) keyshift[s_from[i]] = s_to[i];
    keyshift[','] = '<'; keyshift['.'] = '>'; keyshift['['] = '{'; keyshift[']'] = '}';
    menubound[K_ESCAPE] = true; for (int i = 0; i < 12; i++) menubound[K_F1 + i] = true;
    Cmd::AddCommand("bind", Key_Bind_f); Cmd::AddCommand("unbind", Key_Unbind_f); Cmd::AddCommand("unbindall", Key_Unbindall_f);
}

void Key_Event(int key, bool down) {
    keydown[key] = down; if (!down) key_repeats[key] = 0;
    key_lastpress = key; key_count++; if (key_count <= 0) return;
    if (down) {
        key_repeats[key]++;
        if (key != K_BACKSPACE && key != K_PAUSE && key_repeats[key] > 1) return;
        if (key >= 200 && keybindings[key].empty()) Console::Con_Printf("%s is unbound, hit F4 to set.\n", Key_KeynumToString(key));
    }
    if (key == K_SHIFT) shift_down = down;
    if (key == K_ESCAPE) {
        if (!down) return;
        switch (key_dest) {
        case key_message: Key_Message(key); break;
        case key_menu: Menu::M_Keydown(key); break;
        case key_game: case key_console: Menu::M_ToggleMenu_f(); break;
        default: Common::Sys_Error("Bad key_dest");
        }
        return;
    }
    if (!down) {
        auto ExecRelease = [](int k) {
            const auto& kb = keybindings[k]; if (!kb.empty() && kb[0] == '+') Cmd::BufferAddText(Common::va("-%s %i\n", kb.c_str() + 1, k));
        };
        ExecRelease(key); if (keyshift[key] != key) ExecRelease(keyshift[key]); return;
    }
    if (Client::cls.demoplayback && down && consolekeys[key] && key_dest == key_game) { Menu::M_ToggleMenu_f(); return; }
    if ((key_dest == key_menu && menubound[key]) || (key_dest == key_console && !consolekeys[key]) || (key_dest == key_game && (!Console::GetConsoleSystem().IsForcedUp() || !consolekeys[key]))) {
        const auto& kb = keybindings[key];
        if (!kb.empty()) { if (kb[0] == '+') Cmd::BufferAddText(Common::va("%s %i\n", kb.c_str(), key)); else { Cmd::BufferAddText(kb.c_str()); Cmd::BufferAddText("\n"); } }
        return;
    }
    if (!down) return;
    if (shift_down) key = keyshift[key];
    switch (key_dest) {
    case key_message: Key_Message(key); break;
    case key_menu: Menu::M_Keydown(key); break;
    case key_game: case key_console: Key_Console(key); break;
    default: Common::Sys_Error("Bad key_dest");
    }
}

} // namespace Keys

// ============================================================================
// MOUSE & PERIPHERAL INPUT SUBSYSTEM
// ============================================================================

namespace Input {

static cvar_t _windowed_mouse = {"_windowed_mouse", "1", {}, {}, {}, {}};
static qboolean mouse_avail = 0;
static float mouse_x = 0.0f, mouse_y = 0.0f;
static int mouse_oldbuttonstate = 0;

void IN_MouseMove(float xrel, float yrel) {
    mouse_x = xrel;
    mouse_y = yrel;
}

void IN_Init(void) {
    if (Common::COM_CheckParm("-nomouse")) return;
    mouse_x = mouse_y = 0.0f;
    mouse_avail = 1;
    Cvar::Register(&_windowed_mouse);
}

void IN_Shutdown(void) {
    mouse_avail = 0;
}

void IN_Commands(void) {
    int i, mouse_buttonstate;
    if (!mouse_avail) return;
    i = SDL_GetMouseState(NULL, NULL);
    mouse_buttonstate = (i & ~0x06) | ((i & 0x02) << 1) | ((i & 0x04) >> 1);
    for (i = 0; i < 3; i++) {
        if ((mouse_buttonstate & (1 << i)) && !(mouse_oldbuttonstate & (1 << i))) Keys::Key_Event(Keys::K_MOUSE1 + i, true);
        if (!(mouse_buttonstate & (1 << i)) && (mouse_oldbuttonstate & (1 << i))) Keys::Key_Event(Keys::K_MOUSE1 + i, false);
    }
    mouse_oldbuttonstate = mouse_buttonstate;
}

void IN_Move(usercmd_t* cmd) {
    if (!mouse_avail) return;
    mouse_x *= Client::sensitivity.value;
    mouse_y *= Client::sensitivity.value;
    if ((Client::in_strafe.state & 1) || (Client::lookstrafe.value && ((Client::in_mlook.state & 1) || _windowed_mouse.value))) {
        cmd->sidemove += Client::m_side.value * mouse_x;
    } else {
        Client::cl.viewangles[YAW] -= Client::m_yaw.value * mouse_x;
    }
    if ((Client::in_mlook.state & 1) || _windowed_mouse.value) {
        View::V_StopPitchDrift();
    }
    if (((Client::in_mlook.state & 1) || _windowed_mouse.value) && !(Client::in_strafe.state & 1)) {
        Client::cl.viewangles[PITCH] += Client::m_pitch.value * mouse_y;
        if (Client::cl.viewangles[PITCH] > 80) Client::cl.viewangles[PITCH] = 80;
        if (Client::cl.viewangles[PITCH] < -70) Client::cl.viewangles[PITCH] = -70;
    } else {
        if ((Client::in_strafe.state & 1) && Host::noclip_anglehack) {
            cmd->upmove -= Client::m_forward.value * mouse_y;
        } else {
            cmd->forwardmove -= Client::m_forward.value * mouse_y;
        }
    }
    mouse_x = mouse_y = 0.0f;
}

} // namespace Input

namespace Common {

void Sys_SendKeyEvents(void)
{
    SDL_Event event;
    int sym, state;
    int modstate;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            sym = event.key.keysym.sym;
            state = event.key.state;
            modstate = SDL_GetModState();
            switch (sym) {
            case SDLK_KP_ENTER:
            case SDLK_RETURN: sym = Keys::K_ENTER; break;
            case SDLK_ESCAPE: sym = Keys::K_ESCAPE; break;
            case SDLK_DELETE: sym = Keys::K_DEL; break;
            case SDLK_BACKSPACE: sym = Keys::K_BACKSPACE; break;
            case SDLK_F1: sym = Keys::K_F1; break;
            case SDLK_F2: sym = Keys::K_F2; break;
            case SDLK_F3: sym = Keys::K_F3; break;
            case SDLK_F4: sym = Keys::K_F4; break;
            case SDLK_F5: sym = Keys::K_F5; break;
            case SDLK_F6: sym = Keys::K_F6; break;
            case SDLK_F7: sym = Keys::K_F7; break;
            case SDLK_F8: sym = Keys::K_F8; break;
            case SDLK_F9: sym = Keys::K_F9; break;
            case SDLK_F10: sym = Keys::K_F10; break;
            case SDLK_F11: sym = Keys::K_F11; break;
            case SDLK_F12: sym = Keys::K_F12; break;
            case SDLK_PAUSE: sym = Keys::K_PAUSE; break;
            case SDLK_UP: sym = Keys::K_UPARROW; break;
            case SDLK_DOWN: sym = Keys::K_DOWNARROW; break;
            case SDLK_RIGHT: sym = Keys::K_RIGHTARROW; break;
            case SDLK_LEFT: sym = Keys::K_LEFTARROW; break;
            case SDLK_INSERT: sym = Keys::K_INS; break;
            case SDLK_HOME: sym = Keys::K_HOME; break;
            case SDLK_END: sym = Keys::K_END; break;
            case SDLK_PAGEUP: sym = Keys::K_PGUP; break;
            case SDLK_PAGEDOWN: sym = Keys::K_PGDN; break;
            case SDLK_RSHIFT:
            case SDLK_LSHIFT: sym = Keys::K_SHIFT; break;
            case SDLK_RCTRL:
            case SDLK_LCTRL: sym = Keys::K_CTRL; break;
            case SDLK_RALT:
            case SDLK_LALT: sym = Keys::K_ALT; break;
            case SDLK_KP_0: sym = (modstate & KMOD_NUM) ? SDLK_0 : Keys::K_INS; break;
            case SDLK_KP_1: sym = (modstate & KMOD_NUM) ? SDLK_1 : Keys::K_END; break;
            case SDLK_KP_2: sym = (modstate & KMOD_NUM) ? SDLK_2 : Keys::K_DOWNARROW; break;
            case SDLK_KP_3: sym = (modstate & KMOD_NUM) ? SDLK_3 : Keys::K_PGDN; break;
            case SDLK_KP_4: sym = (modstate & KMOD_NUM) ? SDLK_4 : Keys::K_LEFTARROW; break;
            case SDLK_KP_5: sym = SDLK_5; break;
            case SDLK_KP_6: sym = (modstate & KMOD_NUM) ? SDLK_6 : Keys::K_RIGHTARROW; break;
            case SDLK_KP_7: sym = (modstate & KMOD_NUM) ? SDLK_7 : Keys::K_HOME; break;
            case SDLK_KP_8: sym = (modstate & KMOD_NUM) ? SDLK_8 : Keys::K_UPARROW; break;
            case SDLK_KP_9: sym = (modstate & KMOD_NUM) ? SDLK_9 : Keys::K_PGUP; break;
            }
            if (sym > 255) sym = 0;
            Keys::Key_Event(sym, state);
            break;
        case SDL_MOUSEMOTION:
            if (((unsigned)event.motion.x != (Vid::vid.width / 2)) || ((unsigned)event.motion.y != (Vid::vid.height / 2))) {
                Input::IN_MouseMove(static_cast<float>(event.motion.xrel * 10), static_cast<float>(event.motion.yrel * 10));
                if (((unsigned)event.motion.x < ((Vid::vid.width / 2) - (Vid::vid.width / 4))) || ((unsigned)event.motion.x > ((Vid::vid.width / 2) + (Vid::vid.width / 4))) || ((unsigned)event.motion.y < ((Vid::vid.height / 2) - (Vid::vid.height / 4))) || ((unsigned)event.motion.y > ((Vid::vid.height / 2) + (Vid::vid.height / 4)))) {
                    if (Vid::GetWindow()) SDL_WarpMouseInWindow(Vid::GetWindow(), Vid::vid.width / 2, Vid::vid.height / 2);
                }
            }
            break;
        case SDL_QUIT:
            Client::CL_Disconnect();
            Host::Host_ShutdownServer(false);
            Sys_Quit();
            break;
        default:
            break;
        }
    }
}

} // namespace Common
