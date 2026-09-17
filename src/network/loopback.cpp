// loopback.cpp -- In-Memory Loopback Network Driver Implementation
#include "network/loopback.hpp"
#include "server/server_types.hpp"
#include "client/client_types.hpp"
#include "core/cmd.hpp"
#include "core/math.hpp"

namespace Net {

int LoopbackDriver::Init() {
    return (Client::cls.state == ca_dedicated) ? -1 : 0;
}

void LoopbackDriver::SearchForHosts(qboolean) {
    if (!Server::sv.active) return;
    hostCacheCount = 1;
    const char* name = (Common::Q_strcmp(hostname.string.c_str(), "UNNAMED") == 0) ? "local" : hostname.string.c_str();
    Common::Q_strncpy(hostcache[0].name, name, sizeof(hostcache[0].name));
    Common::Q_strncpy(hostcache[0].map, Server::sv.name.data(), sizeof(hostcache[0].map));
    hostcache[0].users = net_activeconnections;
    hostcache[0].maxusers = Server::svs.maxclients;
    hostcache[0].driver = net_driverlevel;
    Common::Q_strcpy(hostcache[0].cname, "local");
}

qsocket_t* LoopbackDriver::Connect(const char* host) {
    if (Common::Q_strcmp(host, "local") != 0) return nullptr;
    localconnectpending = true;

    if (!loop_client && !(loop_client = NET_NewQSocket())) return nullptr;
    Common::Q_strcpy(loop_client->address, "localhost");
    loop_client->receiveMessageLength = loop_client->sendMessageLength = 0;
    loop_client->canSend = true;

    if (!loop_server && !(loop_server = NET_NewQSocket())) return nullptr;
    Common::Q_strcpy(loop_server->address, "LOCAL");
    loop_server->receiveMessageLength = loop_server->sendMessageLength = 0;
    loop_server->canSend = true;

    loop_client->driverdata = (void*)loop_server;
    loop_server->driverdata = (void*)loop_client;
    return loop_client;
}

qsocket_t* LoopbackDriver::CheckNewConnections() {
    if (!localconnectpending) return nullptr;
    localconnectpending = false;
    loop_server->sendMessageLength = loop_server->receiveMessageLength = 0;
    loop_server->canSend = loop_client->canSend = true;
    loop_client->sendMessageLength = loop_client->receiveMessageLength = 0;
    return loop_server;
}

int LoopbackDriver::GetMessage(qsocket_t* sock) {
    if (sock->receiveMessageLength == 0) return 0;
    int ret = sock->receiveMessage[0];
    int length = sock->receiveMessage[1] + (sock->receiveMessage[2] << 8);

    Common::SZ_Clear(&net_message);
    Common::SZ_Write(&net_message, &sock->receiveMessage[4], length);
    length = IntAlign(length + 4);
    sock->receiveMessageLength -= length;
    if (sock->receiveMessageLength) {
        Common::Q_memcpy(sock->receiveMessage.data(), &sock->receiveMessage[length], sock->receiveMessageLength);
    }
    if (sock->driverdata && ret == 1) {
        ((qsocket_t*)sock->driverdata)->canSend = true;
    }
    return ret;
}

int LoopbackDriver::SendMessage(qsocket_t* sock, sizebuf_t* data) {
    if (!sock->driverdata) return -1;
    qsocket_t* peer = (qsocket_t*)sock->driverdata;
    if ((peer->receiveMessageLength + data->cursize + 4) > NET_MAXMESSAGE) {
        Common::Sys_Error("Loop_SendMessage: overflow\n");
    }

    byte* buffer = peer->receiveMessage.data() + peer->receiveMessageLength;
    *buffer++ = 1;
    *buffer++ = static_cast<byte>(data->cursize & 0xff);
    *buffer++ = static_cast<byte>(data->cursize >> 8);
    buffer++;
    Common::Q_memcpy(buffer, data->data, data->cursize);
    peer->receiveMessageLength = IntAlign(peer->receiveMessageLength + data->cursize + 4);
    sock->canSend = false;
    return 1;
}

int LoopbackDriver::SendUnreliableMessage(qsocket_t* sock, sizebuf_t* data) {
    if (!sock->driverdata) return -1;
    qsocket_t* peer = (qsocket_t*)sock->driverdata;
    if ((peer->receiveMessageLength + data->cursize + 3) > NET_MAXMESSAGE) return 0;

    byte* buffer = peer->receiveMessage.data() + peer->receiveMessageLength;
    *buffer++ = 2;
    *buffer++ = static_cast<byte>(data->cursize & 0xff);
    *buffer++ = static_cast<byte>(data->cursize >> 8);
    buffer++;
    Common::Q_memcpy(buffer, data->data, data->cursize);
    peer->receiveMessageLength = IntAlign(peer->receiveMessageLength + data->cursize + 4);
    return 1;
}

qboolean LoopbackDriver::CanSendMessage(qsocket_t* sock) {
    return sock->driverdata ? sock->canSend : false;
}

void LoopbackDriver::Close(qsocket_t* sock) {
    if (sock->driverdata) {
        ((qsocket_t*)sock->driverdata)->driverdata = nullptr;
    }
    sock->receiveMessageLength = sock->sendMessageLength = 0;
    sock->canSend = true;
    if (sock == loop_client) {
        loop_client = nullptr;
    } else {
        loop_server = nullptr;
    }
}

} // namespace Net
