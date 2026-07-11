// cmd.cpp -- Quake script command processing module

#include "quakedef.hpp"
#include <cstring>

using namespace CDAudio;
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
    SZ_Alloc(&cmd_text_, 8192); // space for commands and script files
}

void CommandRegistry::BufferAddText(std::string_view text)
{
    int l = static_cast<int>(text.length());

    if (cmd_text_.cursize + l >= cmd_text_.maxsize) {
        Con_Printf("Cmd::BufferAddText: overflow\n");
        return;
    }

    SZ_Write(&cmd_text_, const_cast<char*>(text.data()), l);
}

void CommandRegistry::BufferInsertText(std::string_view text)
{
    int templen = cmd_text_.cursize;
    char* temp = nullptr;
    if (templen) {
        temp = (char *) Z_Malloc(templen);
        Q_memcpy(temp, cmd_text_.data, templen);
        SZ_Clear(&cmd_text_);
    }

    BufferAddText(text);

    if (templen) {
        SZ_Write(&cmd_text_, temp, templen);
        Z_Free(temp);
    }
}

void CommandRegistry::BufferExecute(void)
{
    char line[1024];

    while (cmd_text_.cursize) {
        char* text = (char*)cmd_text_.data;

        int quotes = 0;
        int i;
        for (i = 0; i < cmd_text_.cursize; i++) {
            if (text[i] == '"') {
                quotes++;
            }

            if (!(quotes & 1) && text[i] == ';') {
                break;
            }

            if (text[i] == '\n') {
                break;
            }
        }

        std::memcpy(line, text, i);
        line[i] = 0;

        if (i == cmd_text_.cursize) {
            cmd_text_.cursize = 0;
        } else {
            i++;
            cmd_text_.cursize -= i;
            Q_memcpy(text, text + i, cmd_text_.cursize);
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

    int s = 0;
    for (int i = 1; i < com_argc; i++) {
        if (!com_argv[i]) {
            continue;
        }
        s += Q_strlen(com_argv[i]) + 1;
    }
    if (!s) {
        return;
    }

    char* text = (char *) Z_Malloc(s + 1);
    text[0] = 0;
    for (int i = 1; i < com_argc; i++) {
        if (!com_argv[i]) {
            continue;
        }

        Q_strcat(text, com_argv[i]);
        if (i != com_argc - 1) {
            Q_strcat(text, " ");
        }
    }

    char* build = (char *) Z_Malloc(s + 1);
    build[0] = 0;

    for (int i = 0; i < s - 1; i++) {
        if (text[i] == '+') {
            i++;
            int j;
            for (j = i; (text[j] != '+') && (text[j] != '-') && (text[j] != 0); j++) {
                ;
            }

            char c = text[j];
            text[j] = 0;

            Q_strcat(build, text + i);
            Q_strcat(build, "\n");
            text[j] = c;
            i = j - 1;
        }
    }

    if (build[0]) {
        Cmd::BufferInsertText(build);
    }

    Z_Free(text);
    Z_Free(build);
}

static void Exec_f(void)
{
    if (Cmd::Argc() != 2) {
        Con_Printf("exec <filename> : execute a script file\n");
        return;
    }

    int mark = Hunk_LowMark();
    std::string_view filename = Cmd::Argv(1);
    char* f = (char*)COM_LoadHunkFile(const_cast<char*>(filename.data()));
    if (!f) {
        Con_Printf("couldn't exec %.*s\n", static_cast<int>(filename.length()), filename.data());
        return;
    }

    Con_Printf("execing %.*s\n", static_cast<int>(filename.length()), filename.data());

    Cmd::BufferInsertText(f);
    Hunk_FreeToLowMark(mark);
}

static void Echo_f(void)
{
    for (int i = 1; i < Cmd::Argc(); i++) {
        Con_Printf("%.*s ", static_cast<int>(Cmd::Argv(i).length()), Cmd::Argv(i).data());
    }
    Con_Printf("\n");
}

static char* CopyString(const char* in)
{
    char* out = (char *) Z_Malloc(static_cast<int>(std::strlen(in)) + 1);
    strcpy_s(out, std::strlen(in) + 1, in);
    return out;
}

static void Alias_f(void)
{
    cmdalias_t* a;
    char cmd[1024];

    if (Cmd::Argc() == 1) {
        Con_Printf("Current alias commands:\n");
        for (a = GetCommandRegistry().GetAliases(); a; a = a->next) {
            Con_Printf("%s : %s\n", a->name, a->value);
        }
        return;
    }

    std::string_view alias_name = Cmd::Argv(1);
    if (alias_name.length() >= 32) { // MAX_ALIAS_NAME
        Con_Printf("Alias name is too long\n");
        return;
    }

    for (a = GetCommandRegistry().GetAliases(); a; a = a->next) {
        if (alias_name == a->name) {
            Z_Free(a->value);
            break;
        }
    }

    if (!a) {
        a = (cmdalias_t *) Z_Malloc(sizeof(cmdalias_t));
        a->next = GetCommandRegistry().GetAliases();
        GetCommandRegistry().GetAliases() = a;
    }

    std::memcpy(a->name, alias_name.data(), alias_name.length());
    a->name[alias_name.length()] = '\0';

    cmd[0] = 0;
    int c = Cmd::Argc();
    for (int i = 2; i < c; i++) {
        strcat_s(cmd, sizeof(cmd), Cmd::Argv(i).data());
        if (i != c) {
            strcat_s(cmd, sizeof(cmd), " ");
        }
    }
    strcat_s(cmd, sizeof(cmd), "\n");

    a->value = CopyString(cmd);
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

    cmd_function_t* cmd = (cmd_function_t*) Hunk_Alloc(sizeof(cmd_function_t), "cmd");
    cmd->name = (char*) Hunk_Alloc(static_cast<int>(cmd_name.length()) + 1, "cmdname");
    std::memcpy(cmd->name, cmd_name.data(), cmd_name.length());
    cmd->name[cmd_name.length()] = '\0';
    cmd->function = function;
    cmd->next = cmd_functions_;
    cmd_functions_ = cmd;
}

bool CommandRegistry::Exists(std::string_view cmd_name)
{
    for (cmd_function_t* cmd = cmd_functions_; cmd; cmd = cmd->next) {
        if (cmd_name == cmd->name) {
            return true;
        }
    }
    return false;
}

std::string_view CommandRegistry::CompleteCommand(std::string_view partial)
{
    if (partial.empty()) {
        return "";
    }

    for (cmd_function_t* cmd = cmd_functions_; cmd; cmd = cmd->next) {
        if (std::string_view(cmd->name).starts_with(partial)) {
            return cmd->name;
        }
    }
    return "";
}

int CommandRegistry::Argc(void)
{
    return cmd_argc_;
}

std::string_view CommandRegistry::Argv(int arg)
{
    if (arg < 0 || arg >= cmd_argc_) {
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
    for (int i = 0; i < cmd_argc_; i++) {
        Z_Free(cmd_argv_[i]);
    }

    cmd_argc_ = 0;
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

        char* next_ptr = COM_Parse(const_cast<char*>(ptr));
        if (!next_ptr) {
            return;
        }
        ptr = next_ptr;

        if (!command_parsed) {
            command_parsed = true;
        }

        if (cmd_argc_ < 80) { // MAX_ARGS
            cmd_argv_[cmd_argc_] = (char *) Z_Malloc(Q_strlen(com_token) + 1);
            Q_strcpy(cmd_argv_[cmd_argc_], com_token);
            cmd_argc_++;
        }
    }
}

void CommandRegistry::ExecuteString(std::string_view text, Source src)
{
    state_.source = src;
    TokenizeString(text);

    if (!Argc()) {
        return;
    }

    for (cmd_function_t* cmd = cmd_functions_; cmd; cmd = cmd->next) {
        if (Q_strcasecmp(cmd_argv_[0], cmd->name) == 0) {
            cmd->function();
            return;
        }
    }

    for (cmdalias_t* a = cmd_alias_; a; a = a->next) {
        if (Q_strcasecmp(cmd_argv_[0], a->name) == 0) {
            BufferInsertText(a->value);
            return;
        }
    }

    if (!Cvar::Command()) {
        Con_Printf("Unknown command \"%.*s\"\n", static_cast<int>(Argv(0).length()), Argv(0).data());
    }
}

void ForwardToServer(void)
{
    if (cls.state != ca_connected) {
        Con_Printf("Can't \"%.*s\", not connected\n", static_cast<int>(Argv(0).length()), Argv(0).data());
        return;
    }

    if (cls.demoplayback) {
        return;
    }

    MSG_WriteByte(&cls.message, clc_stringcmd);
    if (Q_strcasecmp(const_cast<char*>(Argv(0).data()), "cmd") != 0) {
        SZ_Print(&cls.message, const_cast<char*>(Argv(0).data()));
        SZ_Print(&cls.message, " ");
    }

    if (Argc() > 1) {
        SZ_Print(&cls.message, const_cast<char*>(Args().data()));
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
// ForwardToServer is implemented directly above

} // namespace Cmd