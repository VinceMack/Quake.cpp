// cmd.cpp -- Command parsing, tokenization, ring buffer, and registry implementation
#include "quakedef.hpp"
#include "core/cmd.hpp"
#include "core/cvar.hpp"
#include "core/string_utils.hpp"
#include "core/filesystem.hpp"
#include "core/msg.hpp"
#include "ui/console.hpp"
#include "host/host.hpp"

#include <utility>
#include <vector>

namespace Cmd {

CommandRegistry& GetCommandRegistry() { static CommandRegistry registry; return registry; }

static void Wait_f(void) { GetCommandRegistry().GetCmdWait() = true; }

void CommandRegistry::BufferInit(void) {
    cmd_text_.clear();
    cmd_text_.reserve(8192);
}

void CommandRegistry::BufferAddText(std::string_view text) {
    if (cmd_text_.length() + text.length() >= 8192) {
        Console::Con_Printf("Cmd::BufferAddText: overflow\n");
        return;
    }
    cmd_text_.append(text.data(), text.length());
}

void CommandRegistry::BufferInsertText(std::string_view text) {
    if (cmd_text_.length() + text.length() >= 8192) {
        Console::Con_Printf("Cmd::BufferAddText: overflow\n");
        return;
    }
    cmd_text_.insert(0, text.data(), text.length());
}

void CommandRegistry::BufferExecute(void) {
    while (!cmd_text_.empty()) {
        int quotes = 0;
        size_t i = 0;
        for (i = 0; i < cmd_text_.length(); ++i) {
            if (cmd_text_[i] == '"') quotes++;
            if (!(quotes & 1) && cmd_text_[i] == ';') break;
            if (cmd_text_[i] == '\n') break;
        }
        std::string line = cmd_text_.substr(0, i);
        if (i == cmd_text_.length()) cmd_text_.clear();
        else cmd_text_.erase(0, i + 1);

        ExecuteString(std::string_view(line.data(), line.length()), Source::Command);
        if (cmd_wait_) {
            cmd_wait_ = false;
            break;
        }
    }
}

static void StuffCmds_f(void) {
    if (Cmd::Argc() != 1) {
        Console::Con_Printf("stuffcmds : execute command line parameters\n");
        return;
    }
    std::string text;
    for (int i = 1; i < Common::com_argc; i++) {
        if (!Common::com_argv[i]) continue;
        if (!text.empty()) text += " ";
        text += Common::com_argv[i];
    }
    if (text.empty()) return;
    std::string build;
    size_t i = 0;
    while (i < text.length()) {
        if (text[i] == '+') {
            i++;
            size_t j = i;
            while (j < text.length() && text[j] != '+' && text[j] != '-') j++;
            build += text.substr(i, j - i);
            build += "\n";
            i = j;
        } else {
            i++;
        }
    }
    if (!build.empty()) Cmd::BufferInsertText(std::string_view(build.data(), build.length()));
}

static void Exec_f(void) {
    if (Cmd::Argc() != 2) {
        Console::Con_Printf("exec <filename> : execute a script file\n");
        return;
    }
    std::string_view filename = Cmd::Argv(1);
    std::string filename_str(filename.data(), filename.length());
    std::vector<byte> script = Common::COM_LoadFile(filename_str.c_str());
    if (script.empty()) {
        Console::Con_Printf("couldn't exec %s\n", filename_str.c_str());
        return;
    }
    Console::Con_Printf("execing %s\n", filename_str.c_str());
    Cmd::BufferInsertText(reinterpret_cast<const char*>(script.data()));
}

static void Echo_f(void) {
    for (int i = 1; i < Cmd::Argc(); i++) {
        std::string_view arg = Cmd::Argv(i);
        Console::Con_Printf("%.*s ", static_cast<int>(arg.length()), arg.data());
    }
    Console::Con_Printf("\n");
}

static void Alias_f(void) {
    auto& registry = GetCommandRegistry();
    if (Cmd::Argc() == 1) {
        Console::Con_Printf("Current alias commands:\n");
        for (const auto& [name, value] : registry.GetAliases()) {
            Console::Con_Printf("%s : %s\n", name.c_str(), value.c_str());
        }
        return;
    }
    std::string_view alias_name = Cmd::Argv(1);
    if (alias_name.length() >= 32) {
        Console::Con_Printf("Alias name is too long\n");
        return;
    }
    std::string cmd;
    for (int i = 2; i < Cmd::Argc(); ++i) {
        std::string_view arg = Cmd::Argv(i);
        cmd.append(arg.data(), arg.length());
        cmd += " ";
    }
    cmd += "\n";
    registry.AddAlias(alias_name, std::string_view(cmd.data(), cmd.length()));
}

void CommandRegistry::Init(void) {
    AddCommand("stuffcmds", StuffCmds_f);
    AddCommand("exec", Exec_f);
    AddCommand("echo", Echo_f);
    AddCommand("alias", Alias_f);
    AddCommand("cmd", ForwardToServer);
    AddCommand("wait", Wait_f);
}

void CommandRegistry::AddCommand(std::string_view cmd_name, xcommand_t function) {
    if (Host::host_initialized) Common::Sys_Error("Cmd::AddCommand after host_initialized");
    if (Cvar::FindVar(cmd_name) != nullptr) {
        Console::Con_Printf("Cmd::AddCommand: %.*s already defined as a var\n", static_cast<int>(cmd_name.length()), cmd_name.data());
        return;
    }
    if (Exists(cmd_name)) {
        Console::Con_Printf("Cmd::AddCommand: %.*s already defined\n", static_cast<int>(cmd_name.length()), cmd_name.data());
        return;
    }
    commands_.emplace(std::string(cmd_name.data(), cmd_name.length()), std::move(function));
}

bool CommandRegistry::Exists(std::string_view cmd_name) { return commands_.count(cmd_name) > 0; }

std::string_view CommandRegistry::CompleteCommand(std::string_view partial) {
    if (partial.empty()) return "";
    auto it = commands_.lower_bound(partial);
    if (it != commands_.end()) {
        std::string_view cmd_name(it->first.data(), it->first.length());
        if (cmd_name.length() >= partial.length()) {
            std::string_view prefix = cmd_name.substr(0, partial.length());
            if (Common::Q_strcasecmp(std::string(prefix.data(), prefix.length()).c_str(), std::string(partial.data(), partial.length()).c_str()) == 0) {
                return cmd_name;
            }
        }
    }
    return "";
}

int CommandRegistry::Argc(void) { return static_cast<int>(cmd_argv_.size()); }

std::string_view CommandRegistry::Argv(int arg) {
    if (arg < 0 || static_cast<size_t>(arg) >= cmd_argv_.size()) return "";
    return std::string_view(cmd_argv_[arg].data(), cmd_argv_[arg].length());
}

std::string_view CommandRegistry::Args(void) { return cmd_args_; }

void CommandRegistry::TokenizeString(std::string_view text) {
    cmd_argv_.clear();
    cmd_args_ = "";
    if (text.empty()) return;
    const char* ptr = text.data();
    bool command_parsed = false;
    while (true) {
        while (*ptr && *ptr <= ' ' && *ptr != '\n') ptr++;
        if (*ptr == '\n') { ptr++; break; }
        if (!*ptr) return;
        if (command_parsed && cmd_args_.empty()) cmd_args_ = std::string_view(ptr);
        const char* next_ptr = Common::COM_Parse(ptr);
        if (!next_ptr) return;
        ptr = next_ptr;
        if (!command_parsed) command_parsed = true;
        if (cmd_argv_.size() < 80) cmd_argv_.push_back(Common::com_token);
    }
}

void CommandRegistry::ExecuteString(std::string_view text, Source src) {
    state_.source = src;
    TokenizeString(text);
    if (cmd_argv_.empty()) return;
    const auto& cmd_name = cmd_argv_[0];
    auto cmd_it = commands_.find(cmd_name);
    if (cmd_it != commands_.end()) {
        cmd_it->second();
        return;
    }
    auto alias_it = aliases_.find(cmd_name);
    if (alias_it != aliases_.end()) {
        BufferInsertText(std::string_view(alias_it->second.data(), alias_it->second.length()));
        return;
    }
    if (!Cvar::Command()) {
        Console::Con_Printf("Unknown command \"%s\"\n", cmd_name.c_str());
    }
}

void ForwardToServer(void) {
    if (Client::cls.state != ca_connected) {
        std::string_view cmd_name = Argv(0);
        Console::Con_Printf("Can't \"%.*s\", not connected\n", static_cast<int>(cmd_name.length()), cmd_name.data());
        return;
    }
    if (Client::cls.demoplayback) return;
    Common::MSG_WriteByte(&Client::cls.message, clc_stringcmd);
    std::string argv0(Argv(0).data(), Argv(0).length());
    if (Common::Q_strcasecmp(argv0.c_str(), "cmd") != 0) {
        Common::SZ_Print(&Client::cls.message, argv0.c_str());
        Common::SZ_Print(&Client::cls.message, " ");
    }
    if (Argc() > 1) {
        std::string args_str(Args().data(), Args().length());
        Common::SZ_Print(&Client::cls.message, args_str.c_str());
    } else {
        Common::SZ_Print(&Client::cls.message, "\n");
    }
}

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
