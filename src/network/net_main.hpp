// net_main.hpp -- Network Subsystem Lifecycle and Core API
#pragma once

#include "network/socket.hpp"

namespace Net {

void NET_Init();
void NET_Shutdown();
void NET_Poll();

qsocket_t* NET_CheckNewConnections();
qsocket_t* NET_Connect(const char* host);
qboolean NET_CanSendMessage(qsocket_t* sock);
int NET_GetMessage(qsocket_t* sock);
int NET_SendMessage(qsocket_t* sock, sizebuf_t* data);
int NET_SendUnreliableMessage(qsocket_t* sock, sizebuf_t* data);
int NET_SendToAll(sizebuf_t* data, int blocktime);
void NET_Close(qsocket_t* sock);

void NET_Slist_f();

} // namespace Net
