// client.h -- client state, connection, and entity structures
#pragma once

typedef struct {
    Vector3 viewangles;

    // intended velocities
    float forwardmove;
    float sidemove;
    float upmove;
} usercmd_t;

typedef struct {
    int length;
    char map[MAX_STYLESTRING];
} lightstyle_t;

typedef struct {
    char name[MAX_SCOREBOARDNAME];
    float entertime;
    int frags;
    int colors; // two 4 bit fields
    byte translations[VID_GRADES * 256];
} scoreboard_t;

typedef struct {
    int destcolor[3];
    int percent; // 0-256
} cshift_t;

#define CSHIFT_CONTENTS 0
#define CSHIFT_DAMAGE 1
#define CSHIFT_BONUS 2
#define CSHIFT_POWERUP 3
#define NUM_CSHIFTS 4

#define NAME_LENGTH 64

//
// client_state_t should hold all pieces of the client state
//

#define SIGNONS 4 // signon messages to receive before connected

#define MAX_DLIGHTS 32

typedef struct {
    Vector3 origin;
    float radius;
    float die;      // stop lighting after this time
    float decay;    // drop this each second
    float minlight; // don't add when contributing less
    int key;
} dlight_t;

#define MAX_BEAMS 24

typedef struct {
    int entity;
    struct model_s* model;
    float endtime;
    Vector3 start, end;
} beam_t;

#define MAX_EFRAGS 640

#define MAX_MAPSTRING 2048
#define MAX_DEMOS 8
#define MAX_DEMONAME 16

typedef enum {
    ca_dedicated,    // a dedicated server with no ability to start a client
    ca_disconnected, // full screen console with no connection
    ca_connected     // valid netcon, talking to a server
} cactive_t;

//
// the client_static_t structure is persistant through an arbitrary number
// of server connections
//
typedef struct {
    cactive_t state;

    // personalization data sent to server
    char mapstring[MAX_QPATH];
    char spawnparms[MAX_MAPSTRING]; // to restart a level

    // demo loop control
    int demonum;                         // -1 = don't play demos
    char demos[MAX_DEMOS][MAX_DEMONAME]; // when not playing

    // demo recording info must be here, because record is started before
    // entering a map (and clearing client_state_t)
    qboolean demorecording;
    qboolean demoplayback;
    qboolean timedemo;
    int forcetrack; // -1 = use normal cd track
    FILE* demofile;
    int td_lastframe;   // to meter out one message a frame
    int td_startframe;  // host_framecount at start
    float td_starttime; // realtime at second frame of timedemo

    // connection information
    int signon; // 0 to SIGNONS
    struct qsocket_s* netcon;
    sizebuf_t message; // writing buffer to send to server

} client_static_t;

namespace Client {

//
// the client_state_t structure is wiped completely at every
// server signon
//
typedef struct {
    int movemessages; // since connecting to this server
    // throw out the first couple, so the player
    // doesn't accidentally do something the
    // first frame
    usercmd_t cmd; // last command sent to the server

    // information for local display
    int stats[MAX_CL_STATS]; // health, etc
    int items;               // inventory bit flags
    float item_gettime[32];  // cl.time of aquiring item, for blinking
    float faceanimtime;      // use anim frame if cl.time < this

    cshift_t cshifts[NUM_CSHIFTS];      // color shifts for damage, powerups
    cshift_t prev_cshifts[NUM_CSHIFTS]; // and content types

    // the client maintains its own idea of view angles, which are
    // sent to the server each frame.  The server sets punchangle when
    // the view is temporarliy offset, and an angle reset commands at the start
    // of each level and after teleporting.
    Vector3 mviewangles[2]; // during demo playback viewangles is lerped
    // between these
    Vector3 viewangles;

    Vector3 mvelocity[2]; // update by server, used for lean+bob
    // (0 is newest)
    Vector3 velocity; // lerped between mvelocity[0] and [1]

    Vector3 punchangle; // temporary offset

    // pitch drifting vars
    float idealpitch;
    float pitchvel;
    qboolean nodrift;
    float driftmove;
    double laststop;

    float viewheight;
    float crouch; // local amount for smoothing stepups

    qboolean paused; // send over by server
    qboolean onground;
    qboolean inwater;

    int intermission;   // don't change view angle, full screen, etc
    int completed_time; // latched at intermission start

    double mtime[2]; // the timestamp of last two messages
    double time;     // clients view of time, should be between
    // servertime and oldservertime to generate
    // a lerp point for other data
    double oldtime; // previous cl.time, time-oldtime is used
    // to decay light values and smooth step ups

    float last_received_message; // (realtime) for net trouble icon

    //
    // information that is static for the entire time connected to a server
    //
    struct model_s* model_precache[MAX_MODELS];
    struct sfx_s* sound_precache[MAX_SOUNDS];

    char levelname[40]; // for display on solo scoreboard
    int viewentity;     // cl_entitites[cl.viewentity] = player
    int maxclients;
    int gametype;

    // refresh related state
    struct model_s* worldmodel; // cl_entitites[0].model
    struct efrag_s* free_efrags;
    int num_entities; // held in cl_entities array
    int num_statics;  // held in cl_staticentities array
    entity_t viewent; // the gun model

    int cdtrack, looptrack; // cd audio

    // frag scoreboard
    scoreboard_t* scores; // [cl.maxclients]

} client_state_t;

//
// cvars
//
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

#define MAX_TEMP_ENTITIES 64    // lightning bolts, etc
#define MAX_STATIC_ENTITIES 128 // torches, etc

using EfragArray = efrag_t[MAX_EFRAGS];
using EntityArray = entity_t[MAX_EDICTS];
using StaticEntityArray = entity_t[MAX_STATIC_ENTITIES];
using LightstyleArray = lightstyle_t[MAX_LIGHTSTYLES];
using DlightArray = dlight_t[MAX_DLIGHTS];
using TempEntityArray = entity_t[MAX_TEMP_ENTITIES];
using BeamArray = beam_t[MAX_BEAMS];

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
    efrag_t cl_efrags_[MAX_EFRAGS];
    entity_t cl_entities_[MAX_EDICTS];
    entity_t cl_static_entities_[MAX_STATIC_ENTITIES];
    lightstyle_t cl_lightstyle_[MAX_LIGHTSTYLES];
    dlight_t cl_dlights_[MAX_DLIGHTS];
    entity_t cl_temp_entities_[MAX_TEMP_ENTITIES];
    beam_t cl_beams_[MAX_BEAMS];
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

//=============================================================================

//
// cl_main
//
dlight_t* CL_AllocDlight(int key);
void CL_DecayLights(void);

void CL_Init(void);

void CL_EstablishConnection(const char* host);
void CL_Signon1(void);
void CL_Signon2(void);
void CL_Signon3(void);
void CL_Signon4(void);

void CL_Disconnect(void);
void CL_Disconnect_f(void);
void CL_NextDemo(void);

#define MAX_VISEDICTS 256
extern int cl_numvisedicts;
extern entity_t* cl_visedicts[MAX_VISEDICTS];

//
// cl_input
//
typedef struct {
    int down[2]; // key nums holding it down
    int state;   // low bit is down state
} kbutton_t;

extern kbutton_t in_mlook, in_klook;
extern kbutton_t in_strafe;
extern kbutton_t in_speed;

void CL_InitInput(void);
void CL_SendCmd(void);
void CL_SendMove(usercmd_t* cmd);

void CL_ParseTEnt(void);
void CL_UpdateTEnts(void);

void CL_ClearState(void);

int CL_ReadFromServer(void);
void CL_WriteToServer(usercmd_t* cmd);
void CL_BaseMove(usercmd_t* cmd);

float CL_KeyState(kbutton_t* key);

//
// cl_demo.cpp
//
void CL_StopPlayback(void);
int CL_GetMessage(void);

void CL_Stop_f(void);
void CL_Record_f(void);
void CL_PlayDemo_f(void);
void CL_TimeDemo_f(void);

//
// cl_parse.cpp
//
void CL_ParseServerMessage(void);
void CL_NewTranslation(int slot);

//
//
// cl_tent
//
void CL_InitTEnts(void);
void CL_SignonReply(void);

//
// chase
//
extern cvar_t chase_active;

void Chase_Init(void);
void Chase_Update(void);

} // namespace Client
