// console.hpp -- In-game console subsystem (buffer, rendering, input line, notifications, logging)
#pragma once

#include <cstdint>
#include "core/print.hpp"
#include <vector>
#include <string_view>

namespace Console {

class ConsoleSystem {
public:
    ConsoleSystem();
    ~ConsoleSystem() = default;

    void Init();
    void CheckResize();
    void DrawConsole(int lines, bool drawinput);
    void Print(std::string_view txt);
    void Printf(const char* fmt, ...);
    void DPrintf(const char* fmt, ...);
    void Clear();
    void DrawNotify();
    void ClearNotify();
    void ToggleConsole();

    static void Clear_f();
    static void ToggleConsole_f();

    // State shared with the key handler, the screen and the view.
    bool initialized { false };
    bool forcedup { false }; // console fills the screen because no level is loaded
    int totallines { 0 };
    int backscroll { 0 };
    int notifylines { 0 };

private:
    bool debuglog_ { false };
    int linewidth_ { 0 }, current_ { 0 }, x_ { 0 }, vislines_ { 0 };
    float cursorspeed_ { 4.0f };
    std::vector<char> text_;
    std::vector<float> times_;

    void Linefeed();
    void DebugLog(std::string_view file, std::string_view text);
    void DrawInput();
};

[[nodiscard]] ConsoleSystem& GetConsoleSystem() noexcept;

} // namespace Console
