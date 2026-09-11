// net_main.cpp -- Network Subsystem Lifecycle and Core API Implementation
#include "quakedef.hpp"
#include "network/net_main.hpp"
#include "network/loopback.hpp"
#include "network/udp_driver.hpp"
#include "network/datagram.hpp"

using namespace Common;
using namespace Console;
using namespace Cvar;
using namespace Cmd;
using namespace Server;
using namespace Client;

namespace Net {

eastl::vector<eastl::unique_ptr<NetDriver>> net_drivers;
int net_numdrivers = 0;
eastl::vector<eastl::unique_ptr<NetLanDriver>> net_landrivers;
int net_numlandrivers = 0;

qsocket_t* net_activeSockets = nullptr;
qsocket_t* net_freeSockets = nullptr;
int net_numsockets = 0;

qboolean serialAvailable = false;
qboolean ipxAvailable = false;
qboolean tcpipAvailable = false;

int net_hostport = 0;
int DEFAULTnet_hostport = 26000;

char my_ipx_address[NET_NAMELEN]{};
char my_tcpip_address[NET_NAMELEN]{};

sizebuf_t net_message;
int net_activeconnections = 0;
int messagesSent = 0, messagesReceived = 0;
int unreliableMessagesSent = 0, unreliableMessagesReceived = 0;
int packetsSent = 0, packetsReSent = 0, packetsReceived = 0;
int receivedDuplicateCount = 0, shortPacketCount = 0, droppedDatagrams = 0;

int hostCacheCount = 0;
eastl::array<hostcache_t, HOSTCACHESIZE> hostcache;
int net_driverlevel = 0;
double net_time = 0.0;

qboolean slistInProgress = false, slistSilent = false, slistLocal = true;
static qboolean listening = false;
static double slistStartTime = 0.0;
static int slistLastShown = 0;
static eastl::vector<eastl::unique_ptr<qsocket_t>> socket_pool;

cvar_t net_messagetimeout = { "net_messagetimeout", "300", {}, {}, {}, {} };
cvar_t hostname = { "hostname", "UNNAMED", {}, {}, {}, {} };

double SetNetTime() {
    net_time = Sys_FloatTime();
    return net_time;
}

qsocket_t* NET_NewQSocket() {
    if (!net_freeSockets || net_activeconnections >= svs.maxclients) return nullptr;

    qsocket_t* sock = net_freeSockets;
    net_freeSockets = sock->next;
    sock->next = net_activeSockets;
    net_activeSockets = sock;
    sock->disconnected = false;
    sock->connecttime = net_time;
    Q_strcpy(sock->address, "UNSET ADDRESS");
    sock->driver = net_driverlevel;
    sock->socket = 0;
    sock->driverdata = nullptr;
    sock->canSend = true;
    sock->sendNext = false;
    sock->lastMessageTime = net_time;
    sock->ackSequence = sock->sendSequence = sock->unreliableSendSequence = 0;
    sock->sendMessageLength = sock->receiveSequence = sock->unreliableReceiveSequence = sock->receiveMessageLength = 0;
    return sock;
}

void NET_FreeQSocket(qsocket_t* sock) {
    if (sock == net_activeSockets) {
        net_activeSockets = net_activeSockets->next;
    } else {
        qsocket_t* s = nullptr;
        for (s = net_activeSockets; s; s = s->next) {
            if (s->next == sock) {
                s->next = sock->next;
                break;
            }
        }
        if (!s) Sys_Error("NET_FreeQSocket: not active\n");
    }
    sock->next = net_freeSockets;
    net_freeSockets = sock;
    sock->disconnected = true;
}

static void NET_Listen_f() {
    if (Cmd::Argc() != 2) {
        Con_Printf("\"listen\" is \"%u\"\n", listening ? 1 : 0);
        return;
    }
    listening = Q_atoi(Cmd::Argv(1)) ? true : false;
    for (net_driverlevel = 0; net_driverlevel < net_numdrivers; net_driverlevel++) {
        if (DriverFunc(net_driverlevel).IsInitialized()) DriverFunc(net_driverlevel).Listen(listening);
    }
}

static void MaxPlayers_f() {
    if (Cmd::Argc() != 2) {
        Con_Printf("\"maxplayers\" is \"%u\"\n", svs.maxclients);
        return;
    }
    if (sv.active) {
        Con_Printf("maxplayers can not be changed while a server is running.\n");
        return;
    }
    int n = Q_atoi(Cmd::Argv(1));
    if (n < 1) n = 1;
    if (n > svs.maxclientslimit) {
        n = svs.maxclientslimit;
        Con_Printf("\"maxplayers\" set to \"%u\"\n", n);
    }
    if (n == 1 && listening) Cmd::BufferAddText("listen 0\n");
    if (n > 1 && !listening) Cmd::BufferAddText("listen 1\n");
    svs.maxclients = n;
    Cvar::Set("deathmatch", (n == 1) ? "0" : "1");
}

static void NET_Port_f() {
    if (Cmd::Argc() != 2) {
        Con_Printf("\"port\" is \"%u\"\n", net_hostport);
        return;
    }
    int n = Q_atoi(Cmd::Argv(1));
    if (n < 1 || n > 65534) {
        Con_Printf("Bad value, must be between 1 and 65534\n");
        return;
    }
    DEFAULTnet_hostport = net_hostport = n;
    if (listening) {
        Cmd::BufferAddText("listen 0\n");
        Cmd::BufferAddText("listen 1\n");
    }
}

static void PrintSlistHeader() {
    Con_Printf("Server          Map             Users\n--------------- --------------- -----\n");
    slistLastShown = 0;
}

static void PrintSlist() {
    int n;
    for (n = slistLastShown; n < hostCacheCount; n++) {
        if (hostcache[n].maxusers) {
            Con_Printf("%-15.15s %-15.15s %2u/%2u\n", hostcache[n].name, hostcache[n].map, hostcache[n].users, hostcache[n].maxusers);
        } else {
            Con_Printf("%-15.15s %-15.15s\n", hostcache[n].name, hostcache[n].map);
        }
    }
    slistLastShown = n;
}

static void PrintSlistTrailer() {
    Con_Printf(hostCacheCount ? "== end list ==\n\n" : "No Quake servers found.\n\n");
}

static void Slist_Send();
static void Slist_Poll();
static PollProcedure slistSendProcedure = { nullptr, 0.0, Slist_Send };
static PollProcedure slistPollProcedure = { nullptr, 0.0, Slist_Poll };

void NET_Slist_f() {
    if (slistInProgress) return;
    if (!slistSilent) {
        Con_Printf("Looking for Quake servers...\n");
        PrintSlistHeader();
    }
    slistInProgress = true;
    slistStartTime = Sys_FloatTime();
    SchedulePollProcedure(&slistSendProcedure, 0.0);
    SchedulePollProcedure(&slistPollProcedure, 0.1);
    hostCacheCount = 0;
}

static void Slist_Send() {
    for (net_driverlevel = 0; net_driverlevel < net_numdrivers; net_driverlevel++) {
        if (!slistLocal && net_driverlevel == 0) continue;
        if (DriverFunc(net_driverlevel).IsInitialized()) DriverFunc(net_driverlevel).SearchForHosts(true);
    }
    if ((Sys_FloatTime() - slistStartTime) < 0.5) SchedulePollProcedure(&slistSendProcedure, 0.75);
}

static void Slist_Poll() {
    for (net_driverlevel = 0; net_driverlevel < net_numdrivers; net_driverlevel++) {
        if (!slistLocal && net_driverlevel == 0) continue;
        if (DriverFunc(net_driverlevel).IsInitialized()) DriverFunc(net_driverlevel).SearchForHosts(false);
    }
    if (!slistSilent) PrintSlist();
    if ((Sys_FloatTime() - slistStartTime) < 1.5) {
        SchedulePollProcedure(&slistPollProcedure, 0.1);
        return;
    }
    if (!slistSilent) PrintSlistTrailer();
    slistInProgress = slistSilent = false;
    slistLocal = true;
}

qsocket_t* NET_Connect(const char* host) {
    SetNetTime();
    if (host && *host == 0) host = nullptr;
    int numdrivers = net_numdrivers;

    if (host) {
        if (Q_strcasecmp(host, "local") == 0) {
            numdrivers = 1;
            goto JustDoIt;
        }
        if (hostCacheCount) {
            for (int n = 0; n < hostCacheCount; n++) {
                if (Q_strcasecmp(host, hostcache[n].name) == 0) {
                    host = hostcache[n].cname;
                    break;
                }
            }
        }
    }

    slistSilent = host ? true : false;
    NET_Slist_f();
    while (slistInProgress) NET_Poll();

    if (!host) {
        if (hostCacheCount != 1) return nullptr;
        host = hostcache[0].cname;
        Con_Printf("Connecting to...\n%s @ %s\n\n", hostcache[0].name, host);
    }
    if (hostCacheCount) {
        for (int n = 0; n < hostCacheCount; n++) {
            if (Q_strcasecmp(host, hostcache[n].name) == 0) {
                host = hostcache[n].cname;
                break;
            }
        }
    }

JustDoIt:
    for (net_driverlevel = 0; net_driverlevel < numdrivers; net_driverlevel++) {
        if (!DriverFunc(net_driverlevel).IsInitialized()) continue;
        qsocket_t* ret = DriverFunc(net_driverlevel).Connect(host);
        if (ret) return ret;
    }

    if (host) {
        Con_Printf("\n");
        PrintSlistHeader();
        PrintSlist();
        PrintSlistTrailer();
    }
    return nullptr;
}

qsocket_t* NET_CheckNewConnections() {
    SetNetTime();
    for (net_driverlevel = 0; net_driverlevel < net_numdrivers; net_driverlevel++) {
        if (!DriverFunc(net_driverlevel).IsInitialized() || (net_driverlevel && !listening)) continue;
        qsocket_t* ret = DriverFunc(net_driverlevel).CheckNewConnections();
        if (ret) return ret;
    }
    return nullptr;
}

void NET_Close(qsocket_t* sock) {
    if (!sock || sock->disconnected) return;
    SetNetTime();
    DriverFunc(sock->driver).Close(sock);
    NET_FreeQSocket(sock);
}

int NET_GetMessage(qsocket_t* sock) {
    if (!sock) return -1;
    if (sock->disconnected) {
        Con_Printf("NET_GetMessage: disconnected socket\n");
        return -1;
    }
    SetNetTime();
    int ret = DriverFunc(sock->driver).GetMessage(sock);
    if (ret == 0 && sock->driver && (net_time - sock->lastMessageTime > net_messagetimeout.value)) {
        NET_Close(sock);
        return -1;
    }
    if (ret > 0 && sock->driver) {
        sock->lastMessageTime = net_time;
        if (ret == 1) messagesReceived++;
        else if (ret == 2) unreliableMessagesReceived++;
    }
    return ret;
}

int NET_SendMessage(qsocket_t* sock, sizebuf_t* data) {
    if (!sock) return -1;
    if (sock->disconnected) {
        Con_Printf("NET_SendMessage: disconnected socket\n");
        return -1;
    }
    SetNetTime();
    int r = DriverFunc(sock->driver).SendMessage(sock, data);
    if (r == 1 && sock->driver) messagesSent++;
    return r;
}

int NET_SendUnreliableMessage(qsocket_t* sock, sizebuf_t* data) {
    if (!sock) return -1;
    if (sock->disconnected) {
        Con_Printf("NET_SendMessage: disconnected socket\n");
        return -1;
    }
    SetNetTime();
    int r = DriverFunc(sock->driver).SendUnreliableMessage(sock, data);
    if (r == 1 && sock->driver) unreliableMessagesSent++;
    return r;
}

qboolean NET_CanSendMessage(qsocket_t* sock) {
    if (!sock || sock->disconnected) return false;
    SetNetTime();
    int r = DriverFunc(sock->driver).CanSendMessage(sock);
    return r;
}

int NET_SendToAll(sizebuf_t* data, int blocktime) {
    qboolean state1[MAX_SCOREBOARD], state2[MAX_SCOREBOARD];
    int count = 0;

    for (int i = 0; i < svs.maxclients; i++) {
        client_t* client = &svs.clients[i];
        if (!client->netconnection) continue;
        if (client->active) {
            if (client->netconnection->driver == 0) {
                NET_SendMessage(client->netconnection, data);
                state1[i] = state2[i] = true;
                continue;
            }
            count++;
            state1[i] = state2[i] = false;
        } else {
            state1[i] = state2[i] = true;
        }
    }

    double start = Sys_FloatTime();
    while (count) {
        count = 0;
        for (int i = 0; i < svs.maxclients; i++) {
            client_t* client = &svs.clients[i];
            if (!state1[i]) {
                if (NET_CanSendMessage(client->netconnection)) {
                    state1[i] = true;
                    NET_SendMessage(client->netconnection, data);
                } else {
                    NET_GetMessage(client->netconnection);
                }
                count++;
                continue;
            }
            if (!state2[i]) {
                if (NET_CanSendMessage(client->netconnection)) {
                    state2[i] = true;
                } else {
                    NET_GetMessage(client->netconnection);
                }
                count++;
                continue;
            }
        }
        if ((Sys_FloatTime() - start) > blocktime) break;
    }
    return count;
}

void NET_Init() {
    net_drivers.clear();
    net_drivers.push_back(eastl::make_unique<LoopbackDriver>());
    net_drivers.push_back(eastl::make_unique<DatagramDriver>());
    net_numdrivers = 2;

    net_landrivers.clear();
    net_landrivers.push_back(eastl::make_unique<UDPDriver>());
    net_numlandrivers = 1;

    int i = COM_CheckParm("-port");
    if (!i) i = COM_CheckParm("-udpport");
    if (i) {
        if (i < com_argc - 1) DEFAULTnet_hostport = Q_atoi(com_argv[i + 1]);
        else Sys_Error("NET_Init: you must specify a number after -port");
    }
    net_hostport = DEFAULTnet_hostport;
    if (COM_CheckParm("-listen") || cls.state == ca_dedicated) listening = true;

    net_numsockets = svs.maxclientslimit;
    if (cls.state != ca_dedicated) net_numsockets++;
    SetNetTime();

    socket_pool.clear();
    socket_pool.reserve(net_numsockets);
    net_freeSockets = net_activeSockets = nullptr;
    for (i = 0; i < net_numsockets; i++) {
        socket_pool.push_back(eastl::make_unique<qsocket_t>());
        qsocket_t* s = socket_pool.back().get();
        s->next = net_freeSockets;
        net_freeSockets = s;
        s->disconnected = true;
    }

    SZ_Alloc(&net_message, NET_MAXMESSAGE);
    cvar_t* cvars[] = { &net_messagetimeout, &hostname };
    for (auto* c : cvars) Cvar::Register(c);

    Cmd::AddCommand("slist", NET_Slist_f);
    Cmd::AddCommand("listen", NET_Listen_f);
    Cmd::AddCommand("maxplayers", MaxPlayers_f);
    Cmd::AddCommand("port", NET_Port_f);

    for (net_driverlevel = 0; net_driverlevel < net_numdrivers; net_driverlevel++) {
        int controlSocket = DriverFunc(net_driverlevel).Init();
        if (controlSocket == -1) continue;
        DriverFunc(net_driverlevel).SetInitialized(true);
        DriverFunc(net_driverlevel).SetControlSocket(controlSocket);
        if (listening) DriverFunc(net_driverlevel).Listen(true);
    }
    if (*my_tcpip_address) Con_DPrintf("TCP/IP address %s\n", my_tcpip_address);
}

void NET_Shutdown() {
    SetNetTime();
    for (qsocket_t* sock = net_activeSockets; sock; sock = sock->next) NET_Close(sock);
    for (net_driverlevel = 0; net_driverlevel < net_numdrivers; net_driverlevel++) {
        if (DriverFunc(net_driverlevel).IsInitialized()) {
            DriverFunc(net_driverlevel).Shutdown();
            DriverFunc(net_driverlevel).SetInitialized(false);
        }
    }
}

static PollProcedure* pollProcedureList = nullptr;

void NET_Poll() {
    SetNetTime();
    while (pollProcedureList && pollProcedureList->nextTime <= net_time) {
        PollProcedure* pp = pollProcedureList;
        pollProcedureList = pp->next;
        pp->next = nullptr;
        pp->procedure();
    }
}

void SchedulePollProcedure(PollProcedure* pp, double timeOffset) {
    pp->nextTime = net_time + timeOffset;
    if (pollProcedureList == pp) pollProcedureList = pp->next;
    else if (pollProcedureList) {
        for (PollProcedure* p = pollProcedureList; p->next; p = p->next) {
            if (p->next == pp) {
                p->next = pp->next;
                break;
            }
        }
    }
    pp->next = nullptr;
    if (!pollProcedureList || pp->nextTime < pollProcedureList->nextTime) {
        pp->next = pollProcedureList;
        pollProcedureList = pp;
        return;
    }
    PollProcedure* p = pollProcedureList;
    while (p->next && p->next->nextTime <= pp->nextTime) p = p->next;
    pp->next = p->next;
    p->next = pp;
}

} // namespace Net
