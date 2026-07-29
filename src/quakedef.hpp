// quakedef.hpp -- primary header for client
#pragma once
#include <cstdint>

//#define	GLTEST			// experimental stuff

#define QUAKE_GAME // as opposed to utilities

constexpr double VERSION = 1.09;

//define	PARANOID			// speed sapping error checking

#define GAMENAME "id1"

#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string_view>

#if !defined(_WIN32)
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

typedef int errno_t;

#ifndef _TRUNCATE
#define _TRUNCATE ((size_t)-1)
#endif

#ifndef STRUNCATE
#define STRUNCATE 80
#endif

inline errno_t strcpy_s(char* dest, size_t destsz, const char* src) {
    if (!dest || destsz == 0) return EINVAL;
    if (!src) {
        dest[0] = '\0';
        return EINVAL;
    }
    size_t src_len = strlen(src);
    if (src_len >= destsz) {
        dest[0] = '\0';
        return ERANGE;
    }
    memcpy(dest, src, src_len + 1);
    return 0;
}

inline errno_t strncpy_s(char* dest, size_t destsz, const char* src, size_t count) {
    if (!dest || destsz == 0) return EINVAL;
    if (!src) {
        dest[0] = '\0';
        return EINVAL;
    }
    if (count == _TRUNCATE) {
        size_t src_len = strlen(src);
        if (src_len >= destsz) {
            memcpy(dest, src, destsz - 1);
            dest[destsz - 1] = '\0';
            return STRUNCATE;
        }
        memcpy(dest, src, src_len + 1);
        return 0;
    } else {
        size_t src_len = strlen(src);
        size_t copy_len = (src_len < count) ? src_len : count;
        if (copy_len >= destsz) {
            dest[0] = '\0';
            return ERANGE;
        }
        memcpy(dest, src, copy_len);
        dest[copy_len] = '\0';
        return 0;
    }
}

inline errno_t strcat_s(char* dest, size_t destsz, const char* src) {
    if (!dest || destsz == 0) return EINVAL;
    if (!src) return EINVAL;
    size_t dest_len = strlen(dest);
    if (dest_len >= destsz) {
        dest[0] = '\0';
        return EINVAL;
    }
    size_t src_len = strlen(src);
    if (dest_len + src_len >= destsz) {
        dest[0] = '\0';
        return ERANGE;
    }
    memcpy(dest + dest_len, src, src_len + 1);
    return 0;
}

inline int sprintf_s(char* dest, size_t destsz, const char* format, ...) {
    if (!dest || destsz == 0) return -1;
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(dest, destsz, format, args);
    va_end(args);
    if (ret < 0 || (size_t)ret >= destsz) {
        dest[0] = '\0';
        return -1;
    }
    return ret;
}

inline int vsprintf_s(char* dest, size_t destsz, const char* format, va_list args) {
    if (!dest || destsz == 0) return -1;
    int ret = vsnprintf(dest, destsz, format, args);
    if (ret < 0 || (size_t)ret >= destsz) {
        dest[0] = '\0';
        return -1;
    }
    return ret;
}

inline errno_t fopen_s(FILE** pFile, const char* filename, const char* mode) {
    if (!pFile || !filename || !mode) return EINVAL;
    *pFile = fopen(filename, mode);
    if (*pFile == nullptr) {
        return errno;
    }
    return 0;
}

inline errno_t strerror_s(char* buf, size_t bufsz, int errnum) {
    if (!buf || bufsz == 0) return EINVAL;
    const char* msg = strerror(errnum);
    if (!msg) {
        buf[0] = '\0';
        return EINVAL;
    }
    size_t msg_len = strlen(msg);
    if (msg_len >= bufsz) {
        memcpy(buf, msg, bufsz - 1);
        buf[bufsz - 1] = '\0';
        return ERANGE;
    }
    memcpy(buf, msg, msg_len + 1);
    return 0;
}

inline int vfscanf_s_compat(FILE* stream, const char* format, va_list ap) {
    void* args[16] = { nullptr };
    int arg_count = 0;

    const char* p = format;
    while (*p) {
        if (*p == '%') {
            p++;
            if (*p == '%') {
                p++;
                continue;
            }
            if (*p == '*') {
                p++;
            } else {
                while (*p && strchr("0123456789+-hljztL", *p)) {
                    p++;
                }
                char type = *p;
                if (type == '[') {
                    p++;
                    if (*p == '^') p++;
                    if (*p == ']') p++;
                    while (*p && *p != ']') {
                        p++;
                    }
                    type = '[';
                }
                if (*p) p++;
                
                void* ptr = va_arg(ap, void*);
                if (arg_count < 16) {
                    args[arg_count++] = ptr;
                }
                if (type == 's' || type == 'c' || type == '[') {
                    (void)va_arg(ap, unsigned int);
                }
                continue;
            }
        }
        p++;
    }

    switch (arg_count) {
        case 0:  return fscanf(stream, format);
        case 1:  return fscanf(stream, format, args[0]);
        case 2:  return fscanf(stream, format, args[0], args[1]);
        case 3:  return fscanf(stream, format, args[0], args[1], args[2]);
        case 4:  return fscanf(stream, format, args[0], args[1], args[2], args[3]);
        case 5:  return fscanf(stream, format, args[0], args[1], args[2], args[3], args[4]);
        case 6:  return fscanf(stream, format, args[0], args[1], args[2], args[3], args[4], args[5]);
        case 7:  return fscanf(stream, format, args[0], args[1], args[2], args[3], args[4], args[5], args[6]);
        case 8:  return fscanf(stream, format, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
        default: return -1;
    }
}

inline int vsscanf_s_compat(const char* buffer, const char* format, va_list ap) {
    void* args[16] = { nullptr };
    int arg_count = 0;

    const char* p = format;
    while (*p) {
        if (*p == '%') {
            p++;
            if (*p == '%') {
                p++;
                continue;
            }
            if (*p == '*') {
                p++;
            } else {
                while (*p && strchr("0123456789+-hljztL", *p)) {
                    p++;
                }
                char type = *p;
                if (type == '[') {
                    p++;
                    if (*p == '^') p++;
                    if (*p == ']') p++;
                    while (*p && *p != ']') {
                        p++;
                    }
                    type = '[';
                }
                if (*p) p++;
                
                void* ptr = va_arg(ap, void*);
                if (arg_count < 16) {
                    args[arg_count++] = ptr;
                }
                if (type == 's' || type == 'c' || type == '[') {
                    (void)va_arg(ap, unsigned int);
                }
                continue;
            }
        }
        p++;
    }

    switch (arg_count) {
        case 0:  return sscanf(buffer, format);
        case 1:  return sscanf(buffer, format, args[0]);
        case 2:  return sscanf(buffer, format, args[0], args[1]);
        case 3:  return sscanf(buffer, format, args[0], args[1], args[2]);
        case 4:  return sscanf(buffer, format, args[0], args[1], args[2], args[3]);
        case 5:  return sscanf(buffer, format, args[0], args[1], args[2], args[3], args[4]);
        case 6:  return sscanf(buffer, format, args[0], args[1], args[2], args[3], args[4], args[5]);
        case 7:  return sscanf(buffer, format, args[0], args[1], args[2], args[3], args[4], args[5], args[6]);
        case 8:  return sscanf(buffer, format, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
        default: return -1;
    }
}

inline int fscanf_s(FILE* stream, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vfscanf_s_compat(stream, format, ap);
    va_end(ap);
    return ret;
}

inline int sscanf_s(const char* buffer, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsscanf_s_compat(buffer, format, ap);
    va_end(ap);
    return ret;
}

constexpr int _SH_DENYNO = 0;
constexpr int _S_IREAD = 0400;
constexpr int _S_IWRITE = 0200;

inline int _sopen_s(int* pfd, const char* filename, int oflag, int shflag, int pmode) {
    (void)shflag;
    if (!pfd) return EINVAL;
    *pfd = open(filename, oflag, pmode);
    if (*pfd == -1) {
        return errno;
    }
    return 0;
}
#define _write write
#define _close close
#define _unlink unlink
#endif


inline void VID_LockBuffer(void) {}
inline void VID_UnlockBuffer(void) {}
constexpr int UNALIGNED_OK = 0;

// !!! if this is changed, it must be changed in d_ifacea.h too !!!
constexpr int CACHE_SIZE = 32; // used to align key data structures

#define UNUSED(x) (x = x) // for pesky compiler / lint warnings

constexpr int MINIMUM_MEMORY = 0x550000;
constexpr int MINIMUM_MEMORY_LEVELPAK = MINIMUM_MEMORY + 0x100000;

constexpr int MAX_NUM_ARGVS = 50;

// up / down
constexpr int PITCH = 0;

// left / right
constexpr int YAW = 1;

// fall over
constexpr int ROLL = 2;

constexpr int MAX_QPATH = 64;   // max length of a quake game pathname
constexpr int MAX_OSPATH = 128; // max length of a filesystem pathname

constexpr double ON_EPSILON = 0.1; // point on plane side epsilon

constexpr int MAX_MSGLEN = 8000;   // max length of a reliable message
constexpr int MAX_DATAGRAM = 1024; // max length of unreliable message

//
// per-level limits
//
constexpr int MAX_EDICTS = 600;
constexpr int MAX_LIGHTSTYLES = 64;
constexpr int MAX_MODELS = 256; // these are sent over the net as bytes
constexpr int MAX_SOUNDS = 256; // so they cannot be blindly increased

constexpr int SAVEGAME_COMMENT_LENGTH = 39;

constexpr int MAX_STYLESTRING = 64;

//
// stats are integers communicated to the client by the server
//
constexpr int MAX_CL_STATS = 32;
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

constexpr int MAX_SCOREBOARD = 16;
constexpr int MAX_SCOREBOARDNAME = 32;

constexpr int SOUND_CHANNELS = 8;

// This makes anyone on id's net privileged
// Use for multiplayer testing only - VERY dangerous!!!
// #define IDGODS

#include "sys_core.hpp"
#include "sys_render.hpp"
#include "sys_audio.hpp"
#include "sys_network.hpp"
#include "sys_vm.hpp"
#include "sys_server.hpp"
#include "sys_client.hpp"

