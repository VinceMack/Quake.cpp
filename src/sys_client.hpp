// sys_client.hpp -- Subsystem Client: Client Engine, Host Frame Loop, Keys, Console, Menu & Camera
#pragma once

#include <cstdio>
#include <cstdint>
#include <ostream>
#include <EASTL/array.h>
#include <EASTL/vector.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/span.h>

#include "sys_core.hpp"
#include "sys_render.hpp"

//=============================================================================
// Host & Parms
//=============================================================================

struct quakeparms_t {
    const char* basedir{nullptr};
    const char* cachedir{nullptr};
    int argc{0};
    char** argv{nullptr};
    void* membase{nullptr};
    int memsize{0};
};

namespace Host {

extern quakeparms_t host_parms;
extern cvar_t sys_ticrate, sys_nostdout, developer;
extern qboolean host_initialized, isDedicated, noclip_anglehack;
extern double host_frametime, host_time, realtime;
extern byte *host_basepal, *host_colormap;
extern int host_framecount, current_skill, minimum_memory;
extern ::client_t* host_client;

void Host_ClearMemory();
void Host_ServerFrame();
void Host_InitCommands();
void Host_Init(quakeparms_t* parms);
void Host_Shutdown();

[[noreturn]] void Host_Error(const char* error, ...);
[[noreturn]] void Host_EndGame(const char* message, ...);
void Host_Frame(float time);
void Host_Quit_f();
void Host_ClientCommands(const char* fmt, ...);
void Host_ShutdownServer(qboolean crash);

} // namespace Host

//=============================================================================
// Keys & Input Constants
//=============================================================================

namespace Keys {

inline constexpr int K_TAB = 9, K_ENTER = 13, K_ESCAPE = 27, K_SPACE = 32, K_BACKSPACE = 127;
inline constexpr int K_UPARROW = 128, K_DOWNARROW = 129, K_LEFTARROW = 130, K_RIGHTARROW = 131;
inline constexpr int K_ALT = 132, K_CTRL = 133, K_SHIFT = 134;
inline constexpr int K_F1 = 135, K_F2 = 136, K_F3 = 137, K_F4 = 138, K_F5 = 139, K_F6 = 140;
inline constexpr int K_F7 = 141, K_F8 = 142, K_F9 = 143, K_F10 = 144, K_F11 = 145, K_F12 = 146;
inline constexpr int K_INS = 147, K_DEL = 148, K_PGDN = 149, K_PGUP = 150, K_HOME = 151, K_END = 152;
inline constexpr int K_PAUSE = 255;
inline constexpr int K_MOUSE1 = 200, K_MOUSE2 = 201, K_MOUSE3 = 202;
inline constexpr int K_JOY1 = 203, K_JOY2 = 204, K_JOY3 = 205, K_JOY4 = 206;
inline constexpr int K_AUX1 = 207, K_AUX32 = 238;
inline constexpr int K_MWHEELUP = 239, K_MWHEELDOWN = 240;
inline constexpr int MAXCMDLINE = 256;

enum keydest_t { key_game, key_console, key_message, key_menu };

extern keydest_t key_dest;
extern eastl::array<eastl::array<char, MAXCMDLINE>, 32> key_lines;
extern int key_linepos, edit_line, key_count, key_lastpress;
extern eastl::array<char, 32> chat_buffer;
extern bool team_message;
extern eastl::array<eastl::string, 256> keybindings;
extern eastl::array<int, 256> key_repeats;

void Key_Event(int key, bool down);
void Key_Init();
void Key_WriteBindings(std::ostream& f);
void Key_SetBinding(int keynum, const char* binding);
[[nodiscard]] const char* Key_KeynumToString(int keynum);

} // namespace Keys

//=============================================================================
// Console System
//=============================================================================

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

//=============================================================================
// Menu System
//=============================================================================

namespace Menu {

enum class MenuState {
    None, Main, SinglePlayer, Load, Save, MultiPlayer, Setup, Net, Options,
    Video, Keys, Help, Quit, SerialConfig, ModemConfig, LanConfig, GameOptions,
    Search, SList
};

void M_Init();
void M_Keydown(int key);
void M_Draw();
void M_ToggleMenu_f();
void M_Menu_Main_f();
void M_Menu_Quit_f();
void M_DrawPic(int x, int y, qpic_t* pic);

extern MenuState m_state, m_return_state;
extern bool m_return_onerror;
extern eastl::string m_return_reason;

} // namespace Menu

//=============================================================================
// Client State & Entities
//=============================================================================

namespace Client {

template <typename T, std::size_t N>
struct compat_array : public eastl::array<T, N> {
    [[nodiscard]] constexpr operator T*() noexcept { return this->data(); }
    [[nodiscard]] constexpr operator const T*() const noexcept { return this->data(); }
    [[nodiscard]] constexpr operator eastl::string_view() const noexcept {
        return eastl::string_view(reinterpret_cast<const char*>(this->data()));
    }
};

} // namespace Client

struct lightstyle_t {
    int length{0};
    Client::compat_array<char, MAX_STYLESTRING> map{};
};

struct scoreboard_t {
    Client::compat_array<char, MAX_SCOREBOARDNAME> name{};
    float entertime{0.0f};
    int frags{0}, colors{0};
    Client::compat_array<byte, VID_GRADES * 256> translations{};
};

struct cshift_t {
    eastl::array<int, 3> destcolor{};
    int percent{0};
};

inline constexpr int CSHIFT_CONTENTS = 0, CSHIFT_DAMAGE = 1, CSHIFT_BONUS = 2, CSHIFT_POWERUP = 3, NUM_CSHIFTS = 4;
inline constexpr int NAME_LENGTH = 64, SIGNONS = 4, MAX_DLIGHTS = 32, MAX_BEAMS = 24;

struct beam_t {
    int entity{0};
    model_t* model{nullptr};
    float endtime{0.0f};
    Vector3 start{}, end{};
};

inline constexpr int MAX_EFRAGS = 640, MAX_MAPSTRING = 2048, MAX_DEMOS = 8, MAX_DEMONAME = 16;

enum cactive_t { ca_dedicated, ca_disconnected, ca_connected };

struct client_static_t {
    cactive_t state{ca_disconnected};
    Client::compat_array<char, MAX_QPATH> mapstring{};
    Client::compat_array<char, MAX_MAPSTRING> spawnparms{};
    int demonum{-1};
    Client::compat_array<Client::compat_array<char, MAX_DEMONAME>, MAX_DEMOS> demos{};
    bool demorecording{false}, demoplayback{false}, timedemo{false};
    int forcetrack{0};
    FILE* demofile{nullptr};
    int td_lastframe{0}, td_startframe{0};
    float td_starttime{0.0f};
    int signon{0};
    struct qsocket_s* netcon{nullptr};
    sizebuf_t message{};
};

namespace Client {

struct client_state_t {
    int movemessages{0};
    usercmd_t cmd{};
    eastl::array<int, MAX_CL_STATS> stats{};
    int items{0};
    eastl::array<float, 32> item_gettime{};
    float faceanimtime{0.0f};
    eastl::array<cshift_t, NUM_CSHIFTS> cshifts{}, prev_cshifts{};
    eastl::array<Vector3, 2> mviewangles{}, mvelocity{};
    Vector3 viewangles{}, velocity{}, punchangle{};
    float idealpitch{0.0f}, pitchvel{0.0f};
    bool nodrift{false};
    float driftmove{0.0f};
    double laststop{0.0};
    float viewheight{0.0f}, crouch{0.0f};
    bool paused{false}, onground{false}, inwater{false};
    int intermission{0}, completed_time{0};
    eastl::array<double, 2> mtime{};
    double time{0.0}, oldtime{0.0};
    float last_received_message{0.0f};
    eastl::array<model_t*, MAX_MODELS> model_precache{};
    eastl::array<sfx_t*, MAX_SOUNDS> sound_precache{};
    compat_array<char, 40> levelname{};
    int viewentity{0}, maxclients{0}, gametype{0};
    model_t* worldmodel{nullptr};
    struct efrag_s* free_efrags{nullptr};
    int num_entities{0}, num_statics{0};
    entity_t viewent{};
    int cdtrack{0}, looptrack{0};
    scoreboard_t* scores{nullptr};
};

extern cvar_t cl_name, cl_color, cl_upspeed, cl_forwardspeed, cl_backspeed, cl_sidespeed;
extern cvar_t cl_movespeedkey, cl_yawspeed, cl_pitchspeed, cl_anglespeedkey, cl_autofire;
extern cvar_t cl_shownet, cl_nolerp, cl_pitchdriftspeed, lookspring, lookstrafe, sensitivity;
extern cvar_t m_pitch, m_yaw, m_forward, m_side;

inline constexpr int MAX_TEMP_ENTITIES = 64, MAX_STATIC_ENTITIES = 128;

using EfragArray = compat_array<efrag_t, MAX_EFRAGS>;
using EntityArray = compat_array<entity_t, MAX_EDICTS>;
using StaticEntityArray = compat_array<entity_t, MAX_STATIC_ENTITIES>;
using LightstyleArray = compat_array<lightstyle_t, MAX_LIGHTSTYLES>;
using DlightArray = compat_array<dlight_t, MAX_DLIGHTS>;
using TempEntityArray = compat_array<entity_t, MAX_TEMP_ENTITIES>;
using BeamArray = compat_array<beam_t, MAX_BEAMS>;

class ClientSubsystem {
public:
    [[nodiscard]] client_static_t& GetStaticState() noexcept { return cls_; }
    [[nodiscard]] const client_static_t& GetStaticState() const noexcept { return cls_; }
    [[nodiscard]] client_state_t& GetState() noexcept { return cl_; }
    [[nodiscard]] const client_state_t& GetState() const noexcept { return cl_; }
    [[nodiscard]] EfragArray& GetEfrags() noexcept { return cl_efrags_; }
    [[nodiscard]] EntityArray& GetEntities() noexcept { return cl_entities_; }
    [[nodiscard]] StaticEntityArray& GetStaticEntities() noexcept { return cl_static_entities_; }
    [[nodiscard]] LightstyleArray& GetLightstyles() noexcept { return cl_lightstyle_; }
    [[nodiscard]] DlightArray& GetDlights() noexcept { return cl_dlights_; }
    [[nodiscard]] TempEntityArray& GetTempEntities() noexcept { return cl_temp_entities_; }
    [[nodiscard]] BeamArray& GetBeams() noexcept { return cl_beams_; }

private:
    client_static_t cls_{};
    client_state_t cl_{};
    EfragArray cl_efrags_{};
    EntityArray cl_entities_{};
    StaticEntityArray cl_static_entities_{};
    LightstyleArray cl_lightstyle_{};
    DlightArray cl_dlights_{};
    TempEntityArray cl_temp_entities_{};
    BeamArray cl_beams_{};
};

[[nodiscard]] ClientSubsystem& GetClientSubsystem() noexcept;

inline client_static_t& cls = GetClientSubsystem().GetStaticState();
inline client_state_t& cl = GetClientSubsystem().GetState();
inline EfragArray& cl_efrags = GetClientSubsystem().GetEfrags();
inline EntityArray& cl_entities = GetClientSubsystem().GetEntities();
inline StaticEntityArray& cl_static_entities = GetClientSubsystem().GetStaticEntities();
inline LightstyleArray& cl_lightstyle = GetClientSubsystem().GetLightstyles();
inline DlightArray& cl_dlights = GetClientSubsystem().GetDlights();
inline TempEntityArray& cl_temp_entities = GetClientSubsystem().GetTempEntities();
inline BeamArray& cl_beams = GetClientSubsystem().GetBeams();

[[nodiscard]] dlight_t* CL_AllocDlight(int key);
void CL_DecayLights();
void CL_Init();
void CL_EstablishConnection(const char* host);
void CL_Signon1(); void CL_Signon2(); void CL_Signon3(); void CL_Signon4();
void CL_Disconnect();
void CL_Disconnect_f();
void CL_NextDemo();

inline constexpr int MAX_VISEDICTS = 256;
extern int cl_numvisedicts;
extern entity_t* cl_visedicts[MAX_VISEDICTS];

struct kbutton_t {
    eastl::array<int, 2> down{};
    int state{0};
};

extern kbutton_t in_mlook, in_klook, in_strafe, in_speed;

void CL_InitInput();
void CL_SendCmd();
void CL_SendMove(usercmd_t* cmd);
void CL_ParseTEnt();
void CL_UpdateTEnts();
void CL_ClearState();
int CL_ReadFromServer();
void CL_WriteToServer(usercmd_t* cmd);
void CL_BaseMove(usercmd_t* cmd);
[[nodiscard]] float CL_KeyState(kbutton_t* key);

void CL_StopPlayback();
int CL_GetMessage();
void CL_Stop_f();
void CL_Record_f();
void CL_PlayDemo_f();
void CL_TimeDemo_f();

void CL_ParseServerMessage();
void CL_NewTranslation(int slot);

void CL_InitTEnts();
void CL_SignonReply();

extern cvar_t chase_active;
void Chase_Init();
void Chase_Update();

} // namespace Client
