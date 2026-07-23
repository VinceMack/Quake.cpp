// console.hpp -- console display and input declarations
#pragma once

#include <EASTL/vector.h>
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

    // Getters and setters for encapsulating the state:
    bool IsInitialized() const { return initialized_; }
    
    bool IsForcedUp() const { return forcedup_; }
    void SetForcedUp(bool val) { forcedup_ = val; }
    
    int GetTotalLines() const { return totallines_; }
    
    int GetBackscroll() const { return backscroll_; }
    void SetBackscroll(int val) { backscroll_ = val; }
    
    int GetNotifyLines() const { return notifylines_; }
    void SetNotifyLines(int val) { notifylines_ = val; }

private:
    bool initialized_ = false;
    bool forcedup_ = false;
    int totallines_ = 0;
    int backscroll_ = 0;
    int notifylines_ = 0;
    
    // Internal helper methods and state variables
    int linewidth_ = 0;
    float cursorspeed_ = 4.0f;
    int current_ = 0;
    int x_ = 0;
    eastl::vector<char> text_;
    eastl::vector<float> times_; // size NUM_CON_TIMES (4)
    int vislines_ = 0;
    bool debuglog_ = false;

    void Linefeed();
    void DebugLog(std::string_view file, std::string_view text);
    void DrawInput();
};

ConsoleSystem& GetConsoleSystem();

// Print utilities matching C function signature for function pointer compatibility
void Con_Printf(const char* fmt, ...);
void Con_DPrintf(const char* fmt, ...);

} // namespace Console
