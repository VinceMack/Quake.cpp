// sys_client.hpp -- Subsystem Client: Client Engine, Host Frame Loop, Keys, Console, Menu & Camera
#pragma once

#include <cstdio>
#include <cstdint>
#include <ostream>
#include <EASTL/array.h>
#include <EASTL/vector.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>

#include "sys_core.hpp"
#include "sys_render.hpp"

//=============================================================================
// Host & Parms (from host.hpp)
//=============================================================================

struct quakeparms_t {
    const char* basedir;
    const char* cachedir;
    int argc;
    char** argv;
    void* membase;
    int memsize;
};

namespace Host {

extern quakeparms_t host_parms;
extern cvar_t sys_ticrate;
extern cvar_t sys_nostdout;
extern cvar_t developer;

extern qboolean host_initialized;
extern double host_frametime;
extern byte* host_basepal;
extern byte* host_colormap;
extern int host_framecount;
extern double realtime;

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

extern int current_skill;
extern qboolean isDedicated;
extern int minimum_memory;
extern qboolean noclip_anglehack;

extern ::client_t* host_client;
extern double host_time;

} // namespace Host

//=============================================================================
// Keys & Input Constants (from keys.hpp)
//=============================================================================

namespace Keys {

inline constexpr int K_TAB = 9;
inline constexpr int K_ENTER = 13;
inline constexpr int K_ESCAPE = 27;
inline constexpr int K_SPACE = 32;

inline constexpr int K_BACKSPACE = 127;
inline constexpr int K_UPARROW = 128;
inline constexpr int K_DOWNARROW = 129;
inline constexpr int K_LEFTARROW = 130;
inline constexpr int K_RIGHTARROW = 131;

inline constexpr int K_ALT = 132;
inline constexpr int K_CTRL = 133;
inline constexpr int K_SHIFT = 134;
inline constexpr int K_F1 = 135;
inline constexpr int K_F2 = 136;
inline constexpr int K_F3 = 137;
inline constexpr int K_F4 = 138;
inline constexpr int K_F5 = 139;
inline constexpr int K_F6 = 140;
inline constexpr int K_F7 = 141;
inline constexpr int K_F8 = 142;
inline constexpr int K_F9 = 143;
inline constexpr int K_F10 = 144;
inline constexpr int K_F11 = 145;
inline constexpr int K_F12 = 146;
inline constexpr int K_INS = 147;
inline constexpr int K_DEL = 148;
inline constexpr int K_PGDN = 149;
inline constexpr int K_PGUP = 150;
inline constexpr int K_HOME = 151;
inline constexpr int K_END = 152;

inline constexpr int K_PAUSE = 255;

inline constexpr int K_MOUSE1 = 200;
inline constexpr int K_MOUSE2 = 201;
inline constexpr int K_MOUSE3 = 202;

inline constexpr int K_JOY1 = 203;
inline constexpr int K_JOY2 = 204;
inline constexpr int K_JOY3 = 205;
inline constexpr int K_JOY4 = 206;

inline constexpr int K_AUX1 = 207;
inline constexpr int K_AUX2 = 208;
inline constexpr int K_AUX3 = 209;
inline constexpr int K_AUX4 = 210;
inline constexpr int K_AUX5 = 211;
inline constexpr int K_AUX6 = 212;
inline constexpr int K_AUX7 = 213;
inline constexpr int K_AUX8 = 214;
inline constexpr int K_AUX9 = 215;
inline constexpr int K_AUX10 = 216;
inline constexpr int K_AUX11 = 217;
inline constexpr int K_AUX12 = 218;
inline constexpr int K_AUX13 = 219;
inline constexpr int K_AUX14 = 220;
inline constexpr int K_AUX15 = 221;
inline constexpr int K_AUX16 = 222;
inline constexpr int K_AUX17 = 223;
inline constexpr int K_AUX18 = 224;
inline constexpr int K_AUX19 = 225;
inline constexpr int K_AUX20 = 226;
inline constexpr int K_AUX21 = 227;
inline constexpr int K_AUX22 = 228;
inline constexpr int K_AUX23 = 229;
inline constexpr int K_AUX24 = 230;
inline constexpr int K_AUX25 = 231;
inline constexpr int K_AUX26 = 232;
inline constexpr int K_AUX27 = 233;
inline constexpr int K_AUX28 = 234;
inline constexpr int K_AUX29 = 235;
inline constexpr int K_AUX30 = 236;
inline constexpr int K_AUX31 = 237;
inline constexpr int K_AUX32 = 238;

inline constexpr int K_MWHEELUP = 239;
inline constexpr int K_MWHEELDOWN = 240;

inline constexpr int MAXCMDLINE = 256;

extern eastl::array<eastl::array<char, MAXCMDLINE>, 32> key_lines;
extern int key_linepos;
extern int edit_line;
extern eastl::array<char, 32> chat_buffer;
extern bool team_message;

enum keydest_t {
    key_game,
    key_console,
    key_message,
    key_menu
};

extern keydest_t key_dest;
extern eastl::array<eastl::string, 256> keybindings;
extern eastl::array<int, 256> key_repeats;
extern int key_count;
extern int key_lastpress;

void Key_Event(int key, bool down);
void Key_Init();
void Key_WriteBindings(std::ostream& f);
void Key_SetBinding(int keynum, const char* binding);
const char* Key_KeynumToString(int keynum);

} // namespace Keys

//=============================================================================
// Console System (from console.hpp)
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

    int linewidth_ = 0;
    float cursorspeed_ = 4.0f;
    int current_ = 0;
    int x_ = 0;
    eastl::vector<char> text_;
    eastl::vector<float> times_;
    int vislines_ = 0;
    bool debuglog_ = false;

    void Linefeed();
    void DebugLog(eastl::string_view file, eastl::string_view text);
    void DrawInput();
};

ConsoleSystem& GetConsoleSystem();

void Con_Printf(const char* fmt, ...);
void Con_DPrintf(const char* fmt, ...);

} // namespace Console

//=============================================================================
// Menu System (from menu.hpp)
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

extern MenuState m_state;
extern MenuState m_return_state;
extern bool m_return_onerror;
extern eastl::string m_return_reason;

} // namespace Menu

//=============================================================================
// Client State & Entities (from client.hpp)
//=============================================================================

struct lightstyle_t {
    int length;
    char map[MAX_STYLESTRING];
};

struct scoreboard_t {
    char name[MAX_SCOREBOARDNAME];
    float entertime;
    int frags;
    int colors;
    byte translations[VID_GRADES * 256];
};

struct cshift_t {
    eastl::array<int, 3> destcolor;
    int percent;
};

#define CSHIFT_CONTENTS 0
#define CSHIFT_DAMAGE 1
#define CSHIFT_BONUS 2
#define CSHIFT_POWERUP 3
#define NUM_CSHIFTS 4

#define NAME_LENGTH 64
#define SIGNONS 4
#define MAX_DLIGHTS 32


#define MAX_BEAMS 24

struct beam_t {
    int entity;
    model_t* model;
    float endtime;
    Vector3 start, end;
};

#define MAX_EFRAGS 640
#define MAX_MAPSTRING 2048
#define MAX_DEMOS 8
#define MAX_DEMONAME 16

enum cactive_t {
    ca_dedicated,
    ca_disconnected,
    ca_connected
};

struct client_static_t {
    cactive_t state;
    char mapstring[MAX_QPATH];
    char spawnparms[MAX_MAPSTRING];

    int demonum;
    char demos[MAX_DEMOS][MAX_DEMONAME];

    bool demorecording;
    bool demoplayback;
    bool timedemo;
    int forcetrack;
    FILE* demofile;
    int td_lastframe;
    int td_startframe;
    float td_starttime;

    int signon;
    struct qsocket_s* netcon;
    sizebuf_t message;
};

namespace Client {

template <typename T, std::size_t N>
struct compat_array : public eastl::array<T, N> {
    operator T*() { return this->data(); }
    operator const T*() const { return this->data(); }
};

struct client_state_t {
    int movemessages;
    usercmd_t cmd;

    eastl::array<int, MAX_CL_STATS> stats;
    int items;
    eastl::array<float, 32> item_gettime;
    float faceanimtime;

    eastl::array<cshift_t, NUM_CSHIFTS> cshifts;
    eastl::array<cshift_t, NUM_CSHIFTS> prev_cshifts;

    eastl::array<Vector3, 2> mviewangles;
    Vector3 viewangles;

    eastl::array<Vector3, 2> mvelocity;
    Vector3 velocity;

    Vector3 punchangle;

    float idealpitch;
    float pitchvel;
    bool nodrift;
    float driftmove;
    double laststop;

    float viewheight;
    float crouch;

    bool paused;
    bool onground;
    bool inwater;

    int intermission;
    int completed_time;

    eastl::array<double, 2> mtime;
    double time;
    double oldtime;
    float last_received_message;

    eastl::array<model_t*, MAX_MODELS> model_precache;
    eastl::array<sfx_t*, MAX_SOUNDS> sound_precache;

    char levelname[40];
    int viewentity;
    int maxclients;
    int gametype;

    model_t* worldmodel;
    struct efrag_s* free_efrags;
    int num_entities;
    int num_statics;
    entity_t viewent;

    int cdtrack, looptrack;
    scoreboard_t* scores;
};

extern cvar_t cl_name;
extern cvar_t cl_color;
extern cvar_t cl_upspeed;
extern cvar_t cl_forwardspeed;
extern cvar_t cl_backspeed;
extern cvar_t cl_sidespeed;
extern cvar_t cl_movespeedkey;
extern cvar_t cl_yawspeed;
extern cvar_t cl_pitchspeed;
extern cvar_t cl_anglespeedkey;
extern cvar_t cl_autofire;
extern cvar_t cl_shownet;
extern cvar_t cl_nolerp;
extern cvar_t cl_pitchdriftspeed;
extern cvar_t lookspring;
extern cvar_t lookstrafe;
extern cvar_t sensitivity;
extern cvar_t m_pitch;
extern cvar_t m_yaw;
extern cvar_t m_forward;
extern cvar_t m_side;

#define MAX_TEMP_ENTITIES 64
#define MAX_STATIC_ENTITIES 128

using EfragArray = compat_array<efrag_t, MAX_EFRAGS>;
using EntityArray = compat_array<entity_t, MAX_EDICTS>;
using StaticEntityArray = compat_array<entity_t, MAX_STATIC_ENTITIES>;
using LightstyleArray = compat_array<lightstyle_t, MAX_LIGHTSTYLES>;
using DlightArray = compat_array<dlight_t, MAX_DLIGHTS>;
using TempEntityArray = compat_array<entity_t, MAX_TEMP_ENTITIES>;
using BeamArray = compat_array<beam_t, MAX_BEAMS>;

class ClientSubsystem {
public:
    client_static_t& GetStaticState() { return cls_; }
    const client_static_t& GetStaticState() const { return cls_; }

    client_state_t& GetState() { return cl_; }
    const client_state_t& GetState() const { return cl_; }

    EfragArray& GetEfrags() { return cl_efrags_; }
    EntityArray& GetEntities() { return cl_entities_; }
    StaticEntityArray& GetStaticEntities() { return cl_static_entities_; }
    LightstyleArray& GetLightstyles() { return cl_lightstyle_; }
    DlightArray& GetDlights() { return cl_dlights_; }
    TempEntityArray& GetTempEntities() { return cl_temp_entities_; }
    BeamArray& GetBeams() { return cl_beams_; }

private:
    client_static_t cls_;
    client_state_t cl_;
    EfragArray cl_efrags_;
    EntityArray cl_entities_;
    StaticEntityArray cl_static_entities_;
    LightstyleArray cl_lightstyle_;
    DlightArray cl_dlights_;
    TempEntityArray cl_temp_entities_;
    BeamArray cl_beams_;
};

ClientSubsystem& GetClientSubsystem();

inline client_static_t& cls = GetClientSubsystem().GetStaticState();
inline client_state_t& cl = GetClientSubsystem().GetState();
inline EfragArray& cl_efrags = GetClientSubsystem().GetEfrags();
inline EntityArray& cl_entities = GetClientSubsystem().GetEntities();
inline StaticEntityArray& cl_static_entities = GetClientSubsystem().GetStaticEntities();
inline LightstyleArray& cl_lightstyle = GetClientSubsystem().GetLightstyles();
inline DlightArray& cl_dlights = GetClientSubsystem().GetDlights();
inline TempEntityArray& cl_temp_entities = GetClientSubsystem().GetTempEntities();
inline BeamArray& cl_beams = GetClientSubsystem().GetBeams();

dlight_t* CL_AllocDlight(int key);
void CL_DecayLights();
void CL_Init();
void CL_EstablishConnection(const char* host);
void CL_Signon1();
void CL_Signon2();
void CL_Signon3();
void CL_Signon4();
void CL_Disconnect();
void CL_Disconnect_f();
void CL_NextDemo();

#define MAX_VISEDICTS 256
extern int cl_numvisedicts;
extern entity_t* cl_visedicts[MAX_VISEDICTS];

struct kbutton_t {
    std::array<int, 2> down;
    int state;
};

extern kbutton_t in_mlook, in_klook;
extern kbutton_t in_strafe;
extern kbutton_t in_speed;

void CL_InitInput();
void CL_SendCmd();
void CL_SendMove(usercmd_t* cmd);
void CL_ParseTEnt();
void CL_UpdateTEnts();
void CL_ClearState();
int CL_ReadFromServer();
void CL_WriteToServer(usercmd_t* cmd);
void CL_BaseMove(usercmd_t* cmd);
float CL_KeyState(kbutton_t* key);

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
