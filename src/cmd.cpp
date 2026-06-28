// cmd.cpp -- Quake script command processing module

#include "quakedef.hpp"

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

void ForwardToServer(void);

#define MAX_ALIAS_NAME 32

typedef struct cmdalias_s {
    struct cmdalias_s* next;
    char name[MAX_ALIAS_NAME];
    char* value;
} cmdalias_t;

static cmdalias_t* cmd_alias = nullptr;

static qboolean cmd_wait = false;

//=============================================================================

/*
============
Wait_f

Causes execution of the remainder of the command buffer to be delayed until
next frame.  This allows commands like:
bind g "impulse 5 ; +attack ; wait ; -attack ; impulse 2"
============
*/
static void Wait_f(void)
{
    cmd_wait = true;
}

//=============================================================================
//						COMMAND BUFFER
//=============================================================================

static sizebuf_t cmd_text;

/*
============
BufferInit
============
*/
void BufferInit(void)
{
    SZ_Alloc(&cmd_text, 8192); // space for commands and script files
}

/*
============
BufferAddText

Adds command text at the end of the buffer
============
*/
void BufferAddText(std::string_view text)
{
    int l = static_cast<int>(text.length());

    if (cmd_text.cursize + l >= cmd_text.maxsize) {
        Con_Printf("Cmd::BufferAddText: overflow\n");
        return;
    }

    SZ_Write(&cmd_text, const_cast<char*>(text.data()), l);
}

/*
============
BufferInsertText

Adds command text immediately after the current command
Adds a \n to the text
============
*/
void BufferInsertText(std::string_view text)
{
    int templen = cmd_text.cursize;
    char* temp = nullptr;
    if (templen) {
        temp = (char *) Z_Malloc(templen);
        Q_memcpy(temp, cmd_text.data, templen);
        SZ_Clear(&cmd_text);
    }

    // add the entire text of the file
    BufferAddText(text);

    // add the copied off data
    if (templen) {
        SZ_Write(&cmd_text, temp, templen);
        Z_Free(temp);
    }
}

/*
============
BufferExecute
============
*/
void BufferExecute(void)
{
    char line[1024];

    while (cmd_text.cursize) {
        // find a \n or ; line break
        char* text = (char*)cmd_text.data;

        int quotes = 0;
        int i;
        for (i = 0; i < cmd_text.cursize; i++) {
            if (text[i] == '"') {
                quotes++;
            }

            if (!(quotes & 1) && text[i] == ';') {
                break; // don't break if inside a quoted string
            }

            if (text[i] == '\n') {
                break;
            }
        }

        std::memcpy(line, text, i);
        line[i] = 0;

        // delete the text from the command buffer and move remaining commands down
        if (i == cmd_text.cursize) {
            cmd_text.cursize = 0;
        } else {
            i++;
            cmd_text.cursize -= i;
            Q_memcpy(text, text + i, cmd_text.cursize);
        }

        // execute the command line
        ExecuteString(line, Source::Command);

        if (cmd_wait) { // skip out while text still remains in buffer, leaving it for next frame
            cmd_wait = false;
            break;
        }
    }
}

//==============================================================================
//						SCRIPT COMMANDS
//==============================================================================

/*
===============
StuffCmds_f

Adds command line parameters as script statements
Commands lead with a +, and continue until a - or another +
===============
*/
static void StuffCmds_f(void)
{
    if (Argc() != 1) {
        Con_Printf("stuffcmds : execute command line parameters\n");
        return;
    }

    // build the combined string to parse from
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

    // pull out the commands
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
        BufferInsertText(build);
    }

    Z_Free(text);
    Z_Free(build);
}

/*
===============
Exec_f
===============
*/
static void Exec_f(void)
{
    if (Argc() != 2) {
        Con_Printf("exec <filename> : execute a script file\n");
        return;
    }

    int mark = Hunk_LowMark();
    std::string_view filename = Argv(1);
    char* f = (char*)COM_LoadHunkFile(const_cast<char*>(filename.data()));
    if (!f) {
        Con_Printf("couldn't exec %.*s\n", static_cast<int>(filename.length()), filename.data());
        return;
    }

    Con_Printf("execing %.*s\n", static_cast<int>(filename.length()), filename.data());

    BufferInsertText(f);
    Hunk_FreeToLowMark(mark);
}

/*
===============
Echo_f

Just prints the rest of the line to the console
===============
*/
static void Echo_f(void)
{
    for (int i = 1; i < Argc(); i++) {
        Con_Printf("%.*s ", static_cast<int>(Argv(i).length()), Argv(i).data());
    }
    Con_Printf("\n");
}

/*
===============
Alias_f

Creates a new command that executes a command string (possibly ; seperated)
===============
*/
static char* CopyString(const char* in)
{
    char* out = (char *) Z_Malloc(static_cast<int>(std::strlen(in)) + 1);
    std::strcpy(out, in);
    return out;
}

static void Alias_f(void)
{
    cmdalias_t* a;
    char cmd[1024];

    if (Argc() == 1) {
        Con_Printf("Current alias commands:\n");
        for (a = cmd_alias; a; a = a->next) {
            Con_Printf("%s : %s\n", a->name, a->value);
        }
        return;
    }

    std::string_view alias_name = Argv(1);
    if (alias_name.length() >= MAX_ALIAS_NAME) {
        Con_Printf("Alias name is too long\n");
        return;
    }

    // if the alias allready exists, reuse it
    for (a = cmd_alias; a; a = a->next) {
        if (alias_name == a->name) {
            Z_Free(a->value);
            break;
        }
    }

    if (!a) {
        a = (cmdalias_t *) Z_Malloc(sizeof(cmdalias_t));
        a->next = cmd_alias;
        cmd_alias = a;
    }

    std::memcpy(a->name, alias_name.data(), alias_name.length());
    a->name[alias_name.length()] = '\0';

    // copy the rest of the command line
    cmd[0] = 0; // start out with a null string
    int c = Argc();
    for (int i = 2; i < c; i++) {
        std::strcat(cmd, Argv(i).data());
        if (i != c) {
            std::strcat(cmd, " ");
        }
    }
    std::strcat(cmd, "\n");

    a->value = CopyString(cmd);
}

//=============================================================================
//						COMMAND EXECUTION
//=============================================================================

typedef struct cmd_function_s {
    struct cmd_function_s* next;
    char* name;
    xcommand_t function;
} cmd_function_t;

#define MAX_ARGS 80

static int cmd_argc = 0;
static char* cmd_argv[MAX_ARGS];
static std::string_view cmd_args;

static cmd_function_t* cmd_functions = nullptr; // possible commands to execute

/*
============
Init
============
*/
void Init(void)
{
    AddCommand("stuffcmds", StuffCmds_f);
    AddCommand("exec", Exec_f);
    AddCommand("echo", Echo_f);
    AddCommand("alias", Alias_f);
    AddCommand("cmd", ForwardToServer);
    AddCommand("wait", Wait_f);
}

/*
============
Argc
============
*/
int Argc(void)
{
    return cmd_argc;
}

/*
============
Argv
============
*/
std::string_view Argv(int arg)
{
    if (arg < 0 || arg >= cmd_argc) {
        return "";
    }
    return cmd_argv[arg];
}

/*
============
Args
============
*/
std::string_view Args(void)
{
    return cmd_args;
}

/*
============
TokenizeString

Parses the given string into command line tokens.
============
*/
void TokenizeString(std::string_view text)
{
    // clear the args from the last string
    for (int i = 0; i < cmd_argc; i++) {
        Z_Free(cmd_argv[i]);
    }

    cmd_argc = 0;
    cmd_args = "";

    if (text.empty()) {
        return;
    }

    const char* ptr = text.data();
    bool command_parsed = false;

    while (true) {
        // skip whitespace up to a /n
        while (*ptr && *ptr <= ' ' && *ptr != '\n') {
            ptr++;
        }

        if (*ptr == '\n') { // a newline separates commands in the buffer
            ptr++;
            break;
        }

        if (!*ptr) {
            return;
        }

        if (command_parsed && cmd_args.empty()) {
            cmd_args = std::string_view(ptr);
        }

        char* next_ptr = COM_Parse(const_cast<char*>(ptr));
        if (!next_ptr) {
            return;
        }
        ptr = next_ptr;

        if (!command_parsed) {
            command_parsed = true;
        }

        if (cmd_argc < MAX_ARGS) {
            cmd_argv[cmd_argc] = (char *) Z_Malloc(Q_strlen(com_token) + 1);
            Q_strcpy(cmd_argv[cmd_argc], com_token);
            cmd_argc++;
        }
    }
}

/*
============
AddCommand
============
*/
void AddCommand(std::string_view cmd_name, xcommand_t function)
{
    if (host_initialized) { // because hunk allocation would get stomped
        Sys_Error("Cmd::AddCommand after host_initialized");
    }

    // fail if the command is a variable name
    if (Cvar::FindVar(cmd_name) != nullptr) {
        Con_Printf("Cmd::AddCommand: %.*s already defined as a var\n", static_cast<int>(cmd_name.length()), cmd_name.data());
        return;
    }

    // check to see if it has allready been defined
    if (Exists(cmd_name)) {
        Con_Printf("Cmd::AddCommand: %.*s already defined\n", static_cast<int>(cmd_name.length()), cmd_name.data());
        return;
    }

    cmd_function_t* cmd = (cmd_function_t*) Hunk_Alloc(sizeof(cmd_function_t), "cmd");
    cmd->name = (char*) Hunk_Alloc(static_cast<int>(cmd_name.length()) + 1, "cmdname");
    std::memcpy(cmd->name, cmd_name.data(), cmd_name.length());
    cmd->name[cmd_name.length()] = '\0';
    cmd->function = function;
    cmd->next = cmd_functions;
    cmd_functions = cmd;
}

/*
============
Exists
============
*/
bool Exists(std::string_view cmd_name)
{
    for (cmd_function_t* cmd = cmd_functions; cmd; cmd = cmd->next) {
        if (cmd_name == cmd->name) {
            return true;
        }
    }
    return false;
}

/*
============
CompleteCommand
============
*/
std::string_view CompleteCommand(std::string_view partial)
{
    if (partial.empty()) {
        return "";
    }

    for (cmd_function_t* cmd = cmd_functions; cmd; cmd = cmd->next) {
        if (std::string_view(cmd->name).starts_with(partial)) {
            return cmd->name;
        }
    }
    return "";
}

/*
============
ExecuteString

A complete command line has been parsed, so try to execute it
============
*/
void ExecuteString(std::string_view text, Source src)
{
    state.source = src;
    TokenizeString(text);

    // execute the command line
    if (!Argc()) {
        return; // no tokens
    }

    // check functions
    for (cmd_function_t* cmd = cmd_functions; cmd; cmd = cmd->next) {
        if (Q_strcasecmp(cmd_argv[0], cmd->name) == 0) {
            cmd->function();
            return;
        }
    }

    // check alias
    for (cmdalias_t* a = cmd_alias; a; a = a->next) {
        if (Q_strcasecmp(cmd_argv[0], a->name) == 0) {
            BufferInsertText(a->value);
            return;
        }
    }

    // check cvars
    if (!Cvar::Command()) {
        Con_Printf("Unknown command \"%.*s\"\n", static_cast<int>(Argv(0).length()), Argv(0).data());
    }
}

/*
===================
ForwardToServer

Sends the entire command line over to the server
===================
*/
void ForwardToServer(void)
{
    if (cls.state != ca_connected) {
        Con_Printf("Can't \"%.*s\", not connected\n", static_cast<int>(Argv(0).length()), Argv(0).data());
        return;
    }

    if (cls.demoplayback) {
        return; // not really connected
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

} // namespace Cmd