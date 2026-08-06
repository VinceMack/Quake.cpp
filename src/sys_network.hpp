// sys_network.hpp -- Subsystem Network Header
#pragma once

#ifdef _WIN32
#ifdef GetMessage
#undef GetMessage
#endif
#ifdef SendMessage
#undef SendMessage
#endif
#endif

#include <cstdint>
#include <EASTL/array.h>
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/functional.h>

#include "sys_core.hpp"

//=============================================================================
// Protocol Constants (from protocol.hpp)
//=============================================================================

constexpr int PROTOCOL_VERSION = 15;

constexpr int U_MOREBITS = (1 << 0);
constexpr int U_ORIGIN1 = (1 << 1);
constexpr int U_ORIGIN2 = (1 << 2);
constexpr int U_ORIGIN3 = (1 << 3);
constexpr int U_ANGLE2 = (1 << 4);
constexpr int U_NOLERP = (1 << 5);
constexpr int U_FRAME = (1 << 6);
constexpr int U_SIGNAL = (1 << 7);

constexpr int U_ANGLE1 = (1 << 8);
constexpr int U_ANGLE3 = (1 << 9);
constexpr int U_MODEL = (1 << 10);
constexpr int U_COLORMAP = (1 << 11);
constexpr int U_SKIN = (1 << 12);
constexpr int U_EFFECTS = (1 << 13);
constexpr int U_LONGENTITY = (1 << 14);

constexpr int SU_VIEWHEIGHT = (1 << 0);
constexpr int SU_IDEALPITCH = (1 << 1);
constexpr int SU_PUNCH1 = (1 << 2);
constexpr int SU_PUNCH2 = (1 << 3);
constexpr int SU_PUNCH3 = (1 << 4);
constexpr int SU_VELOCITY1 = (1 << 5);
constexpr int SU_VELOCITY2 = (1 << 6);
constexpr int SU_VELOCITY3 = (1 << 7);
constexpr int SU_ITEMS = (1 << 9);
constexpr int SU_ONGROUND = (1 << 10);
constexpr int SU_INWATER = (1 << 11);
constexpr int SU_WEAPONFRAME = (1 << 12);
constexpr int SU_ARMOR = (1 << 13);
constexpr int SU_WEAPON = (1 << 14);

constexpr int SND_VOLUME = (1 << 0);
constexpr int SND_ATTENUATION = (1 << 1);
constexpr int SND_LOOPING = (1 << 2);

constexpr int DEFAULT_VIEWHEIGHT = 22;
constexpr int GAME_COOP = 0;
constexpr int GAME_DEATHMATCH = 1;

constexpr int svc_bad = 0;
constexpr int svc_nop = 1;
constexpr int svc_disconnect = 2;
constexpr int svc_updatestat = 3;
constexpr int svc_version = 4;
constexpr int svc_setview = 5;
constexpr int svc_sound = 6;
constexpr int svc_time = 7;
constexpr int svc_print = 8;
constexpr int svc_stufftext = 9;
constexpr int svc_setangle = 10;

constexpr int svc_serverinfo = 11;
constexpr int svc_lightstyle = 12;
constexpr int svc_updatename = 13;
constexpr int svc_updatefrags = 14;
constexpr int svc_clientdata = 15;
constexpr int svc_stopsound = 16;
constexpr int svc_updatecolors = 17;
constexpr int svc_particle = 18;
constexpr int svc_damage = 19;
constexpr int svc_spawnstatic = 20;
constexpr int svc_spawnbaseline = 22;
constexpr int svc_temp_entity = 23;
constexpr int svc_setpause = 24;
constexpr int svc_signonnum = 25;
constexpr int svc_centerprint = 26;
constexpr int svc_killedmonster = 27;
constexpr int svc_foundsecret = 28;
constexpr int svc_spawnstaticsound = 29;
constexpr int svc_intermission = 30;
constexpr int svc_finale = 31;
constexpr int svc_cdtrack = 32;
constexpr int svc_sellscreen = 33;
constexpr int svc_cutscene = 34;

constexpr int clc_bad = 0;
constexpr int clc_nop = 1;
constexpr int clc_disconnect = 2;
constexpr int clc_move = 3;
constexpr int clc_stringcmd = 4;

constexpr int TE_SPIKE = 0;
constexpr int TE_SUPERSPIKE = 1;
constexpr int TE_GUNSHOT = 2;
constexpr int TE_EXPLOSION = 3;
constexpr int TE_TAREXPLOSION = 4;
constexpr int TE_LIGHTNING1 = 5;
constexpr int TE_LIGHTNING2 = 6;
constexpr int TE_WIZSPIKE = 7;
constexpr int TE_KNIGHTSPIKE = 8;
constexpr int TE_LIGHTNING3 = 9;
constexpr int TE_LAVASPLASH = 10;
constexpr int TE_TELEPORT = 11;
constexpr int TE_EXPLOSION2 = 12;
constexpr int TE_BEAM = 13;

//=============================================================================
// Network Sockets & Data Structures (from network.hpp)
//=============================================================================

struct qsockaddr {
    short sa_family;
    unsigned char sa_data[14];
};

#define NET_NAMELEN 64
#define NET_MAXMESSAGE 8192
#define NET_HEADERSIZE (2 * sizeof(unsigned int))
#define NET_DATAGRAMSIZE (MAX_DATAGRAM + NET_HEADERSIZE)

#define NETFLAG_LENGTH_MASK 0x0000ffff
#define NETFLAG_DATA 0x00010000
#define NETFLAG_ACK 0x00020000
#define NETFLAG_NAK 0x00040000
#define NETFLAG_EOM 0x00080000
#define NETFLAG_UNRELIABLE 0x00100000
#define NETFLAG_CTL 0x80000000

#define NET_PROTOCOL_VERSION 3

#define CCREQ_CONNECT 0x01
#define CCREQ_SERVER_INFO 0x02
#define CCREQ_PLAYER_INFO 0x03
#define CCREQ_RULE_INFO 0x04

#define CCREP_ACCEPT 0x81
#define CCREP_REJECT 0x82
#define CCREP_SERVER_INFO 0x83
#define CCREP_PLAYER_INFO 0x84
#define CCREP_RULE_INFO 0x85

struct qsocket_s {
    struct qsocket_s* next = nullptr;
    double connecttime = 0.0;
    double lastMessageTime = 0.0;
    double lastSendTime = 0.0;

    qboolean disconnected = true;
    qboolean canSend = true;
    qboolean sendNext = false;

    int driver = 0;
    int landriver = 0;
    int socket = 0;
    void* driverdata = nullptr;

    unsigned int ackSequence = 0;
    unsigned int sendSequence = 0;
    unsigned int unreliableSendSequence = 0;
    int sendMessageLength = 0;
    eastl::array<byte, NET_MAXMESSAGE> sendMessage{};

    unsigned int receiveSequence = 0;
    unsigned int unreliableReceiveSequence = 0;
    int receiveMessageLength = 0;
    eastl::array<byte, NET_MAXMESSAGE> receiveMessage{};

    struct qsockaddr addr{};
    char address[NET_NAMELEN]{};
};
using qsocket_t = struct qsocket_s;

namespace Net {

extern void (*GetComPortConfig)(int portNumber, int* port, int* irq, int* baud, qboolean* useModem);
extern void (*SetComPortConfig)(int portNumber, int port, int irq, int baud, qboolean useModem);
extern void (*GetModemConfig)(int portNumber, char* dialType, char* clear, char* init, char* hangup);
extern void (*SetModemConfig)(int portNumber, const char* dialType, const char* clear, const char* init, const char* hangup);

class NetDriver {
public:
    virtual ~NetDriver() = default;
    virtual const char* GetName() const = 0;
    virtual qboolean IsInitialized() const { return initialized; }
    virtual void SetInitialized(qboolean state) { initialized = state; }
    virtual int GetControlSocket() const { return controlSock; }
    virtual void SetControlSocket(int sock) { controlSock = sock; }

    virtual int Init() { return 0; }
    virtual void Listen(qboolean) {}
    virtual void SearchForHosts(qboolean) {}
    virtual qsocket_t* Connect(const char*) { return nullptr; }
    virtual qsocket_t* CheckNewConnections() { return nullptr; }
    virtual int GetMessage(qsocket_t*) { return 0; }
    virtual int SendMessage(qsocket_t*, sizebuf_t*) { return 0; }
    virtual int SendUnreliableMessage(qsocket_t*, sizebuf_t*) { return 0; }
    virtual qboolean CanSendMessage(qsocket_t*) { return false; }
    virtual qboolean CanSendUnreliableMessage() { return true; }
    virtual void Close(qsocket_t*) {}
    virtual void Shutdown() {}

protected:
    qboolean initialized = false;
    int controlSock = 0;
};

class NetLanDriver {
public:
    virtual ~NetLanDriver() = default;
    virtual const char* GetName() const = 0;
    virtual qboolean IsInitialized() const { return initialized; }
    virtual void SetInitialized(qboolean state) { initialized = state; }
    virtual int GetControlSocket() const { return controlSock; }
    virtual void SetControlSocket(int sock) { controlSock = sock; }

    virtual int Init() { return 0; }
    virtual void Shutdown() {}
    virtual void Listen(qboolean) {}
    virtual int OpenSocket(int) { return -1; }
    virtual int CloseSocket(int) { return -1; }
    virtual int Connect(int, struct qsockaddr*) { return 0; }
    virtual int CheckNewConnections() { return -1; }
    virtual int Read(int, byte*, int, struct qsockaddr*) { return 0; }
    virtual int Write(int, byte*, int, struct qsockaddr*) { return 0; }
    virtual int Broadcast(int, byte*, int) { return 0; }
    virtual char* AddrToString(struct qsockaddr*) { return nullptr; }
    virtual int StringToAddr(const char*, struct qsockaddr*) { return -1; }
    virtual int GetSocketAddr(int, struct qsockaddr*) { return -1; }
    virtual int GetNameFromAddr(struct qsockaddr*, char*) { return -1; }
    virtual int GetAddrFromName(const char*, struct qsockaddr*) { return -1; }
    virtual int AddrCompare(struct qsockaddr*, struct qsockaddr*) { return -1; }
    virtual int GetSocketPort(struct qsockaddr*) { return 0; }
    virtual int SetSocketPort(struct qsockaddr*, int) { return 0; }

protected:
    qboolean initialized = false;
    int controlSock = 0;
};

#define MAX_NET_DRIVERS 8
#define HOSTCACHESIZE 8

struct hostcache_t {
    char name[16];
    char map[16];
    char cname[32];
    int users;
    int maxusers;
    int driver;
    int ldriver;
    struct qsockaddr addr;
};

struct PollProcedure {
    PollProcedure* next = nullptr;
    double nextTime = 0.0;
    eastl::function<void()> procedure;
};

extern qsocket_t* net_activeSockets;
extern qsocket_t* net_freeSockets;
extern int net_numsockets;

extern int net_numlandrivers;
extern eastl::vector<eastl::unique_ptr<NetLanDriver>> net_landrivers;

extern int net_numdrivers;
extern eastl::vector<eastl::unique_ptr<NetDriver>> net_drivers;

extern int DEFAULTnet_hostport;
extern int net_hostport;

extern int net_driverlevel;
extern cvar_t hostname;
extern char playername[];
extern int playercolor;

extern int messagesSent;
extern int messagesReceived;
extern int unreliableMessagesSent;
extern int unreliableMessagesReceived;

qsocket_t* NET_NewQSocket(void);
void NET_FreeQSocket(qsocket_t*);
double SetNetTime(void);

extern int hostCacheCount;
extern eastl::array<hostcache_t, HOSTCACHESIZE> hostcache;

extern double net_time;
extern sizebuf_t net_message;
extern int net_activeconnections;

void NET_Init(void);
void NET_Shutdown(void);

struct qsocket_s* NET_CheckNewConnections(void);
struct qsocket_s* NET_Connect(const char* host);
qboolean NET_CanSendMessage(qsocket_t* sock);
int NET_GetMessage(struct qsocket_s* sock);
int NET_SendMessage(struct qsocket_s* sock, sizebuf_t* data);
int NET_SendUnreliableMessage(struct qsocket_s* sock, sizebuf_t* data);
int NET_SendToAll(sizebuf_t* data, int blocktime);
void NET_Close(struct qsocket_s* sock);
void NET_Poll(void);
void SchedulePollProcedure(PollProcedure* pp, double timeOffset);

extern qboolean serialAvailable;
extern qboolean ipxAvailable;
extern qboolean tcpipAvailable;
extern char my_ipx_address[NET_NAMELEN];
extern char my_tcpip_address[NET_NAMELEN];

extern qboolean slistInProgress;
extern qboolean slistSilent;
extern qboolean slistLocal;

void NET_Slist_f(void);

constexpr int VCR_OP_CONNECT = 1;
constexpr int VCR_OP_GETMESSAGE = 2;
constexpr int VCR_OP_SENDMESSAGE = 3;
constexpr int VCR_OP_CANSENDMESSAGE = 4;
constexpr int VCR_MAX_MESSAGE = 4;

} // namespace Net

