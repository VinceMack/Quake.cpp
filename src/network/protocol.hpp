// protocol.hpp -- Quake Network Protocol Constants and Message IDs
#pragma once
#include "quakedef.hpp"

#include <cstdint>

//=============================================================================
// Protocol Version and Channel Constants
//=============================================================================

constexpr int PROTOCOL_VERSION = 15;

// Entity update flags for svc_update
constexpr int U_MOREBITS   = (1 << 0);
constexpr int U_ORIGIN1    = (1 << 1);
constexpr int U_ORIGIN2    = (1 << 2);
constexpr int U_ORIGIN3    = (1 << 3);
constexpr int U_ANGLE2     = (1 << 4);
constexpr int U_NOLERP     = (1 << 5);
constexpr int U_FRAME      = (1 << 6);
constexpr int U_SIGNAL     = (1 << 7);
constexpr int U_ANGLE1     = (1 << 8);
constexpr int U_ANGLE3     = (1 << 9);
constexpr int U_MODEL      = (1 << 10);
constexpr int U_COLORMAP   = (1 << 11);
constexpr int U_SKIN       = (1 << 12);
constexpr int U_EFFECTS    = (1 << 13);
constexpr int U_LONGENTITY = (1 << 14);

// Client update flags for svc_clientdata
constexpr int SU_VIEWHEIGHT  = (1 << 0);
constexpr int SU_IDEALPITCH  = (1 << 1);
constexpr int SU_PUNCH1      = (1 << 2);
constexpr int SU_PUNCH2      = (1 << 3);
constexpr int SU_PUNCH3      = (1 << 4);
constexpr int SU_VELOCITY1   = (1 << 5);
constexpr int SU_VELOCITY2   = (1 << 6);
constexpr int SU_VELOCITY3   = (1 << 7);
constexpr int SU_ITEMS       = (1 << 9);
constexpr int SU_ONGROUND    = (1 << 10);
constexpr int SU_INWATER     = (1 << 11);
constexpr int SU_WEAPONFRAME = (1 << 12);
constexpr int SU_ARMOR       = (1 << 13);
constexpr int SU_WEAPON      = (1 << 14);

// Sound packet flags
constexpr int SND_VOLUME      = (1 << 0);
constexpr int SND_ATTENUATION = (1 << 1);
constexpr int SND_LOOPING     = (1 << 2);

constexpr int DEFAULT_VIEWHEIGHT = 22;
constexpr int GAME_COOP = 0;
constexpr int GAME_DEATHMATCH = 1;

//=============================================================================
// Server to Client Protocol Messages (svc_*)
//=============================================================================

constexpr int svc_bad              = 0;
constexpr int svc_nop              = 1;
constexpr int svc_disconnect       = 2;
constexpr int svc_updatestat       = 3;
constexpr int svc_version          = 4;
constexpr int svc_setview          = 5;
constexpr int svc_sound            = 6;
constexpr int svc_time             = 7;
constexpr int svc_print            = 8;
constexpr int svc_stufftext        = 9;
constexpr int svc_setangle         = 10;
constexpr int svc_serverinfo       = 11;
constexpr int svc_lightstyle       = 12;
constexpr int svc_updatename       = 13;
constexpr int svc_updatefrags      = 14;
constexpr int svc_clientdata       = 15;
constexpr int svc_stopsound        = 16;
constexpr int svc_updatecolors     = 17;
constexpr int svc_particle         = 18;
constexpr int svc_damage           = 19;
constexpr int svc_spawnstatic      = 20;
constexpr int svc_spawnbaseline    = 22;
constexpr int svc_temp_entity      = 23;
constexpr int svc_setpause         = 24;
constexpr int svc_signonnum        = 25;
constexpr int svc_centerprint      = 26;
constexpr int svc_killedmonster    = 27;
constexpr int svc_foundsecret      = 28;
constexpr int svc_spawnstaticsound = 29;
constexpr int svc_intermission     = 30;
constexpr int svc_finale           = 31;
constexpr int svc_cdtrack          = 32;
constexpr int svc_sellscreen       = 33;
constexpr int svc_cutscene         = 34;

//=============================================================================
// Client to Server Protocol Messages (clc_*)
//=============================================================================

constexpr int clc_bad        = 0;
constexpr int clc_nop        = 1;
constexpr int clc_disconnect = 2;
constexpr int clc_move       = 3;
constexpr int clc_stringcmd  = 4;

//=============================================================================
// Temp Entity Types (TE_*)
//=============================================================================

constexpr int TE_SPIKE         = 0;
constexpr int TE_SUPERSPIKE    = 1;
constexpr int TE_GUNSHOT       = 2;
constexpr int TE_EXPLOSION     = 3;
constexpr int TE_TAREXPLOSION  = 4;
constexpr int TE_LIGHTNING1    = 5;
constexpr int TE_LIGHTNING2    = 6;
constexpr int TE_WIZSPIKE      = 7;
constexpr int TE_KNIGHTSPIKE   = 8;
constexpr int TE_LIGHTNING3    = 9;
constexpr int TE_LAVASPLASH    = 10;
constexpr int TE_TELEPORT      = 11;
constexpr int TE_EXPLOSION2    = 12;
constexpr int TE_BEAM          = 13;

//=============================================================================
// Low-Level Network Packet and Driver Flags
//=============================================================================

#define NET_NAMELEN 64
#define NET_MAXMESSAGE 8192
#define NET_HEADERSIZE (2 * sizeof(unsigned int))
#define NET_DATAGRAMSIZE (MAX_DATAGRAM + NET_HEADERSIZE)

#define NETFLAG_LENGTH_MASK 0x0000ffff
#define NETFLAG_DATA        0x00010000
#define NETFLAG_ACK         0x00020000
#define NETFLAG_NAK         0x00040000
#define NETFLAG_EOM         0x00080000
#define NETFLAG_UNRELIABLE  0x00100000
#define NETFLAG_CTL         0x80000000

#define NET_PROTOCOL_VERSION 3

// Control Commands: Client Requests & Server Replies
#define CCREQ_CONNECT     0x01
#define CCREQ_SERVER_INFO 0x02
#define CCREQ_PLAYER_INFO 0x03
#define CCREQ_RULE_INFO   0x04

#define CCREP_ACCEPT      0x81
#define CCREP_REJECT      0x82
#define CCREP_SERVER_INFO 0x83
#define CCREP_PLAYER_INFO 0x84
#define CCREP_RULE_INFO   0x85

// VCR Opcode Constants
constexpr int VCR_OP_CONNECT        = 1;
constexpr int VCR_OP_GETMESSAGE     = 2;
constexpr int VCR_OP_SENDMESSAGE    = 3;
constexpr int VCR_OP_CANSENDMESSAGE = 4;
constexpr int VCR_MAX_MESSAGE       = 4;

namespace Net {
static inline constexpr int IntAlign(int value) {
    return (value + (sizeof(int) - 1)) & (~(sizeof(int) - 1));
}
} // namespace Net
