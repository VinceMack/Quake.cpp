// console.cpp -- In-game console subsystem (buffer, rendering, input line, notifications, logging)
#include "ui/console.hpp"
#include "platform/crt_compat.hpp"
#include "host/host.hpp"
#include "core/cvar.hpp"
#include "platform/system.hpp"
#include "client/input.hpp"
#include "render/draw2d.hpp"
#include "render/software/sw_vid.hpp"
#include "audio/audio_main.hpp"
#include "client/client_types.hpp"
#include "ui/screen.hpp"
#include "ui/menu.hpp"
#include "core/filesystem.hpp"
#include "core/cmd.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace Console {

constexpr int CON_TEXTSIZE = 16384, NUM_CON_TIMES = 4, MAXPRINTMSG = 4096;
static cvar_t con_notifytime = { "con_notifytime", "3", {}, {}, {}, {} };

struct CmdPair { const char* name; void (*fn)(); };

ConsoleSystem::ConsoleSystem() : text_(CON_TEXTSIZE, ' '), times_(NUM_CON_TIMES, 0.0f) {}
ConsoleSystem& GetConsoleSystem() noexcept { static ConsoleSystem instance; return instance; }

void ConsoleSystem::ToggleConsole() {
    if (Keys::key_dest == Keys::key_console) {
        if (Client::cls.state == ca_connected) { Keys::key_dest = Keys::key_game; Keys::key_lines[Keys::edit_line][1] = 0; Keys::key_linepos = 1; }
        else Menu::M_Menu_Main_f();
    } else Keys::key_dest = Keys::key_console;
    Screen::GetScreenSystem().EndLoadingPlaque(); std::fill(times_.begin(), times_.end(), 0.0f);
}

void ConsoleSystem::Clear() { std::fill(text_.begin(), text_.end(), ' '); }
void ConsoleSystem::ClearNotify() { std::fill(times_.begin(), times_.end(), 0.0f); }

static void Con_MessageMode_f() { Keys::key_dest = Keys::key_message; Keys::team_message = false; }
static void Con_MessageMode2_f() { Keys::key_dest = Keys::key_message; Keys::team_message = true; }

void ConsoleSystem::CheckResize() {
    int width = (Vid::vid.width >> 3) - 2; if (width == linewidth_) return;
    if (width < 1) { linewidth_ = 38; totallines_ = CON_TEXTSIZE / linewidth_; std::fill(text_.begin(), text_.end(), ' '); }
    else {
        int oldwidth = linewidth_, oldtotallines = totallines_; linewidth_ = width; totallines_ = CON_TEXTSIZE / linewidth_;
        int numlines = std::min(oldtotallines, totallines_), numchars = std::min(oldwidth, linewidth_);
        std::vector<char> tbuf = text_; std::fill(text_.begin(), text_.end(), ' ');
        for (int i = 0; i < numlines; i++) {
            for (int j = 0; j < numchars; j++) text_[(totallines_ - 1 - i) * linewidth_ + j] = tbuf[((current_ - i + oldtotallines) % oldtotallines) * oldwidth + j];
        }
        ClearNotify();
    }
    backscroll_ = 0; current_ = totallines_ - 1;
}

void ConsoleSystem::Init() {
    debuglog_ = Common::COM_CheckParm("-condebug") != 0;
    if (debuglog_) { std::error_code ec; std::filesystem::remove((std::string(Common::com_gamedir) + "/qconsole.log").c_str(), ec); }
    std::fill(text_.begin(), text_.end(), ' '); linewidth_ = -1; CheckResize();
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
    std::fill_n(text_.begin() + (current_ % totallines_) * linewidth_, linewidth_, ' ');
}

void ConsoleSystem::Print(std::string_view txt) {
    if (!initialized_) return;
    backscroll_ = 0;
    int mask = 0; size_t index = 0;
    if (!txt.empty() && txt[0] == 1) { mask = 128; Audio::S_LocalSound("misc/talk.wav"); index = 1; }
    else if (!txt.empty() && txt[0] == 2) { mask = 128; index = 1; }
    static bool cr = false;
    while (index < txt.length()) {
        char c = txt[index];
        int l; for (l = 0; l < linewidth_; l++) { if (index + l >= txt.length() || txt[index + l] <= ' ') break; }
        if (l != linewidth_ && (x_ + l > linewidth_)) x_ = 0;
        index++; if (cr) { current_--; cr = false; }
        if (x_ == 0) { Linefeed(); if (current_ >= 0) times_[current_ % NUM_CON_TIMES] = static_cast<float>(Host::realtime); }
        switch (c) {
        case '\n': x_ = 0; break; case '\r': x_ = 0; cr = true; break;
        default: text_[(current_ % totallines_) * linewidth_ + x_] = static_cast<char>(c | mask); if (++x_ >= linewidth_) x_ = 0; break;
        }
    }
}

void ConsoleSystem::DebugLog(std::string_view file, std::string_view text) {
    std::ofstream log_file(file.data(), std::ios::app | std::ios::binary); if (log_file) log_file.write(text.data(), text.size());
}

void ConsoleSystem::Printf(const char* fmt, ...) {
    va_list argptr; char msg[MAXPRINTMSG]; static bool inupdate = false;
    va_start(argptr, fmt); vsprintf_s(msg, sizeof(msg), fmt, argptr); va_end(argptr);
    Common::Sys_Printf("%s", msg); if (debuglog_) DebugLog((std::string(Common::com_gamedir) + "/qconsole.log").c_str(), msg);
    if (!initialized_ || Client::cls.state == ca_dedicated) return;
    Print(msg);
    if (Client::cls.signon != SIGNONS && !Screen::GetScreenSystem().GetDisabledForLoading() && !inupdate) {
        inupdate = true; Screen::GetScreenSystem().UpdateScreen(); inupdate = false;
    }
}

void ConsoleSystem::DPrintf(const char* fmt, ...) {
    if (!Host::developer.value) return;
    va_list argptr; char msg[MAXPRINTMSG]; va_start(argptr, fmt); vsprintf_s(msg, sizeof(msg), fmt, argptr); va_end(argptr); Printf("%s", msg);
}

void ConsoleSystem::DrawInput() {
    if (Keys::key_dest != Keys::key_console && !forcedup_) return;
    char* text = Keys::key_lines[Keys::edit_line].data(); text[Keys::key_linepos] = static_cast<char>(10 + ((int)(Host::realtime * cursorspeed_) & 1));
    std::fill_n(text + Keys::key_linepos + 1, std::max(0, linewidth_ - (Keys::key_linepos + 1)), ' ');
    char* text_ptr = (Keys::key_linepos >= linewidth_) ? (text + 1 + Keys::key_linepos - linewidth_) : text;
    for (int i = 0; i < linewidth_; i++) Draw::Draw_Character((i + 1) << 3, vislines_ - 16, text_ptr[i]);
    Keys::key_lines[Keys::edit_line][Keys::key_linepos] = 0;
}

void ConsoleSystem::DrawNotify() {
    int v = 0;
    for (int i = current_ - NUM_CON_TIMES + 1; i <= current_; i++) {
        if (i < 0) continue;
        float time = times_[i % NUM_CON_TIMES]; if (time == 0.0f || (Host::realtime - time) > con_notifytime.value) continue;
        char* text_ptr = text_.data() + (i % totallines_) * linewidth_;
        Screen::GetScreenSystem().SetClearnotify(0); Screen::GetScreenSystem().SetCopytop(1);
        for (int x = 0; x < linewidth_; x++) Draw::Draw_Character((x + 1) << 3, v, text_ptr[x]);
        v += 8;
    }
    if (Keys::key_dest == Keys::key_message) {
        Screen::GetScreenSystem().SetClearnotify(0); Screen::GetScreenSystem().SetCopytop(1);
        int x = 0; Draw::Draw_String(8, v, "say:");
        while (Keys::chat_buffer[x]) { Draw::Draw_Character((x + 5) << 3, v, Keys::chat_buffer[x]); x++; }
        Draw::Draw_Character((x + 5) << 3, v, static_cast<char>(10 + ((int)(Host::realtime * cursorspeed_) & 1))); v += 8;
    }
    if (v > notifylines_) notifylines_ = v;
}

void ConsoleSystem::DrawConsole(int lines, bool drawinput) {
    if (lines <= 0) return;
    Draw::Draw_ConsoleBackground(lines); vislines_ = lines;
    int rows = (lines - 16) >> 3, y = lines - 16 - (rows << 3);
    for (int i = current_ - rows + 1; i <= current_; i++, y += 8) {
        int j = std::max(0, i - backscroll_); char* text_ptr = text_.data() + (j % totallines_) * linewidth_;
        for (int x = 0; x < linewidth_; x++) Draw::Draw_Character((x + 1) << 3, y, text_ptr[x]);
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
