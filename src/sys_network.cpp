// sys_network.cpp -- Subsystem Network Implementation
// Refactored for modern C++20 and EASTL with aggressive LOC reduction

#include "quakedef.hpp"
#include <stdint.h>

using namespace Client; using namespace Common; using namespace Console; using namespace Render;
using namespace Draw; using namespace Host; using namespace Input; using namespace Keys;
using namespace Math; using namespace Menu; using namespace Model; using namespace Net;
using namespace VM; using namespace Sbar; using namespace Screen; using namespace Server;
using namespace Audio; using namespace Vid; using namespace View; using namespace Wad;
using namespace Cvar; using namespace Cmd;

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef GetMessage
#undef GetMessage
#endif
#ifdef SendMessage
#undef SendMessage
#endif
#define ioctl ioctlsocket
#define close closesocket
#ifndef EWOULDBLOCK
#define EWOULDBLOCK WSAEWOULDBLOCK
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED WSAECONNREFUSED
#endif
#undef errno
#define errno WSAGetLastError()
typedef int socklen_t;
#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <sys/param.h>
#include <errno.h>
#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif
#endif

int vcrFile = -1;
qboolean recording = false;

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

int net_hostport;
int DEFAULTnet_hostport = 26000;

char my_ipx_address[NET_NAMELEN];
char my_tcpip_address[NET_NAMELEN];

void (*GetComPortConfig)(int, int*, int*, int*, qboolean*);
void (*SetComPortConfig)(int, int, int, int, qboolean);
void (*GetModemConfig)(int, char*, char*, char*, char*);
void (*SetModemConfig)(int, const char*, const char*, const char*, const char*);

sizebuf_t net_message;
int net_activeconnections = 0;
int messagesSent = 0, messagesReceived = 0;
int unreliableMessagesSent = 0, unreliableMessagesReceived = 0;
int packetsSent = 0, packetsReSent = 0, packetsReceived = 0;
int receivedDuplicateCount = 0, shortPacketCount = 0, droppedDatagrams = 0;

int hostCacheCount = 0;
eastl::array<hostcache_t, HOSTCACHESIZE> hostcache;
int net_driverlevel;
double net_time;

qboolean slistInProgress = false, slistSilent = false, slistLocal = true;
static qboolean listening = false;
static double slistStartTime;
static int slistLastShown;
static eastl::vector<eastl::unique_ptr<qsocket_t>> socket_pool;

cvar_t net_messagetimeout = { "net_messagetimeout", "300", {}, {}, {}, {} };
cvar_t hostname = { "hostname", "UNNAMED", {}, {}, {}, {} };

static qboolean configRestored = false;
static cvar_t config_com_port = { "_config_com_port", "0x3f8", true, {}, {}, {} };
static cvar_t config_com_irq = { "_config_com_irq", "4", true, {}, {}, {} };
static cvar_t config_com_baud = { "_config_com_baud", "57600", true, {}, {}, {} };
static cvar_t config_com_modem = { "_config_com_modem", "1", true, {}, {}, {} };
static cvar_t config_modem_dialtype = { "_config_modem_dialtype", "T", true, {}, {}, {} };
static cvar_t config_modem_clear = { "_config_modem_clear", "ATZ", true, {}, {}, {} };
static cvar_t config_modem_init = { "_config_modem_init", "", true, {}, {}, {} };
static cvar_t config_modem_hangup = { "_config_modem_hangup", "AT H", true, {}, {}, {} };

inline NetLanDriver& LANFunc(int level) { return *net_landrivers[level]; }
inline NetDriver& DriverFunc(int level) { return *net_drivers[level]; }

static inline constexpr int IntAlign(int value) { return (value + (sizeof(int) - 1)) & (~(sizeof(int) - 1)); }

static void WriteControlHeader(sizebuf_t* buf) {
    *((int*)buf->data) = BigLong(NETFLAG_CTL | (buf->cursize & NETFLAG_LENGTH_MASK));
}

static bool ReadControlHeader(int len, int& control) {
    if (len < static_cast<int>(sizeof(int))) return false;
    net_message.cursize = len;
    MSG_BeginReading();
    control = BigLong(*((int*)net_message.data));
    MSG_ReadLong();
    return (control != -1) &&
           ((static_cast<unsigned int>(control) & (~NETFLAG_LENGTH_MASK)) == NETFLAG_CTL) &&
           ((control & NETFLAG_LENGTH_MASK) == len);
}

// VCR logging helper
static void RecordVCR(int op, qsocket_t* sock, int r = 0, int len = -1) {
    if (!recording) return;
    struct { double time; int op; intptr_t session; int r; int len; } vcrRec;
    vcrRec.time = host_time; vcrRec.op = op; vcrRec.session = (intptr_t)sock; vcrRec.r = r; vcrRec.len = len;
    int size = (op == VCR_OP_GETMESSAGE) ? (len >= 0 ? 24 : 20) : (op == VCR_OP_CONNECT ? sizeof(vcrRec) : 20);
    Sys_FileWrite(vcrFile, &vcrRec, size);
    if (op == VCR_OP_CONNECT && sock) Sys_FileWrite(vcrFile, sock->address, NET_NAMELEN);
    else if (op == VCR_OP_GETMESSAGE && len >= 0) Sys_FileWrite(vcrFile, net_message.data, len);
}

// ============================================================================
// Loopback Driver Implementation
// ============================================================================
class LoopbackDriver : public NetDriver {
    qboolean localconnectpending = false;
    qsocket_t *loop_client = nullptr, *loop_server = nullptr;

public:
    const char* GetName() const override { return "Loopback"; }
    int Init() override { return (cls.state == ca_dedicated) ? -1 : 0; }

    void SearchForHosts(qboolean) override {
        if (!sv.active) return;
        hostCacheCount = 1;
        Q_strcpy(hostcache[0].name, (Q_strcmp(hostname.string.c_str(), "UNNAMED") == 0) ? "local" : hostname.string.c_str());
        Q_strcpy(hostcache[0].map, sv.name.data());
        hostcache[0].users = net_activeconnections;
        hostcache[0].maxusers = svs.maxclients;
        hostcache[0].driver = net_driverlevel;
        Q_strcpy(hostcache[0].cname, "local");
    }

    qsocket_t* Connect(const char* host) override {
        if (Q_strcmp(host, "local") != 0) return nullptr;
        localconnectpending = true;

        if (!loop_client && !(loop_client = NET_NewQSocket())) return nullptr;
        Q_strcpy(loop_client->address, "localhost");
        loop_client->receiveMessageLength = loop_client->sendMessageLength = 0;
        loop_client->canSend = true;

        if (!loop_server && !(loop_server = NET_NewQSocket())) return nullptr;
        Q_strcpy(loop_server->address, "LOCAL");
        loop_server->receiveMessageLength = loop_server->sendMessageLength = 0;
        loop_server->canSend = true;

        loop_client->driverdata = (void*)loop_server;
        loop_server->driverdata = (void*)loop_client;
        return loop_client;
    }

    qsocket_t* CheckNewConnections() override {
        if (!localconnectpending) return nullptr;
        localconnectpending = false;
        loop_server->sendMessageLength = loop_server->receiveMessageLength = 0;
        loop_server->canSend = loop_client->canSend = true;
        loop_client->sendMessageLength = loop_client->receiveMessageLength = 0;
        return loop_server;
    }

    int GetMessage(qsocket_t* sock) override {
        if (sock->receiveMessageLength == 0) return 0;
        int ret = sock->receiveMessage[0];
        int length = sock->receiveMessage[1] + (sock->receiveMessage[2] << 8);

        SZ_Clear(&net_message);
        SZ_Write(&net_message, &sock->receiveMessage[4], length);
        length = IntAlign(length + 4);
        sock->receiveMessageLength -= length;
        if (sock->receiveMessageLength) {
            Q_memcpy(sock->receiveMessage.data(), &sock->receiveMessage[length], sock->receiveMessageLength);
        }
        if (sock->driverdata && ret == 1) ((qsocket_t*)sock->driverdata)->canSend = true;
        return ret;
    }

    int SendMessage(qsocket_t* sock, sizebuf_t* data) override {
        if (!sock->driverdata) return -1;
        qsocket_t* peer = (qsocket_t*)sock->driverdata;
        if ((peer->receiveMessageLength + data->cursize + 4) > NET_MAXMESSAGE) Sys_Error("Loop_SendMessage: overflow\n");

        byte* buffer = peer->receiveMessage.data() + peer->receiveMessageLength;
        *buffer++ = 1; *buffer++ = static_cast<byte>(data->cursize & 0xff); *buffer++ = static_cast<byte>(data->cursize >> 8); buffer++;
        Q_memcpy(buffer, data->data, data->cursize);
        peer->receiveMessageLength = IntAlign(peer->receiveMessageLength + data->cursize + 4);
        sock->canSend = false;
        return 1;
    }

    int SendUnreliableMessage(qsocket_t* sock, sizebuf_t* data) override {
        if (!sock->driverdata) return -1;
        qsocket_t* peer = (qsocket_t*)sock->driverdata;
        if ((peer->receiveMessageLength + data->cursize + 3) > NET_MAXMESSAGE) return 0;

        byte* buffer = peer->receiveMessage.data() + peer->receiveMessageLength;
        *buffer++ = 2; *buffer++ = static_cast<byte>(data->cursize & 0xff); *buffer++ = static_cast<byte>(data->cursize >> 8); buffer++;
        Q_memcpy(buffer, data->data, data->cursize);
        peer->receiveMessageLength = IntAlign(peer->receiveMessageLength + data->cursize + 4);
        return 1;
    }

    qboolean CanSendMessage(qsocket_t* sock) override { return sock->driverdata ? sock->canSend : false; }

    void Close(qsocket_t* sock) override {
        if (sock->driverdata) ((qsocket_t*)sock->driverdata)->driverdata = nullptr;
        sock->receiveMessageLength = sock->sendMessageLength = 0;
        sock->canSend = true;
        if (sock == loop_client) loop_client = nullptr; else loop_server = nullptr;
    }
};

// ============================================================================
// UDP Driver Implementation
// ============================================================================
static int net_acceptsocket = -1, net_controlsocket, net_broadcastsocket = 0;
static struct qsockaddr broadcastaddr;
static unsigned long myAddr;

static int PartialIPAddress(const char* in, struct qsockaddr* hostaddr) {
    char buff[256]; buff[0] = '.';
    strcpy_s(buff + 1, sizeof(buff) - 1, in);
    char* b = (buff[1] == '.') ? buff + 1 : buff;
    int addr = 0, mask = -1, port = net_hostport;

    while (*b == '.') {
        b++; int num = 0, run = 0;
        while (*b >= '0' && *b <= '9') {
            num = num * 10 + (*b++ - '0');
            if (++run > 3) return -1;
        }
        if ((*b < '0' || *b > '9') && *b != '.' && *b != ':' && *b != 0) return -1;
        if (num > 255) return -1;
        mask <<= 8; addr = (addr << 8) + num;
    }
    if (*b++ == ':') port = Q_atoi(b);
    hostaddr->sa_family = AF_INET;
    ((struct sockaddr_in*)hostaddr)->sin_port = htons(static_cast<u_short>(port));
    ((struct sockaddr_in*)hostaddr)->sin_addr.s_addr = (myAddr & htonl(mask)) | htonl(addr);
    return 0;
}

class UDPDriver : public NetLanDriver {
public:
    const char* GetName() const override { return "UDP"; }

    int Init() override {
        char buff[MAXHOSTNAMELEN]; struct qsockaddr addr;
        if (COM_CheckParm("-noudp")) return -1;
#ifdef _WIN32
        WSADATA wsaData; if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return -1;
#endif
        gethostname(buff, MAXHOSTNAMELEN);
        struct addrinfo hints = {}, *result = nullptr;
        hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(buff, nullptr, &hints, &result) != 0 || !result) Sys_Error("UDP_Init: unable to resolve hostname");
        myAddr = ((struct sockaddr_in*)result->ai_addr)->sin_addr.s_addr;
        freeaddrinfo(result);

        if (Q_strcmp(hostname.string.c_str(), "UNNAMED") == 0) { buff[15] = 0; Cvar::Set("hostname", buff); }
        if ((net_controlsocket = OpenSocket(0)) == -1) Sys_Error("UDP_Init: Unable to open control socket\n");

        ((struct sockaddr_in*)&broadcastaddr)->sin_family = AF_INET;
        ((struct sockaddr_in*)&broadcastaddr)->sin_addr.s_addr = INADDR_BROADCAST;
        ((struct sockaddr_in*)&broadcastaddr)->sin_port = htons(static_cast<u_short>(net_hostport));

        GetSocketAddr(net_controlsocket, &addr);
        Q_strcpy(my_tcpip_address, AddrToString(&addr));
        char* colon = Q_strrchr(my_tcpip_address, ':'); if (colon) *colon = 0;
        Con_Printf("UDP Initialized\n");
        tcpipAvailable = true;
        return net_controlsocket;
    }

    void Shutdown() override { Listen(false); CloseSocket(net_controlsocket);
#ifdef _WIN32
        WSACleanup();
#endif
    }

    void Listen(qboolean state) override {
        if (state) {
            if (net_acceptsocket == -1 && (net_acceptsocket = OpenSocket(net_hostport)) == -1)
                Sys_Error("UDP_Listen: Unable to open accept socket\n");
        } else if (net_acceptsocket != -1) { CloseSocket(net_acceptsocket); net_acceptsocket = -1; }
    }

    int OpenSocket(int port) override {
        int newsocket = static_cast<int>(socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP));
        if (newsocket == -1) return -1;
        unsigned long _true = 1;
        if (ioctl(newsocket, FIONBIO, &_true) == -1) { close(newsocket); return -1; }
#ifdef _WIN32
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
        BOOL bNewBehavior = FALSE; DWORD dwBytesReturned = 0;
        WSAIoctl(newsocket, SIO_UDP_CONNRESET, &bNewBehavior, sizeof(bNewBehavior), NULL, 0, &dwBytesReturned, NULL, NULL);
#endif
        int opt = 1; setsockopt(newsocket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
        struct sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(static_cast<u_short>(port));
        if (bind(newsocket, (struct sockaddr*)&address, sizeof(address)) == -1) { close(newsocket); return -1; }
        return newsocket;
    }

    int CloseSocket(int socket) override { if (socket == net_broadcastsocket) net_broadcastsocket = 0; return close(socket); }

    int CheckNewConnections() override {
        if (net_acceptsocket == -1) return -1;
        unsigned long available;
        if (ioctl(net_acceptsocket, FIONREAD, &available) == -1) Sys_Error("UDP: ioctlsocket (FIONREAD) failed\n");
        return available ? net_acceptsocket : -1;
    }

    int Read(int socket, byte* buf, int len, struct qsockaddr* addr) override {
        socklen_t addrlen = sizeof(struct qsockaddr);
        int ret = recvfrom(socket, (char*)buf, len, 0, (struct sockaddr*)addr, &addrlen);
        if (ret == -1) {
            int err = errno;
#ifdef _WIN32
            if (err == WSAEWOULDBLOCK || err == WSAECONNRESET || err == WSAECONNREFUSED || err == WSAEMSGSIZE) return 0;
#endif
            if (err == EWOULDBLOCK || err == ECONNREFUSED) return 0;
            return -1;
        }
        return ret;
    }

    int Write(int socket, byte* buf, int len, struct qsockaddr* addr) override {
        int ret = sendto(socket, (const char*)buf, len, 0, (struct sockaddr*)addr, sizeof(struct qsockaddr));
        if (ret == -1) {
            int err = errno;
#ifdef _WIN32
            if (err == WSAEWOULDBLOCK || err == WSAECONNRESET || err == WSAENOBUFS) return 0;
#endif
            if (err == EWOULDBLOCK) return 0;
            return -1;
        }
        return ret;
    }

    int Broadcast(int socket, byte* buf, int len) override {
        if (socket != net_broadcastsocket) {
            int i = 1;
            if (setsockopt(socket, SOL_SOCKET, SO_BROADCAST, (char*)&i, sizeof(i)) < 0) return -1;
            net_broadcastsocket = socket;
        }
        return Write(socket, buf, len, &broadcastaddr);
    }

    char* AddrToString(struct qsockaddr* addr) override {
        static char buffer[22];
        int haddr = ntohl(((struct sockaddr_in*)addr)->sin_addr.s_addr);
        sprintf_s(buffer, sizeof(buffer), "%d.%d.%d.%d:%d", (haddr >> 24) & 0xff, (haddr >> 16) & 0xff, (haddr >> 8) & 0xff, haddr & 0xff, ntohs(((struct sockaddr_in*)addr)->sin_port));
        return buffer;
    }

    int StringToAddr(const char* string, struct qsockaddr* addr) override {
        int ha1, ha2, ha3, ha4, hp; sscanf_s(string, "%d.%d.%d.%d:%d", &ha1, &ha2, &ha3, &ha4, &hp);
        addr->sa_family = AF_INET;
        ((struct sockaddr_in*)addr)->sin_addr.s_addr = htonl((ha1 << 24) | (ha2 << 16) | (ha3 << 8) | ha4);
        ((struct sockaddr_in*)addr)->sin_port = htons(static_cast<u_short>(hp));
        return 0;
    }

    int GetSocketAddr(int socket, struct qsockaddr* addr) override {
        socklen_t addrlen = sizeof(struct qsockaddr); Q_memset(addr, 0, sizeof(struct qsockaddr));
        getsockname(socket, (struct sockaddr*)addr, &addrlen);
        unsigned int a = ((struct sockaddr_in*)addr)->sin_addr.s_addr; struct in_addr loopbackAddr;
        inet_pton(AF_INET, "127.0.0.1", &loopbackAddr);
        if (a == 0 || a == loopbackAddr.s_addr) ((struct sockaddr_in*)addr)->sin_addr.s_addr = myAddr;
        return 0;
    }

    int GetNameFromAddr(struct qsockaddr* addr, char* name) override {
        char hostname_buf[NI_MAXHOST];
        if (getnameinfo((const sockaddr*)addr, sizeof(struct qsockaddr), hostname_buf, NI_MAXHOST, nullptr, 0, NI_NAMEREQD) == 0) {
            Q_strncpy(name, hostname_buf, NET_NAMELEN - 1); return 0;
        }
        Q_strcpy(name, AddrToString(addr));
        return 0;
    }

    int GetAddrFromName(const char* name, struct qsockaddr* addr) override {
        if (name[0] >= '0' && name[0] <= '9') return PartialIPAddress(name, addr);
        struct addrinfo hints = {}, *result = nullptr;
        hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(name, nullptr, &hints, &result) != 0 || !result) return -1;
        addr->sa_family = AF_INET;
        ((struct sockaddr_in*)addr)->sin_port = htons(static_cast<u_short>(net_hostport));
        ((struct sockaddr_in*)addr)->sin_addr.s_addr = ((struct sockaddr_in*)result->ai_addr)->sin_addr.s_addr;
        freeaddrinfo(result);
        return 0;
    }

    int AddrCompare(struct qsockaddr* a1, struct qsockaddr* a2) override {
        if (a1->sa_family != a2->sa_family) return -1;
        auto* s1 = (struct sockaddr_in*)a1; auto* s2 = (struct sockaddr_in*)a2;
        return (s1->sin_addr.s_addr != s2->sin_addr.s_addr) ? -1 : (s1->sin_port != s2->sin_port ? 1 : 0);
    }

    int GetSocketPort(struct qsockaddr* addr) override { return ntohs(((struct sockaddr_in*)addr)->sin_port); }
    int SetSocketPort(struct qsockaddr* addr, int port) override { ((struct sockaddr_in*)addr)->sin_port = htons(static_cast<u_short>(port)); return 0; }
};

// ============================================================================
// Datagram Driver Implementation
// ============================================================================
static_assert(sizeof(int) == 4, "int size check");
static struct { unsigned int length; unsigned int sequence; byte data[MAX_DATAGRAM]; } packetBuffer;

static int myDriverLevel;
static int net_landriverlevel = 0;

static qboolean testInProgress = false, test2InProgress = false;
static int testPollCount, testDriver, testSocket, test2Driver, test2Socket;
static void Test_Poll(); static void Test2_Poll();
static PollProcedure testPollProcedure = { nullptr, 0.0, Test_Poll };
static PollProcedure test2PollProcedure = { nullptr, 0.0, Test2_Poll };

static int SendDatagramPacket(qsocket_t* sock, unsigned int sequence, bool isResend) {
    NetLanDriver& lan = LANFunc(sock->landriver);
    unsigned int dataLen = (sock->sendMessageLength <= MAX_DATAGRAM) ? sock->sendMessageLength : MAX_DATAGRAM;
    unsigned int eom = (sock->sendMessageLength <= MAX_DATAGRAM) ? NETFLAG_EOM : 0;
    unsigned int packetLen = NET_HEADERSIZE + dataLen;

    packetBuffer.length = BigLong(packetLen | (NETFLAG_DATA | eom));
    packetBuffer.sequence = BigLong(sequence);
    Q_memcpy(packetBuffer.data, sock->sendMessage.data(), dataLen);

    sock->sendNext = false;
    if (lan.Write(sock->socket, (byte*)&packetBuffer, packetLen, &sock->addr) == -1) return -1;
    sock->lastSendTime = net_time;
    if (isResend) packetsReSent++; else packetsSent++;
    return 1;
}

static void PrintStats(qsocket_t* s) {
    Con_Printf("canSend = %4u   \nsendSeq = %4u   recvSeq = %4u   \n\n", s->canSend, s->sendSequence, s->receiveSequence);
}

static void NET_Stats_f() {
    if (Cmd::Argc() == 1) {
        Con_Printf("unreliable messages sent   = %i\nunreliable messages recv   = %i\nreliable messages sent     = %i\nreliable messages received = %i\npacketsSent                = %i\npacketsReSent              = %i\npacketsReceived            = %i\nreceivedDuplicateCount     = %i\nshortPacketCount           = %i\ndroppedDatagrams           = %i\n",
            unreliableMessagesSent, unreliableMessagesReceived, messagesSent, messagesReceived, packetsSent, packetsReSent, packetsReceived, receivedDuplicateCount, shortPacketCount, droppedDatagrams);
    } else if (Q_strcmp(Cmd::Argv(1), "*") == 0) {
        for (qsocket_t* s = net_activeSockets; s; s = s->next) PrintStats(s);
        for (qsocket_t* s = net_freeSockets; s; s = s->next) PrintStats(s);
    } else {
        qsocket_t* s = nullptr;
        for (s = net_activeSockets; s; s = s->next) if (Q_strcasecmp(Cmd::Argv(1), s->address) == 0) break;
        if (!s) for (s = net_freeSockets; s; s = s->next) if (Q_strcasecmp(Cmd::Argv(1), s->address) == 0) break;
        if (s) PrintStats(s);
    }
}

static void Test_Poll() {
    struct qsockaddr clientaddr; int control; net_landriverlevel = testDriver;
    NetLanDriver& lan = LANFunc(net_landriverlevel);

    while (1) {
        int len = lan.Read(testSocket, net_message.data, net_message.maxsize, &clientaddr);
        if (!ReadControlHeader(len, control)) break;
        if (MSG_ReadByte() != CCREP_PLAYER_INFO) Sys_Error("Unexpected repsonse to Player Info request\n");

        MSG_ReadByte(); char name[32], address[64];
        Q_strcpy(name, MSG_ReadString());
        int colors = MSG_ReadLong(), frags = MSG_ReadLong(), connectTime = MSG_ReadLong();
        Q_strcpy(address, MSG_ReadString());
        Con_Printf("%s\n  frags:%3i  colors:%u %u  time:%u\n  %s\n", name, frags, colors >> 4, colors & 0x0f, connectTime / 60, address);
    }

    if (--testPollCount) SchedulePollProcedure(&testPollProcedure, 0.1);
    else { lan.CloseSocket(testSocket); testInProgress = false; }
}

static void Test_f() {
    if (testInProgress) return;
    eastl::string_view host = Cmd::Argv(1); int max = MAX_SCOREBOARD; struct qsockaddr sendaddr;

    if (!host.empty() && hostCacheCount) {
        for (int n = 0; n < hostCacheCount; n++) {
            if (Q_strcasecmp(host, hostcache[n].name) == 0) {
                if (hostcache[n].driver != myDriverLevel) continue;
                net_landriverlevel = hostcache[n].ldriver; max = hostcache[n].maxusers;
                Q_memcpy(&sendaddr, &hostcache[n].addr, sizeof(struct qsockaddr)); goto JustDoIt;
            }
        }
    }

    for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++) {
        if (!LANFunc(net_landriverlevel).IsInitialized()) continue;
        if (LANFunc(net_landriverlevel).GetAddrFromName(eastl::string(host.data(), host.length()).c_str(), &sendaddr) != -1) break;
    }
    if (net_landriverlevel == net_numlandrivers) return;

JustDoIt:
    if ((testSocket = LANFunc(net_landriverlevel).OpenSocket(0)) == -1) return;
    testInProgress = true; testPollCount = 20; testDriver = net_landriverlevel;

    for (int n = 0; n < max; n++) {
        SZ_Clear(&net_message); MSG_WriteLong(&net_message, 0); MSG_WriteByte(&net_message, CCREQ_PLAYER_INFO);
        MSG_WriteByte(&net_message, n); WriteControlHeader(&net_message);
        LANFunc(testDriver).Write(testSocket, net_message.data, net_message.cursize, &sendaddr);
    }
    SZ_Clear(&net_message); SchedulePollProcedure(&testPollProcedure, 0.1);
}

static void Test2_Poll() {
    struct qsockaddr clientaddr; int control; net_landriverlevel = test2Driver;
    NetLanDriver& lan = LANFunc(net_landriverlevel);

    int len = lan.Read(test2Socket, net_message.data, net_message.maxsize, &clientaddr);
    if (len < static_cast<int>(sizeof(int))) goto Reschedule;
    if (!ReadControlHeader(len, control) || MSG_ReadByte() != CCREP_RULE_INFO) goto Error;

    char name[256], value[256]; Q_strcpy(name, MSG_ReadString());
    if (name[0] == 0) goto Done;

    Q_strcpy(value, MSG_ReadString()); Con_Printf("%-16.16s  %-16.16s\n", name, value);
    SZ_Clear(&net_message); MSG_WriteLong(&net_message, 0); MSG_WriteByte(&net_message, CCREQ_RULE_INFO);
    MSG_WriteString(&net_message, name); WriteControlHeader(&net_message);
    lan.Write(test2Socket, net_message.data, net_message.cursize, &clientaddr); SZ_Clear(&net_message);

Reschedule:
    SchedulePollProcedure(&test2PollProcedure, 0.05); return;
Error:
    Con_Printf("Unexpected repsonse to Rule Info request\n");
Done:
    lan.CloseSocket(test2Socket); test2InProgress = false;
}

static void Test2_f() {
    if (test2InProgress) return;
    eastl::string_view host = Cmd::Argv(1); struct qsockaddr sendaddr;

    if (!host.empty() && hostCacheCount) {
        for (int n = 0; n < hostCacheCount; n++) {
            if (Q_strcasecmp(host, hostcache[n].name) == 0) {
                if (hostcache[n].driver != myDriverLevel) continue;
                net_landriverlevel = hostcache[n].ldriver;
                Q_memcpy(&sendaddr, &hostcache[n].addr, sizeof(struct qsockaddr)); goto JustDoIt;
            }
        }
    }

    for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++) {
        if (!LANFunc(net_landriverlevel).IsInitialized()) continue;
        if (LANFunc(net_landriverlevel).GetAddrFromName(eastl::string(host.data(), host.length()).c_str(), &sendaddr) != -1) break;
    }
    if (net_landriverlevel == net_numlandrivers) return;

JustDoIt:
    if ((test2Socket = LANFunc(net_landriverlevel).OpenSocket(0)) == -1) return;
    test2InProgress = true; test2Driver = net_landriverlevel;

    SZ_Clear(&net_message); MSG_WriteLong(&net_message, 0); MSG_WriteByte(&net_message, CCREQ_RULE_INFO);
    MSG_WriteString(&net_message, ""); WriteControlHeader(&net_message);
    LANFunc(test2Driver).Write(test2Socket, net_message.data, net_message.cursize, &sendaddr);
    SZ_Clear(&net_message); SchedulePollProcedure(&test2PollProcedure, 0.05);
}

class DatagramDriver : public NetDriver {
public:
    const char* GetName() const override { return "Datagram"; }

    int Init() override {
        myDriverLevel = net_driverlevel; Cmd::AddCommand("net_stats", NET_Stats_f);
        if (COM_CheckParm("-nolan")) return -1;
        for (int i = 0; i < net_numlandrivers; i++) {
            int csock = LANFunc(i).Init(); if (csock == -1) continue;
            LANFunc(i).SetInitialized(true); LANFunc(i).SetControlSocket(csock);
        }
        Cmd::AddCommand("test", Test_f); Cmd::AddCommand("test2", Test2_f);
        return 0;
    }

    void Shutdown() override {
        for (int i = 0; i < net_numlandrivers; i++) {
            if (LANFunc(i).IsInitialized()) { LANFunc(i).Shutdown(); LANFunc(i).SetInitialized(false); }
        }
    }

    void Listen(qboolean state) override {
        for (int i = 0; i < net_numlandrivers; i++) {
            if (LANFunc(i).IsInitialized()) LANFunc(i).Listen(state);
        }
    }

    void Close(qsocket_t* sock) override { LANFunc(sock->landriver).CloseSocket(sock->socket); }

    int SendMessage(qsocket_t* sock, sizebuf_t* data) override {
        Q_memcpy(sock->sendMessage.data(), data->data, data->cursize);
        sock->sendMessageLength = data->cursize; sock->canSend = false;
        return SendDatagramPacket(sock, sock->sendSequence++, false);
    }

    int SendUnreliableMessage(qsocket_t* sock, sizebuf_t* data) override {
        NetLanDriver& lan = LANFunc(sock->landriver);
        int packetLen = NET_HEADERSIZE + data->cursize;
        packetBuffer.length = BigLong(packetLen | NETFLAG_UNRELIABLE);
        packetBuffer.sequence = BigLong(sock->unreliableSendSequence++);
        Q_memcpy(packetBuffer.data, data->data, data->cursize);
        if (lan.Write(sock->socket, (byte*)&packetBuffer, packetLen, &sock->addr) == -1) return -1;
        packetsSent++; return 1;
    }

    qboolean CanSendMessage(qsocket_t* sock) override {
        if (sock->sendNext) SendDatagramPacket(sock, sock->sendSequence++, false);
        return sock->canSend;
    }

    int GetMessage(qsocket_t* sock) override {
        NetLanDriver& lan = LANFunc(sock->landriver);
        if (!sock->canSend && (net_time - sock->lastSendTime) > 1.0) SendDatagramPacket(sock, sock->sendSequence - 1, true);

        int ret = 0; struct qsockaddr readaddr;
        while (1) {
            unsigned int length = lan.Read(sock->socket, (byte*)&packetBuffer, NET_DATAGRAMSIZE, &readaddr);
            if (length == 0) break;
            if (static_cast<int>(length) == -1) return -1;
            if (lan.AddrCompare(&readaddr, &sock->addr) != 0) continue;
            if (length < NET_HEADERSIZE) { shortPacketCount++; continue; }

            length = BigLong(packetBuffer.length);
            unsigned int flags = length & (~NETFLAG_LENGTH_MASK); length &= NETFLAG_LENGTH_MASK;
            if (flags & NETFLAG_CTL) continue;

            unsigned int sequence = BigLong(packetBuffer.sequence); packetsReceived++;
            if (flags & NETFLAG_UNRELIABLE) {
                if (sequence < sock->unreliableReceiveSequence) break;
                if (sequence != sock->unreliableReceiveSequence) droppedDatagrams += (sequence - sock->unreliableReceiveSequence);
                sock->unreliableReceiveSequence = sequence + 1; length -= NET_HEADERSIZE;
                SZ_Clear(&net_message); SZ_Write(&net_message, packetBuffer.data, length);
                ret = 2; break;
            }

            if (flags & NETFLAG_ACK) {
                if (sequence != (sock->sendSequence - 1)) continue;
                if (sequence == sock->ackSequence) sock->ackSequence++; else continue;
                sock->sendMessageLength -= MAX_DATAGRAM;
                if (sock->sendMessageLength > 0) {
                    Q_memcpy(sock->sendMessage.data(), sock->sendMessage.data() + MAX_DATAGRAM, sock->sendMessageLength);
                    sock->sendNext = true;
                } else { sock->sendMessageLength = 0; sock->canSend = true; }
                continue;
            }

            if (flags & NETFLAG_DATA) {
                packetBuffer.length = BigLong(NET_HEADERSIZE | NETFLAG_ACK); packetBuffer.sequence = BigLong(sequence);
                lan.Write(sock->socket, (byte*)&packetBuffer, NET_HEADERSIZE, &readaddr);
                if (sequence != sock->receiveSequence) { receivedDuplicateCount++; continue; }
                sock->receiveSequence++; length -= NET_HEADERSIZE;
                if (flags & NETFLAG_EOM) {
                    SZ_Clear(&net_message); SZ_Write(&net_message, sock->receiveMessage.data(), sock->receiveMessageLength);
                    SZ_Write(&net_message, packetBuffer.data, length); sock->receiveMessageLength = 0;
                    ret = 1; break;
                }
                Q_memcpy(sock->receiveMessage.data() + sock->receiveMessageLength, packetBuffer.data, length);
                sock->receiveMessageLength += length; continue;
            }
        }
        if (sock->sendNext) SendDatagramPacket(sock, sock->sendSequence++, false);
        return ret;
    }

    qsocket_t* CheckNewConnections() override {
        qsocket_t* ret = nullptr;
        for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++) {
            if (!LANFunc(net_landriverlevel).IsInitialized()) continue;
            NetLanDriver& lan = LANFunc(net_landriverlevel);
            int acceptsock = lan.CheckNewConnections(); if (acceptsock == -1) continue;

            SZ_Clear(&net_message); struct qsockaddr clientaddr;
            int len = lan.Read(acceptsock, net_message.data, net_message.maxsize, &clientaddr); int control;
            if (!ReadControlHeader(len, control)) continue;

            auto SendReply = [&](byte repCmd, auto&& writePayload) {
                SZ_Clear(&net_message); MSG_WriteLong(&net_message, 0); MSG_WriteByte(&net_message, repCmd);
                writePayload(); WriteControlHeader(&net_message);
                lan.Write(acceptsock, net_message.data, net_message.cursize, &clientaddr); SZ_Clear(&net_message);
            };

            int command = MSG_ReadByte();
            if (command == CCREQ_SERVER_INFO) {
                if (Q_strcmp(MSG_ReadString(), "QUAKE") != 0) continue;
                struct qsockaddr newaddr; lan.GetSocketAddr(acceptsock, &newaddr);
                SendReply(CCREP_SERVER_INFO, [&]() {
                    MSG_WriteString(&net_message, lan.AddrToString(&newaddr));
                    MSG_WriteString(&net_message, hostname.string.c_str()); MSG_WriteString(&net_message, sv.name.data());
                    MSG_WriteByte(&net_message, net_activeconnections); MSG_WriteByte(&net_message, svs.maxclients);
                    MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
                });
                continue;
            }

            if (command == CCREQ_PLAYER_INFO) {
                int pNum = MSG_ReadByte(), activeNum = -1, cNum = 0; client_t* client = svs.clients;
                for (; cNum < svs.maxclients; cNum++, client++) if (client->active && ++activeNum == pNum) break;
                if (cNum == svs.maxclients) continue;
                SendReply(CCREP_PLAYER_INFO, [&]() {
                    MSG_WriteByte(&net_message, pNum); MSG_WriteString(&net_message, client->name.data());
                    MSG_WriteLong(&net_message, client->colors); MSG_WriteLong(&net_message, (int)client->edict->v.frags);
                    MSG_WriteLong(&net_message, (int)(net_time - client->netconnection->connecttime));
                    MSG_WriteString(&net_message, client->netconnection->address);
                });
                continue;
            }

            if (command == CCREQ_RULE_INFO) {
                char* pName = MSG_ReadString(); cvar_t* var = *pName ? Cvar::FindVar(pName) : Cvar::state.vars;
                if (*pName && var) var = var->next;
                while (var && !var->server) var = var->next;
                SendReply(CCREP_RULE_INFO, [&]() {
                    if (var) { MSG_WriteString(&net_message, var->name.c_str()); MSG_WriteString(&net_message, var->string.c_str()); }
                });
                continue;
            }

            if (command != CCREQ_CONNECT || Q_strcmp(MSG_ReadString(), "QUAKE") != 0) continue;

            if (MSG_ReadByte() != NET_PROTOCOL_VERSION) {
                SendReply(CCREP_REJECT, [&]() { MSG_WriteString(&net_message, "Incompatible version.\n"); });
                continue;
            }

            for (qsocket_t* s = net_activeSockets; s; s = s->next) {
                if (s->driver != net_driverlevel) continue;
                if (lan.AddrCompare(&clientaddr, &s->addr) == 0) {
                    if (net_time - s->connecttime < 2.0) {
                        SendReply(CCREP_ACCEPT, [&]() {
                            struct qsockaddr newaddr; lan.GetSocketAddr(s->socket, &newaddr);
                            MSG_WriteLong(&net_message, lan.GetSocketPort(&newaddr));
                        });
                        return nullptr;
                    }
                    NET_Close(s); break;
                }
            }

            qsocket_t* sock = NET_NewQSocket();
            if (!sock) {
                SendReply(CCREP_REJECT, [&]() { MSG_WriteString(&net_message, "Server is full.\n"); });
                continue;
            }

            int newsock = lan.OpenSocket(0);
            if (newsock == -1) { NET_FreeQSocket(sock); continue; }
            if (lan.Connect(newsock, &clientaddr) == -1) { lan.CloseSocket(newsock); NET_FreeQSocket(sock); continue; }

            sock->socket = newsock; sock->landriver = net_landriverlevel; sock->addr = clientaddr;
            Q_strcpy(sock->address, lan.AddrToString(&clientaddr));
            SendReply(CCREP_ACCEPT, [&]() {
                struct qsockaddr newaddr; lan.GetSocketAddr(newsock, &newaddr);
                MSG_WriteLong(&net_message, lan.GetSocketPort(&newaddr));
            });
            ret = sock; break;
        }
        return ret;
    }

    void SearchForHosts(qboolean xmit) override {
        for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++) {
            if (hostCacheCount == HOSTCACHESIZE) break;
            if (!LANFunc(net_landriverlevel).IsInitialized()) continue;
            NetLanDriver& lan = LANFunc(net_landriverlevel);

            struct qsockaddr readaddr, myaddr; lan.GetSocketAddr(lan.GetControlSocket(), &myaddr);
            if (xmit) {
                SZ_Clear(&net_message); MSG_WriteLong(&net_message, 0); MSG_WriteByte(&net_message, CCREQ_SERVER_INFO);
                MSG_WriteString(&net_message, "QUAKE"); MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
                WriteControlHeader(&net_message);
                lan.Broadcast(lan.GetControlSocket(), net_message.data, net_message.cursize); SZ_Clear(&net_message);
            }

            int ret;
            while ((ret = lan.Read(lan.GetControlSocket(), net_message.data, net_message.maxsize, &readaddr)) > 0) {
                int control; if (!ReadControlHeader(ret, control)) continue;
                if (lan.AddrCompare(&readaddr, &myaddr) >= 0 || hostCacheCount == HOSTCACHESIZE) continue;
                if (MSG_ReadByte() != CCREP_SERVER_INFO) continue;

                lan.GetAddrFromName(MSG_ReadString(), &readaddr); int n;
                for (n = 0; n < hostCacheCount; n++) if (lan.AddrCompare(&readaddr, &hostcache[n].addr) == 0) break;
                if (n < hostCacheCount) continue;

                hostCacheCount++;
                Q_strcpy(hostcache[n].name, MSG_ReadString()); Q_strcpy(hostcache[n].map, MSG_ReadString());
                hostcache[n].users = MSG_ReadByte(); hostcache[n].maxusers = MSG_ReadByte();
                if (MSG_ReadByte() != NET_PROTOCOL_VERSION) {
                    Q_strcpy(hostcache[n].cname, hostcache[n].name); hostcache[n].cname[14] = 0;
                    Q_strcpy(hostcache[n].name, "*"); Q_strcat(hostcache[n].name, hostcache[n].cname);
                }
                Q_memcpy(&hostcache[n].addr, &readaddr, sizeof(struct qsockaddr));
                hostcache[n].driver = net_driverlevel; hostcache[n].ldriver = net_landriverlevel;
                Q_strcpy(hostcache[n].cname, lan.AddrToString(&readaddr));

                for (int i = 0; i < hostCacheCount; i++) {
                    if (i == n) continue;
                    if (Q_strcasecmp(hostcache[n].name, hostcache[i].name) == 0) {
                        int len = Q_strlen(hostcache[n].name);
                        if (len < 15 && hostcache[n].name[len - 1] > '8') { hostcache[n].name[len] = '0'; hostcache[n].name[len + 1] = 0; }
                        else { hostcache[n].name[len - 1]++; }
                        i = -1;
                    }
                }
            }
        }
    }

    qsocket_t* Connect(const char* host) override {
        for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++) {
            if (!LANFunc(net_landriverlevel).IsInitialized()) continue;
            NetLanDriver& lan = LANFunc(net_landriverlevel);

            struct qsockaddr sendaddr, readaddr; Sys_Printf("_Datagram_Connect: connecting to '%s'...\n", host);
            if (lan.GetAddrFromName(host, &sendaddr) == -1) continue;

            int newsock = lan.OpenSocket(0); if (newsock == -1) continue;
            qsocket_t* sock = NET_NewQSocket(); if (!sock) { lan.CloseSocket(newsock); return nullptr; }

            sock->socket = newsock; sock->landriver = net_landriverlevel;
            if (lan.Connect(newsock, &sendaddr) == -1) { NET_FreeQSocket(sock); lan.CloseSocket(newsock); continue; }

            Con_Printf("trying...\n"); Screen::GetScreenSystem().UpdateScreen();
            int ret = 0; const char* reason = nullptr;

            for (int reps = 0; reps < 3; reps++) {
                double start_time = SetNetTime();
                SZ_Clear(&net_message); MSG_WriteLong(&net_message, 0); MSG_WriteByte(&net_message, CCREQ_CONNECT);
                MSG_WriteString(&net_message, "QUAKE"); MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
                WriteControlHeader(&net_message); lan.Write(newsock, net_message.data, net_message.cursize, &sendaddr);
                SZ_Clear(&net_message);

                do {
                    ret = lan.Read(newsock, net_message.data, net_message.maxsize, &readaddr);
                    if (ret > 0) {
                        int control;
                        if (lan.AddrCompare(&readaddr, &sendaddr) != 0 || !ReadControlHeader(ret, control)) ret = 0;
                    }
#ifdef _WIN32
                    if (ret == 0) Sleep(1);
#else
                    if (ret == 0) usleep(1000);
#endif
                } while (ret == 0 && (SetNetTime() - start_time) < 2.5);

                if (ret) break;
                Con_Printf("still trying...\n"); Screen::GetScreenSystem().UpdateScreen();
            }

            if (ret <= 0) {
                reason = (ret == 0) ? "No Response" : "Network Error"; Con_Printf("%s\n", reason); m_return_reason = reason;
            } else {
                ret = MSG_ReadByte();
                if (ret == CCREP_REJECT) { reason = MSG_ReadString(); Con_Printf(reason); m_return_reason = reason; }
                else if (ret == CCREP_ACCEPT) {
                    Q_memcpy(&sock->addr, &sendaddr, sizeof(struct qsockaddr)); lan.SetSocketPort(&sock->addr, MSG_ReadLong());
                    lan.GetNameFromAddr(&sendaddr, sock->address); Con_Printf("Connection accepted\n");
                    sock->lastMessageTime = SetNetTime();
                    if (lan.Connect(newsock, &sock->addr) != -1) { m_return_onerror = false; return sock; }
                    reason = "Connect to Game failed"; Con_Printf("%s\n", reason); m_return_reason = reason;
                } else { reason = "Bad Response"; Con_Printf("%s\n", reason); m_return_reason = reason; }
            }

            NET_FreeQSocket(sock); lan.CloseSocket(newsock);
            if (m_return_onerror) { key_dest = key_menu; m_state = m_return_state; m_return_onerror = false; }
            return nullptr;
        }
        return nullptr;
    }
};

// ============================================================================
// VCR Driver Implementation
// ============================================================================
static struct { double time; int op; long session; } vcrNext;

class VCRDriver : public NetDriver {
    static void ReadNext() {
        if (Sys_FileRead(vcrFile, &vcrNext, sizeof(vcrNext)) == 0) { vcrNext.op = 255; Sys_Error("=== END OF PLAYBACK===\n"); }
        if (vcrNext.op < 1 || vcrNext.op > VCR_MAX_MESSAGE) Sys_Error("VCR_ReadNext: bad op");
    }

public:
    const char* GetName() const override { return "VCR"; }
    int Init() override { Sys_FileRead(vcrFile, &vcrNext, sizeof(vcrNext)); return 0; }

    qsocket_t* CheckNewConnections() override {
        if (host_time != vcrNext.time || vcrNext.op != VCR_OP_CONNECT) Sys_Error("VCR missmatch");
        if (!vcrNext.session) { ReadNext(); return nullptr; }
        qsocket_t* sock = NET_NewQSocket(); *(long*)(&sock->driverdata) = vcrNext.session;
        Sys_FileRead(vcrFile, sock->address, NET_NAMELEN); ReadNext(); return sock;
    }

    int GetMessage(qsocket_t* sock) override {
        if (host_time != vcrNext.time || vcrNext.op != VCR_OP_GETMESSAGE || vcrNext.session != *(long*)(&sock->driverdata)) Sys_Error("VCR missmatch");
        int ret; Sys_FileRead(vcrFile, &ret, sizeof(int));
        if (ret != 1) { ReadNext(); return ret; }
        Sys_FileRead(vcrFile, &net_message.cursize, sizeof(int)); Sys_FileRead(vcrFile, net_message.data, net_message.cursize);
        ReadNext(); return 1;
    }

    int SendMessage(qsocket_t* sock, sizebuf_t*) override {
        if (host_time != vcrNext.time || vcrNext.op != VCR_OP_SENDMESSAGE || vcrNext.session != *(long*)(&sock->driverdata)) Sys_Error("VCR missmatch");
        int ret; Sys_FileRead(vcrFile, &ret, sizeof(int)); ReadNext(); return ret;
    }

    qboolean CanSendMessage(qsocket_t* sock) override {
        if (host_time != vcrNext.time || vcrNext.op != VCR_OP_CANSENDMESSAGE || vcrNext.session != *(long*)(&sock->driverdata)) Sys_Error("VCR missmatch");
        qboolean ret; Sys_FileRead(vcrFile, &ret, sizeof(int)); ReadNext(); return ret;
    }
};

// ============================================================================
// Network Subsystem Core API Implementation
// ============================================================================
double SetNetTime() { net_time = Sys_FloatTime(); return net_time; }

qsocket_t* NET_NewQSocket() {
    if (!net_freeSockets || net_activeconnections >= svs.maxclients) return nullptr;

    qsocket_t* sock = net_freeSockets; net_freeSockets = sock->next;
    sock->next = net_activeSockets; net_activeSockets = sock;
    sock->disconnected = false; sock->connecttime = net_time;
    Q_strcpy(sock->address, "UNSET ADDRESS");
    sock->driver = net_driverlevel; sock->socket = 0; sock->driverdata = nullptr;
    sock->canSend = true; sock->sendNext = false; sock->lastMessageTime = net_time;
    sock->ackSequence = sock->sendSequence = sock->unreliableSendSequence = 0;
    sock->sendMessageLength = sock->receiveSequence = sock->unreliableReceiveSequence = sock->receiveMessageLength = 0;
    return sock;
}

void NET_FreeQSocket(qsocket_t* sock) {
    if (sock == net_activeSockets) { net_activeSockets = net_activeSockets->next; }
    else {
        qsocket_t* s; for (s = net_activeSockets; s; s = s->next) if (s->next == sock) { s->next = sock->next; break; }
        if (!s) Sys_Error("NET_FreeQSocket: not active\n");
    }
    sock->next = net_freeSockets; net_freeSockets = sock; sock->disconnected = true;
}

static void NET_Listen_f() {
    if (Cmd::Argc() != 2) { Con_Printf("\"listen\" is \"%u\"\n", listening ? 1 : 0); return; }
    listening = Q_atoi(Cmd::Argv(1)) ? true : false;
    for (net_driverlevel = 0; net_driverlevel < net_numdrivers; net_driverlevel++) {
        if (DriverFunc(net_driverlevel).IsInitialized()) DriverFunc(net_driverlevel).Listen(listening);
    }
}

static void MaxPlayers_f() {
    if (Cmd::Argc() != 2) { Con_Printf("\"maxplayers\" is \"%u\"\n", svs.maxclients); return; }
    if (sv.active) { Con_Printf("maxplayers can not be changed while a server is running.\n"); return; }
    int n = Q_atoi(Cmd::Argv(1)); if (n < 1) n = 1;
    if (n > svs.maxclientslimit) { n = svs.maxclientslimit; Con_Printf("\"maxplayers\" set to \"%u\"\n", n); }
    if (n == 1 && listening) Cmd::BufferAddText("listen 0\n");
    if (n > 1 && !listening) Cmd::BufferAddText("listen 1\n");
    svs.maxclients = n; Cvar::Set("deathmatch", (n == 1) ? "0" : "1");
}

static void NET_Port_f() {
    if (Cmd::Argc() != 2) { Con_Printf("\"port\" is \"%u\"\n", net_hostport); return; }
    int n = Q_atoi(Cmd::Argv(1));
    if (n < 1 || n > 65534) { Con_Printf("Bad value, must be between 1 and 65534\n"); return; }
    DEFAULTnet_hostport = net_hostport = n;
    if (listening) { Cmd::BufferAddText("listen 0\n"); Cmd::BufferAddText("listen 1\n"); }
}

static void PrintSlistHeader() { Con_Printf("Server          Map             Users\n--------------- --------------- -----\n"); slistLastShown = 0; }
static void PrintSlist() {
    int n;
    for (n = slistLastShown; n < hostCacheCount; n++) {
        if (hostcache[n].maxusers) Con_Printf("%-15.15s %-15.15s %2u/%2u\n", hostcache[n].name, hostcache[n].map, hostcache[n].users, hostcache[n].maxusers);
        else Con_Printf("%-15.15s %-15.15s\n", hostcache[n].name, hostcache[n].map);
    }
    slistLastShown = n;
}
static void PrintSlistTrailer() { Con_Printf(hostCacheCount ? "== end list ==\n\n" : "No Quake servers found.\n\n"); }

static void Slist_Send(); static void Slist_Poll();
static PollProcedure slistSendProcedure = { nullptr, 0.0, Slist_Send };
static PollProcedure slistPollProcedure = { nullptr, 0.0, Slist_Poll };

void NET_Slist_f() {
    if (slistInProgress) return;
    if (!slistSilent) { Con_Printf("Looking for Quake servers...\n"); PrintSlistHeader(); }
    slistInProgress = true; slistStartTime = Sys_FloatTime();
    SchedulePollProcedure(&slistSendProcedure, 0.0); SchedulePollProcedure(&slistPollProcedure, 0.1);
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
    if ((Sys_FloatTime() - slistStartTime) < 1.5) { SchedulePollProcedure(&slistPollProcedure, 0.1); return; }
    if (!slistSilent) PrintSlistTrailer();
    slistInProgress = slistSilent = false; slistLocal = true;
}

qsocket_t* NET_Connect(const char* host) {
    SetNetTime(); if (host && *host == 0) host = nullptr;
    int numdrivers = net_numdrivers;

    if (host) {
        if (Q_strcasecmp(host, "local") == 0) { numdrivers = 1; goto JustDoIt; }
        if (hostCacheCount) {
            for (int n = 0; n < hostCacheCount; n++) {
                if (Q_strcasecmp(host, hostcache[n].name) == 0) { host = hostcache[n].cname; break; }
            }
        }
    }

    slistSilent = host ? true : false; NET_Slist_f();
    while (slistInProgress) NET_Poll();

    if (!host) {
        if (hostCacheCount != 1) return nullptr;
        host = hostcache[0].cname; Con_Printf("Connecting to...\n%s @ %s\n\n", hostcache[0].name, host);
    }
    if (hostCacheCount) {
        for (int n = 0; n < hostCacheCount; n++) {
            if (Q_strcasecmp(host, hostcache[n].name) == 0) { host = hostcache[n].cname; break; }
        }
    }

JustDoIt:
    for (net_driverlevel = 0; net_driverlevel < numdrivers; net_driverlevel++) {
        if (!DriverFunc(net_driverlevel).IsInitialized()) continue;
        qsocket_t* ret = DriverFunc(net_driverlevel).Connect(host);
        if (ret) return ret;
    }

    if (host) { Con_Printf("\n"); PrintSlistHeader(); PrintSlist(); PrintSlistTrailer(); }
    return nullptr;
}

qsocket_t* NET_CheckNewConnections() {
    SetNetTime();
    for (net_driverlevel = 0; net_driverlevel < net_numdrivers; net_driverlevel++) {
        if (!DriverFunc(net_driverlevel).IsInitialized() || (net_driverlevel && !listening)) continue;
        qsocket_t* ret = DriverFunc(net_driverlevel).CheckNewConnections();
        if (ret) { RecordVCR(VCR_OP_CONNECT, ret); return ret; }
    }
    RecordVCR(VCR_OP_CONNECT, nullptr);
    return nullptr;
}

void NET_Close(qsocket_t* sock) {
    if (!sock || sock->disconnected) return;
    SetNetTime(); DriverFunc(sock->driver).Close(sock); NET_FreeQSocket(sock);
}

int NET_GetMessage(qsocket_t* sock) {
    if (!sock) return -1;
    if (sock->disconnected) { Con_Printf("NET_GetMessage: disconnected socket\n"); return -1; }
    SetNetTime();
    int ret = DriverFunc(sock->driver).GetMessage(sock);
    if (ret == 0 && sock->driver && (net_time - sock->lastMessageTime > net_messagetimeout.value)) {
        NET_Close(sock); return -1;
    }
    if (ret > 0 && sock->driver) {
        sock->lastMessageTime = net_time;
        if (ret == 1) messagesReceived++; else if (ret == 2) unreliableMessagesReceived++;
    }
    RecordVCR(VCR_OP_GETMESSAGE, sock, ret, (ret > 0) ? net_message.cursize : -1);
    return ret;
}

int NET_SendMessage(qsocket_t* sock, sizebuf_t* data) {
    if (!sock) return -1;
    if (sock->disconnected) { Con_Printf("NET_SendMessage: disconnected socket\n"); return -1; }
    SetNetTime();
    int r = DriverFunc(sock->driver).SendMessage(sock, data);
    if (r == 1 && sock->driver) messagesSent++;
    RecordVCR(VCR_OP_SENDMESSAGE, sock, r);
    return r;
}

int NET_SendUnreliableMessage(qsocket_t* sock, sizebuf_t* data) {
    if (!sock) return -1;
    if (sock->disconnected) { Con_Printf("NET_SendMessage: disconnected socket\n"); return -1; }
    SetNetTime();
    int r = DriverFunc(sock->driver).SendUnreliableMessage(sock, data);
    if (r == 1 && sock->driver) unreliableMessagesSent++;
    RecordVCR(VCR_OP_SENDMESSAGE, sock, r);
    return r;
}

qboolean NET_CanSendMessage(qsocket_t* sock) {
    if (!sock || sock->disconnected) return false;
    SetNetTime();
    int r = DriverFunc(sock->driver).CanSendMessage(sock);
    RecordVCR(VCR_OP_CANSENDMESSAGE, sock, r);
    return r;
}

int NET_SendToAll(sizebuf_t* data, int blocktime) {
    qboolean state1[MAX_SCOREBOARD], state2[MAX_SCOREBOARD]; int count = 0;

    for (int i = 0; i < svs.maxclients; i++) {
        client_t* client = &svs.clients[i]; if (!client->netconnection) continue;
        if (client->active) {
            if (client->netconnection->driver == 0) {
                NET_SendMessage(client->netconnection, data); state1[i] = state2[i] = true; continue;
            }
            count++; state1[i] = state2[i] = false;
        } else { state1[i] = state2[i] = true; }
    }

    double start = Sys_FloatTime();
    while (count) {
        count = 0;
        for (int i = 0; i < svs.maxclients; i++) {
            client_t* client = &svs.clients[i];
            if (!state1[i]) {
                if (NET_CanSendMessage(client->netconnection)) { state1[i] = true; NET_SendMessage(client->netconnection, data); }
                else { NET_GetMessage(client->netconnection); }
                count++; continue;
            }
            if (!state2[i]) {
                if (NET_CanSendMessage(client->netconnection)) { state2[i] = true; }
                else { NET_GetMessage(client->netconnection); }
                count++; continue;
            }
        }
        if ((Sys_FloatTime() - start) > blocktime) break;
    }
    return count;
}

void NET_Init() {
    net_drivers.clear();
    if (COM_CheckParm("-playback")) {
        net_drivers.push_back(eastl::make_unique<VCRDriver>()); net_numdrivers = 1;
    } else {
        net_drivers.push_back(eastl::make_unique<LoopbackDriver>());
        net_drivers.push_back(eastl::make_unique<DatagramDriver>()); net_numdrivers = 2;
    }

    net_landrivers.clear(); net_landrivers.push_back(eastl::make_unique<UDPDriver>()); net_numlandrivers = 1;
    if (COM_CheckParm("-record")) recording = true;

    int i = COM_CheckParm("-port"); if (!i) i = COM_CheckParm("-udpport"); if (!i) i = COM_CheckParm("-ipxport");
    if (i) {
        if (i < com_argc - 1) DEFAULTnet_hostport = Q_atoi(com_argv[i + 1]);
        else Sys_Error("NET_Init: you must specify a number after -port");
    }
    net_hostport = DEFAULTnet_hostport;
    if (COM_CheckParm("-listen") || cls.state == ca_dedicated) listening = true;

    net_numsockets = svs.maxclientslimit; if (cls.state != ca_dedicated) net_numsockets++;
    SetNetTime();

    socket_pool.clear(); socket_pool.reserve(net_numsockets); net_freeSockets = net_activeSockets = nullptr;
    for (i = 0; i < net_numsockets; i++) {
        socket_pool.push_back(eastl::make_unique<qsocket_t>());
        qsocket_t* s = socket_pool.back().get();
        s->next = net_freeSockets; net_freeSockets = s; s->disconnected = true;
    }

    SZ_Alloc(&net_message, NET_MAXMESSAGE);
    cvar_t* cvars[] = { &net_messagetimeout, &hostname, &config_com_port, &config_com_irq, &config_com_baud, &config_com_modem, &config_modem_dialtype, &config_modem_clear, &config_modem_init, &config_modem_hangup };
    for (auto* c : cvars) Cvar::Register(c);

    Cmd::AddCommand("slist", NET_Slist_f); Cmd::AddCommand("listen", NET_Listen_f);
    Cmd::AddCommand("maxplayers", MaxPlayers_f); Cmd::AddCommand("port", NET_Port_f);

    for (net_driverlevel = 0; net_driverlevel < net_numdrivers; net_driverlevel++) {
        int controlSocket = DriverFunc(net_driverlevel).Init(); if (controlSocket == -1) continue;
        DriverFunc(net_driverlevel).SetInitialized(true); DriverFunc(net_driverlevel).SetControlSocket(controlSocket);
        if (listening) DriverFunc(net_driverlevel).Listen(true);
    }
    if (*my_ipx_address) Con_DPrintf("IPX address %s\n", my_ipx_address);
    if (*my_tcpip_address) Con_DPrintf("TCP/IP address %s\n", my_tcpip_address);
}

void NET_Shutdown() {
    SetNetTime();
    for (qsocket_t* sock = net_activeSockets; sock; sock = sock->next) NET_Close(sock);
    for (net_driverlevel = 0; net_driverlevel < net_numdrivers; net_driverlevel++) {
        if (DriverFunc(net_driverlevel).IsInitialized()) {
            DriverFunc(net_driverlevel).Shutdown(); DriverFunc(net_driverlevel).SetInitialized(false);
        }
    }
    if (vcrFile != -1) { Con_Printf("Closing vcrfile.\n"); Sys_FileClose(vcrFile); }
}

static PollProcedure* pollProcedureList = nullptr;

void NET_Poll() {
    if (!configRestored) {
        if (serialAvailable) {
            SetComPortConfig(0, (int)config_com_port.value, (int)config_com_irq.value, (int)config_com_baud.value, config_com_modem.value == 1.0);
            SetModemConfig(0, config_modem_dialtype.string.c_str(), config_modem_clear.string.c_str(), config_modem_init.string.c_str(), config_modem_hangup.string.c_str());
        }
        configRestored = true;
    }
    SetNetTime();
    while (pollProcedureList && pollProcedureList->nextTime <= net_time) {
        PollProcedure* pp = pollProcedureList; pollProcedureList = pp->next; pp->next = nullptr; pp->procedure();
    }
}

void SchedulePollProcedure(PollProcedure* pp, double timeOffset) {
    pp->nextTime = net_time + timeOffset;
    if (pollProcedureList == pp) pollProcedureList = pp->next;
    else if (pollProcedureList) {
        for (PollProcedure* p = pollProcedureList; p->next; p = p->next) {
            if (p->next == pp) { p->next = pp->next; break; }
        }
    }
    pp->next = nullptr;
    if (!pollProcedureList || pp->nextTime < pollProcedureList->nextTime) {
        pp->next = pollProcedureList; pollProcedureList = pp; return;
    }
    PollProcedure* p = pollProcedureList;
    while (p->next && p->next->nextTime <= pp->nextTime) p = p->next;
    pp->next = p->next; p->next = pp;
}

} // namespace Net
