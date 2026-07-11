// network.cpp -- merged network subsystem
// Contains: loopback, datagram, UDP, and VCR network drivers

#include "quakedef.hpp"

using namespace CDAudio;
using namespace Client;
using namespace Common;
using namespace Console;
using namespace Render;
using namespace Draw;
using namespace Host;
using namespace Input;
using namespace Keys;
using namespace Math;
using namespace Menu;
using namespace Model;
using namespace Net;
using namespace VM;
using namespace Sbar;
using namespace Screen;
using namespace Server;
using namespace Audio;
using namespace Vid;
using namespace View;
using namespace Wad;
using namespace Cvar;
using namespace Cmd;

#include <cstdint>
#include <cstddef>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>

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
#else
#include <unistd.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif

// VCR op constants
#define VCR_OP_CONNECT 1
#define VCR_OP_GETMESSAGE 2
#define VCR_OP_SENDMESSAGE 3
#define VCR_OP_CANSENDMESSAGE 4
#define VCR_MAX_MESSAGE 4

// VCR recording — accessed from host.cpp and sys_sdl.cpp via extern
int vcrFile = -1;
qboolean recording = false;

namespace Net {

// ============================================================================
// Forward declarations of all driver functions
// ============================================================================

// net_loop.hpp -- loopback driver
int Loop_Init(void);
void Loop_Listen(qboolean state);
void Loop_SearchForHosts(qboolean xmit);
qsocket_t* Loop_Connect(const char* host);
qsocket_t* Loop_CheckNewConnections(void);
int Loop_GetMessage(qsocket_t* sock);
int Loop_SendMessage(qsocket_t* sock, sizebuf_t* data);
int Loop_SendUnreliableMessage(qsocket_t* sock, sizebuf_t* data);
qboolean Loop_CanSendMessage(qsocket_t* sock);
qboolean Loop_CanSendUnreliableMessage(void);
void Loop_Close(qsocket_t* sock);
void Loop_Shutdown(void);

// net_dgrm.hpp -- datagram driver
int Datagram_Init(void);
void Datagram_Listen(qboolean state);
void Datagram_SearchForHosts(qboolean xmit);
qsocket_t* Datagram_Connect(const char* host);
qsocket_t* Datagram_CheckNewConnections(void);
int Datagram_GetMessage(qsocket_t* sock);
int Datagram_SendMessage(qsocket_t* sock, sizebuf_t* data);
int Datagram_SendUnreliableMessage(qsocket_t* sock, sizebuf_t* data);
qboolean Datagram_CanSendMessage(qsocket_t* sock);
qboolean Datagram_CanSendUnreliableMessage(void);
void Datagram_Close(qsocket_t* sock);
void Datagram_Shutdown(void);

// net_udp.hpp -- UDP driver
int UDP_Init(void);
void UDP_Shutdown(void);
void UDP_Listen(qboolean state);
int UDP_OpenSocket(int port);
int UDP_CloseSocket(int socket);
int UDP_Connect(int socket, struct qsockaddr* addr);
int UDP_CheckNewConnections(void);
int UDP_Read(int socket, byte* buf, int len, struct qsockaddr* addr);
int UDP_Write(int socket, byte* buf, int len, struct qsockaddr* addr);
int UDP_Broadcast(int socket, byte* buf, int len);
char* UDP_AddrToString(struct qsockaddr* addr);
int UDP_StringToAddr(const char* string, struct qsockaddr* addr);
int UDP_GetSocketAddr(int socket, struct qsockaddr* addr);
int UDP_GetNameFromAddr(struct qsockaddr* addr, char* name);
int UDP_GetAddrFromName(const char* name, struct qsockaddr* addr);
int UDP_AddrCompare(struct qsockaddr* addr1, struct qsockaddr* addr2);
int UDP_GetSocketPort(struct qsockaddr* addr);
int UDP_SetSocketPort(struct qsockaddr* addr, int port);

// net_vcr.hpp -- VCR driver
int VCR_Init(void);
void VCR_SearchForHosts(qboolean xmit);
qsocket_t* VCR_Connect(const char* host);
qsocket_t* VCR_CheckNewConnections(void);
int VCR_GetMessage(qsocket_t* sock);
int VCR_SendMessage(qsocket_t* sock, sizebuf_t* data);
qboolean VCR_CanSendMessage(qsocket_t* sock);
void VCR_Close(qsocket_t* sock);
void VCR_Shutdown(void);

// ============================================================================
// net_bsd.cpp -- network driver registration tables
// ============================================================================

net_driver_t net_drivers[MAX_NET_DRIVERS] = {
    { "Loopback", false, Loop_Init, Loop_Listen, Loop_SearchForHosts,
        Loop_Connect, Loop_CheckNewConnections, Loop_GetMessage, Loop_SendMessage,
        Loop_SendUnreliableMessage, Loop_CanSendMessage,
        Loop_CanSendUnreliableMessage, Loop_Close, Loop_Shutdown },
    { "Datagram", false, Datagram_Init, Datagram_Listen, Datagram_SearchForHosts,
        Datagram_Connect, Datagram_CheckNewConnections, Datagram_GetMessage,
        Datagram_SendMessage, Datagram_SendUnreliableMessage,
        Datagram_CanSendMessage, Datagram_CanSendUnreliableMessage, Datagram_Close,
        Datagram_Shutdown }
};

int net_numdrivers = 2;

net_landriver_t net_landrivers[MAX_NET_DRIVERS] = { { "UDP",
    false,
    0,
    UDP_Init,
    UDP_Shutdown,
    UDP_Listen,
    UDP_OpenSocket,
    UDP_CloseSocket,
    UDP_Connect,
    UDP_CheckNewConnections,
    UDP_Read,
    UDP_Write,
    UDP_Broadcast,
    UDP_AddrToString,
    UDP_StringToAddr,
    UDP_GetSocketAddr,
    UDP_GetNameFromAddr,
    UDP_GetAddrFromName,
    UDP_AddrCompare,
    UDP_GetSocketPort,
    UDP_SetSocketPort } };

int net_numlandrivers = 1;

// ============================================================================
// net_loop.cpp -- loopback network driver
// ============================================================================

qboolean localconnectpending = false;
qsocket_t* loop_client = NULL;
qsocket_t* loop_server = NULL;

int Loop_Init(void)
{
    if (cls.state == ca_dedicated) {
        return -1;
    }

    return 0;
}

void Loop_Shutdown(void)
{
}

void Loop_Listen(qboolean /*state*/)
{
}

void Loop_SearchForHosts(qboolean /*xmit*/)
{
    if (!sv.active) {
        return;
    }

    hostCacheCount = 1;
    if (Q_strcmp(hostname.string, "UNNAMED") == 0) {
        Q_strcpy(hostcache[0].name, "local");
    } else {
        Q_strcpy(hostcache[0].name, hostname.string);
    }

    Q_strcpy(hostcache[0].map, sv.name);
    hostcache[0].users = net_activeconnections;
    hostcache[0].maxusers = svs.maxclients;
    hostcache[0].driver = net_driverlevel;
    Q_strcpy(hostcache[0].cname, "local");
}

qsocket_t* Loop_Connect(const char* host)
{
    if (Q_strcmp(host, "local") != 0) {
        return NULL;
    }

    localconnectpending = true;

    if (!loop_client) {
        if ((loop_client = NET_NewQSocket()) == NULL) {
            Con_Printf("Loop_Connect: no qsocket available\n");

            return NULL;
        }

        Q_strcpy(loop_client->address, "localhost");
    }

    loop_client->receiveMessageLength = 0;
    loop_client->sendMessageLength = 0;
    loop_client->canSend = true;

    if (!loop_server) {
        if ((loop_server = NET_NewQSocket()) == NULL) {
            Con_Printf("Loop_Connect: no qsocket available\n");

            return NULL;
        }

        Q_strcpy(loop_server->address, "LOCAL");
    }

    loop_server->receiveMessageLength = 0;
    loop_server->sendMessageLength = 0;
    loop_server->canSend = true;

    loop_client->driverdata = (void*)loop_server;
    loop_server->driverdata = (void*)loop_client;

    return loop_client;
}

qsocket_t* Loop_CheckNewConnections(void)
{
    if (!localconnectpending) {
        return NULL;
    }

    localconnectpending = false;
    loop_server->sendMessageLength = 0;
    loop_server->receiveMessageLength = 0;
    loop_server->canSend = true;
    loop_client->sendMessageLength = 0;
    loop_client->receiveMessageLength = 0;
    loop_client->canSend = true;

    return loop_server;
}

static inline constexpr int IntAlign(int value)
{
    return (value + (sizeof(int) - 1)) & (~(sizeof(int) - 1));
}

int Loop_GetMessage(qsocket_t* sock)
{
    int ret;
    int length;

    if (sock->receiveMessageLength == 0) {
        return 0;
    }

    ret = sock->receiveMessage[0];
    length = sock->receiveMessage[1] + (sock->receiveMessage[2] << 8);
    // alignment byte skipped here
    SZ_Clear(&net_message);
    SZ_Write(&net_message, &sock->receiveMessage[4], length);

    length = IntAlign(length + 4);
    sock->receiveMessageLength -= length;

    if (sock->receiveMessageLength) {
        Q_memcpy(sock->receiveMessage, &sock->receiveMessage[length],
            sock->receiveMessageLength);
    }

    if (sock->driverdata && ret == 1) {
        ((qsocket_t*)sock->driverdata)->canSend = true;
    }

    return ret;
}

int Loop_SendMessage(qsocket_t* sock, sizebuf_t* data)
{
    byte* buffer;
    int* bufferLength;

    if (!sock->driverdata) {
        return -1;
    }

    bufferLength = &((qsocket_t*)sock->driverdata)->receiveMessageLength;

    if ((*bufferLength + data->cursize + 4) > NET_MAXMESSAGE) {
        Sys_Error("Loop_SendMessage: overflow\n");
    }

    buffer = ((qsocket_t*)sock->driverdata)->receiveMessage + *bufferLength;

    // message type
    *buffer++ = 1;

    // length
    *buffer++ = data->cursize & 0xff;
    *buffer++ = data->cursize >> 8;

    // align
    buffer++;

    // message
    Q_memcpy(buffer, data->data, data->cursize);
    *bufferLength = IntAlign(*bufferLength + data->cursize + 4);

    sock->canSend = false;

    return 1;
}

int Loop_SendUnreliableMessage(qsocket_t* sock, sizebuf_t* data)
{
    byte* buffer;
    int* bufferLength;

    if (!sock->driverdata) {
        return -1;
    }

    bufferLength = &((qsocket_t*)sock->driverdata)->receiveMessageLength;

    if ((*bufferLength + data->cursize + sizeof(byte) + sizeof(short)) > NET_MAXMESSAGE) {
        return 0;
    }

    buffer = ((qsocket_t*)sock->driverdata)->receiveMessage + *bufferLength;

    // message type
    *buffer++ = 2;

    // length
    *buffer++ = data->cursize & 0xff;
    *buffer++ = data->cursize >> 8;

    // align
    buffer++;

    // message
    Q_memcpy(buffer, data->data, data->cursize);
    *bufferLength = IntAlign(*bufferLength + data->cursize + 4);

    return 1;
}

qboolean Loop_CanSendMessage(qsocket_t* sock)
{
    if (!sock->driverdata) {
        return false;
    }

    return sock->canSend;
}

qboolean Loop_CanSendUnreliableMessage(void)
{
    return true;
}

void Loop_Close(qsocket_t* sock)
{
    if (sock->driverdata) {
        ((qsocket_t*)sock->driverdata)->driverdata = NULL;
    }

    sock->receiveMessageLength = 0;
    sock->sendMessageLength = 0;
    sock->canSend = true;
    if (sock == loop_client) {
        loop_client = NULL;
    } else {
        loop_server = NULL;
    }
}

// ============================================================================
// net_udp.cpp -- UDP network socket driver
// ============================================================================

extern cvar_t hostname;

static int net_acceptsocket = -1;
static int net_controlsocket;
static int net_broadcastsocket = 0;
static struct qsockaddr broadcastaddr;

static unsigned long myAddr;

//=============================================================================

int UDP_Init(void)
{
    struct hostent* local;
    char buff[MAXHOSTNAMELEN];
    struct qsockaddr addr;
    char* colon;

    if (COM_CheckParm("-noudp")) {
        return -1;
    }

#ifdef _WIN32
    {
        WSADATA wsaData;
        int err = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (err != 0) {
            Con_Printf("WSAStartup failed with error: %d\n", err);
            return -1;
        }
    }
#endif

    // determine my name & address
    gethostname(buff, MAXHOSTNAMELEN);
    local = gethostbyname(buff);
    myAddr = *(int*)local->h_addr_list[0];

    // if the quake hostname isn't set, set it to the machine name
    if (Q_strcmp(hostname.string, "UNNAMED") == 0) {
        buff[15] = 0;
        Cvar::Set("hostname", buff);
    }

    if ((net_controlsocket = UDP_OpenSocket(0)) == -1) {
        Sys_Error("UDP_Init: Unable to open control socket\n");
    }

    ((struct sockaddr_in*)&broadcastaddr)->sin_family = AF_INET;
    ((struct sockaddr_in*)&broadcastaddr)->sin_addr.s_addr = INADDR_BROADCAST;
    ((struct sockaddr_in*)&broadcastaddr)->sin_port = htons(net_hostport);

    UDP_GetSocketAddr(net_controlsocket, &addr);
    Q_strcpy(my_tcpip_address, UDP_AddrToString(&addr));
    colon = Q_strrchr(my_tcpip_address, ':');
    if (colon) {
        *colon = 0;
    }

    Con_Printf("UDP Initialized\n");
    tcpipAvailable = true;

    return net_controlsocket;
}

//=============================================================================

void UDP_Shutdown(void)
{
    UDP_Listen(false);
    UDP_CloseSocket(net_controlsocket);
#ifdef _WIN32
    WSACleanup();
#endif
}

//=============================================================================

void UDP_Listen(qboolean state)
{
    // enable listening
    if (state) {
        if (net_acceptsocket != -1) {
            return;
        }

        if ((net_acceptsocket = UDP_OpenSocket(net_hostport)) == -1) {
            Sys_Error("UDP_Listen: Unable to open accept socket\n");
        }

        return;
    }

    // disable listening
    if (net_acceptsocket == -1) {
        return;
    }

    UDP_CloseSocket(net_acceptsocket);
    net_acceptsocket = -1;
}

//=============================================================================

int UDP_OpenSocket(int port)
{
    int newsocket;
    struct sockaddr_in address;

    if ((newsocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        return -1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    if (bind(newsocket, (struct sockaddr*)&address, sizeof(address)) == -1) {
        goto ErrorReturn;
    }

    return newsocket;

ErrorReturn:
    close(newsocket);

    return -1;
}

//=============================================================================

int UDP_CloseSocket(int socket)
{
    if (socket == net_broadcastsocket) {
        net_broadcastsocket = 0;
    }

    return close(socket);
}

//=============================================================================

static int PartialIPAddress(const char* in, struct qsockaddr* hostaddr)
{
    char buff[256];
    char* b;
    int addr;
    int num;
    int mask;
    int run;
    int port;

    buff[0] = '.';
    b = buff;
    strcpy(buff + 1, in);
    if (buff[1] == '.') {
        b++;
    }

    addr = 0;
    mask = -1;
    while (*b == '.') {
        b++;
        num = 0;
        run = 0;
        while (!(*b < '0' || *b > '9')) {
            num = num * 10 + *b++ - '0';
            if (++run > 3) {
                return -1;
            }
        }
        if ((*b < '0' || *b > '9') && *b != '.' && *b != ':' && *b != 0) {
            return -1;
        }

        if (num < 0 || num > 255) {
            return -1;
        }

        mask <<= 8;
        addr = (addr << 8) + num;
    }

    if (*b++ == ':') {
        port = Q_atoi(b);
    } else {
        port = net_hostport;
    }

    hostaddr->sa_family = AF_INET;
    ((struct sockaddr_in*)hostaddr)->sin_port = htons((short)port);
    ((struct sockaddr_in*)hostaddr)->sin_addr.s_addr = (myAddr & htonl(mask)) | htonl(addr);

    return 0;
}

//=============================================================================

int UDP_Connect(int /*socket*/, struct qsockaddr* /*addr*/)
{
    return 0;
}

//=============================================================================

int UDP_CheckNewConnections(void)
{
    unsigned long available;

    if (net_acceptsocket == -1) {
        return -1;
    }

    if (ioctl(net_acceptsocket, FIONREAD, &available) == -1) {
        Sys_Error("UDP: ioctlsocket (FIONREAD) failed\n");
    }

    if (available) {
        return net_acceptsocket;
    }

    return -1;
}

//=============================================================================

int UDP_Read(int socket, byte* buf, int len, struct qsockaddr* addr)
{
    socklen_t addrlen = sizeof(struct qsockaddr);
    int ret;

    ret = recvfrom(socket, (char*)buf, len, 0, (struct sockaddr*)addr, &addrlen);
    if (ret == -1 && (errno == EWOULDBLOCK || errno == ECONNREFUSED)) {
        return 0;
    }

    return ret;
}

//=============================================================================

int UDP_MakeSocketBroadcastCapable(int socket)
{
    int i = 1;

    // make this socket broadcast capable
    if (setsockopt(socket, SOL_SOCKET, SO_BROADCAST, (char*)&i, sizeof(i)) < 0) {
        return -1;
    }

    net_broadcastsocket = socket;

    return 0;
}

//=============================================================================

int UDP_Broadcast(int socket, byte* buf, int len)
{
    int ret;

    if (socket != net_broadcastsocket) {
        if (net_broadcastsocket != 0) {
            Sys_Error("Attempted to use multiple broadcasts sockets\n");
        }

        ret = UDP_MakeSocketBroadcastCapable(socket);
        if (ret == -1) {
            Con_Printf("Unable to make socket broadcast capable\n");

            return ret;
        }
    }

    return UDP_Write(socket, buf, len, &broadcastaddr);
}

//=============================================================================

int UDP_Write(int socket, byte* buf, int len, struct qsockaddr* addr)
{
    int ret;

    ret = sendto(socket, (const char*)buf, len, 0, (struct sockaddr*)addr,
        sizeof(struct qsockaddr));
    if (ret == -1 && errno == EWOULDBLOCK) {
        return 0;
    }

    return ret;
}

//=============================================================================

char* UDP_AddrToString(struct qsockaddr* addr)
{
    static char buffer[22];
    int haddr;

    haddr = ntohl(((struct sockaddr_in*)addr)->sin_addr.s_addr);
    sprintf(buffer, "%d.%d.%d.%d:%d", (haddr >> 24) & 0xff, (haddr >> 16) & 0xff,
        (haddr >> 8) & 0xff, haddr & 0xff,
        ntohs(((struct sockaddr_in*)addr)->sin_port));

    return buffer;
}

//=============================================================================

int UDP_StringToAddr(const char* string, struct qsockaddr* addr)
{
    int ha1, ha2, ha3, ha4, hp;
    int ipaddr;

    sscanf(string, "%d.%d.%d.%d:%d", &ha1, &ha2, &ha3, &ha4, &hp);
    ipaddr = (ha1 << 24) | (ha2 << 16) | (ha3 << 8) | ha4;

    addr->sa_family = AF_INET;
    ((struct sockaddr_in*)addr)->sin_addr.s_addr = htonl(ipaddr);
    ((struct sockaddr_in*)addr)->sin_port = htons(hp);

    return 0;
}

//=============================================================================

int UDP_GetSocketAddr(int socket, struct qsockaddr* addr)
{
    socklen_t addrlen = sizeof(struct qsockaddr);
    unsigned int a;

    Q_memset(addr, 0, sizeof(struct qsockaddr));
    getsockname(socket, (struct sockaddr*)addr, &addrlen);
    a = ((struct sockaddr_in*)addr)->sin_addr.s_addr;
    if (a == 0 || a == inet_addr("127.0.0.1")) {
        ((struct sockaddr_in*)addr)->sin_addr.s_addr = myAddr;
    }

    return 0;
}

//=============================================================================

int UDP_GetNameFromAddr(struct qsockaddr* addr, char* name)
{
    struct hostent* hostentry;

    hostentry = gethostbyaddr((char*)&((struct sockaddr_in*)addr)->sin_addr,
        sizeof(struct in_addr), AF_INET);
    if (hostentry) {
        Q_strncpy(name, (char*)hostentry->h_name, NET_NAMELEN - 1);

        return 0;
    }

    Q_strcpy(name, UDP_AddrToString(addr));

    return 0;
}

//=============================================================================

int UDP_GetAddrFromName(const char* name, struct qsockaddr* addr)
{
    struct hostent* hostentry;

    if (name[0] >= '0' && name[0] <= '9') {
        return PartialIPAddress(name, addr);
    }

    hostentry = gethostbyname(name);
    if (!hostentry) {
        return -1;
    }

    addr->sa_family = AF_INET;
    ((struct sockaddr_in*)addr)->sin_port = htons(net_hostport);
    ((struct sockaddr_in*)addr)->sin_addr.s_addr = *(int*)hostentry->h_addr_list[0];

    return 0;
}

//=============================================================================

int UDP_AddrCompare(struct qsockaddr* addr1, struct qsockaddr* addr2)
{
    if (addr1->sa_family != addr2->sa_family) {
        return -1;
    }

    if (((struct sockaddr_in*)addr1)->sin_addr.s_addr != ((struct sockaddr_in*)addr2)->sin_addr.s_addr) {
        return -1;
    }

    if (((struct sockaddr_in*)addr1)->sin_port != ((struct sockaddr_in*)addr2)->sin_port) {
        return 1;
    }

    return 0;
}

//=============================================================================

int UDP_GetSocketPort(struct qsockaddr* addr)
{
    return ntohs(((struct sockaddr_in*)addr)->sin_port);
}

int UDP_SetSocketPort(struct qsockaddr* addr, int port)
{
    ((struct sockaddr_in*)addr)->sin_port = htons(port);

    return 0;
}

// ============================================================================
// net_dgrm.cpp -- datagram network driver
// ============================================================================

// These two macros are to make the code more readable for the datagram driver.
// They refer to net_landrivers[] (not net_drivers[]).
#define sfunc net_landrivers[sock->landriver]
#define dfunc net_landrivers[net_landriverlevel]

static int net_landriverlevel;
static int myDriverLevel;

static struct {
    unsigned int length;
    unsigned int sequence;
    byte data[MAX_DATAGRAM];
} packetBuffer;

int packetsSent = 0;
int packetsReSent = 0;
int packetsReceived = 0;
int receivedDuplicateCount = 0;
int shortPacketCount = 0;
int droppedDatagrams;

static qboolean testInProgress = false;
static int testPollCount;
static int testDriver;
static int testSocket;

static void Test_Poll(void);
PollProcedure testPollProcedure = { NULL, 0.0, Test_Poll };

static qboolean test2InProgress = false;
static int test2Driver;
static int test2Socket;

static void Test2_Poll(void);
PollProcedure test2PollProcedure = { NULL, 0.0, Test2_Poll };

#ifdef DEBUG
char* StrAddr(struct qsockaddr* addr)
{
    static char buf[34];
    byte* p = (byte*)addr;
    int n;

    for (n = 0; n < 16; n++) {
        sprintf(buf + n * 2, "%02x", *p++);
    }

    return buf;
}
#endif


int Datagram_SendMessage(qsocket_t* sock, sizebuf_t* data)
{
    unsigned int packetLen;
    unsigned int dataLen;
    unsigned int eom;

#ifdef DEBUG
    if (data->cursize == 0) {
        Sys_Error("Datagram_SendMessage: zero length message\n");
    }

    if (data->cursize > NET_MAXMESSAGE) {
        Sys_Error("Datagram_SendMessage: message too big %u\n", data->cursize);
    }

    if (sock->canSend == false) {
        Sys_Error("SendMessage: called with canSend == false\n");
    }

#endif

    Q_memcpy(sock->sendMessage, data->data, data->cursize);
    sock->sendMessageLength = data->cursize;

    if (data->cursize <= MAX_DATAGRAM) {
        dataLen = data->cursize;
        eom = NETFLAG_EOM;
    } else {
        dataLen = MAX_DATAGRAM;
        eom = 0;
    }

    packetLen = NET_HEADERSIZE + dataLen;

    packetBuffer.length = BigLong(packetLen | (NETFLAG_DATA | eom));
    packetBuffer.sequence = BigLong(sock->sendSequence++);
    Q_memcpy(packetBuffer.data, sock->sendMessage, dataLen);

    sock->canSend = false;

    if (sfunc.Write(sock->socket, (byte*)&packetBuffer, packetLen, &sock->addr) == -1) {
        return -1;
    }

    sock->lastSendTime = net_time;
    packetsSent++;

    return 1;
}

int SendMessageNext(qsocket_t* sock)
{
    unsigned int packetLen;
    unsigned int dataLen;
    unsigned int eom;

    if (sock->sendMessageLength <= MAX_DATAGRAM) {
        dataLen = sock->sendMessageLength;
        eom = NETFLAG_EOM;
    } else {
        dataLen = MAX_DATAGRAM;
        eom = 0;
    }

    packetLen = NET_HEADERSIZE + dataLen;

    packetBuffer.length = BigLong(packetLen | (NETFLAG_DATA | eom));
    packetBuffer.sequence = BigLong(sock->sendSequence++);
    Q_memcpy(packetBuffer.data, sock->sendMessage, dataLen);

    sock->sendNext = false;

    if (sfunc.Write(sock->socket, (byte*)&packetBuffer, packetLen, &sock->addr) == -1) {
        return -1;
    }

    sock->lastSendTime = net_time;
    packetsSent++;

    return 1;
}

int ReSendMessage(qsocket_t* sock)
{
    unsigned int packetLen;
    unsigned int dataLen;
    unsigned int eom;

    if (sock->sendMessageLength <= MAX_DATAGRAM) {
        dataLen = sock->sendMessageLength;
        eom = NETFLAG_EOM;
    } else {
        dataLen = MAX_DATAGRAM;
        eom = 0;
    }

    packetLen = NET_HEADERSIZE + dataLen;

    packetBuffer.length = BigLong(packetLen | (NETFLAG_DATA | eom));
    packetBuffer.sequence = BigLong(sock->sendSequence - 1);
    Q_memcpy(packetBuffer.data, sock->sendMessage, dataLen);

    sock->sendNext = false;

    if (sfunc.Write(sock->socket, (byte*)&packetBuffer, packetLen, &sock->addr) == -1) {
        return -1;
    }

    sock->lastSendTime = net_time;
    packetsReSent++;

    return 1;
}

qboolean Datagram_CanSendMessage(qsocket_t* sock)
{
    if (sock->sendNext) {
        SendMessageNext(sock);
    }

    return sock->canSend;
}

qboolean Datagram_CanSendUnreliableMessage(void)
{
    return true;
}

int Datagram_SendUnreliableMessage(qsocket_t* sock, sizebuf_t* data)
{
    int packetLen;

#ifdef DEBUG
    if (data->cursize == 0) {
        Sys_Error("Datagram_SendUnreliableMessage: zero length message\n");
    }

    if (data->cursize > MAX_DATAGRAM) {
        Sys_Error("Datagram_SendUnreliableMessage: message too big %u\n",
            data->cursize);
    }

#endif

    packetLen = NET_HEADERSIZE + data->cursize;

    packetBuffer.length = BigLong(packetLen | NETFLAG_UNRELIABLE);
    packetBuffer.sequence = BigLong(sock->unreliableSendSequence++);
    Q_memcpy(packetBuffer.data, data->data, data->cursize);

    if (sfunc.Write(sock->socket, (byte*)&packetBuffer, packetLen, &sock->addr) == -1) {
        return -1;
    }

    packetsSent++;

    return 1;
}

int Datagram_GetMessage(qsocket_t* sock)
{
    unsigned int length;
    unsigned int flags;
    int ret = 0;
    struct qsockaddr readaddr;
    unsigned int sequence;
    unsigned int count;

    if (!sock->canSend) {
        if ((net_time - sock->lastSendTime) > 1.0) {
            ReSendMessage(sock);
        }
    }

    while (1) {
        length = sfunc.Read(sock->socket, (byte*)&packetBuffer, NET_DATAGRAMSIZE,
            &readaddr);

        //	if ((rand() & 255) > 220)
        //		continue;

        if (length == 0) {
            break;
        }

        if (length == -1) {
            Con_Printf("Read error\n");

            return -1;
        }

        if (sfunc.AddrCompare(&readaddr, &sock->addr) != 0) {
#ifdef DEBUG
            Con_DPrintf("Forged packet received\n");
            Con_DPrintf("Expected: %s\n", StrAddr(&sock->addr));
            Con_DPrintf("Received: %s\n", StrAddr(&readaddr));
#endif
            continue;
        }

        if (length < NET_HEADERSIZE) {
            shortPacketCount++;
            continue;
        }

        length = BigLong(packetBuffer.length);
        flags = length & (~NETFLAG_LENGTH_MASK);
        length &= NETFLAG_LENGTH_MASK;

        if (flags & NETFLAG_CTL) {
            continue;
        }

        sequence = BigLong(packetBuffer.sequence);
        packetsReceived++;

        if (flags & NETFLAG_UNRELIABLE) {
            if (sequence < sock->unreliableReceiveSequence) {
                Con_DPrintf("Got a stale datagram\n");
                ret = 0;
                break;
            }

            if (sequence != sock->unreliableReceiveSequence) {
                count = sequence - sock->unreliableReceiveSequence;
                droppedDatagrams += count;
                Con_DPrintf("Dropped %u datagram(s)\n", count);
            }

            sock->unreliableReceiveSequence = sequence + 1;

            length -= NET_HEADERSIZE;

            SZ_Clear(&net_message);
            SZ_Write(&net_message, packetBuffer.data, length);

            ret = 2;
            break;
        }

        if (flags & NETFLAG_ACK) {
            if (sequence != (sock->sendSequence - 1)) {
                Con_DPrintf("Stale ACK received\n");
                continue;
            }

            if (sequence == sock->ackSequence) {
                sock->ackSequence++;
                if (sock->ackSequence != sock->sendSequence) {
                    Con_DPrintf("ack sequencing error\n");
                }
            } else {
                Con_DPrintf("Duplicate ACK received\n");
                continue;
            }

            sock->sendMessageLength -= MAX_DATAGRAM;
            if (sock->sendMessageLength > 0) {
                Q_memcpy(sock->sendMessage, sock->sendMessage + MAX_DATAGRAM,
                    sock->sendMessageLength);
                sock->sendNext = true;
            } else {
                sock->sendMessageLength = 0;
                sock->canSend = true;
            }

            continue;
        }

        if (flags & NETFLAG_DATA) {
            packetBuffer.length = BigLong(NET_HEADERSIZE | NETFLAG_ACK);
            packetBuffer.sequence = BigLong(sequence);
            sfunc.Write(sock->socket, (byte*)&packetBuffer, NET_HEADERSIZE,
                &readaddr);

            if (sequence != sock->receiveSequence) {
                receivedDuplicateCount++;
                continue;
            }

            sock->receiveSequence++;

            length -= NET_HEADERSIZE;

            if (flags & NETFLAG_EOM) {
                SZ_Clear(&net_message);
                SZ_Write(&net_message, sock->receiveMessage,
                    sock->receiveMessageLength);
                SZ_Write(&net_message, packetBuffer.data, length);
                sock->receiveMessageLength = 0;

                ret = 1;
                break;
            }

            Q_memcpy(sock->receiveMessage + sock->receiveMessageLength,
                packetBuffer.data, length);
            sock->receiveMessageLength += length;
            continue;
        }
    }

    if (sock->sendNext) {
        SendMessageNext(sock);
    }

    return ret;
}

void PrintStats(qsocket_t* s)
{
    Con_Printf("canSend = %4u   \n", s->canSend);
    Con_Printf("sendSeq = %4u   ", s->sendSequence);
    Con_Printf("recvSeq = %4u   \n", s->receiveSequence);
    Con_Printf("\n");
}

void NET_Stats_f(void)
{
    qsocket_t* s;

    if (Cmd::Argc() == 1) {
        Con_Printf("unreliable messages sent   = %i\n", unreliableMessagesSent);
        Con_Printf("unreliable messages recv   = %i\n", unreliableMessagesReceived);
        Con_Printf("reliable messages sent     = %i\n", messagesSent);
        Con_Printf("reliable messages received = %i\n", messagesReceived);
        Con_Printf("packetsSent                = %i\n", packetsSent);
        Con_Printf("packetsReSent              = %i\n", packetsReSent);
        Con_Printf("packetsReceived            = %i\n", packetsReceived);
        Con_Printf("receivedDuplicateCount     = %i\n", receivedDuplicateCount);
        Con_Printf("shortPacketCount           = %i\n", shortPacketCount);
        Con_Printf("droppedDatagrams           = %i\n", droppedDatagrams);
    } else if (Q_strcmp(Cmd::Argv(1), "*") == 0) {
        for (s = net_activeSockets; s; s = s->next) {
            PrintStats(s);
        }
        for (s = net_freeSockets; s; s = s->next) {
            PrintStats(s);
        }
    } else {
        for (s = net_activeSockets; s; s = s->next) {
            if (Q_strcasecmp(Cmd::Argv(1), s->address) == 0) {
                break;
            }
        }
        if (s == NULL) {
            for (s = net_freeSockets; s; s = s->next) {
                if (Q_strcasecmp(Cmd::Argv(1), s->address) == 0) {
                    break;
                }
            }
        }

        if (s == NULL) {
            return;
        }

        PrintStats(s);
    }
}

static void Test_Poll(void)
{
    struct qsockaddr clientaddr;
    int control;
    int len;
    char name[32];
    char address[64];
    int colors;
    int frags;
    int connectTime;
    byte playerNumber;

    net_landriverlevel = testDriver;

    while (1) {
        len = dfunc.Read(testSocket, net_message.data, net_message.maxsize,
            &clientaddr);
        if (len < sizeof(int)) {
            break;
        }

        net_message.cursize = len;

        MSG_BeginReading();
        control = BigLong(*((int*)net_message.data));
        MSG_ReadLong();
        if (control == -1) {
            break;
        }

        if ((control & (~NETFLAG_LENGTH_MASK)) != NETFLAG_CTL) {
            break;
        }

        if ((control & NETFLAG_LENGTH_MASK) != len) {
            break;
        }

        if (MSG_ReadByte() != CCREP_PLAYER_INFO) {
            Sys_Error("Unexpected repsonse to Player Info request\n");
        }

        playerNumber = MSG_ReadByte();
        Q_strcpy(name, MSG_ReadString());
        colors = MSG_ReadLong();
        frags = MSG_ReadLong();
        connectTime = MSG_ReadLong();
        Q_strcpy(address, MSG_ReadString());

        Con_Printf("%s\n  frags:%3i  colors:%u %u  time:%u\n  %s\n", name, frags,
            colors >> 4, colors & 0x0f, connectTime / 60, address);
    }

    testPollCount--;
    if (testPollCount) {
        SchedulePollProcedure(&testPollProcedure, 0.1);
    } else {
        dfunc.CloseSocket(testSocket);
        testInProgress = false;
    }
}

static void Test_f(void)
{
    std::string_view host;
    int n;
    int max = MAX_SCOREBOARD;
    struct qsockaddr sendaddr;

    if (testInProgress) {
        return;
    }

    host = Cmd::Argv(1);

    if (!host.empty() && hostCacheCount) {
        for (n = 0; n < hostCacheCount; n++) {
            if (Q_strcasecmp(host, hostcache[n].name) == 0) {
                if (hostcache[n].driver != myDriverLevel) {
                    continue;
                }

                net_landriverlevel = hostcache[n].ldriver;
                max = hostcache[n].maxusers;
                Q_memcpy(&sendaddr, &hostcache[n].addr, sizeof(struct qsockaddr));
                break;
            }
        }
        if (n < hostCacheCount) {
            goto JustDoIt;
        }
    }

    for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers;
        net_landriverlevel++) {
        if (!net_landrivers[net_landriverlevel].initialized) {
            continue;
        }

        // see if we can resolve the host name
        if (dfunc.GetAddrFromName(std::string(host).c_str(), &sendaddr) != -1) {
            break;
        }
    }
    if (net_landriverlevel == net_numlandrivers) {
        return;
    }

JustDoIt:
    testSocket = dfunc.OpenSocket(0);
    if (testSocket == -1) {
        return;
    }

    testInProgress = true;
    testPollCount = 20;
    testDriver = net_landriverlevel;

    for (n = 0; n < max; n++) {
        SZ_Clear(&net_message);
        // save space for the header, filled in later
        MSG_WriteLong(&net_message, 0);
        MSG_WriteByte(&net_message, CCREQ_PLAYER_INFO);
        MSG_WriteByte(&net_message, n);
        *((int*)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
        dfunc.Write(testSocket, net_message.data, net_message.cursize, &sendaddr);
    }
    SZ_Clear(&net_message);
    SchedulePollProcedure(&testPollProcedure, 0.1);
}

static void Test2_Poll(void)
{
    struct qsockaddr clientaddr;
    int control;
    int len;
    char name[256];
    char value[256];

    net_landriverlevel = test2Driver;
    name[0] = 0;

    len = dfunc.Read(test2Socket, net_message.data, net_message.maxsize,
        &clientaddr);
    if (len < sizeof(int)) {
        goto Reschedule;
    }

    net_message.cursize = len;

    MSG_BeginReading();
    control = BigLong(*((int*)net_message.data));
    MSG_ReadLong();
    if (control == -1) {
        goto Error;
    }

    if ((control & (~NETFLAG_LENGTH_MASK)) != NETFLAG_CTL) {
        goto Error;
    }

    if ((control & NETFLAG_LENGTH_MASK) != len) {
        goto Error;
    }

    if (MSG_ReadByte() != CCREP_RULE_INFO) {
        goto Error;
    }

    Q_strcpy(name, MSG_ReadString());
    if (name[0] == 0) {
        goto Done;
    }

    Q_strcpy(value, MSG_ReadString());

    Con_Printf("%-16.16s  %-16.16s\n", name, value);

    SZ_Clear(&net_message);
    // save space for the header, filled in later
    MSG_WriteLong(&net_message, 0);
    MSG_WriteByte(&net_message, CCREQ_RULE_INFO);
    MSG_WriteString(&net_message, name);
    *((int*)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
    dfunc.Write(test2Socket, net_message.data, net_message.cursize, &clientaddr);
    SZ_Clear(&net_message);

Reschedule:
    SchedulePollProcedure(&test2PollProcedure, 0.05);

    return;

Error:
    Con_Printf("Unexpected repsonse to Rule Info request\n");
Done:
    dfunc.CloseSocket(test2Socket);
    test2InProgress = false;

    return;
}

static void Test2_f(void)
{
    std::string_view host;
    int n;
    struct qsockaddr sendaddr;

    if (test2InProgress) {
        return;
    }

    host = Cmd::Argv(1);

    if (!host.empty() && hostCacheCount) {
        for (n = 0; n < hostCacheCount; n++) {
            if (Q_strcasecmp(host, hostcache[n].name) == 0) {
                if (hostcache[n].driver != myDriverLevel) {
                    continue;
                }

                net_landriverlevel = hostcache[n].ldriver;
                Q_memcpy(&sendaddr, &hostcache[n].addr, sizeof(struct qsockaddr));
                break;
            }
        }
        if (n < hostCacheCount) {
            goto JustDoIt;
        }
    }

    for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers;
        net_landriverlevel++) {
        if (!net_landrivers[net_landriverlevel].initialized) {
            continue;
        }

        // see if we can resolve the host name
        if (dfunc.GetAddrFromName(std::string(host).c_str(), &sendaddr) != -1) {
            break;
        }
    }
    if (net_landriverlevel == net_numlandrivers) {
        return;
    }

JustDoIt:
    test2Socket = dfunc.OpenSocket(0);
    if (test2Socket == -1) {
        return;
    }

    test2InProgress = true;
    test2Driver = net_landriverlevel;

    SZ_Clear(&net_message);
    // save space for the header, filled in later
    MSG_WriteLong(&net_message, 0);
    MSG_WriteByte(&net_message, CCREQ_RULE_INFO);
    MSG_WriteString(&net_message, "");
    *((int*)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
    dfunc.Write(test2Socket, net_message.data, net_message.cursize, &sendaddr);
    SZ_Clear(&net_message);
    SchedulePollProcedure(&test2PollProcedure, 0.05);
}

int Datagram_Init(void)
{
    int i;
    int csock;

    myDriverLevel = net_driverlevel;
    Cmd::AddCommand("net_stats", NET_Stats_f);

    if (COM_CheckParm("-nolan")) {
        return -1;
    }

    for (i = 0; i < net_numlandrivers; i++) {
        csock = net_landrivers[i].Init();
        if (csock == -1) {
            continue;
        }

        net_landrivers[i].initialized = true;
        net_landrivers[i].controlSock = csock;
    }

    Cmd::AddCommand("test", Test_f);
    Cmd::AddCommand("test2", Test2_f);

    return 0;
}

void Datagram_Shutdown(void)
{
    int i;

    //
    // shutdown the lan drivers
    //
    for (i = 0; i < net_numlandrivers; i++) {
        if (net_landrivers[i].initialized) {
            net_landrivers[i].Shutdown();
            net_landrivers[i].initialized = false;
        }
    }
}

void Datagram_Close(qsocket_t* sock)
{
    sfunc.CloseSocket(sock->socket);
}

void Datagram_Listen(qboolean state)
{
    int i;

    for (i = 0; i < net_numlandrivers; i++) {
        if (net_landrivers[i].initialized) {
            net_landrivers[i].Listen(state);
        }
    }
}

static qsocket_t* _Datagram_CheckNewConnections(void)
{
    struct qsockaddr clientaddr;
    struct qsockaddr newaddr;
    int newsock;
    int acceptsock;
    qsocket_t* sock;
    qsocket_t* s;
    int len;
    int command;
    int control;
    int ret;

    acceptsock = dfunc.CheckNewConnections();
    if (acceptsock == -1) {
        return NULL;
    }

    SZ_Clear(&net_message);

    len = dfunc.Read(acceptsock, net_message.data, net_message.maxsize,
        &clientaddr);
    if (len < sizeof(int)) {
        return NULL;
    }

    net_message.cursize = len;

    MSG_BeginReading();
    control = BigLong(*((int*)net_message.data));
    MSG_ReadLong();
    if (control == -1) {
        return NULL;
    }

    if ((control & (~NETFLAG_LENGTH_MASK)) != NETFLAG_CTL) {
        return NULL;
    }

    if ((control & NETFLAG_LENGTH_MASK) != len) {
        return NULL;
    }

    command = MSG_ReadByte();
    if (command == CCREQ_SERVER_INFO) {
        if (Q_strcmp(MSG_ReadString(), "QUAKE") != 0) {
            return NULL;
        }

        SZ_Clear(&net_message);
        // save space for the header, filled in later
        MSG_WriteLong(&net_message, 0);
        MSG_WriteByte(&net_message, CCREP_SERVER_INFO);
        dfunc.GetSocketAddr(acceptsock, &newaddr);
        MSG_WriteString(&net_message, dfunc.AddrToString(&newaddr));
        MSG_WriteString(&net_message, hostname.string);
        MSG_WriteString(&net_message, sv.name);
        MSG_WriteByte(&net_message, net_activeconnections);
        MSG_WriteByte(&net_message, svs.maxclients);
        MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
        *((int*)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
        dfunc.Write(acceptsock, net_message.data, net_message.cursize, &clientaddr);
        SZ_Clear(&net_message);

        return NULL;
    }

    if (command == CCREQ_PLAYER_INFO) {
        int playerNumber;
        int activeNumber;
        int clientNumber;
        client_t* client;

        playerNumber = MSG_ReadByte();
        activeNumber = -1;
        for (clientNumber = 0, client = svs.clients; clientNumber < svs.maxclients;
            clientNumber++, client++) {
            if (client->active) {
                activeNumber++;
                if (activeNumber == playerNumber) {
                    break;
                }
            }
        }
        if (clientNumber == svs.maxclients) {
            return NULL;
        }

        SZ_Clear(&net_message);
        // save space for the header, filled in later
        MSG_WriteLong(&net_message, 0);
        MSG_WriteByte(&net_message, CCREP_PLAYER_INFO);
        MSG_WriteByte(&net_message, playerNumber);
        MSG_WriteString(&net_message, client->name);
        MSG_WriteLong(&net_message, client->colors);
        MSG_WriteLong(&net_message, (int)client->edict->v.frags);
        MSG_WriteLong(&net_message,
            (int)(net_time - client->netconnection->connecttime));
        MSG_WriteString(&net_message, client->netconnection->address);
        *((int*)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
        dfunc.Write(acceptsock, net_message.data, net_message.cursize, &clientaddr);
        SZ_Clear(&net_message);

        return NULL;
    }

    if (command == CCREQ_RULE_INFO) {
        char* prevCvarName;
        cvar_t* var;

        // find the search start location
        prevCvarName = MSG_ReadString();
        if (*prevCvarName) {
            var = Cvar::FindVar(prevCvarName);
            if (!var) {
                return NULL;
            }

            var = var->next;
        } else {
            var = Cvar::state.vars;
        }

        // search for the next server cvar
        while (var) {
            if (var->server) {
                break;
            }

            var = var->next;
        }

        // send the response

        SZ_Clear(&net_message);
        // save space for the header, filled in later
        MSG_WriteLong(&net_message, 0);
        MSG_WriteByte(&net_message, CCREP_RULE_INFO);
        if (var) {
            MSG_WriteString(&net_message, var->name);
            MSG_WriteString(&net_message, var->string);
        }

        *((int*)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
        dfunc.Write(acceptsock, net_message.data, net_message.cursize, &clientaddr);
        SZ_Clear(&net_message);

        return NULL;
    }

    if (command != CCREQ_CONNECT) {
        return NULL;
    }

    if (Q_strcmp(MSG_ReadString(), "QUAKE") != 0) {
        return NULL;
    }

    if (MSG_ReadByte() != NET_PROTOCOL_VERSION) {
        SZ_Clear(&net_message);
        // save space for the header, filled in later
        MSG_WriteLong(&net_message, 0);
        MSG_WriteByte(&net_message, CCREP_REJECT);
        MSG_WriteString(&net_message, "Incompatible version.\n");
        *((int*)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
        dfunc.Write(acceptsock, net_message.data, net_message.cursize, &clientaddr);
        SZ_Clear(&net_message);

        return NULL;
    }


    // see if this guy is already connected
    for (s = net_activeSockets; s; s = s->next) {
        if (s->driver != net_driverlevel) {
            continue;
        }

        ret = dfunc.AddrCompare(&clientaddr, &s->addr);
        if (ret >= 0) {
            // is this a duplicate connection reqeust?
            if (ret == 0 && net_time - s->connecttime < 2.0) {
                // yes, so send a duplicate reply
                SZ_Clear(&net_message);
                // save space for the header, filled in later
                MSG_WriteLong(&net_message, 0);
                MSG_WriteByte(&net_message, CCREP_ACCEPT);
                dfunc.GetSocketAddr(s->socket, &newaddr);
                MSG_WriteLong(&net_message, dfunc.GetSocketPort(&newaddr));
                *((int*)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
                dfunc.Write(acceptsock, net_message.data, net_message.cursize,
                    &clientaddr);
                SZ_Clear(&net_message);

                return NULL;
            }

            // it's somebody coming back in from a crash/disconnect
            // so close the old qsocket and let their retry get them back in
            NET_Close(s);

            return NULL;
        }
    }

    // allocate a QSocket
    sock = NET_NewQSocket();
    if (sock == NULL) {
        // no room; try to let him know
        SZ_Clear(&net_message);
        // save space for the header, filled in later
        MSG_WriteLong(&net_message, 0);
        MSG_WriteByte(&net_message, CCREP_REJECT);
        MSG_WriteString(&net_message, "Server is full.\n");
        *((int*)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
        dfunc.Write(acceptsock, net_message.data, net_message.cursize, &clientaddr);
        SZ_Clear(&net_message);

        return NULL;
    }

    // allocate a network socket
    newsock = dfunc.OpenSocket(0);
    if (newsock == -1) {
        NET_FreeQSocket(sock);

        return NULL;
    }

    // connect to the client
    if (dfunc.Connect(newsock, &clientaddr) == -1) {
        dfunc.CloseSocket(newsock);
        NET_FreeQSocket(sock);

        return NULL;
    }

    // everything is allocated, just fill in the details
    sock->socket = newsock;
    sock->landriver = net_landriverlevel;
    sock->addr = clientaddr;
    Q_strcpy(sock->address, dfunc.AddrToString(&clientaddr));

    // send him back the info about the server connection he has been allocated
    SZ_Clear(&net_message);
    // save space for the header, filled in later
    MSG_WriteLong(&net_message, 0);
    MSG_WriteByte(&net_message, CCREP_ACCEPT);
    dfunc.GetSocketAddr(newsock, &newaddr);
    MSG_WriteLong(&net_message, dfunc.GetSocketPort(&newaddr));
    //	MSG_WriteString(&net_message, dfunc.AddrToString(&newaddr));
    *((int*)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
    dfunc.Write(acceptsock, net_message.data, net_message.cursize, &clientaddr);
    SZ_Clear(&net_message);

    return sock;
}

qsocket_t* Datagram_CheckNewConnections(void)
{
    qsocket_t* ret = NULL;

    for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers;
        net_landriverlevel++) {
        if (net_landrivers[net_landriverlevel].initialized) {
            if ((ret = _Datagram_CheckNewConnections()) != NULL) {
                break;
            }
        }
    }

    return ret;
}

static void _Datagram_SearchForHosts(qboolean xmit)
{
    int ret;
    int n;
    int i;
    struct qsockaddr readaddr;
    struct qsockaddr myaddr;
    int control;

    dfunc.GetSocketAddr(dfunc.controlSock, &myaddr);
    if (xmit) {
        SZ_Clear(&net_message);
        // save space for the header, filled in later
        MSG_WriteLong(&net_message, 0);
        MSG_WriteByte(&net_message, CCREQ_SERVER_INFO);
        MSG_WriteString(&net_message, "QUAKE");
        MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
        *((int*)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
        dfunc.Broadcast(dfunc.controlSock, net_message.data, net_message.cursize);
        SZ_Clear(&net_message);
    }

    while ((ret = dfunc.Read(dfunc.controlSock, net_message.data,
                net_message.maxsize, &readaddr))
        > 0) {
        if (ret < sizeof(int)) {
            continue;
        }

        net_message.cursize = ret;

        // don't answer our own query
        if (dfunc.AddrCompare(&readaddr, &myaddr) >= 0) {
            continue;
        }

        // is the cache full?
        if (hostCacheCount == HOSTCACHESIZE) {
            continue;
        }

        MSG_BeginReading();
        control = BigLong(*((int*)net_message.data));
        MSG_ReadLong();
        if (control == -1) {
            continue;
        }

        if ((control & (~NETFLAG_LENGTH_MASK)) != NETFLAG_CTL) {
            continue;
        }

        if ((control & NETFLAG_LENGTH_MASK) != ret) {
            continue;
        }

        if (MSG_ReadByte() != CCREP_SERVER_INFO) {
            continue;
        }

        dfunc.GetAddrFromName(MSG_ReadString(), &readaddr);
        // search the cache for this server
        for (n = 0; n < hostCacheCount; n++) {
            if (dfunc.AddrCompare(&readaddr, &hostcache[n].addr) == 0) {
                break;
            }
        }

        // is it already there?
        if (n < hostCacheCount) {
            continue;
        }

        // add it
        hostCacheCount++;
        Q_strcpy(hostcache[n].name, MSG_ReadString());
        Q_strcpy(hostcache[n].map, MSG_ReadString());
        hostcache[n].users = MSG_ReadByte();
        hostcache[n].maxusers = MSG_ReadByte();
        if (MSG_ReadByte() != NET_PROTOCOL_VERSION) {
            Q_strcpy(hostcache[n].cname, hostcache[n].name);
            hostcache[n].cname[14] = 0;
            Q_strcpy(hostcache[n].name, "*");
            Q_strcat(hostcache[n].name, hostcache[n].cname);
        }

        Q_memcpy(&hostcache[n].addr, &readaddr, sizeof(struct qsockaddr));
        hostcache[n].driver = net_driverlevel;
        hostcache[n].ldriver = net_landriverlevel;
        Q_strcpy(hostcache[n].cname, dfunc.AddrToString(&readaddr));

        // check for a name conflict
        for (i = 0; i < hostCacheCount; i++) {
            if (i == n) {
                continue;
            }

            if (Q_strcasecmp(hostcache[n].name, hostcache[i].name) == 0) {
                i = Q_strlen(hostcache[n].name);
                if (i < 15 && hostcache[n].name[i - 1] > '8') {
                    hostcache[n].name[i] = '0';
                    hostcache[n].name[i + 1] = 0;
                } else {
                    hostcache[n].name[i - 1]++;
                }

                i = -1;
            }
        }
    }
}

void Datagram_SearchForHosts(qboolean xmit)
{
    for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers;
        net_landriverlevel++) {
        if (hostCacheCount == HOSTCACHESIZE) {
            break;
        }

        if (net_landrivers[net_landriverlevel].initialized) {
            _Datagram_SearchForHosts(xmit);
        }
    }
}

static qsocket_t* _Datagram_Connect(const char* host)
{
    struct qsockaddr sendaddr;
    struct qsockaddr readaddr;
    qsocket_t* sock;
    int newsock;
    int ret;
    int reps;
    double start_time;
    int control;
    const char* reason;

    // see if we can resolve the host name
    if (dfunc.GetAddrFromName(host, &sendaddr) == -1) {
        return NULL;
    }

    newsock = dfunc.OpenSocket(0);
    if (newsock == -1) {
        return NULL;
    }

    sock = NET_NewQSocket();
    if (sock == NULL) {
        goto ErrorReturn2;
    }

    sock->socket = newsock;
    sock->landriver = net_landriverlevel;

    // connect to the host
    if (dfunc.Connect(newsock, &sendaddr) == -1) {
        goto ErrorReturn;
    }

    // send the connection request
    Con_Printf("trying...\n");
    SCR_UpdateScreen();
    start_time = net_time;

    for (reps = 0; reps < 3; reps++) {
        SZ_Clear(&net_message);
        // save space for the header, filled in later
        MSG_WriteLong(&net_message, 0);
        MSG_WriteByte(&net_message, CCREQ_CONNECT);
        MSG_WriteString(&net_message, "QUAKE");
        MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
        *((int*)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
        dfunc.Write(newsock, net_message.data, net_message.cursize, &sendaddr);
        SZ_Clear(&net_message);
        do {
            ret = dfunc.Read(newsock, net_message.data, net_message.maxsize, &readaddr);
            // if we got something, validate it
            if (ret > 0) {
                // is it from the right place?
                if (sfunc.AddrCompare(&readaddr, &sendaddr) != 0) {
#ifdef DEBUG
                    Con_Printf("wrong reply address\n");
                    Con_Printf("Expected: %s\n", StrAddr(&sendaddr));
                    Con_Printf("Received: %s\n", StrAddr(&readaddr));
                    SCR_UpdateScreen();
#endif
                    ret = 0;
                    continue;
                }

                if (ret < sizeof(int)) {
                    ret = 0;
                    continue;
                }

                net_message.cursize = ret;
                MSG_BeginReading();

                control = BigLong(*((int*)net_message.data));
                MSG_ReadLong();
                if (control == -1) {
                    ret = 0;
                    continue;
                }

                if ((control & (~NETFLAG_LENGTH_MASK)) != NETFLAG_CTL) {
                    ret = 0;
                    continue;
                }

                if ((control & NETFLAG_LENGTH_MASK) != ret) {
                    ret = 0;
                    continue;
                }
            }
        } while (ret == 0 && (SetNetTime() - start_time) < 2.5);
        if (ret) {
            break;
        }

        Con_Printf("still trying...\n");
        SCR_UpdateScreen();
        start_time = SetNetTime();
    }

    if (ret == 0) {
        reason = "No Response";
        Con_Printf("%s\n", reason);
        Q_strcpy(m_return_reason, reason);
        goto ErrorReturn;
    }

    if (ret == -1) {
        reason = "Network Error";
        Con_Printf("%s\n", reason);
        Q_strcpy(m_return_reason, reason);
        goto ErrorReturn;
    }

    ret = MSG_ReadByte();
    if (ret == CCREP_REJECT) {
        reason = MSG_ReadString();
        Con_Printf(reason);
        Q_strncpy(m_return_reason, reason, 31);
        goto ErrorReturn;
    }

    if (ret == CCREP_ACCEPT) {
        Q_memcpy(&sock->addr, &sendaddr, sizeof(struct qsockaddr));
        dfunc.SetSocketPort(&sock->addr, MSG_ReadLong());
    } else {
        reason = "Bad Response";
        Con_Printf("%s\n", reason);
        Q_strcpy(m_return_reason, reason);
        goto ErrorReturn;
    }

    dfunc.GetNameFromAddr(&sendaddr, sock->address);

    Con_Printf("Connection accepted\n");
    sock->lastMessageTime = SetNetTime();

    // switch the connection to the specified address
    if (dfunc.Connect(newsock, &sock->addr) == -1) {
        reason = "Connect to Game failed";
        Con_Printf("%s\n", reason);
        Q_strcpy(m_return_reason, reason);
        goto ErrorReturn;
    }

    m_return_onerror = false;

    return sock;

ErrorReturn:
    NET_FreeQSocket(sock);
ErrorReturn2:
    dfunc.CloseSocket(newsock);
    if (m_return_onerror) {
        key_dest = key_menu;
        m_state = m_return_state;
        m_return_onerror = false;
    }

    return NULL;
}

qsocket_t* Datagram_Connect(const char* host)
{
    qsocket_t* ret = NULL;

    for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers;
        net_landriverlevel++) {
        if (net_landrivers[net_landriverlevel].initialized) {
            if ((ret = _Datagram_Connect(host)) != NULL) {
                break;
            }
        }
    }

    return ret;
}

// Undefine the datagram-specific macros before the main section
#undef sfunc
#undef dfunc

// ============================================================================
// net_vcr.cpp -- VCR demo recording and playback
// ============================================================================

static struct {
    double time;
    int op;
    long session;
} next;

int VCR_Init(void)
{
    net_drivers[0].Init = VCR_Init;

    net_drivers[0].SearchForHosts = VCR_SearchForHosts;
    net_drivers[0].Connect = VCR_Connect;
    net_drivers[0].CheckNewConnections = VCR_CheckNewConnections;
    net_drivers[0].QGetMessage = VCR_GetMessage;
    net_drivers[0].QSendMessage = VCR_SendMessage;
    net_drivers[0].CanSendMessage = VCR_CanSendMessage;
    net_drivers[0].Close = VCR_Close;
    net_drivers[0].Shutdown = VCR_Shutdown;

    Sys_FileRead(vcrFile, &next, sizeof(next));

    return 0;
}

void VCR_ReadNext(void)
{
    if (Sys_FileRead(vcrFile, &next, sizeof(next)) == 0) {
        next.op = 255;
        Sys_Error("=== END OF PLAYBACK===\n");
    }

    if (next.op < 1 || next.op > VCR_MAX_MESSAGE) {
        Sys_Error("VCR_ReadNext: bad op");
    }
}

void VCR_Shutdown(void)
{
}

int VCR_GetMessage(qsocket_t* sock)
{
    int ret;

    if (host_time != next.time || next.op != VCR_OP_GETMESSAGE || next.session != *(long*)(&sock->driverdata)) {
        Sys_Error("VCR missmatch");
    }

    Sys_FileRead(vcrFile, &ret, sizeof(int));
    if (ret != 1) {
        VCR_ReadNext();

        return ret;
    }

    Sys_FileRead(vcrFile, &net_message.cursize, sizeof(int));
    Sys_FileRead(vcrFile, net_message.data, net_message.cursize);

    VCR_ReadNext();

    return 1;
}

int VCR_SendMessage(qsocket_t* sock, sizebuf_t* /*data*/)
{
    int ret;

    if (host_time != next.time || next.op != VCR_OP_SENDMESSAGE || next.session != *(long*)(&sock->driverdata)) {
        Sys_Error("VCR missmatch");
    }

    Sys_FileRead(vcrFile, &ret, sizeof(int));

    VCR_ReadNext();

    return ret;
}

qboolean VCR_CanSendMessage(qsocket_t* sock)
{
    qboolean ret;

    if (host_time != next.time || next.op != VCR_OP_CANSENDMESSAGE || next.session != *(long*)(&sock->driverdata)) {
        Sys_Error("VCR missmatch");
    }

    Sys_FileRead(vcrFile, &ret, sizeof(int));

    VCR_ReadNext();

    return ret;
}

void VCR_Close(qsocket_t* /*sock*/)
{
}

void VCR_SearchForHosts(qboolean /*xmit*/)
{
}

qsocket_t* VCR_Connect(const char* /*host*/)
{
    return NULL;
}

qsocket_t* VCR_CheckNewConnections(void)
{
    qsocket_t* sock;

    if (host_time != next.time || next.op != VCR_OP_CONNECT) {
        Sys_Error("VCR missmatch");
    }

    if (!next.session) {
        VCR_ReadNext();

        return NULL;
    }

    sock = NET_NewQSocket();
    *(long*)(&sock->driverdata) = next.session;

    Sys_FileRead(vcrFile, sock->address, NET_NAMELEN);
    VCR_ReadNext();

    return sock;
}

// ============================================================================
// net_main.cpp -- network initialization and socket management
// ============================================================================

// These two macros are to make the code more readable for the main network code.
// They refer to net_drivers[] (not net_landrivers[]).
#define sfunc net_drivers[sock->driver]
#define dfunc net_drivers[net_driverlevel]

qsocket_t* net_activeSockets = NULL;
qsocket_t* net_freeSockets = NULL;
int net_numsockets = 0;

qboolean serialAvailable = false;
qboolean ipxAvailable = false;
qboolean tcpipAvailable = false;

int net_hostport;
int DEFAULTnet_hostport = 26000;

char my_ipx_address[NET_NAMELEN];
char my_tcpip_address[NET_NAMELEN];

void (*GetComPortConfig)(int portNumber,
    int* port,
    int* irq,
    int* baud,
    qboolean* useModem);
void (*SetComPortConfig)(int portNumber,
    int port,
    int irq,
    int baud,
    qboolean useModem);
void (*GetModemConfig)(int portNumber,
    char* dialType,
    char* clear,
    char* init,
    char* hangup);
void (*SetModemConfig)(int portNumber,
    const char* dialType,
    const char* clear,
    const char* init,
    const char* hangup);

static qboolean listening = false;

qboolean slistInProgress = false;
qboolean slistSilent = false;
qboolean slistLocal = true;
static double slistStartTime;
static int slistLastShown;

static void Slist_Send(void);
static void Slist_Poll(void);
PollProcedure slistSendProcedure = { NULL, 0.0, Slist_Send };
PollProcedure slistPollProcedure = { NULL, 0.0, Slist_Poll };

sizebuf_t net_message;
int net_activeconnections = 0;

int messagesSent = 0;
int messagesReceived = 0;
int unreliableMessagesSent = 0;
int unreliableMessagesReceived = 0;

cvar_t net_messagetimeout = { "net_messagetimeout", "300" };
cvar_t hostname = { "hostname", "UNNAMED" };

qboolean configRestored = false;
cvar_t config_com_port = { "_config_com_port", "0x3f8", true };
cvar_t config_com_irq = { "_config_com_irq", "4", true };
cvar_t config_com_baud = { "_config_com_baud", "57600", true };
cvar_t config_com_modem = { "_config_com_modem", "1", true };
cvar_t config_modem_dialtype = { "_config_modem_dialtype", "T", true };
cvar_t config_modem_clear = { "_config_modem_clear", "ATZ", true };
cvar_t config_modem_init = { "_config_modem_init", "", true };
cvar_t config_modem_hangup = { "_config_modem_hangup", "AT H", true };


int net_driverlevel;

double net_time;

double SetNetTime(void)
{
    net_time = Sys_FloatTime();

    return net_time;
}

qsocket_t* NET_NewQSocket(void)
{
    qsocket_t* sock;

    if (net_freeSockets == NULL) {
        return NULL;
    }

    if (net_activeconnections >= svs.maxclients) {
        return NULL;
    }

    // get one from free list
    sock = net_freeSockets;
    net_freeSockets = sock->next;

    // add it to active list
    sock->next = net_activeSockets;
    net_activeSockets = sock;

    sock->disconnected = false;
    sock->connecttime = net_time;
    Q_strcpy(sock->address, "UNSET ADDRESS");
    sock->driver = net_driverlevel;
    sock->socket = 0;
    sock->driverdata = NULL;
    sock->canSend = true;
    sock->sendNext = false;
    sock->lastMessageTime = net_time;
    sock->ackSequence = 0;
    sock->sendSequence = 0;
    sock->unreliableSendSequence = 0;
    sock->sendMessageLength = 0;
    sock->receiveSequence = 0;
    sock->unreliableReceiveSequence = 0;
    sock->receiveMessageLength = 0;

    return sock;
}

void NET_FreeQSocket(qsocket_t* sock)
{
    qsocket_t* s;

    // remove it from active list
    if (sock == net_activeSockets) {
        net_activeSockets = net_activeSockets->next;
    } else {
        for (s = net_activeSockets; s; s = s->next) {
            if (s->next == sock) {
                s->next = sock->next;
                break;
            }
        }
        if (!s) {
            Sys_Error("NET_FreeQSocket: not active\n");
        }
    }

    // add it to free list
    sock->next = net_freeSockets;
    net_freeSockets = sock;
    sock->disconnected = true;
}

static void NET_Listen_f(void)
{
    if (Cmd::Argc() != 2) {
        Con_Printf("\"listen\" is \"%u\"\n", listening ? 1 : 0);

        return;
    }

    listening = Q_atoi(Cmd::Argv(1)) ? true : false;

    for (net_driverlevel = 0; net_driverlevel < net_numdrivers;
        net_driverlevel++) {
        if (net_drivers[net_driverlevel].initialized == false) {
            continue;
        }

        dfunc.Listen(listening);
    }
}

static void MaxPlayers_f(void)
{
    int n;

    if (Cmd::Argc() != 2) {
        Con_Printf("\"maxplayers\" is \"%u\"\n", svs.maxclients);

        return;
    }

    if (sv.active) {
        Con_Printf("maxplayers can not be changed while a server is running.\n");

        return;
    }

    n = Q_atoi(Cmd::Argv(1));
    if (n < 1) {
        n = 1;
    }

    if (n > svs.maxclientslimit) {
        n = svs.maxclientslimit;
        Con_Printf("\"maxplayers\" set to \"%u\"\n", n);
    }

    if ((n == 1) && listening) {
        Cmd::BufferAddText("listen 0\n");
    }

    if ((n > 1) && (!listening)) {
        Cmd::BufferAddText("listen 1\n");
    }

    svs.maxclients = n;
    if (n == 1) {
        Cvar::Set("deathmatch", "0");
    } else {
        Cvar::Set("deathmatch", "1");
    }
}

static void NET_Port_f(void)
{
    int n;

    if (Cmd::Argc() != 2) {
        Con_Printf("\"port\" is \"%u\"\n", net_hostport);

        return;
    }

    n = Q_atoi(Cmd::Argv(1));
    if (n < 1 || n > 65534) {
        Con_Printf("Bad value, must be between 1 and 65534\n");

        return;
    }

    DEFAULTnet_hostport = n;
    net_hostport = n;

    if (listening) {
        // force a change to the new port
        Cmd::BufferAddText("listen 0\n");
        Cmd::BufferAddText("listen 1\n");
    }
}

static void PrintSlistHeader(void)
{
    Con_Printf("Server          Map             Users\n");
    Con_Printf("--------------- --------------- -----\n");
    slistLastShown = 0;
}

static void PrintSlist(void)
{
    int n;

    for (n = slistLastShown; n < hostCacheCount; n++) {
        if (hostcache[n].maxusers) {
            Con_Printf("%-15.15s %-15.15s %2u/%2u\n", hostcache[n].name,
                hostcache[n].map, hostcache[n].users, hostcache[n].maxusers);
        } else {
            Con_Printf("%-15.15s %-15.15s\n", hostcache[n].name, hostcache[n].map);
        }
    }
    slistLastShown = n;
}

static void PrintSlistTrailer(void)
{
    if (hostCacheCount) {
        Con_Printf("== end list ==\n\n");
    } else {
        Con_Printf("No Quake servers found.\n\n");
    }
}

void NET_Slist_f(void)
{
    if (slistInProgress) {
        return;
    }

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

static void Slist_Send(void)
{
    for (net_driverlevel = 0; net_driverlevel < net_numdrivers;
        net_driverlevel++) {
        if (!slistLocal && net_driverlevel == 0) {
            continue;
        }

        if (net_drivers[net_driverlevel].initialized == false) {
            continue;
        }

        dfunc.SearchForHosts(true);
    }

    if ((Sys_FloatTime() - slistStartTime) < 0.5) {
        SchedulePollProcedure(&slistSendProcedure, 0.75);
    }
}

static void Slist_Poll(void)
{
    for (net_driverlevel = 0; net_driverlevel < net_numdrivers;
        net_driverlevel++) {
        if (!slistLocal && net_driverlevel == 0) {
            continue;
        }

        if (net_drivers[net_driverlevel].initialized == false) {
            continue;
        }

        dfunc.SearchForHosts(false);
    }

    if (!slistSilent) {
        PrintSlist();
    }

    if ((Sys_FloatTime() - slistStartTime) < 1.5) {
        SchedulePollProcedure(&slistPollProcedure, 0.1);

        return;
    }

    if (!slistSilent) {
        PrintSlistTrailer();
    }

    slistInProgress = false;
    slistSilent = false;
    slistLocal = true;
}

int hostCacheCount = 0;
hostcache_t hostcache[HOSTCACHESIZE];

qsocket_t* NET_Connect(const char* host)
{
    qsocket_t* ret;
    int n;
    int numdrivers = net_numdrivers;

    SetNetTime();

    if (host && *host == 0) {
        host = NULL;
    }

    if (host) {
        if (Q_strcasecmp(host, "local") == 0) {
            numdrivers = 1;
            goto JustDoIt;
        }

        if (hostCacheCount) {
            for (n = 0; n < hostCacheCount; n++) {
                if (Q_strcasecmp(host, hostcache[n].name) == 0) {
                    host = hostcache[n].cname;
                    break;
                }
            }
            if (n < hostCacheCount) {
                goto JustDoIt;
            }
        }
    }

    slistSilent = host ? true : false;
    NET_Slist_f();

    while (slistInProgress) {
        NET_Poll();
    }

    if (host == NULL) {
        if (hostCacheCount != 1) {
            return NULL;
        }

        host = hostcache[0].cname;
        Con_Printf("Connecting to...\n%s @ %s\n\n", hostcache[0].name, host);
    }

    if (hostCacheCount) {
        for (n = 0; n < hostCacheCount; n++) {
            if (Q_strcasecmp(host, hostcache[n].name) == 0) {
                host = hostcache[n].cname;
                break;
            }
        }
    }

JustDoIt:
    for (net_driverlevel = 0; net_driverlevel < numdrivers; net_driverlevel++) {
        if (net_drivers[net_driverlevel].initialized == false) {
            continue;
        }

        ret = dfunc.Connect(host);
        if (ret) {
            return ret;
        }
    }

    if (host) {
        Con_Printf("\n");
        PrintSlistHeader();
        PrintSlist();
        PrintSlistTrailer();
    }

    return NULL;
}

struct {
    double time;
    int op;
    intptr_t session; // Changed from long to intptr_t
} vcrConnect;

qsocket_t* NET_CheckNewConnections(void)
{
    qsocket_t* ret;

    SetNetTime();

    for (net_driverlevel = 0; net_driverlevel < net_numdrivers;
        net_driverlevel++) {
        if (net_drivers[net_driverlevel].initialized == false) {
            continue;
        }

        if (net_driverlevel && listening == false) {
            continue;
        }

        ret = dfunc.CheckNewConnections();
        if (ret) {
            if (recording) {
                vcrConnect.time = host_time;
                vcrConnect.op = VCR_OP_CONNECT;
                vcrConnect.session = (intptr_t)ret;
                Sys_FileWrite(vcrFile, &vcrConnect, sizeof(vcrConnect));
                Sys_FileWrite(vcrFile, ret->address, NET_NAMELEN);
            }

            return ret;
        }
    }

    if (recording) {
        vcrConnect.time = host_time;
        vcrConnect.op = VCR_OP_CONNECT;
        vcrConnect.session = 0;
        Sys_FileWrite(vcrFile, &vcrConnect, sizeof(vcrConnect));
    }

    return NULL;
}

void NET_Close(qsocket_t* sock)
{
    if (!sock) {
        return;
    }

    if (sock->disconnected) {
        return;
    }

    SetNetTime();

    // call the driver_Close function
    sfunc.Close(sock);

    NET_FreeQSocket(sock);
}

struct {
    double time;
    int op;
    intptr_t session; // Changed from long to intptr_t
    int ret;
    int len;
} vcrGetMessage;

extern void PrintStats(qsocket_t* s);

int NET_GetMessage(qsocket_t* sock)
{
    int ret;

    if (!sock) {
        return -1;
    }

    if (sock->disconnected) {
        Con_Printf("NET_GetMessage: disconnected socket\n");

        return -1;
    }

    SetNetTime();

    ret = sfunc.QGetMessage(sock);

    // see if this connection has timed out
    if (ret == 0 && sock->driver) {
        if (net_time - sock->lastMessageTime > net_messagetimeout.value) {
            NET_Close(sock);

            return -1;
        }
    }

    if (ret > 0) {
        if (sock->driver) {
            sock->lastMessageTime = net_time;
            if (ret == 1) {
                messagesReceived++;
            } else if (ret == 2) {
                unreliableMessagesReceived++;
            }
        }

        if (recording) {
            vcrGetMessage.time = host_time;
            vcrGetMessage.op = VCR_OP_GETMESSAGE;
            vcrGetMessage.session = (intptr_t)sock;
            vcrGetMessage.ret = ret;
            vcrGetMessage.len = net_message.cursize;
            Sys_FileWrite(vcrFile, &vcrGetMessage, 24);
            Sys_FileWrite(vcrFile, net_message.data, net_message.cursize);
        }
    } else {
        if (recording) {
            vcrGetMessage.time = host_time;
            vcrGetMessage.op = VCR_OP_GETMESSAGE;
            vcrGetMessage.session = (intptr_t)sock;
            vcrGetMessage.ret = ret;
            Sys_FileWrite(vcrFile, &vcrGetMessage, 20);
        }
    }

    return ret;
}

struct {
    double time;
    int op;
    intptr_t session; // Changed from long to intptr_t
    int r;
} vcrSendMessage;

int NET_SendMessage(qsocket_t* sock, sizebuf_t* data)
{
    int r;

    if (!sock) {
        return -1;
    }

    if (sock->disconnected) {
        Con_Printf("NET_SendMessage: disconnected socket\n");

        return -1;
    }

    SetNetTime();
    r = sfunc.QSendMessage(sock, data);
    if (r == 1 && sock->driver) {
        messagesSent++;
    }

    if (recording) {
        vcrSendMessage.time = host_time;
        vcrSendMessage.op = VCR_OP_SENDMESSAGE;
        vcrSendMessage.session = (intptr_t)sock;
        vcrSendMessage.r = r;
        Sys_FileWrite(vcrFile, &vcrSendMessage, 20);
    }

    return r;
}

int NET_SendUnreliableMessage(qsocket_t* sock, sizebuf_t* data)
{
    int r;

    if (!sock) {
        return -1;
    }

    if (sock->disconnected) {
        Con_Printf("NET_SendMessage: disconnected socket\n");

        return -1;
    }

    SetNetTime();
    r = sfunc.SendUnreliableMessage(sock, data);
    if (r == 1 && sock->driver) {
        unreliableMessagesSent++;
    }

    if (recording) {
        vcrSendMessage.time = host_time;
        vcrSendMessage.op = VCR_OP_SENDMESSAGE;
        vcrSendMessage.session = (intptr_t)sock;
        vcrSendMessage.r = r;
        Sys_FileWrite(vcrFile, &vcrSendMessage, 20);
    }

    return r;
}

qboolean NET_CanSendMessage(qsocket_t* sock)
{
    int r;

    if (!sock) {
        return false;
    }

    if (sock->disconnected) {
        return false;
    }

    SetNetTime();

    r = sfunc.CanSendMessage(sock);

    if (recording) {
        vcrSendMessage.time = host_time;
        vcrSendMessage.op = VCR_OP_CANSENDMESSAGE;
        vcrSendMessage.session = (intptr_t)sock;
        vcrSendMessage.r = r;
        Sys_FileWrite(vcrFile, &vcrSendMessage, 20);
    }

    return r;
}

int NET_SendToAll(sizebuf_t* data, int blocktime)
{
    double start;
    int i;
    int count = 0;
    qboolean state1[MAX_SCOREBOARD];
    qboolean state2[MAX_SCOREBOARD];

    for (i = 0, host_client = svs.clients; i < svs.maxclients;
        i++, host_client++) {
        if (!host_client->netconnection) {
            continue;
        }

        if (host_client->active) {
            if (host_client->netconnection->driver == 0) {
                NET_SendMessage(host_client->netconnection, data);
                state1[i] = true;
                state2[i] = true;
                continue;
            }

            count++;
            state1[i] = false;
            state2[i] = false;
        } else {
            state1[i] = true;
            state2[i] = true;
        }
    }

    start = Sys_FloatTime();
    while (count) {
        count = 0;
        for (i = 0, host_client = svs.clients; i < svs.maxclients;
            i++, host_client++) {
            if (!state1[i]) {
                if (NET_CanSendMessage(host_client->netconnection)) {
                    state1[i] = true;
                    NET_SendMessage(host_client->netconnection, data);
                } else {
                    NET_GetMessage(host_client->netconnection);
                }

                count++;
                continue;
            }

            if (!state2[i]) {
                if (NET_CanSendMessage(host_client->netconnection)) {
                    state2[i] = true;
                } else {
                    NET_GetMessage(host_client->netconnection);
                }

                count++;
                continue;
            }
        }
        if ((Sys_FloatTime() - start) > blocktime) {
            break;
        }
    }

    return count;
}

//=============================================================================

void NET_Init(void)
{
    int i;
    int controlSocket;
    qsocket_t* s;

    if (COM_CheckParm("-playback")) {
        net_numdrivers = 1;
        net_drivers[0].Init = VCR_Init;
    }

    if (COM_CheckParm("-record")) {
        recording = true;
    }

    i = COM_CheckParm("-port");
    if (!i) {
        i = COM_CheckParm("-udpport");
    }

    if (!i) {
        i = COM_CheckParm("-ipxport");
    }

    if (i) {
        if (i < com_argc - 1) {
            DEFAULTnet_hostport = Q_atoi(com_argv[i + 1]);
        } else {
            Sys_Error("NET_Init: you must specify a number after -port");
        }
    }

    net_hostport = DEFAULTnet_hostport;

    if (COM_CheckParm("-listen") || cls.state == ca_dedicated) {
        listening = true;
    }

    net_numsockets = svs.maxclientslimit;
    if (cls.state != ca_dedicated) {
        net_numsockets++;
    }

    SetNetTime();

    for (i = 0; i < net_numsockets; i++) {
        s = (qsocket_t*)Hunk_Alloc(sizeof(qsocket_t), "qsocket");
        s->next = net_freeSockets;
        net_freeSockets = s;
        s->disconnected = true;
    }

    // allocate space for network message buffer
    SZ_Alloc(&net_message, NET_MAXMESSAGE);

    Cvar::Register(&net_messagetimeout);
    Cvar::Register(&hostname);
    Cvar::Register(&config_com_port);
    Cvar::Register(&config_com_irq);
    Cvar::Register(&config_com_baud);
    Cvar::Register(&config_com_modem);
    Cvar::Register(&config_modem_dialtype);
    Cvar::Register(&config_modem_clear);
    Cvar::Register(&config_modem_init);
    Cvar::Register(&config_modem_hangup);

    Cmd::AddCommand("slist", NET_Slist_f);
    Cmd::AddCommand("listen", NET_Listen_f);
    Cmd::AddCommand("maxplayers", MaxPlayers_f);
    Cmd::AddCommand("port", NET_Port_f);

    // initialize all the drivers
    for (net_driverlevel = 0; net_driverlevel < net_numdrivers;
        net_driverlevel++) {
        controlSocket = net_drivers[net_driverlevel].Init();
        if (controlSocket == -1) {
            continue;
        }

        net_drivers[net_driverlevel].initialized = true;
        net_drivers[net_driverlevel].controlSock = controlSocket;
        if (listening) {
            net_drivers[net_driverlevel].Listen(true);
        }
    }

    if (*my_ipx_address) {
        Con_DPrintf("IPX address %s\n", my_ipx_address);
    }

    if (*my_tcpip_address) {
        Con_DPrintf("TCP/IP address %s\n", my_tcpip_address);
    }
}

void NET_Shutdown(void)
{
    qsocket_t* sock;

    SetNetTime();

    for (sock = net_activeSockets; sock; sock = sock->next) {
        NET_Close(sock);
    }

    //
    // shutdown the drivers
    //
    for (net_driverlevel = 0; net_driverlevel < net_numdrivers;
        net_driverlevel++) {
        if (net_drivers[net_driverlevel].initialized == true) {
            net_drivers[net_driverlevel].Shutdown();
            net_drivers[net_driverlevel].initialized = false;
        }
    }

    if (vcrFile != -1) {
        Con_Printf("Closing vcrfile.\n");
        Sys_FileClose(vcrFile);
    }
}

static PollProcedure* pollProcedureList = NULL;

void NET_Poll(void)
{
    PollProcedure* pp;
    qboolean useModem;

    if (!configRestored) {
        if (serialAvailable) {
            if (config_com_modem.value == 1.0) {
                useModem = true;
            } else {
                useModem = false;
            }

            SetComPortConfig(0, (int)config_com_port.value, (int)config_com_irq.value,
                (int)config_com_baud.value, useModem);
            SetModemConfig(0, config_modem_dialtype.string, config_modem_clear.string,
                config_modem_init.string, config_modem_hangup.string);
        }

        configRestored = true;
    }

    SetNetTime();

    for (pp = pollProcedureList; pp; pp = pp->next) {
        if (pp->nextTime > net_time) {
            break;
        }

        pollProcedureList = pp->next;
        ((void (*)(void*))pp->procedure)(pp->arg);
    }
}

void SchedulePollProcedure(PollProcedure* proc, double timeOffset)
{
    PollProcedure *pp, *prev;

    proc->nextTime = Sys_FloatTime() + timeOffset;
    for (pp = pollProcedureList, prev = NULL; pp; pp = pp->next) {
        if (pp->nextTime >= proc->nextTime) {
            break;
        }

        prev = pp;
    }

    if (prev == NULL) {
        proc->next = pollProcedureList;
        pollProcedureList = proc;

        return;
    }

    proc->next = pp;
    prev->next = proc;
}

#undef sfunc
#undef dfunc
} // namespace Net
