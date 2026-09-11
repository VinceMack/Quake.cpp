// console.hpp -- In-game console subsystem (buffer, rendering, input line, notifications, logging)
#pragma once

#include <cstdint>
#include <EASTL/vector.h>
#include <EASTL/string_view.h>

namespace Console {

class ConsoleSystem {
public:
    ConsoleSystem();
    ~ConsoleSystem() = default;

    void Init();
    void CheckResize();
    void DrawConsole(int lines, bool drawinput);
    void Print(eastl::string_view txt);
    void Printf(const char* fmt, ...);
    void DPrintf(const char* fmt, ...);
    void Clear();
    void DrawNotify();
    void ClearNotify();
    void ToggleConsole();

    static void Clear_f();
    static void ToggleConsole_f();

    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_; }
    [[nodiscard]] bool IsForcedUp() const noexcept { return forcedup_; }
    void SetForcedUp(bool val) noexcept { forcedup_ = val; }
    [[nodiscard]] int GetTotalLines() const noexcept { return totallines_; }
    [[nodiscard]] int GetBackscroll() const noexcept { return backscroll_; }
    void SetBackscroll(int val) noexcept { backscroll_ = val; }
    [[nodiscard]] int GetNotifyLines() const noexcept { return notifylines_; }
    void SetNotifyLines(int val) noexcept { notifylines_ = val; }

private:
    bool initialized_{false}, forcedup_{false}, debuglog_{false};
    int totallines_{0}, backscroll_{0}, notifylines_{0}, linewidth_{0}, current_{0}, x_{0}, vislines_{0};
    float cursorspeed_{4.0f};
    eastl::vector<char> text_;
    eastl::vector<float> times_;

    void Linefeed();
    void DebugLog(eastl::string_view file, eastl::string_view text);
    void DrawInput();
};

[[nodiscard]] ConsoleSystem& GetConsoleSystem() noexcept;
void Con_Printf(const char* fmt, ...);
void Con_DPrintf(const char* fmt, ...);

} // namespace Console
