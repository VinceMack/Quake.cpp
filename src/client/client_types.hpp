// client_types.hpp -- Client Subsystem Core Types, State, and Limits
#pragma once

#include <cstdio>
#include <cstdint>
#include <ostream>
#include <array>
#include <vector>
#include <string>
#include <string_view>
#include <span>

#include "core/types.hpp"
#include "core/math.hpp"
#include "core/msg.hpp"
#include "core/cvar.hpp"
#include "world/model.hpp"
#include "render/render_types.hpp"

namespace Audio { struct sfx_t; }
using Audio::sfx_t;

namespace Client {

template <typename T, std::size_t N>
struct compat_array : public std::array<T, N> {
    [[nodiscard]] constexpr operator T*() noexcept { return this->data(); }
    [[nodiscard]] constexpr operator const T*() const noexcept { return this->data(); }
    [[nodiscard]] constexpr operator std::string_view() const noexcept {
        return std::string_view(reinterpret_cast<const char*>(this->data()));
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
    std::array<int, 3> destcolor{};
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
inline constexpr int MAX_VISEDICTS = 256;
inline constexpr int MAX_TEMP_ENTITIES = 64, MAX_STATIC_ENTITIES = 128;

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
    std::array<byte, 1024> message_buf{};
};

namespace Client {

struct client_state_t {
    int movemessages{0};
    usercmd_t cmd{};
    std::array<int, MAX_CL_STATS> stats{};
    int items{0};
    std::array<float, 32> item_gettime{};
    float faceanimtime{0.0f};
    std::array<cshift_t, NUM_CSHIFTS> cshifts{}, prev_cshifts{};
    std::array<Vector3, 2> mviewangles{}, mvelocity{};
    Vector3 viewangles{}, velocity{}, punchangle{};
    float idealpitch{0.0f}, pitchvel{0.0f};
    bool nodrift{false};
    float driftmove{0.0f};
    double laststop{0.0};
    float viewheight{0.0f}, crouch{0.0f};
    bool paused{false}, onground{false}, inwater{false};
    int intermission{0}, completed_time{0};
    std::array<double, 2> mtime{};
    double time{0.0}, oldtime{0.0};
    float last_received_message{0.0f};
    std::array<model_t*, MAX_MODELS> model_precache{};
    std::array<sfx_t*, MAX_SOUNDS> sound_precache{};
    compat_array<char, 40> levelname{};
    int viewentity{0}, maxclients{0}, gametype{0};
    model_t* worldmodel{nullptr};
    struct efrag_s* free_efrags{nullptr};
    int num_entities{0}, num_statics{0};
    entity_t viewent{};
    int cdtrack{0}, looptrack{0};
    std::vector<scoreboard_t> scores_storage;
    scoreboard_t* scores{nullptr};
};

struct kbutton_t {
    std::array<int, 2> down{};
    int state{0};
};

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

extern int cl_numvisedicts;
extern entity_t* cl_visedicts[MAX_VISEDICTS];

extern cvar_t cl_name, cl_color, cl_upspeed, cl_forwardspeed, cl_backspeed, cl_sidespeed;
extern cvar_t cl_movespeedkey, cl_yawspeed, cl_pitchspeed, cl_anglespeedkey;
extern cvar_t cl_shownet, cl_nolerp, lookspring, lookstrafe, sensitivity;
extern cvar_t m_pitch, m_yaw, m_forward, m_side;

} // namespace Client
