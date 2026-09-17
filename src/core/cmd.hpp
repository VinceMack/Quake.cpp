// cmd.hpp -- Command parsing, tokenization, ring buffer, and registry
#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <functional>
#include <algorithm>

namespace Cmd {

using xcommand_t = std::function<void()>;

enum class Source { Client, Command };

struct State { Source source = Source::Command; };

struct CaseInsensitiveLess {
    using is_transparent = void;
    template <typename T, typename U>
    bool operator()(const T& lhs, const U& rhs) const {
        std::string_view a(lhs.data(), lhs.size()), b(rhs.data(), rhs.size());
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
            return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
        });
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
        aliases_[std::string(name.data(), name.length())] = std::string(value.data(), value.length());
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
