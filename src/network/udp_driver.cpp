// udp_driver.cpp -- Winsock / BSD Sockets UDP Driver Implementation
#include "network/udp_driver.hpp"
#include "platform/crt_compat.hpp"
#include "core/filesystem.hpp"
#include "core/print.hpp"
#include "core/math.hpp"

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

namespace Net {

static int net_acceptsocket = -1;
static int net_controlsocket = 0;
static int net_broadcastsocket = 0;
static struct qsockaddr broadcastaddr;
static unsigned long myAddr;

static int PartialIPAddress(const char* in, struct qsockaddr* hostaddr)
{
    char buff[256];
    buff[0] = '.';
    strcpy_s(buff + 1, sizeof(buff) - 1, in);
    char* b = (buff[1] == '.') ? buff + 1 : buff;
    int addr = 0, mask = -1, port = net_hostport;

    while (*b == '.') {
        b++;
        int num = 0, run = 0;
        while (*b >= '0' && *b <= '9') {
            num = num * 10 + (*b++ - '0');
            if (++run > 3) return -1;
        }
        if ((*b < '0' || *b > '9') && *b != '.' && *b != ':' && *b != 0) return -1;
        if (num > 255) return -1;
        mask <<= 8;
        addr = (addr << 8) + num;
    }
    if (*b++ == ':') port = Common::Q_atoi(b);
    hostaddr->sa_family = AF_INET;
    ((struct sockaddr_in*)hostaddr)->sin_port = htons(static_cast<u_short>(port));
    ((struct sockaddr_in*)hostaddr)->sin_addr.s_addr = (myAddr & htonl(mask)) | htonl(addr);
    return 0;
}

int UDPDriver::Init()
{
    char buff[MAXHOSTNAMELEN];
    struct qsockaddr addr;
    if (Common::COM_CheckParm("-noudp")) return -1;

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return -1;
#endif

    gethostname(buff, MAXHOSTNAMELEN);
    struct addrinfo hints = { }, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(buff, nullptr, &hints, &result) != 0 || !result) {
        Common::Sys_Error("UDP_Init: unable to resolve hostname");
    }
    myAddr = ((struct sockaddr_in*)result->ai_addr)->sin_addr.s_addr;
    freeaddrinfo(result);

    if (Common::Q_strcmp(hostname.string.c_str(), "UNNAMED") == 0) {
        buff[15] = 0;
        Cvar::Set("hostname", buff);
    }
    if ((net_controlsocket = OpenSocket(0)) == -1) {
        Common::Sys_Error("UDP_Init: Unable to open control socket\n");
    }

    ((struct sockaddr_in*)&broadcastaddr)->sin_family = AF_INET;
    ((struct sockaddr_in*)&broadcastaddr)->sin_addr.s_addr = INADDR_BROADCAST;
    ((struct sockaddr_in*)&broadcastaddr)->sin_port = htons(static_cast<u_short>(net_hostport));

    GetSocketAddr(net_controlsocket, &addr);
    Common::Q_strcpy(my_tcpip_address, AddrToString(&addr));
    char* colon = Common::Q_strrchr(my_tcpip_address, ':');
    if (colon) *colon = 0;
    Console::Con_Printf("UDP Initialized\n");
    tcpipAvailable = true;
    return net_controlsocket;
}

void UDPDriver::Shutdown()
{
    Listen(false);
    CloseSocket(net_controlsocket);
#ifdef _WIN32
    WSACleanup();
#endif
}

void UDPDriver::Listen(qboolean state)
{
    if (state) {
        if (net_acceptsocket == -1 && (net_acceptsocket = OpenSocket(net_hostport)) == -1) {
            Common::Sys_Error("UDP_Listen: Unable to open accept socket\n");
        }
    } else if (net_acceptsocket != -1) {
        CloseSocket(net_acceptsocket);
        net_acceptsocket = -1;
    }
}

int UDPDriver::OpenSocket(int port)
{
    int newsocket = static_cast<int>(socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (newsocket == -1) return -1;
    unsigned long _true = 1;
    if (ioctl(newsocket, FIONBIO, &_true) == -1) {
        close(newsocket);
        return -1;
    }
#ifdef _WIN32
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
    BOOL bNewBehavior = FALSE;
    DWORD dwBytesReturned = 0;
    WSAIoctl(newsocket, SIO_UDP_CONNRESET, &bNewBehavior, sizeof(bNewBehavior), NULL, 0, &dwBytesReturned, NULL, NULL);
#endif
    int opt = 1;
    setsockopt(newsocket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    struct sockaddr_in address { };
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(static_cast<u_short>(port));
    if (bind(newsocket, (struct sockaddr*)&address, sizeof(address)) == -1) {
        close(newsocket);
        return -1;
    }
    return newsocket;
}

int UDPDriver::CloseSocket(int socket)
{
    if (socket == net_broadcastsocket) net_broadcastsocket = 0;
    return close(socket);
}

int UDPDriver::CheckNewConnections()
{
    if (net_acceptsocket == -1) return -1;
    unsigned long available;
    if (ioctl(net_acceptsocket, FIONREAD, &available) == -1) {
        Common::Sys_Error("UDP: ioctlsocket (FIONREAD) failed\n");
    }
    return available ? net_acceptsocket : -1;
}

int UDPDriver::Read(int socket, byte* buf, int len, struct qsockaddr* addr)
{
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

int UDPDriver::Write(int socket, byte* buf, int len, struct qsockaddr* addr)
{
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

int UDPDriver::Broadcast(int socket, byte* buf, int len)
{
    if (socket != net_broadcastsocket) {
        int i = 1;
        if (setsockopt(socket, SOL_SOCKET, SO_BROADCAST, (char*)&i, sizeof(i)) < 0) return -1;
        net_broadcastsocket = socket;
    }
    return Write(socket, buf, len, &broadcastaddr);
}

char* UDPDriver::AddrToString(struct qsockaddr* addr)
{
    static char buffer[22];
    int haddr = ntohl(((struct sockaddr_in*)addr)->sin_addr.s_addr);
    sprintf_s(buffer, sizeof(buffer), "%d.%d.%d.%d:%d", (haddr >> 24) & 0xff, (haddr >> 16) & 0xff, (haddr >> 8) & 0xff,
        haddr & 0xff, ntohs(((struct sockaddr_in*)addr)->sin_port));
    return buffer;
}

int UDPDriver::StringToAddr(const char* string, struct qsockaddr* addr)
{
    int ha1, ha2, ha3, ha4, hp;
    sscanf_s(string, "%d.%d.%d.%d:%d", &ha1, &ha2, &ha3, &ha4, &hp);
    addr->sa_family = AF_INET;
    ((struct sockaddr_in*)addr)->sin_addr.s_addr = htonl((ha1 << 24) | (ha2 << 16) | (ha3 << 8) | ha4);
    ((struct sockaddr_in*)addr)->sin_port = htons(static_cast<u_short>(hp));
    return 0;
}

int UDPDriver::GetSocketAddr(int socket, struct qsockaddr* addr)
{
    socklen_t addrlen = sizeof(struct qsockaddr);
    Common::Q_memset(addr, 0, sizeof(struct qsockaddr));
    getsockname(socket, (struct sockaddr*)addr, &addrlen);
    unsigned int a = ((struct sockaddr_in*)addr)->sin_addr.s_addr;
    struct in_addr loopbackAddr;
    inet_pton(AF_INET, "127.0.0.1", &loopbackAddr);
    if (a == 0 || a == loopbackAddr.s_addr) {
        ((struct sockaddr_in*)addr)->sin_addr.s_addr = myAddr;
    }
    return 0;
}

int UDPDriver::GetNameFromAddr(struct qsockaddr* addr, char* name)
{
    char hostname_buf[NI_MAXHOST];
    if (getnameinfo((const sockaddr*)addr, sizeof(struct qsockaddr), hostname_buf, NI_MAXHOST, nullptr, 0, NI_NAMEREQD)
        == 0) {
        Common::Q_strncpy(name, hostname_buf, NET_NAMELEN - 1);
        return 0;
    }
    Common::Q_strcpy(name, AddrToString(addr));
    return 0;
}

int UDPDriver::GetAddrFromName(const char* name, struct qsockaddr* addr)
{
    if (name[0] >= '0' && name[0] <= '9') return PartialIPAddress(name, addr);
    struct addrinfo hints = { }, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(name, nullptr, &hints, &result) != 0 || !result) return -1;
    addr->sa_family = AF_INET;
    ((struct sockaddr_in*)addr)->sin_port = htons(static_cast<u_short>(net_hostport));
    ((struct sockaddr_in*)addr)->sin_addr.s_addr = ((struct sockaddr_in*)result->ai_addr)->sin_addr.s_addr;
    freeaddrinfo(result);
    return 0;
}

int UDPDriver::AddrCompare(struct qsockaddr* a1, struct qsockaddr* a2)
{
    if (a1->sa_family != a2->sa_family) return -1;
    auto* s1 = (struct sockaddr_in*)a1;
    auto* s2 = (struct sockaddr_in*)a2;
    return (s1->sin_addr.s_addr != s2->sin_addr.s_addr) ? -1 : (s1->sin_port != s2->sin_port ? 1 : 0);
}

int UDPDriver::GetSocketPort(struct qsockaddr* addr)
{
    return ntohs(((struct sockaddr_in*)addr)->sin_port);
}

int UDPDriver::SetSocketPort(struct qsockaddr* addr, int port)
{
    ((struct sockaddr_in*)addr)->sin_port = htons(static_cast<u_short>(port));
    return 0;
}

} // namespace Net
