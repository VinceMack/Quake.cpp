// datagram.hpp -- Datagram Network Driver (LAN UDP Packet Protocol)
#pragma once

#include "network/socket.hpp"

namespace Net {

class DatagramDriver : public NetDriver {
public:
    const char* GetName() const override { return "Datagram"; }

    int Init() override;
    void Shutdown() override;
    void Listen(qboolean state) override;
    void Close(qsocket_t* sock) override;
    int SendMessage(qsocket_t* sock, sizebuf_t* data) override;
    int SendUnreliableMessage(qsocket_t* sock, sizebuf_t* data) override;
    qboolean CanSendMessage(qsocket_t* sock) override;
    int GetMessage(qsocket_t* sock) override;
    qsocket_t* CheckNewConnections() override;
    void SearchForHosts(qboolean xmit) override;
    qsocket_t* Connect(const char* host) override;
};

void WriteControlHeader(sizebuf_t* buf);
bool ReadControlHeader(int len, int& control);

} // namespace Net
