// server_types.hpp -- Server Subsystem Core Types and State
#pragma once

#include <array>
#include <vector>
#include <string>
#include <span>
#include <string_view>
#include <algorithm>
#include <cstdint>
#include <cstddef>

#include "core/types.hpp"
#include "core/math.hpp"
#include "core/msg.hpp"
#include "core/cvar.hpp"
#include "vm/edict.hpp"
#include "world/collision.hpp"
#include "quakedef.hpp"

//=============================================================================
// Server Constants and Limits
//=============================================================================

constexpr int NUM_PING_TIMES = 16;
constexpr int NUM_SPAWN_PARMS = 16;

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

//=============================================================================
// Client State on Server
//=============================================================================

struct client_t {
    bool active { false };
    bool spawned { false };
    bool dropasap { false };
    bool privileged { false };
    bool sendsignon { false };

    double last_message { 0.0 };
    struct qsocket_s* netconnection { nullptr };

    usercmd_t cmd { };
    Vector3 wishdir { };

    sizebuf_t message { };
    std::array<byte, MAX_MSGLEN> msgbuf { };
    edict_t* edict { nullptr };
    std::array<char, 32> name { };
    int colors { 0 };

    std::array<float, NUM_PING_TIMES> ping_times { };
    int num_pings { 0 };

    std::array<float, NUM_SPAWN_PARMS> spawn_parms { };
    int old_frags { 0 };

    void Reset() noexcept { *this = client_t { }; }
    [[nodiscard]] bool IsActive() const noexcept { return active; }
    [[nodiscard]] bool IsSpawned() const noexcept { return spawned; }

    [[nodiscard]] const char* GetName() const noexcept { return name.data(); }
    [[nodiscard]] char* GetName() noexcept { return name.data(); }
    void SetName(std::string_view new_name) noexcept
    {
        const size_t copy_len = std::min(new_name.length(), name.size() - 1);
        std::copy_n(new_name.data(), copy_len, name.data());
        name[copy_len] = '\0';
    }

    [[nodiscard]] std::span<const float> GetPingTimes() const noexcept
    {
        return std::span<const float>(ping_times.data(), ping_times.size());
    }
};

struct server_static_t {
    int maxclients { 0 };
    int maxclientslimit { 0 };
    client_t* clients { nullptr };
    std::vector<client_t> client_storage { };
    int serverflags { 0 };
    bool changelevel_issued { false };

    void resize_clients(int count)
    {
        maxclients = count;
        client_storage.assign(static_cast<size_t>(count), client_t { });
        clients = client_storage.data();
    }

    [[nodiscard]] std::span<client_t> GetClients() noexcept
    {
        return std::span<client_t>(client_storage.data(), static_cast<size_t>(maxclients));
    }
    [[nodiscard]] std::span<const client_t> GetClients() const noexcept
    {
        return std::span<const client_t>(client_storage.data(), static_cast<size_t>(maxclients));
    }
    [[nodiscard]] int GetClientIndex(const client_t* client) const noexcept
    {
        return static_cast<int>(client - clients);
    }

    [[nodiscard]] client_t& operator[](size_t idx) noexcept { return client_storage[idx]; }
    [[nodiscard]] const client_t& operator[](size_t idx) const noexcept { return client_storage[idx]; }
};

enum class server_state_t { ss_loading, ss_active };

constexpr server_state_t ss_loading = server_state_t::ss_loading;
constexpr server_state_t ss_active = server_state_t::ss_active;

struct server_t {
    bool active { false };
    bool paused { false };
    bool loadgame { false };

    double time { 0.0 };
    int lastcheck { 0 };
    double lastchecktime { 0.0 };

    std::array<char, 64> name { };
    std::array<char, 64> modelname { };
    struct model_s* worldmodel { nullptr };
    std::array<char*, MAX_MODELS> model_precache { };
    std::array<struct model_s*, MAX_MODELS> models { };
    std::array<char*, MAX_SOUNDS> sound_precache { };
    std::array<const char*, MAX_LIGHTSTYLES> lightstyles { };
    std::array<std::string, MAX_LIGHTSTYLES> loaded_lightstyles { }; // backing store for styles read from a savegame
    int num_edicts { 0 };
    int max_edicts { 0 };
    std::vector<byte> edicts_storage; // max_edicts * pr_edict_size bytes, zero-initialized
    edict_t* edicts { nullptr };

    server_state_t state { server_state_t::ss_loading };

    sizebuf_t datagram { };
    std::array<byte, MAX_DATAGRAM> datagram_buf { };

    sizebuf_t reliable_datagram { };
    std::array<byte, MAX_DATAGRAM> reliable_datagram_buf { };

    sizebuf_t signon { };
    std::array<byte, 8192> signon_buf { };

    [[nodiscard]] bool IsActive() const noexcept { return active; }
    [[nodiscard]] bool IsPaused() const noexcept { return paused; }
    [[nodiscard]] const char* GetName() const noexcept { return name.data(); }
    void SetName(std::string_view new_name) noexcept
    {
        const size_t copy_len = std::min(new_name.length(), name.size() - 1);
        std::copy_n(new_name.data(), copy_len, name.data());
        name[copy_len] = '\0';
    }
};

namespace Server {

extern cvar_t teamplay;
extern cvar_t skill;
extern cvar_t deathmatch;
extern cvar_t coop;
extern cvar_t fraglimit;
extern cvar_t timelimit;
extern cvar_t sv_gravity;
extern cvar_t sv_friction;
extern cvar_t sv_stopspeed;
extern cvar_t sv_maxvelocity;
extern cvar_t sv_nostep;
extern cvar_t sv_idealpitchscale;
extern cvar_t sv_maxspeed;
extern cvar_t sv_accelerate;
extern cvar_t sv_edgefriction;

// The persistent server (connected clients) and the currently running level.
extern server_static_t svs;
extern server_t sv;

extern edict_t* sv_player;

} // namespace Server

// QuakeC stores entity references as byte offsets from the start of the edict array.
[[nodiscard]] inline int EDICT_TO_PROG(const edict_t* e)
{
    return static_cast<int>(reinterpret_cast<const byte*>(e) - reinterpret_cast<const byte*>(Server::sv.edicts));
}

[[nodiscard]] inline edict_t* PROG_TO_EDICT(int offset)
{
    return reinterpret_cast<edict_t*>(reinterpret_cast<byte*>(Server::sv.edicts) + offset);
}

// The entity stored in a QuakeC global (an OFS_PARM slot, for example).
[[nodiscard]] inline edict_t* G_EDICT(int ofs)
{
    return PROG_TO_EDICT(G_INT(ofs));
}
[[nodiscard]] inline int G_EDICTNUM(int ofs)
{
    return VM::NUM_FOR_EDICT(G_EDICT(ofs));
}
