// cmd.h -- Command buffer and command execution
#pragma once
#include <string_view>

typedef void (*xcommand_t)(void);

namespace Cmd {

enum class Source {
    Client,
    Command
};

struct State {
    Source source = Source::Command;
};

struct cmdalias_t {
    cmdalias_t* next;
    char name[32]; // MAX_ALIAS_NAME
    char* value;
};

struct cmd_function_t {
    cmd_function_t* next;
    char* name;
    xcommand_t function;
};

class CommandRegistry {
public:
    void BufferInit(void);
    void BufferAddText(std::string_view text);
    void BufferInsertText(std::string_view text);
    void BufferExecute(void);
    void Init(void);
    void AddCommand(std::string_view cmd_name, xcommand_t function);
    bool Exists(std::string_view cmd_name);
    std::string_view CompleteCommand(std::string_view partial);
    int Argc(void);
    std::string_view Argv(int arg);
    std::string_view Args(void);
    void TokenizeString(std::string_view text);
    void ExecuteString(std::string_view text, Source src);

    State& GetState() { return state_; }
    const State& GetState() const { return state_; }

    cmd_function_t*& GetFunctions() { return cmd_functions_; }
    cmdalias_t*& GetAliases() { return cmd_alias_; }
    sizebuf_t& GetCmdText() { return cmd_text_; }
    bool& GetCmdWait() { return cmd_wait_; }
    int& GetCmdArgc() { return cmd_argc_; }
    char** GetCmdArgv() { return cmd_argv_; }
    std::string_view& GetCmdArgs() { return cmd_args_; }

private:
    State state_;
    sizebuf_t cmd_text_;
    bool cmd_wait_ = false;
    cmdalias_t* cmd_alias_ = nullptr;
    cmd_function_t* cmd_functions_ = nullptr;
    int cmd_argc_ = 0;
    char* cmd_argv_[80]; // MAX_ARGS
    std::string_view cmd_args_;
};

CommandRegistry& GetCommandRegistry();

inline State& state = GetCommandRegistry().GetState();

void BufferInit(void);

void BufferAddText(std::string_view text);

void BufferInsertText(std::string_view text);

void BufferExecute(void);

void Init(void);

void AddCommand(std::string_view cmd_name, xcommand_t function);

bool Exists(std::string_view cmd_name);

std::string_view CompleteCommand(std::string_view partial);

int Argc(void);
std::string_view Argv(int arg);
std::string_view Args(void);

void ExecuteString(std::string_view text, Source src);

void ForwardToServer(void);

} // namespace Cmd
