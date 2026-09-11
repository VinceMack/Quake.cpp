// loopback.hpp -- In-Memory Loopback Network Driver
#pragma once

#include "network/socket.hpp"

namespace Net {

class LoopbackDriver : public NetDriver {
public:
    const char* GetName() const override { return "Loopback"; }
    int Init() override;
    void SearchForHosts(qboolean) override;
    qsocket_t* Connect(const char* host) override;
    qsocket_t* CheckNewConnections() override;
    int GetMessage(qsocket_t* sock) override;
    int SendMessage(qsocket_t* sock, sizebuf_t* data) override;
    int SendUnreliableMessage(qsocket_t* sock, sizebuf_t* data) override;
    qboolean CanSendMessage(qsocket_t* sock) override;
    void Close(qsocket_t* sock) override;

private:
    qboolean localconnectpending = false;
    qsocket_t* loop_client = nullptr;
    qsocket_t* loop_server = nullptr;
};

} // namespace Net
