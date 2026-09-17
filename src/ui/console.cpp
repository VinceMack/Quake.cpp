// console.cpp -- In-game console subsystem (buffer, rendering, input line, notifications, logging)
#include "quakedef.hpp"
#include "ui/console.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

using namespace Client;
using namespace Common;
using namespace Console;
using namespace Render;
using namespace Draw;
using namespace Host;
using namespace Keys;
using namespace Menu;
using namespace Screen;
using namespace Audio;
using namespace Vid;
using namespace Cvar;
using namespace Cmd;

namespace Console {

constexpr int CON_TEXTSIZE = 16384, NUM_CON_TIMES = 4, MAXPRINTMSG = 4096;
static cvar_t con_notifytime = { "con_notifytime", "3", {}, {}, {}, {} };

struct CmdPair { const char* name; void (*fn)(); };

ConsoleSystem::ConsoleSystem() : text_(CON_TEXTSIZE, ' '), times_(NUM_CON_TIMES, 0.0f) {}
ConsoleSystem& GetConsoleSystem() noexcept { static ConsoleSystem instance; return instance; }

void ConsoleSystem::ToggleConsole() {
    if (key_dest == key_console) {
        if (cls.state == ca_connected) { key_dest = key_game; key_lines[edit_line][1] = 0; key_linepos = 1; }
        else M_Menu_Main_f();
    } else key_dest = key_console;
    Screen::GetScreenSystem().EndLoadingPlaque(); eastl::fill(times_.begin(), times_.end(), 0.0f);
}

void ConsoleSystem::Clear() { eastl::fill(text_.begin(), text_.end(), ' '); }
void ConsoleSystem::ClearNotify() { eastl::fill(times_.begin(), times_.end(), 0.0f); }

static void Con_MessageMode_f() { key_dest = key_message; team_message = false; }
static void Con_MessageMode2_f() { key_dest = key_message; team_message = true; }

void ConsoleSystem::CheckResize() {
    int width = (vid.width >> 3) - 2; if (width == linewidth_) return;
    if (width < 1) { linewidth_ = 38; totallines_ = CON_TEXTSIZE / linewidth_; eastl::fill(text_.begin(), text_.end(), ' '); }
    else {
        int oldwidth = linewidth_, oldtotallines = totallines_; linewidth_ = width; totallines_ = CON_TEXTSIZE / linewidth_;
        int numlines = eastl::min(oldtotallines, totallines_), numchars = eastl::min(oldwidth, linewidth_);
        eastl::vector<char> tbuf = text_; eastl::fill(text_.begin(), text_.end(), ' ');
        for (int i = 0; i < numlines; i++) {
            for (int j = 0; j < numchars; j++) text_[(totallines_ - 1 - i) * linewidth_ + j] = tbuf[((current_ - i + oldtotallines) % oldtotallines) * oldwidth + j];
        }
        ClearNotify();
    }
    backscroll_ = 0; current_ = totallines_ - 1;
}

void ConsoleSystem::Init() {
    debuglog_ = COM_CheckParm("-condebug") != 0;
    if (debuglog_) { std::error_code ec; std::filesystem::remove((eastl::string(com_gamedir) + "/qconsole.log").c_str(), ec); }
    eastl::fill(text_.begin(), text_.end(), ' '); linewidth_ = -1; CheckResize();
    Printf("Console initialized.\n"); Cvar::Register(&con_notifytime);
    constexpr CmdPair cmds[] = {
        {"toggleconsole", ConsoleSystem::ToggleConsole_f}, {"messagemode", Con_MessageMode_f},
        {"messagemode2", Con_MessageMode2_f}, {"clear", ConsoleSystem::Clear_f}
    };
    for (auto [name, fn] : cmds) Cmd::AddCommand(name, fn);
    initialized_ = true;
}

void ConsoleSystem::Linefeed() {
    if (!initialized_) return;
    x_ = 0;
    current_++;
    eastl::fill_n(text_.begin() + (current_ % totallines_) * linewidth_, linewidth_, ' ');
}

void ConsoleSystem::Print(eastl::string_view txt) {
    if (!initialized_) return;
    backscroll_ = 0;
    int mask = 0; size_t index = 0;
    if (!txt.empty() && txt[0] == 1) { mask = 128; S_LocalSound("misc/talk.wav"); index = 1; }
    else if (!txt.empty() && txt[0] == 2) { mask = 128; index = 1; }
    static bool cr = false;
    while (index < txt.length()) {
        char c = txt[index];
        int l; for (l = 0; l < linewidth_; l++) { if (index + l >= txt.length() || txt[index + l] <= ' ') break; }
        if (l != linewidth_ && (x_ + l > linewidth_)) x_ = 0;
        index++; if (cr) { current_--; cr = false; }
        if (x_ == 0) { Linefeed(); if (current_ >= 0) times_[current_ % NUM_CON_TIMES] = static_cast<float>(realtime); }
        switch (c) {
        case '\n': x_ = 0; break; case '\r': x_ = 0; cr = true; break;
        default: text_[(current_ % totallines_) * linewidth_ + x_] = static_cast<char>(c | mask); if (++x_ >= linewidth_) x_ = 0; break;
        }
    }
}

void ConsoleSystem::DebugLog(eastl::string_view file, eastl::string_view text) {
    std::ofstream log_file(file.data(), std::ios::app | std::ios::binary); if (log_file) log_file.write(text.data(), text.size());
}

void ConsoleSystem::Printf(const char* fmt, ...) {
    va_list argptr; char msg[MAXPRINTMSG]; static bool inupdate = false;
    va_start(argptr, fmt); vsprintf_s(msg, sizeof(msg), fmt, argptr); va_end(argptr);
    Sys_Printf("%s", msg); if (debuglog_) DebugLog((eastl::string(com_gamedir) + "/qconsole.log").c_str(), msg);
    if (!initialized_ || cls.state == ca_dedicated) return;
    Print(msg);
    if (cls.signon != SIGNONS && !Screen::GetScreenSystem().GetDisabledForLoading() && !inupdate) {
        inupdate = true; Screen::GetScreenSystem().UpdateScreen(); inupdate = false;
    }
}

void ConsoleSystem::DPrintf(const char* fmt, ...) {
    if (!developer.value) return;
    va_list argptr; char msg[MAXPRINTMSG]; va_start(argptr, fmt); vsprintf_s(msg, sizeof(msg), fmt, argptr); va_end(argptr); Printf("%s", msg);
}

void ConsoleSystem::DrawInput() {
    if (key_dest != key_console && !forcedup_) return;
    char* text = key_lines[edit_line].data(); text[key_linepos] = static_cast<char>(10 + ((int)(realtime * cursorspeed_) & 1));
    eastl::fill_n(text + key_linepos + 1, eastl::max(0, linewidth_ - (key_linepos + 1)), ' ');
    char* text_ptr = (key_linepos >= linewidth_) ? (text + 1 + key_linepos - linewidth_) : text;
    for (int i = 0; i < linewidth_; i++) Draw_Character((i + 1) << 3, vislines_ - 16, text_ptr[i]);
    key_lines[edit_line][key_linepos] = 0;
}

void ConsoleSystem::DrawNotify() {
    int v = 0;
    for (int i = current_ - NUM_CON_TIMES + 1; i <= current_; i++) {
        if (i < 0) continue;
        float time = times_[i % NUM_CON_TIMES]; if (time == 0.0f || (realtime - time) > con_notifytime.value) continue;
        char* text_ptr = text_.data() + (i % totallines_) * linewidth_;
        Screen::GetScreenSystem().SetClearnotify(0); Screen::GetScreenSystem().SetCopytop(1);
        for (int x = 0; x < linewidth_; x++) Draw_Character((x + 1) << 3, v, text_ptr[x]);
        v += 8;
    }
    if (key_dest == key_message) {
        Screen::GetScreenSystem().SetClearnotify(0); Screen::GetScreenSystem().SetCopytop(1);
        int x = 0; Draw_String(8, v, "say:");
        while (chat_buffer[x]) { Draw_Character((x + 5) << 3, v, chat_buffer[x]); x++; }
        Draw_Character((x + 5) << 3, v, static_cast<char>(10 + ((int)(realtime * cursorspeed_) & 1))); v += 8;
    }
    if (v > notifylines_) notifylines_ = v;
}

void ConsoleSystem::DrawConsole(int lines, bool drawinput) {
    if (lines <= 0) return;
    Draw_ConsoleBackground(lines); vislines_ = lines;
    int rows = (lines - 16) >> 3, y = lines - 16 - (rows << 3);
    for (int i = current_ - rows + 1; i <= current_; i++, y += 8) {
        int j = eastl::max(0, i - backscroll_); char* text_ptr = text_.data() + (j % totallines_) * linewidth_;
        for (int x = 0; x < linewidth_; x++) Draw_Character((x + 1) << 3, y, text_ptr[x]);
    }
    if (drawinput) DrawInput();
}

void ConsoleSystem::Clear_f() { GetConsoleSystem().Clear(); }
void ConsoleSystem::ToggleConsole_f() { GetConsoleSystem().ToggleConsole(); }

void Con_Printf(const char* fmt, ...) {
    va_list argptr; char msg[MAXPRINTMSG]; va_start(argptr, fmt); vsprintf_s(msg, sizeof(msg), fmt, argptr); va_end(argptr); GetConsoleSystem().Printf("%s", msg);
}
void Con_DPrintf(const char* fmt, ...) {
    va_list argptr; char msg[MAXPRINTMSG]; va_start(argptr, fmt); vsprintf_s(msg, sizeof(msg), fmt, argptr); va_end(argptr); GetConsoleSystem().DPrintf("%s", msg);
}

} // namespace Console
