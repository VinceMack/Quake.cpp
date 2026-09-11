// udp_driver.hpp -- Winsock / BSD Sockets UDP Driver
#pragma once

#include "network/socket.hpp"

namespace Net {

class UDPDriver : public NetLanDriver {
public:
    const char* GetName() const override { return "UDP"; }

    int Init() override;
    void Shutdown() override;
    void Listen(qboolean state) override;
    int OpenSocket(int port) override;
    int CloseSocket(int socket) override;
    int CheckNewConnections() override;
    int Read(int socket, byte* buf, int len, struct qsockaddr* addr) override;
    int Write(int socket, byte* buf, int len, struct qsockaddr* addr) override;
    int Broadcast(int socket, byte* buf, int len) override;
    char* AddrToString(struct qsockaddr* addr) override;
    int StringToAddr(const char* string, struct qsockaddr* addr) override;
    int GetSocketAddr(int socket, struct qsockaddr* addr) override;
    int GetNameFromAddr(struct qsockaddr* addr, char* name) override;
    int GetAddrFromName(const char* name, struct qsockaddr* addr) override;
    int AddrCompare(struct qsockaddr* a1, struct qsockaddr* a2) override;
    int GetSocketPort(struct qsockaddr* addr) override;
    int SetSocketPort(struct qsockaddr* addr, int port) override;
};

} // namespace Net
