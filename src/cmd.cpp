// cmd.cpp -- Quake script command processing module

#include "quakedef.hpp"
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <algorithm>
#include <cctype>

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

namespace Cmd {

CommandRegistry& GetCommandRegistry()
{
    static CommandRegistry registry;
    return registry;
}

//=============================================================================

/*
============
Wait_f
============
*/
static void Wait_f(void)
{
    GetCommandRegistry().GetCmdWait() = true;
}

//=============================================================================
//						COMMAND BUFFER
//=============================================================================

void CommandRegistry::BufferInit(void)
{
    cmd_text_.clear();
    cmd_text_.reserve(8192);
}

void CommandRegistry::BufferAddText(std::string_view text)
{
    if (cmd_text_.length() + text.length() >= 8192) {
        Con_Printf("Cmd::BufferAddText: overflow\n");
        return;
    }
    cmd_text_ += text;
}

void CommandRegistry::BufferInsertText(std::string_view text)
{
    if (cmd_text_.length() + text.length() >= 8192) {
        Con_Printf("Cmd::BufferAddText: overflow\n");
        return;
    }
    cmd_text_.insert(0, text);
}

void CommandRegistry::BufferExecute(void)
{
    while (!cmd_text_.empty()) {
        int quotes = 0;
        size_t i = 0;
        for (i = 0; i < cmd_text_.length(); ++i) {
            if (cmd_text_[i] == '"') {
                quotes++;
            }
            if (!(quotes & 1) && cmd_text_[i] == ';') {
                break;
            }
            if (cmd_text_[i] == '\n') {
                break;
            }
        }

        std::string line = cmd_text_.substr(0, i);

        if (i == cmd_text_.length()) {
            cmd_text_.clear();
        } else {
            cmd_text_.erase(0, i + 1);
        }

        ExecuteString(line, Source::Command);

        if (cmd_wait_) {
            cmd_wait_ = false;
            break;
        }
    }
}

//==============================================================================
//						SCRIPT COMMANDS
//==============================================================================

static void StuffCmds_f(void)
{
    if (Cmd::Argc() != 1) {
        Con_Printf("stuffcmds : execute command line parameters\n");
        return;
    }

    std::string text;
    for (int i = 1; i < com_argc; i++) {
        if (!com_argv[i]) {
            continue;
        }
        if (!text.empty()) {
            text += " ";
        }
        text += com_argv[i];
    }

    if (text.empty()) {
        return;
    }

    std::string build;
    size_t i = 0;
    while (i < text.length()) {
        if (text[i] == '+') {
            i++;
            size_t j = i;
            while (j < text.length() && text[j] != '+' && text[j] != '-') {
                j++;
            }
            build += text.substr(i, j - i);
            build += "\n";
            i = j;
        } else {
            i++;
        }
    }

    if (!build.empty()) {
        Cmd::BufferInsertText(build);
    }
}

static void Exec_f(void)
{
    if (Cmd::Argc() != 2) {
        Con_Printf("exec <filename> : execute a script file\n");
        return;
    }

    int mark = Hunk_LowMark();
    std::string_view filename = Cmd::Argv(1);
    std::string filename_str(filename);
    char* f = (char*)COM_LoadHunkFile(filename_str.c_str());
    if (!f) {
        Con_Printf("couldn't exec %s\n", filename_str.c_str());
        return;
    }

    Con_Printf("execing %s\n", filename_str.c_str());

    Cmd::BufferInsertText(f);
    Hunk_FreeToLowMark(mark);
}

static void Echo_f(void)
{
    for (int i = 1; i < Cmd::Argc(); i++) {
        std::string_view arg = Cmd::Argv(i);
        Con_Printf("%.*s ", static_cast<int>(arg.length()), arg.data());
    }
    Con_Printf("\n");
}

static void Alias_f(void)
{
    auto& registry = GetCommandRegistry();
    if (Cmd::Argc() == 1) {
        Con_Printf("Current alias commands:\n");
        for (const auto& [name, value] : registry.GetAliases()) {
            Con_Printf("%s : %s\n", name.c_str(), value.c_str());
        }
        return;
    }

    std::string_view alias_name = Cmd::Argv(1);
    if (alias_name.length() >= 32) { // MAX_ALIAS_NAME
        Con_Printf("Alias name is too long\n");
        return;
    }

    std::string cmd;
    int c = Cmd::Argc();
    for (int i = 2; i < c; ++i) {
        cmd += Cmd::Argv(i);
        cmd += " ";
    }
    cmd += "\n";

    registry.AddAlias(alias_name, cmd);
}

//=============================================================================
//						COMMAND EXECUTION
//=============================================================================

void CommandRegistry::Init(void)
{
    AddCommand("stuffcmds", StuffCmds_f);
    AddCommand("exec", Exec_f);
    AddCommand("echo", Echo_f);
    AddCommand("alias", Alias_f);
    AddCommand("cmd", ForwardToServer);
    AddCommand("wait", Wait_f);
}

void CommandRegistry::AddCommand(std::string_view cmd_name, xcommand_t function)
{
    if (host_initialized) {
        Sys_Error("Cmd::AddCommand after host_initialized");
    }

    if (Cvar::FindVar(cmd_name) != nullptr) {
        Con_Printf("Cmd::AddCommand: %.*s already defined as a var\n", static_cast<int>(cmd_name.length()), cmd_name.data());
        return;
    }

    if (Exists(cmd_name)) {
        Con_Printf("Cmd::AddCommand: %.*s already defined\n", static_cast<int>(cmd_name.length()), cmd_name.data());
        return;
    }

    commands_.emplace(cmd_name, std::move(function));
}

bool CommandRegistry::Exists(std::string_view cmd_name)
{
    return commands_.count(cmd_name) > 0;
}

std::string_view CommandRegistry::CompleteCommand(std::string_view partial)
{
    if (partial.empty()) {
        return "";
    }

    auto it = commands_.lower_bound(partial);
    if (it != commands_.end()) {
        std::string_view cmd_name = it->first;
        if (cmd_name.length() >= partial.length()) {
            std::string_view prefix = cmd_name.substr(0, partial.length());
            if (Q_strcasecmp(std::string(prefix).c_str(), std::string(partial).c_str()) == 0) {
                return it->first;
            }
        }
    }
    return "";
}

int CommandRegistry::Argc(void)
{
    return static_cast<int>(cmd_argv_.size());
}

std::string_view CommandRegistry::Argv(int arg)
{
    if (arg < 0 || static_cast<size_t>(arg) >= cmd_argv_.size()) {
        return "";
    }
    return cmd_argv_[arg];
}

std::string_view CommandRegistry::Args(void)
{
    return cmd_args_;
}

void CommandRegistry::TokenizeString(std::string_view text)
{
    cmd_argv_.clear();
    cmd_args_ = "";

    if (text.empty()) {
        return;
    }

    const char* ptr = text.data();
    bool command_parsed = false;

    while (true) {
        while (*ptr && *ptr <= ' ' && *ptr != '\n') {
            ptr++;
        }

        if (*ptr == '\n') {
            ptr++;
            break;
        }

        if (!*ptr) {
            return;
        }

        if (command_parsed && cmd_args_.empty()) {
            cmd_args_ = std::string_view(ptr);
        }

        const char* next_ptr = COM_Parse(ptr);
        if (!next_ptr) {
            return;
        }
        ptr = next_ptr;

        if (!command_parsed) {
            command_parsed = true;
        }

        if (cmd_argv_.size() < 80) { // MAX_ARGS
            cmd_argv_.push_back(com_token);
        }
    }
}

void CommandRegistry::ExecuteString(std::string_view text, Source src)
{
    state_.source = src;
    TokenizeString(text);

    if (cmd_argv_.empty()) {
        return;
    }

    const auto& cmd_name = cmd_argv_[0];

    auto cmd_it = commands_.find(cmd_name);
    if (cmd_it != commands_.end()) {
        cmd_it->second();
        return;
    }

    auto alias_it = aliases_.find(cmd_name);
    if (alias_it != aliases_.end()) {
        BufferInsertText(alias_it->second);
        return;
    }

    if (!Cvar::Command()) {
        Con_Printf("Unknown command \"%s\"\n", cmd_name.c_str());
    }
}

void ForwardToServer(void)
{
    if (cls.state != ca_connected) {
        std::string_view cmd_name = Argv(0);
        Con_Printf("Can't \"%.*s\", not connected\n", static_cast<int>(cmd_name.length()), cmd_name.data());
        return;
    }

    if (cls.demoplayback) {
        return;
    }

    MSG_WriteByte(&cls.message, clc_stringcmd);
    
    std::string argv0(Argv(0));
    if (Q_strcasecmp(argv0.c_str(), "cmd") != 0) {
        SZ_Print(&cls.message, argv0.c_str());
        SZ_Print(&cls.message, " ");
    }

    if (Argc() > 1) {
        std::string args_str(Args());
        SZ_Print(&cls.message, args_str.c_str());
    } else {
        SZ_Print(&cls.message, "\n");
    }
}

// Wrapper APIs forwarding to the singleton registry

void BufferInit(void) { GetCommandRegistry().BufferInit(); }
void BufferAddText(std::string_view text) { GetCommandRegistry().BufferAddText(text); }
void BufferInsertText(std::string_view text) { GetCommandRegistry().BufferInsertText(text); }
void BufferExecute(void) { GetCommandRegistry().BufferExecute(); }
void Init(void) { GetCommandRegistry().Init(); }
void AddCommand(std::string_view cmd_name, xcommand_t function) { GetCommandRegistry().AddCommand(cmd_name, function); }
bool Exists(std::string_view cmd_name) { return GetCommandRegistry().Exists(cmd_name); }
std::string_view CompleteCommand(std::string_view partial) { return GetCommandRegistry().CompleteCommand(partial); }
int Argc(void) { return GetCommandRegistry().Argc(); }
std::string_view Argv(int arg) { return GetCommandRegistry().Argv(arg); }
std::string_view Args(void) { return GetCommandRegistry().Args(); }
void ExecuteString(std::string_view text, Source src) { GetCommandRegistry().ExecuteString(text, src); }

} // namespace Cmd
