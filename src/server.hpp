// server.hpp -- server state and entity management structures
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

constexpr int NUM_PING_TIMES = 16;
constexpr int NUM_SPAWN_PARMS = 16;

struct client_t {
    bool active{false};     // false = client is free
    bool spawned{false};    // false = don't send datagrams
    bool dropasap{false};   // has been told to go to another level
    bool privileged{false}; // can execute any host command
    bool sendsignon{false}; // only valid before spawned

    double last_message{0.0}; // reliable messages must be sent periodically

    struct qsocket_s* netconnection{nullptr}; // communications handle

    usercmd_t cmd{};   // movement
    Vector3 wishdir{}; // intended motion calced from cmd

    sizebuf_t message{}; // can be added to at any time, copied and clear once per frame
    eastl::array<byte, MAX_MSGLEN> msgbuf{};
    edict_t* edict{nullptr}; // EDICT_NUM(clientnum+1)
    eastl::array<char, 32> name{}; // for printing to other people
    int colors{0};

    eastl::array<float, NUM_PING_TIMES> ping_times{};
    int num_pings{0}; // ping_times[num_pings%NUM_PING_TIMES]

    // spawn parms are carried from level to level
    eastl::array<float, NUM_SPAWN_PARMS> spawn_parms{};

    // client known data for deltas
    int old_frags{0};

    void Reset() noexcept {
        *this = client_t{};
    }

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
    client_t* clients{nullptr}; // [maxclients] pointer to client elements
    eastl::vector<client_t> client_storage{};
    int serverflags{0};             // episode completion information
    bool changelevel_issued{false}; // cleared when at SV_SpawnServer

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

//=============================================================================

enum class server_state_t {
    ss_loading,
    ss_active
};

struct server_t {
    bool active{false}; // false if only a net client

    bool paused{false};
    bool loadgame{false}; // handle connections specially

    double time{0.0};

    int lastcheck{0}; // used by PF_checkclient
    double lastchecktime{0.0};

    eastl::array<char, 64> name{};       // map name
    eastl::array<char, 64> modelname{};  // maps/<name>.bsp, for model_precache[0]
    struct model_s* worldmodel{nullptr};
    eastl::array<char*, MAX_MODELS> model_precache{}; // NULL terminated
    eastl::array<struct model_s*, MAX_MODELS> models{};
    eastl::array<char*, MAX_SOUNDS> sound_precache{}; // NULL terminated
    eastl::array<char*, MAX_LIGHTSTYLES> lightstyles{};
    int num_edicts{0};
    int max_edicts{0};
    edict_t* edicts{nullptr}; // can NOT be array indexed, because edict_t is variable sized

    server_state_t state{server_state_t::ss_loading}; // some actions are only valid during load

    sizebuf_t datagram{};
    eastl::array<byte, MAX_DATAGRAM> datagram_buf{};

    sizebuf_t reliable_datagram{}; // copied to all clients at end of frame
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

// Backwards compatibility alias for ss_loading and ss_active if needed
constexpr server_state_t ss_loading = server_state_t::ss_loading;
constexpr server_state_t ss_active = server_state_t::ss_active;

//=============================================================================

// edict->movetype values
constexpr int MOVETYPE_NONE = 0; // never moves
constexpr int MOVETYPE_ANGLENOCLIP = 1;
constexpr int MOVETYPE_ANGLECLIP = 2;
constexpr int MOVETYPE_WALK = 3; // gravity
constexpr int MOVETYPE_STEP = 4; // gravity, special edge handling
constexpr int MOVETYPE_FLY = 5;
constexpr int MOVETYPE_TOSS = 6; // gravity
constexpr int MOVETYPE_PUSH = 7; // no clip to world, push and crush
constexpr int MOVETYPE_NOCLIP = 8;
constexpr int MOVETYPE_FLYMISSILE = 9; // extra size to monsters
constexpr int MOVETYPE_BOUNCE = 10;

// edict->solid values
constexpr int SOLID_NOT = 0;      // no interaction with other objects
constexpr int SOLID_TRIGGER = 1;  // touch on edge, but not blocking
constexpr int SOLID_BBOX = 2;     // touch on edge, block
constexpr int SOLID_SLIDEBOX = 3; // touch on edge, but not an onground
constexpr int SOLID_BSP = 4;      // bsp clip, touch on edge, block

// edict->deadflag values
constexpr int DEAD_NO = 0;
constexpr int DEAD_DYING = 1;
constexpr int DEAD_DEAD = 2;

constexpr int DAMAGE_NO = 0;
constexpr int DAMAGE_YES = 1;
constexpr int DAMAGE_AIM = 2;

// edict->flags
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
constexpr int FL_PARTIALGROUND = 1024; // not all corners are valid
constexpr int FL_WATERJUMP = 2048;     // player jumping out of water
constexpr int FL_JUMPRELEASED = 4096;  // for jump debouncing

// entity effects
constexpr int EF_BRIGHTFIELD = 1;
constexpr int EF_MUZZLEFLASH = 2;
constexpr int EF_BRIGHTLIGHT = 4;
constexpr int EF_DIMLIGHT = 8;

constexpr int SPAWNFLAG_NOT_EASY = 256;
constexpr int SPAWNFLAG_NOT_MEDIUM = 512;
constexpr int SPAWNFLAG_NOT_HARD = 1024;
constexpr int SPAWNFLAG_NOT_DEATHMATCH = 2048;

//============================================================================

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

//===========================================================

void SV_Init();

void SV_StartParticle(const Vector3& org, const Vector3& dir, int color, int count);
void SV_StartSound(edict_t* entity,
    int channel,
    const char* sample,
    int vol,
    float attenuation);

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

} // namespace Server
