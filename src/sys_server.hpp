// sys_server.hpp -- Subsystem Server: Physics World, Entity Management & Server Host
#pragma once

#include <EASTL/array.h>
#include <EASTL/vector.h>
#include <EASTL/string.h>
#include <EASTL/fixed_string.h>
#include <EASTL/span.h>
#include <EASTL/string_view.h>
#include <EASTL/algorithm.h>
#include <cstdint>
#include <cstddef>

#include "sys_core.hpp"
#include "sys_vm.hpp"

//=============================================================================
// World Physics & Collision (from world.hpp)
//=============================================================================

struct plane_t {
    Vector3 normal{};
    float dist{0.0f};
};

struct trace_t {
    qboolean allsolid{false};
    qboolean startsolid{false};
    qboolean inopen{false};
    qboolean inwater{false};
    float fraction{1.0f};
    Vector3 endpos{};
    plane_t plane{};
    edict_t* ent{nullptr};
};

enum class MoveMode : int {
    Normal = 0,
    NoMonsters = 1,
    Missile = 2
};

constexpr int MOVE_NORMAL = 0;
constexpr int MOVE_NOMONSTERS = 1;
constexpr int MOVE_MISSILE = 2;

//=============================================================================
// Server Structures (from server.hpp)
//=============================================================================

constexpr int NUM_PING_TIMES = 16;
constexpr int NUM_SPAWN_PARMS = 16;

struct client_t {
    bool active{false};
    bool spawned{false};
    bool dropasap{false};
    bool privileged{false};
    bool sendsignon{false};

    double last_message{0.0};
    struct qsocket_s* netconnection{nullptr};

    usercmd_t cmd{};
    Vector3 wishdir{};

    sizebuf_t message{};
    eastl::array<byte, MAX_MSGLEN> msgbuf{};
    edict_t* edict{nullptr};
    eastl::array<char, 32> name{};
    int colors{0};

    eastl::array<float, NUM_PING_TIMES> ping_times{};
    int num_pings{0};

    eastl::array<float, NUM_SPAWN_PARMS> spawn_parms{};
    int old_frags{0};

    void Reset() noexcept { *this = client_t{}; }
    [[nodiscard]] bool IsActive() const noexcept { return active; }
    [[nodiscard]] bool IsSpawned() const noexcept { return spawned; }

    [[nodiscard]] const char* GetName() const noexcept { return name.data(); }
    [[nodiscard]] char* GetName() noexcept { return name.data(); }
    void SetName(eastl::string_view new_name) noexcept {
        const size_t copy_len = eastl::min(new_name.length(), name.size() - 1);
        eastl::copy_n(new_name.data(), copy_len, name.data());
        name[copy_len] = '\0';
    }

    [[nodiscard]] eastl::span<const float> GetPingTimes() const noexcept {
        return eastl::span<const float>(ping_times.data(), ping_times.size());
    }
};

struct server_static_t {
    int maxclients{0};
    int maxclientslimit{0};
    client_t* clients{nullptr};
    eastl::vector<client_t> client_storage{};
    int serverflags{0};
    bool changelevel_issued{false};

    void resize_clients(int count) {
        maxclients = count;
        client_storage.assign(static_cast<size_t>(count), client_t{});
        clients = client_storage.data();
    }

    [[nodiscard]] eastl::span<client_t> GetClients() noexcept {
        return eastl::span<client_t>(client_storage.data(), static_cast<size_t>(maxclients));
    }
    [[nodiscard]] eastl::span<const client_t> GetClients() const noexcept {
        return eastl::span<const client_t>(client_storage.data(), static_cast<size_t>(maxclients));
    }
    [[nodiscard]] int GetClientIndex(const client_t* client) const noexcept {
        return static_cast<int>(client - clients);
    }

    [[nodiscard]] client_t& operator[](size_t idx) noexcept { return client_storage[idx]; }
    [[nodiscard]] const client_t& operator[](size_t idx) const noexcept { return client_storage[idx]; }
};

enum class server_state_t {
    ss_loading,
    ss_active
};

struct server_t {
    bool active{false};
    bool paused{false};
    bool loadgame{false};

    double time{0.0};
    int lastcheck{0};
    double lastchecktime{0.0};

    eastl::array<char, 64> name{};
    eastl::array<char, 64> modelname{};
    struct model_s* worldmodel{nullptr};
    eastl::array<char*, MAX_MODELS> model_precache{};
    eastl::array<struct model_s*, MAX_MODELS> models{};
    eastl::array<char*, MAX_SOUNDS> sound_precache{};
    eastl::array<char*, MAX_LIGHTSTYLES> lightstyles{};
    int num_edicts{0};
    int max_edicts{0};
    edict_t* edicts{nullptr};

    server_state_t state{server_state_t::ss_loading};

    sizebuf_t datagram{};
    eastl::array<byte, MAX_DATAGRAM> datagram_buf{};

    sizebuf_t reliable_datagram{};
    eastl::array<byte, MAX_DATAGRAM> reliable_datagram_buf{};

    sizebuf_t signon{};
    eastl::array<byte, 8192> signon_buf{};

    [[nodiscard]] bool IsActive() const noexcept { return active; }
    [[nodiscard]] bool IsPaused() const noexcept { return paused; }
    [[nodiscard]] const char* GetName() const noexcept { return name.data(); }
    void SetName(eastl::string_view new_name) noexcept {
        const size_t copy_len = eastl::min(new_name.length(), name.size() - 1);
        eastl::copy_n(new_name.data(), copy_len, name.data());
        name[copy_len] = '\0';
    }
};

constexpr server_state_t ss_loading = server_state_t::ss_loading;
constexpr server_state_t ss_active = server_state_t::ss_active;

constexpr int MOVETYPE_NONE = 0;
constexpr int MOVETYPE_ANGLENOCLIP = 1;
constexpr int MOVETYPE_ANGLECLIP = 2;
constexpr int MOVETYPE_WALK = 3;
constexpr int MOVETYPE_STEP = 4;
constexpr int MOVETYPE_FLY = 5;
constexpr int MOVETYPE_TOSS = 6;
constexpr int MOVETYPE_PUSH = 7;
constexpr int MOVETYPE_NOCLIP = 8;
constexpr int MOVETYPE_FLYMISSILE = 9;
constexpr int MOVETYPE_BOUNCE = 10;

constexpr int SOLID_NOT = 0;
constexpr int SOLID_TRIGGER = 1;
constexpr int SOLID_BBOX = 2;
constexpr int SOLID_SLIDEBOX = 3;
constexpr int SOLID_BSP = 4;

constexpr int DEAD_NO = 0;
constexpr int DEAD_DYING = 1;
constexpr int DEAD_DEAD = 2;

constexpr int DAMAGE_NO = 0;
constexpr int DAMAGE_YES = 1;
constexpr int DAMAGE_AIM = 2;

constexpr int FL_FLY = 1;
constexpr int FL_SWIM = 2;
constexpr int FL_CONVEYOR = 4;
constexpr int FL_CLIENT = 8;
constexpr int FL_INWATER = 16;
constexpr int FL_MONSTER = 32;
constexpr int FL_GODMODE = 64;
constexpr int FL_NOTARGET = 128;
constexpr int FL_ITEM = 256;
constexpr int FL_ONGROUND = 512;
constexpr int FL_PARTIALGROUND = 1024;
constexpr int FL_WATERJUMP = 2048;
constexpr int FL_JUMPRELEASED = 4096;

constexpr int EF_BRIGHTFIELD = 1;
constexpr int EF_MUZZLEFLASH = 2;
constexpr int EF_BRIGHTLIGHT = 4;
constexpr int EF_DIMLIGHT = 8;

constexpr int SPAWNFLAG_NOT_EASY = 256;
constexpr int SPAWNFLAG_NOT_MEDIUM = 512;
constexpr int SPAWNFLAG_NOT_HARD = 1024;
constexpr int SPAWNFLAG_NOT_DEATHMATCH = 2048;

namespace Server {

extern cvar_t teamplay;
extern cvar_t skill;
extern cvar_t deathmatch;
extern cvar_t coop;
extern cvar_t fraglimit;
extern cvar_t timelimit;
extern cvar_t sv_gravity;

class ServerSubsystem {
public:
    [[nodiscard]] server_static_t& GetStaticState() noexcept { return svs_; }
    [[nodiscard]] const server_static_t& GetStaticState() const noexcept { return svs_; }

    [[nodiscard]] server_t& GetState() noexcept { return sv_; }
    [[nodiscard]] const server_t& GetState() const noexcept { return sv_; }

private:
    server_static_t svs_{};
    server_t sv_{};
};

[[nodiscard]] ServerSubsystem& GetServerSubsystem() noexcept;

inline server_static_t& svs = GetServerSubsystem().GetStaticState();
inline server_t& sv = GetServerSubsystem().GetState();

extern edict_t* sv_player;

void SV_Init();
void SV_StartParticle(const Vector3& org, const Vector3& dir, int color, int count);
void SV_StartSound(edict_t* entity, int channel, const char* sample, int vol, float attenuation);
void SV_DropClient(bool crash);
void SV_SendClientMessages();
[[nodiscard]] int SV_ModelIndex(const char* name);
void SV_SetIdealPitch();
void SV_AddUpdates();
void SV_ClientThink();
void SV_AddClientToServer(struct qsocket_s* ret);
void SV_ClientPrintf(const char* fmt, ...);
void SV_BroadcastPrintf(const char* fmt, ...);
void SV_Physics();
[[nodiscard]] bool SV_CheckBottom(edict_t* ent);
bool SV_movestep(edict_t* ent, const Vector3& move, bool relink);
void SV_WriteClientdataToMessage(edict_t* ent, sizebuf_t* msg);
void SV_MoveToGoal();
void SV_CheckForNewClients();
void SV_RunClients();
void SV_SaveSpawnparms();
void SV_SpawnServer(const char* server);

void SV_ClearWorld();
void SV_UnlinkEdict(edict_t* ent);
void SV_LinkEdict(edict_t* ent, qboolean touch_triggers);
[[nodiscard]] int SV_PointContents(const Vector3& p);
[[nodiscard]] edict_t* SV_TestEntityPosition(edict_t* ent);

trace_t SV_Move(const Vector3& start, const Vector3& mins, const Vector3& maxs, const Vector3& end, int type, edict_t* passedict);
qboolean SV_RecursiveHullCheck(hull_t* hull, int num, float p1f, float p2f, const Vector3& p1, const Vector3& p2, trace_t* trace);

} // namespace Server
