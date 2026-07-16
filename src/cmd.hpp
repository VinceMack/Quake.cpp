// cmd.h -- Command buffer and command execution
#pragma once
#include <string_view>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Cmd {

using xcommand_t = std::function<void()>;

enum class Source {
    Client,
    Command
};

struct State {
    Source source = Source::Command;
};

struct CaseInsensitiveLess {
    using is_transparent = void;
    bool operator()(std::string_view lhs, std::string_view rhs) const {
        return std::lexicographical_compare(
            lhs.begin(), lhs.end(),
            rhs.begin(), rhs.end(),
            [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) <
                       std::tolower(static_cast<unsigned char>(b));
            }
        );
    }
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

    const std::map<std::string, std::string, CaseInsensitiveLess>& GetAliases() const { return aliases_; }
    std::map<std::string, std::string, CaseInsensitiveLess>& GetAliases() { return aliases_; }
    bool& GetCmdWait() { return cmd_wait_; }

    void AddAlias(std::string_view name, std::string_view value) {
        aliases_[std::string(name)] = std::string(value);
    }

private:
    State state_;
    std::string cmd_text_;
    bool cmd_wait_ = false;
    std::map<std::string, std::string, CaseInsensitiveLess> aliases_;
    std::map<std::string, xcommand_t, CaseInsensitiveLess> commands_;
    std::vector<std::string> cmd_argv_;
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
