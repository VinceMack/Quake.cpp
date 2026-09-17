// socket.hpp -- Network Sockets, Base Drivers, and State Declarations
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

#include "core/types.hpp"
#include "core/msg.hpp"
#include "core/cvar.hpp"
#include "network/protocol.hpp"

//=============================================================================
// Network Socket Data Structures
//=============================================================================

struct qsockaddr {
    short sa_family;
    unsigned char sa_data[14];
};

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

//=============================================================================
// Driver Interfaces
//=============================================================================

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

//=============================================================================
// Host Cache and Polling Support
//=============================================================================

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

//=============================================================================
// Global Network Subsystem State Declarations
//=============================================================================

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
extern cvar_t net_messagetimeout;

extern int messagesSent;
extern int messagesReceived;
extern int unreliableMessagesSent;
extern int unreliableMessagesReceived;
extern int packetsSent;
extern int packetsReSent;
extern int packetsReceived;
extern int receivedDuplicateCount;
extern int shortPacketCount;
extern int droppedDatagrams;

extern int hostCacheCount;
extern eastl::array<hostcache_t, HOSTCACHESIZE> hostcache;

extern double net_time;
extern sizebuf_t net_message;
extern int net_activeconnections;

extern qboolean tcpipAvailable;
extern char my_tcpip_address[NET_NAMELEN];

extern qboolean slistInProgress;
extern qboolean slistSilent;
extern qboolean slistLocal;

// Driver accessor helpers
inline NetLanDriver& LANFunc(int level) { return *net_landrivers[level]; }
inline NetDriver& DriverFunc(int level) { return *net_drivers[level]; }

// Socket lifecycle and timer prototypes
qsocket_t* NET_NewQSocket();
void NET_FreeQSocket(qsocket_t*);
double SetNetTime();
void SchedulePollProcedure(PollProcedure* pp, double timeOffset);

} // namespace Net
