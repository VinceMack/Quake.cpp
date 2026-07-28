// cmd.h -- Command buffer and command execution
#pragma once
#include <EASTL/string_view.h>
#include <EASTL/functional.h>
#include <EASTL/algorithm.h>
#include <EASTL/map.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace Cmd {

using xcommand_t = eastl::function<void()>;

enum class Source {
    Client,
    Command
};

struct State {
    Source source = Source::Command;
};

struct CaseInsensitiveLess {
    using is_transparent = void;
    template <typename T, typename U>
    bool operator()(const T& lhs, const U& rhs) const {
        eastl::string_view sv_lhs(lhs.data(), lhs.size());
        eastl::string_view sv_rhs(rhs.data(), rhs.size());
        return eastl::lexicographical_compare(
            sv_lhs.begin(), sv_lhs.end(),
            sv_rhs.begin(), sv_rhs.end(),
            [](char a, char b) {
                char ca = (a >= 'a' && a <= 'z') ? static_cast<char>(a - ('a' - 'A')) : a;
                char cb = (b >= 'a' && b <= 'z') ? static_cast<char>(b - ('a' - 'A')) : b;
                return ca < cb;
            }
        );
    }
};

class CommandRegistry {
public:
    void BufferInit(void);
    void BufferAddText(eastl::string_view text);
    void BufferInsertText(eastl::string_view text);
    void BufferExecute(void);
    void Init(void);
    void AddCommand(eastl::string_view cmd_name, xcommand_t function);
    bool Exists(eastl::string_view cmd_name);
    eastl::string_view CompleteCommand(eastl::string_view partial);
    int Argc(void);
    eastl::string_view Argv(int arg);
    eastl::string_view Args(void);
    void TokenizeString(eastl::string_view text);
    void ExecuteString(eastl::string_view text, Source src);

    State& GetState() { return state_; }
    const State& GetState() const { return state_; }

    const eastl::map<eastl::string, eastl::string, CaseInsensitiveLess>& GetAliases() const { return aliases_; }
    eastl::map<eastl::string, eastl::string, CaseInsensitiveLess>& GetAliases() { return aliases_; }
    bool& GetCmdWait() { return cmd_wait_; }

    void AddAlias(eastl::string_view name, eastl::string_view value) {
        aliases_[eastl::string(name.data(), name.length())] = eastl::string(value.data(), value.length());
    }

private:
    State state_;
    eastl::string cmd_text_;
    bool cmd_wait_ = false;
    eastl::map<eastl::string, eastl::string, CaseInsensitiveLess> aliases_;
    eastl::map<eastl::string, xcommand_t, CaseInsensitiveLess> commands_;
    eastl::vector<eastl::string> cmd_argv_;
    eastl::string_view cmd_args_;
};

CommandRegistry& GetCommandRegistry();

inline State& state = GetCommandRegistry().GetState();

void BufferInit(void);

void BufferAddText(eastl::string_view text);

void BufferInsertText(eastl::string_view text);

void BufferExecute(void);

void Init(void);

void AddCommand(eastl::string_view cmd_name, xcommand_t function);

bool Exists(eastl::string_view cmd_name);

eastl::string_view CompleteCommand(eastl::string_view partial);

int Argc(void);
eastl::string_view Argv(int arg);
eastl::string_view Args(void);

void ExecuteString(eastl::string_view text, Source src);

void ForwardToServer(void);

} // namespace Cmd
