// network.hpp -- quake's interface to the networking layer
#pragma once

#ifdef _WIN32
#ifdef GetMessage
#undef GetMessage
#endif
#ifdef SendMessage
#undef SendMessage
#endif
#endif

#include <EASTL/array.h>
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/functional.h>

struct qsockaddr {
    short sa_family;
    unsigned char sa_data[14];
};

#define NET_NAMELEN 64

#define NET_MAXMESSAGE 8192
#define NET_HEADERSIZE (2 * sizeof(unsigned int))
#define NET_DATAGRAMSIZE (MAX_DATAGRAM + NET_HEADERSIZE)

// NetHeader flags
#define NETFLAG_LENGTH_MASK 0x0000ffff
#define NETFLAG_DATA 0x00010000
#define NETFLAG_ACK 0x00020000
#define NETFLAG_NAK 0x00040000
#define NETFLAG_EOM 0x00080000
#define NETFLAG_UNRELIABLE 0x00100000
#define NETFLAG_CTL 0x80000000

#define NET_PROTOCOL_VERSION 3

// This is the network info/connection protocol.  It is used to find Quake
// servers, get info about them, and connect to them.  Once connected, the
// Quake game protocol (documented elsewhere) is used.
//
// CCREQ_CONNECT
//		string	game_name				"QUAKE"
//		byte	net_protocol_version	NET_PROTOCOL_VERSION
//
// CCREQ_SERVER_INFO
//		string	game_name				"QUAKE"
//		byte	net_protocol_version	NET_PROTOCOL_VERSION
//
// CCREQ_PLAYER_INFO
//		byte	player_number
//
// CCREQ_RULE_INFO
//		string	rule
//
// CCREP_ACCEPT
//		long	port
//
// CCREP_REJECT
//		string	reason
//
// CCREP_SERVER_INFO
//		string	server_address
//		string	host_name
//		string	level_name
//		byte	current_players
//		byte	max_players
//		byte	protocol_version	NET_PROTOCOL_VERSION
//
// CCREP_PLAYER_INFO
//		byte	player_number
//		string	name
//		long	colors
//		long	frags
//		long	connect_time
//		string	address
//
// CCREP_RULE_INFO
//		string	rule
//		string	value

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

// ============================================================================
// Polymorphic driver interfaces
// ============================================================================

namespace Net {

class NetDriver {
public:
    virtual ~NetDriver() = default;
    virtual const char* GetName() const = 0;
    virtual qboolean IsInitialized() const = 0;
    virtual void SetInitialized(qboolean state) = 0;
    virtual int GetControlSocket() const = 0;
    virtual void SetControlSocket(int sock) = 0;

    virtual int Init() = 0;
    virtual void Listen(qboolean state) = 0;
    virtual void SearchForHosts(qboolean xmit) = 0;
    virtual qsocket_t* Connect(const char* host) = 0;
    virtual qsocket_t* CheckNewConnections() = 0;
    virtual int GetMessage(qsocket_t* sock) = 0;
    virtual int SendMessage(qsocket_t* sock, sizebuf_t* data) = 0;
    virtual int SendUnreliableMessage(qsocket_t* sock, sizebuf_t* data) = 0;
    virtual qboolean CanSendMessage(qsocket_t* sock) = 0;
    virtual qboolean CanSendUnreliableMessage() = 0;
    virtual void Close(qsocket_t* sock) = 0;
    virtual void Shutdown() = 0;
};

class NetLanDriver {
public:
    virtual ~NetLanDriver() = default;
    virtual const char* GetName() const = 0;
    virtual qboolean IsInitialized() const = 0;
    virtual void SetInitialized(qboolean state) = 0;
    virtual int GetControlSocket() const = 0;
    virtual void SetControlSocket(int sock) = 0;

    virtual int Init() = 0;
    virtual void Shutdown() = 0;
    virtual void Listen(qboolean state) = 0;
    virtual int OpenSocket(int port) = 0;
    virtual int CloseSocket(int socket) = 0;
    virtual int Connect(int socket, struct qsockaddr* addr) = 0;
    virtual int CheckNewConnections() = 0;
    virtual int Read(int socket, byte* buf, int len, struct qsockaddr* addr) = 0;
    virtual int Write(int socket, byte* buf, int len, struct qsockaddr* addr) = 0;
    virtual int Broadcast(int socket, byte* buf, int len) = 0;
    virtual char* AddrToString(struct qsockaddr* addr) = 0;
    virtual int StringToAddr(const char* string, struct qsockaddr* addr) = 0;
    virtual int GetSocketAddr(int socket, struct qsockaddr* addr) = 0;
    virtual int GetNameFromAddr(struct qsockaddr* addr, char* name) = 0;
    virtual int GetAddrFromName(const char* name, struct qsockaddr* addr) = 0;
    virtual int AddrCompare(struct qsockaddr* addr1, struct qsockaddr* addr2) = 0;
    virtual int GetSocketPort(struct qsockaddr* addr) = 0;
    virtual int SetSocketPort(struct qsockaddr* addr, int port) = 0;
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

#if !defined(_WIN32) && !defined(__linux__) && !defined(__sun__)
#ifndef htonl
extern unsigned long htonl(unsigned long hostlong);
#endif
#ifndef htons
extern unsigned short htons(unsigned short hostshort);
#endif
#ifndef ntohl
extern unsigned long ntohl(unsigned long netlong);
#endif
#ifndef ntohs
extern unsigned short ntohs(unsigned short netshort);
#endif
#endif

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

//============================================================================
//
// public network functions
//
//============================================================================

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
extern void (*GetComPortConfig)(int portNumber,
    int* port,
    int* irq,
    int* baud,
    qboolean* useModem);
extern void (*SetComPortConfig)(int portNumber,
    int port,
    int irq,
    int baud,
    qboolean useModem);
extern void (*GetModemConfig)(int portNumber,
    char* dialType,
    char* clear,
    char* init,
    char* hangup);
extern void (*SetModemConfig)(int portNumber,
    const char* dialType,
    const char* clear,
    const char* init,
    const char* hangup);

extern qboolean slistInProgress;
extern qboolean slistSilent;
extern qboolean slistLocal;

void NET_Slist_f(void);

} // namespace Net
