// quakedef.hpp -- primary header for client
#pragma once
#include <cstdint>

constexpr double VERSION = 1.09;

#define GAMENAME "id1"

#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string_view>

#include "platform/crt_compat.hpp"

constexpr int CACHE_SIZE = 32; // used to align key data structures

constexpr int MAX_NUM_ARGVS = 50;

// up / down
constexpr int PITCH = 0;

// left / right
constexpr int YAW = 1;

// fall over
constexpr int ROLL = 2;

constexpr double ON_EPSILON = 0.1; // point on plane side epsilon

constexpr int MAX_MSGLEN = 8000;   // max length of a reliable message
constexpr int MAX_DATAGRAM = 1024; // max length of unreliable message

//
// per-level limits (MAX_EDICTS, MAX_LIGHTSTYLES, MAX_MODELS, MAX_SOUNDS, MAX_STYLESTRING defined in core/types.hpp)
//
constexpr int SAVEGAME_COMMENT_LENGTH = 39;

//
// stats are integers communicated to the client by the server (MAX_CL_STATS in core/types.hpp)
//
constexpr int STAT_HEALTH = 0;
constexpr int STAT_FRAGS = 1;
constexpr int STAT_WEAPON = 2;
constexpr int STAT_AMMO = 3;
constexpr int STAT_ARMOR = 4;
constexpr int STAT_WEAPONFRAME = 5;
constexpr int STAT_SHELLS = 6;
constexpr int STAT_NAILS = 7;
constexpr int STAT_ROCKETS = 8;
constexpr int STAT_CELLS = 9;
constexpr int STAT_ACTIVEWEAPON = 10;
constexpr int STAT_TOTALSECRETS = 11;
constexpr int STAT_TOTALMONSTERS = 12;
constexpr int STAT_SECRETS = 13;  // bumped on client side by svc_foundsecret
constexpr int STAT_MONSTERS = 14; // bumped by svc_killedmonster

// stock defines

constexpr uint32_t IT_SHOTGUN = 1;
constexpr uint32_t IT_SUPER_SHOTGUN = 2;
constexpr uint32_t IT_NAILGUN = 4;
constexpr uint32_t IT_SUPER_NAILGUN = 8;
constexpr uint32_t IT_GRENADE_LAUNCHER = 16;
constexpr uint32_t IT_ROCKET_LAUNCHER = 32;
constexpr uint32_t IT_LIGHTNING = 64;
constexpr uint32_t IT_SUPER_LIGHTNING = 128;
constexpr uint32_t IT_SHELLS = 256;
constexpr uint32_t IT_NAILS = 512;
constexpr uint32_t IT_ROCKETS = 1024;
constexpr uint32_t IT_CELLS = 2048;
constexpr uint32_t IT_AXE = 4096;
constexpr uint32_t IT_ARMOR1 = 8192;
constexpr uint32_t IT_ARMOR2 = 16384;
constexpr uint32_t IT_ARMOR3 = 32768;
constexpr uint32_t IT_SUPERHEALTH = 65536;
constexpr uint32_t IT_KEY1 = 131072;
constexpr uint32_t IT_KEY2 = 262144;
constexpr uint32_t IT_INVISIBILITY = 524288;
constexpr uint32_t IT_INVULNERABILITY = 1048576;
constexpr uint32_t IT_SUIT = 2097152;
constexpr uint32_t IT_QUAD = 4194304;
constexpr uint32_t IT_SIGIL1 = (1U << 28);
constexpr uint32_t IT_SIGIL2 = (1U << 29);
constexpr uint32_t IT_SIGIL3 = (1U << 30);
constexpr uint32_t IT_SIGIL4 = (1U << 31);

//===========================================
//rogue changed and added defines

constexpr uint32_t RIT_SHELLS = 128;
constexpr uint32_t RIT_NAILS = 256;
constexpr uint32_t RIT_ROCKETS = 512;
constexpr uint32_t RIT_CELLS = 1024;
constexpr uint32_t RIT_AXE = 2048;
constexpr uint32_t RIT_LAVA_NAILGUN = 4096;
constexpr uint32_t RIT_LAVA_SUPER_NAILGUN = 8192;
constexpr uint32_t RIT_MULTI_GRENADE = 16384;
constexpr uint32_t RIT_MULTI_ROCKET = 32768;
constexpr uint32_t RIT_PLASMA_GUN = 65536;
constexpr uint32_t RIT_ARMOR1 = 8388608;
constexpr uint32_t RIT_ARMOR2 = 16777216;
constexpr uint32_t RIT_ARMOR3 = 33554432;
constexpr uint32_t RIT_LAVA_NAILS = 67108864;
constexpr uint32_t RIT_PLASMA_AMMO = 134217728;
constexpr uint32_t RIT_MULTI_ROCKETS = 268435456;
constexpr uint32_t RIT_SHIELD = 536870912;
constexpr uint32_t RIT_ANTIGRAV = 1073741824;
constexpr uint32_t RIT_SUPERHEALTH = 2147483648U;

//MED 01/04/97 added hipnotic defines
//===========================================
//hipnotic added defines
constexpr uint32_t HIT_PROXIMITY_GUN_BIT = 16;
constexpr uint32_t HIT_MJOLNIR_BIT = 7;
constexpr uint32_t HIT_LASER_CANNON_BIT = 23;
constexpr uint32_t HIT_PROXIMITY_GUN = (1U << HIT_PROXIMITY_GUN_BIT);
constexpr uint32_t HIT_MJOLNIR = (1U << HIT_MJOLNIR_BIT);
constexpr uint32_t HIT_LASER_CANNON = (1U << HIT_LASER_CANNON_BIT);
constexpr uint32_t HIT_WETSUIT = (1U << (23 + 2));
constexpr uint32_t HIT_EMPATHY_SHIELDS = (1U << (23 + 3));

//===========================================
// MAX_SCOREBOARD and MAX_SCOREBOARDNAME defined in core/types.hpp

constexpr int SOUND_CHANNELS = 8;

#include "sys_core.hpp"
#include "sys_render.hpp"
#include "sys_audio.hpp"
#include "sys_network.hpp"
#include "sys_vm.hpp"
#include "sys_server.hpp"
#include "sys_client.hpp"

