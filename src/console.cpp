// console.cpp -- console text display and input

#include <vector>
#include <array>
#include <string_view>
#include <string>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <cstdarg>
#include <cstring>
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

namespace Console {

constexpr int CON_TEXTSIZE = 16384;
constexpr int NUM_CON_TIMES = 4;
constexpr int MAXPRINTMSG = 4096;

static cvar_t con_notifytime = { "con_notifytime", "3", {}, {}, {}, {} }; // seconds

ConsoleSystem::ConsoleSystem()
    : text_(CON_TEXTSIZE, ' '),
      times_(NUM_CON_TIMES, 0.0f)
{
}

ConsoleSystem& GetConsoleSystem()
{
    static ConsoleSystem instance;
    return instance;
}

/*
================
ConsoleSystem::ToggleConsole
================
*/
void ConsoleSystem::ToggleConsole()
{
    if (key_dest == key_console) {
        if (cls.state == ca_connected) {
            key_dest = key_game;
            key_lines[edit_line][1] = 0; // clear any typing
            key_linepos = 1;
        } else {
            M_Menu_Main_f();
        }
    } else {
        key_dest = key_console;
    }

    Screen::GetScreenSystem().EndLoadingPlaque();
    std::fill(times_.begin(), times_.end(), 0.0f);
}

/*
================
ConsoleSystem::Clear
================
*/
void ConsoleSystem::Clear()
{
    std::fill(text_.begin(), text_.end(), ' ');
}

/*
================
ConsoleSystem::ClearNotify
================
*/
void ConsoleSystem::ClearNotify()
{
    std::fill(times_.begin(), times_.end(), 0.0f);
}

/*
================
Con_MessageMode_f
================
*/
static void Con_MessageMode_f(void)
{
    key_dest = key_message;
    team_message = false;
}

/*
================
Con_MessageMode2_f
================
*/
static void Con_MessageMode2_f(void)
{
    key_dest = key_message;
    team_message = true;
}

/*
================
ConsoleSystem::CheckResize

If the line width has changed, reformat the buffer.
================
*/
void ConsoleSystem::CheckResize()
{
    int width = (vid.width >> 3) - 2;

    if (width == linewidth_) {
        return;
    }

    if (width < 1) { // video hasn't been initialized yet
        linewidth_ = 38;
        totallines_ = CON_TEXTSIZE / linewidth_;
        std::fill(text_.begin(), text_.end(), ' ');
    } else {
        int oldwidth = linewidth_;
        linewidth_ = width;
        int oldtotallines = totallines_;
        totallines_ = CON_TEXTSIZE / linewidth_;
        int numlines = oldtotallines;

        if (totallines_ < numlines) {
            numlines = totallines_;
        }

        int numchars = oldwidth;

        if (linewidth_ < numchars) {
            numchars = linewidth_;
        }

        eastl::vector<char> tbuf = text_;
        std::fill(text_.begin(), text_.end(), ' ');

        for (int i = 0; i < numlines; i++) {
            for (int j = 0; j < numchars; j++) {
                text_[(totallines_ - 1 - i) * linewidth_ + j] = 
                    tbuf[((current_ - i + oldtotallines) % oldtotallines) * oldwidth + j];
            }
        }

        ClearNotify();
    }

    backscroll_ = 0;
    current_ = totallines_ - 1;
}

/*
================
ConsoleSystem::Init
================
*/
void ConsoleSystem::Init()
{
    debuglog_ = COM_CheckParm("-condebug") != 0;

    if (debuglog_) {
        std::string log_path = std::string(com_gamedir) + "/qconsole.log";
        std::error_code ec;
        std::filesystem::remove(log_path, ec);
    }

    std::fill(text_.begin(), text_.end(), ' ');
    linewidth_ = -1;
    CheckResize();

    Printf("Console initialized.\n");

    // register our commands
    Cvar::Register(&con_notifytime);

    Cmd::AddCommand("toggleconsole", Con_ToggleConsole_f);
    Cmd::AddCommand("messagemode", Con_MessageMode_f);
    Cmd::AddCommand("messagemode2", Con_MessageMode2_f);
    Cmd::AddCommand("clear", Con_Clear_f);
    initialized_ = true;
}

/*
===============
ConsoleSystem::Linefeed
===============
*/
void ConsoleSystem::Linefeed()
{
    if (!initialized_) {
        return;
    }

    x_ = 0;
    current_++;
    std::fill(text_.begin() + (current_ % totallines_) * linewidth_,
              text_.begin() + (current_ % totallines_) * linewidth_ + linewidth_,
              ' ');
}

/*
================
ConsoleSystem::Print

Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be logged to disk
If no console is visible, the notify window will pop up.
================
*/
void ConsoleSystem::Print(std::string_view txt)
{
    if (!initialized_) {
        return;
    }

    backscroll_ = 0;

    int mask = 0;
    size_t index = 0;
    if (!txt.empty() && txt[0] == 1) {
        mask = 128; // go to colored text
        S_LocalSound("misc/talk.wav");
        index = 1;
    } else if (!txt.empty() && txt[0] == 2) {
        mask = 128; // go to colored text
        index = 1;
    }

    static bool cr = false;

    while (index < txt.length()) {
        char c = txt[index];

        // count word length
        int l;
        for (l = 0; l < linewidth_; l++) {
            if (index + l >= txt.length() || txt[index + l] <= ' ') {
                break;
            }
        }

        // word wrap
        if (l != linewidth_ && (x_ + l > linewidth_)) {
            x_ = 0;
        }

        index++;

        if (cr) {
            current_--;
            cr = false;
        }

        if (x_ == 0) {
            Linefeed();
            // mark time for transparent overlay
            if (current_ >= 0) {
                times_[current_ % NUM_CON_TIMES] = static_cast<float>(realtime);
            }
        }

        switch (c) {
        case '\n':
            x_ = 0;
            break;

        case '\r':
            x_ = 0;
            cr = true;
            break;

        default: // display character and advance
            int y = current_ % totallines_;
            text_[y * linewidth_ + x_] = static_cast<char>(c | mask);
            x_++;
            if (x_ >= linewidth_) {
                x_ = 0;
            }
            break;
        }
    }
}

/*
================
ConsoleSystem::DebugLog
================
*/
void ConsoleSystem::DebugLog(std::string_view file, std::string_view text)
{
    std::ofstream log_file(std::string(file), std::ios::app | std::ios::binary);
    if (log_file) {
        log_file.write(text.data(), text.size());
    }
}

/*
================
ConsoleSystem::Printf

Handles cursor positioning, line wrapping, etc
================
*/
void ConsoleSystem::Printf(const char* fmt, ...)
{
    va_list argptr;
    char msg[MAXPRINTMSG];
    static bool inupdate = false;

    va_start(argptr, fmt);
    vsprintf_s(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    // also echo to debugging console
    Sys_Printf("%s", msg);

    // log all messages to file
    if (debuglog_) {
        std::string log_file_path = std::string(com_gamedir) + "/qconsole.log";
        DebugLog(log_file_path, msg);
    }

    if (!initialized_) {
        return;
    }

    if (cls.state == ca_dedicated) {
        return; // no graphics mode
    }

    // write it to the scrollable buffer
    Print(msg);

    // update the screen if the console is displayed
    if (cls.signon != SIGNONS && !Screen::GetScreenSystem().GetDisabledForLoading()) {
        // protect against infinite loop if something in SCR_UpdateScreen calls Con_Print
        if (!inupdate) {
            inupdate = true;
            Screen::GetScreenSystem().UpdateScreen();
            inupdate = false;
        }
    }
}

/*
================
ConsoleSystem::DPrintf

A Con_Printf that only shows up if the "developer" cvar is set
================
*/
void ConsoleSystem::DPrintf(const char* fmt, ...)
{
    va_list argptr;
    char msg[MAXPRINTMSG];

    if (!developer.value) {
        return; // don't confuse non-developers with techie stuff...
    }

    va_start(argptr, fmt);
    vsprintf_s(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    Printf("%s", msg);
}

/*
==============================================================================

DRAWING

==============================================================================
*/

/*
================
ConsoleSystem::DrawInput

The input line scrolls horizontally if typing goes beyond the right edge
================
*/
void ConsoleSystem::DrawInput()
{
    if (key_dest != key_console && !forcedup_) {
        return; // don't draw anything
    }

    char* text = key_lines[edit_line].data();

    // add the cursor frame
    text[key_linepos] = static_cast<char>(10 + ((int)(realtime * cursorspeed_) & 1));

    // fill out remainder with spaces
    for (int i = key_linepos + 1; i < linewidth_; i++) {
        text[i] = ' ';
    }

    // prestep if horizontally scrolling
    char* text_ptr = text;
    if (key_linepos >= linewidth_) {
        text_ptr += 1 + key_linepos - linewidth_;
    }

    // draw it
    for (int i = 0; i < linewidth_; i++) {
        Draw_Character((i + 1) << 3, vislines_ - 16, text_ptr[i]);
    }

    // remove cursor
    key_lines[edit_line][key_linepos] = 0;
}

/*
================
ConsoleSystem::DrawNotify

Draws the last few lines of output transparently over the game top
================
*/
void ConsoleSystem::DrawNotify()
{
    int v = 0;
    for (int i = current_ - NUM_CON_TIMES + 1; i <= current_; i++) {
        if (i < 0) {
            continue;
        }

        float time = times_[i % NUM_CON_TIMES];
        if (time == 0.0f) {
            continue;
        }

        time = static_cast<float>(realtime - time);
        if (time > con_notifytime.value) {
            continue;
        }

        char* text_ptr = text_.data() + (i % totallines_) * linewidth_;

        Screen::GetScreenSystem().SetClearnotify(0);
        Screen::GetScreenSystem().SetCopytop(1);

        for (int x = 0; x < linewidth_; x++) {
            Draw_Character((x + 1) << 3, v, text_ptr[x]);
        }

        v += 8;
    }

    if (key_dest == key_message) {
        Screen::GetScreenSystem().SetClearnotify(0);
        Screen::GetScreenSystem().SetCopytop(1);

        int x = 0;

        Draw_String(8, v, "say:");
        while (chat_buffer[x]) {
            Draw_Character((x + 5) << 3, v, chat_buffer[x]);
            x++;
        }
        Draw_Character((x + 5) << 3, v,
            static_cast<char>(10 + ((int)(realtime * cursorspeed_) & 1)));
        v += 8;
    }

    if (v > notifylines_) {
        notifylines_ = v;
    }
}

/*
================
ConsoleSystem::DrawConsole

Draws the console with the solid background
The typing input line at the bottom should only be drawn if typing is allowed
================
*/
void ConsoleSystem::DrawConsole(int lines, bool drawinput)
{
    if (lines <= 0) {
        return;
    }

    // draw the background
    Draw_ConsoleBackground(lines);

    // draw the text
    vislines_ = lines;

    int rows = (lines - 16) >> 3;     // rows of text to draw
    int y = lines - 16 - (rows << 3); // may start slightly negative

    for (int i = current_ - rows + 1; i <= current_; i++, y += 8) {
        int j = i - backscroll_;
        if (j < 0) {
            j = 0;
        }

        char* text_ptr = text_.data() + (j % totallines_) * linewidth_;

        for (int x = 0; x < linewidth_; x++) {
            Draw_Character((x + 1) << 3, y, text_ptr[x]);
        }
    }

    // draw the input prompt, user text, and cursor if desired
    if (drawinput) {
        DrawInput();
    }
}

// Delegate functions to maintain compatibility with other modules:
void Con_CheckResize()
{
    GetConsoleSystem().CheckResize();
}

void Con_Init()
{
    GetConsoleSystem().Init();
}

void Con_DrawConsole(int lines, bool drawinput)
{
    GetConsoleSystem().DrawConsole(lines, drawinput);
}

void Con_Printf(const char* fmt, ...)
{
    va_list argptr;
    char msg[MAXPRINTMSG];
    va_start(argptr, fmt);
    vsprintf_s(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);
    
    GetConsoleSystem().Printf("%s", msg);
}

void Con_DPrintf(const char* fmt, ...)
{
    va_list argptr;
    char msg[MAXPRINTMSG];
    va_start(argptr, fmt);
    vsprintf_s(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    GetConsoleSystem().DPrintf("%s", msg);
}

void Con_Clear_f()
{
    GetConsoleSystem().Clear();
}

void Con_DrawNotify()
{
    GetConsoleSystem().DrawNotify();
}

void Con_ClearNotify()
{
    GetConsoleSystem().ClearNotify();
}

void Con_ToggleConsole_f()
{
    GetConsoleSystem().ToggleConsole();
}

} // namespace Console
