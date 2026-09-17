// datagram.cpp -- Datagram Network Driver Implementation
#include "network/datagram.hpp"
#include "host/host.hpp"
#include "platform/system.hpp"
#include "core/filesystem.hpp"
#include "client/input.hpp"
#include "network/net_main.hpp"
#include "core/math.hpp"
#include "ui/screen.hpp"
#include "ui/menu.hpp"
#include "quakedef.hpp"
#include "core/print.hpp"
#include "server/server_types.hpp"
#include "core/cmd.hpp"

#ifdef _WIN32
#include <windows.h>
#ifdef GetMessage
#undef GetMessage
#endif
#ifdef SendMessage
#undef SendMessage
#endif
#else
#include <unistd.h>
#endif

namespace Net {

void WriteControlHeader(sizebuf_t* buf)
{
    *((int*)buf->data) = Common::BigLong(NETFLAG_CTL | (buf->cursize & NETFLAG_LENGTH_MASK));
}

bool ReadControlHeader(int len, int& control)
{
    if (len < static_cast<int>(sizeof(int))) return false;
    net_message.cursize = len;
    Common::MSG_BeginReading();
    control = Common::BigLong(*((int*)net_message.data));
    Common::MSG_ReadLong();
    return (control != -1) && ((static_cast<unsigned int>(control) & (~NETFLAG_LENGTH_MASK)) == NETFLAG_CTL)
        && ((control & NETFLAG_LENGTH_MASK) == len);
}

static_assert(sizeof(int) == 4, "int size check");
static struct {
    unsigned int length;
    unsigned int sequence;
    byte data[MAX_DATAGRAM];
} packetBuffer;

static int myDriverLevel;
static int net_landriverlevel = 0;

static qboolean testInProgress = false, test2InProgress = false;
static int testPollCount, testDriver, testSocket, test2Driver, test2Socket;
static void Test_Poll();
static void Test2_Poll();
static PollProcedure testPollProcedure = { nullptr, 0.0, Test_Poll };
static PollProcedure test2PollProcedure = { nullptr, 0.0, Test2_Poll };

static int SendDatagramPacket(qsocket_t* sock, unsigned int sequence, bool isResend)
{
    NetLanDriver& lan = LANFunc(sock->landriver);
    unsigned int dataLen = (sock->sendMessageLength <= MAX_DATAGRAM) ? sock->sendMessageLength : MAX_DATAGRAM;
    unsigned int eom = (sock->sendMessageLength <= MAX_DATAGRAM) ? NETFLAG_EOM : 0;
    unsigned int packetLen = NET_HEADERSIZE + dataLen;

    packetBuffer.length = Common::BigLong(packetLen | (NETFLAG_DATA | eom));
    packetBuffer.sequence = Common::BigLong(sequence);
    Common::Q_memcpy(packetBuffer.data, sock->sendMessage.data(), dataLen);

    sock->sendNext = false;
    if (lan.Write(sock->socket, (byte*)&packetBuffer, packetLen, &sock->addr) == -1) return -1;
    sock->lastSendTime = net_time;
    if (isResend)
        packetsReSent++;
    else
        packetsSent++;
    return 1;
}

static void PrintStats(qsocket_t* s)
{
    Console::Con_Printf(
        "canSend = %4u   \nsendSeq = %4u   recvSeq = %4u   \n\n", s->canSend, s->sendSequence, s->receiveSequence);
}

static void NET_Stats_f()
{
    if (Cmd::Argc() == 1) {
        Console::Con_Printf("unreliable messages sent   = %i\nunreliable messages recv   = %i\nreliable messages sent  "
                            "   = %i\nreliable messages received = %i\npacketsSent                = %i\npacketsReSent  "
                            "            = %i\npacketsReceived            = %i\nreceivedDuplicateCount     = "
                            "%i\nshortPacketCount           = %i\ndroppedDatagrams           = %i\n",
            unreliableMessagesSent, unreliableMessagesReceived, messagesSent, messagesReceived, packetsSent,
            packetsReSent, packetsReceived, receivedDuplicateCount, shortPacketCount, droppedDatagrams);
    } else if (Common::Q_strcmp(Cmd::Argv(1), "*") == 0) {
        for (qsocket_t* s = net_activeSockets; s; s = s->next) PrintStats(s);
        for (qsocket_t* s = net_freeSockets; s; s = s->next) PrintStats(s);
    } else {
        qsocket_t* s = nullptr;
        for (s = net_activeSockets; s; s = s->next)
            if (Common::Q_strcasecmp(Cmd::Argv(1), s->address) == 0) break;
        if (!s)
            for (s = net_freeSockets; s; s = s->next)
                if (Common::Q_strcasecmp(Cmd::Argv(1), s->address) == 0) break;
        if (s) PrintStats(s);
    }
}

static void Test_Poll()
{
    struct qsockaddr clientaddr;
    int control;
    net_landriverlevel = testDriver;
    NetLanDriver& lan = LANFunc(net_landriverlevel);

    while (1) {
        int len = lan.Read(testSocket, net_message.data, net_message.maxsize, &clientaddr);
        if (!ReadControlHeader(len, control)) break;
        if (Common::MSG_ReadByte() != CCREP_PLAYER_INFO)
            Common::Sys_Error("Unexpected repsonse to Player Info request\n");

        Common::MSG_ReadByte();
        char name[32], address[64];
        Common::Q_strncpy(name, Common::MSG_ReadString(), sizeof(name));
        int colors = Common::MSG_ReadLong(), frags = Common::MSG_ReadLong(), connectTime = Common::MSG_ReadLong();
        Common::Q_strncpy(address, Common::MSG_ReadString(), sizeof(address));
        Console::Con_Printf("%s\n  frags:%3i  colors:%u %u  time:%u\n  %s\n", name, frags, colors >> 4, colors & 0x0f,
            connectTime / 60, address);
    }

    if (--testPollCount)
        SchedulePollProcedure(&testPollProcedure, 0.1);
    else {
        lan.CloseSocket(testSocket);
        testInProgress = false;
    }
}

static void Test_f()
{
    if (testInProgress) return;
    std::string_view host = Cmd::Argv(1);
    int max = MAX_SCOREBOARD;
    struct qsockaddr sendaddr;

    if (!host.empty() && hostCacheCount) {
        for (int n = 0; n < hostCacheCount; n++) {
            if (Common::Q_strcasecmp(host, hostcache[n].name) == 0) {
                if (hostcache[n].driver != myDriverLevel) continue;
                net_landriverlevel = hostcache[n].ldriver;
                max = hostcache[n].maxusers;
                Common::Q_memcpy(&sendaddr, &hostcache[n].addr, sizeof(struct qsockaddr));
                goto JustDoIt;
            }
        }
    }

    for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++) {
        if (!LANFunc(net_landriverlevel).IsInitialized()) continue;
        if (LANFunc(net_landriverlevel).GetAddrFromName(std::string(host.data(), host.length()).c_str(), &sendaddr)
            != -1)
            break;
    }
    if (net_landriverlevel == net_numlandrivers) return;

JustDoIt:
    if ((testSocket = LANFunc(net_landriverlevel).OpenSocket(0)) == -1) return;
    testInProgress = true;
    testPollCount = 20;
    testDriver = net_landriverlevel;

    for (int n = 0; n < max; n++) {
        Common::SZ_Clear(&net_message);
        Common::MSG_WriteLong(&net_message, 0);
        Common::MSG_WriteByte(&net_message, CCREQ_PLAYER_INFO);
        Common::MSG_WriteByte(&net_message, n);
        WriteControlHeader(&net_message);
        LANFunc(testDriver).Write(testSocket, net_message.data, net_message.cursize, &sendaddr);
    }
    Common::SZ_Clear(&net_message);
    SchedulePollProcedure(&testPollProcedure, 0.1);
}

static void Test2_Poll()
{
    struct qsockaddr clientaddr;
    int control;
    net_landriverlevel = test2Driver;
    NetLanDriver& lan = LANFunc(net_landriverlevel);

    int len = lan.Read(test2Socket, net_message.data, net_message.maxsize, &clientaddr);
    if (len < static_cast<int>(sizeof(int))) goto Reschedule;
    if (!ReadControlHeader(len, control) || Common::MSG_ReadByte() != CCREP_RULE_INFO) goto Error;

    char name[256], value[256];
    Common::Q_strncpy(name, Common::MSG_ReadString(), sizeof(name));
    if (name[0] == 0) goto Done;

    Common::Q_strncpy(value, Common::MSG_ReadString(), sizeof(value));
    Console::Con_Printf("%-16.16s  %-16.16s\n", name, value);
    Common::SZ_Clear(&net_message);
    Common::MSG_WriteLong(&net_message, 0);
    Common::MSG_WriteByte(&net_message, CCREQ_RULE_INFO);
    Common::MSG_WriteString(&net_message, name);
    WriteControlHeader(&net_message);
    lan.Write(test2Socket, net_message.data, net_message.cursize, &clientaddr);
    Common::SZ_Clear(&net_message);

Reschedule:
    SchedulePollProcedure(&test2PollProcedure, 0.05);
    return;
Error:
    Console::Con_Printf("Unexpected repsonse to Rule Info request\n");
Done:
    lan.CloseSocket(test2Socket);
    test2InProgress = false;
}

static void Test2_f()
{
    if (test2InProgress) return;
    std::string_view host = Cmd::Argv(1);
    struct qsockaddr sendaddr;

    if (!host.empty() && hostCacheCount) {
        for (int n = 0; n < hostCacheCount; n++) {
            if (Common::Q_strcasecmp(host, hostcache[n].name) == 0) {
                if (hostcache[n].driver != myDriverLevel) continue;
                net_landriverlevel = hostcache[n].ldriver;
                Common::Q_memcpy(&sendaddr, &hostcache[n].addr, sizeof(struct qsockaddr));
                goto JustDoIt;
            }
        }
    }

    for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++) {
        if (!LANFunc(net_landriverlevel).IsInitialized()) continue;
        if (LANFunc(net_landriverlevel).GetAddrFromName(std::string(host.data(), host.length()).c_str(), &sendaddr)
            != -1)
            break;
    }
    if (net_landriverlevel == net_numlandrivers) return;

JustDoIt:
    if ((test2Socket = LANFunc(net_landriverlevel).OpenSocket(0)) == -1) return;
    test2InProgress = true;
    test2Driver = net_landriverlevel;

    Common::SZ_Clear(&net_message);
    Common::MSG_WriteLong(&net_message, 0);
    Common::MSG_WriteByte(&net_message, CCREQ_RULE_INFO);
    Common::MSG_WriteString(&net_message, "");
    WriteControlHeader(&net_message);
    LANFunc(test2Driver).Write(test2Socket, net_message.data, net_message.cursize, &sendaddr);
    Common::SZ_Clear(&net_message);
    SchedulePollProcedure(&test2PollProcedure, 0.05);
}

int DatagramDriver::Init()
{
    myDriverLevel = net_driverlevel;
    Cmd::AddCommand("net_stats", NET_Stats_f);
    if (Common::COM_CheckParm("-nolan")) return -1;
    for (int i = 0; i < net_numlandrivers; i++) {
        int csock = LANFunc(i).Init();
        if (csock == -1) continue;
        LANFunc(i).SetInitialized(true);
        LANFunc(i).SetControlSocket(csock);
    }
    Cmd::AddCommand("test", Test_f);
    Cmd::AddCommand("test2", Test2_f);
    return 0;
}

void DatagramDriver::Shutdown()
{
    for (int i = 0; i < net_numlandrivers; i++) {
        if (LANFunc(i).IsInitialized()) {
            LANFunc(i).Shutdown();
            LANFunc(i).SetInitialized(false);
        }
    }
}

void DatagramDriver::Listen(qboolean state)
{
    for (int i = 0; i < net_numlandrivers; i++) {
        if (LANFunc(i).IsInitialized()) LANFunc(i).Listen(state);
    }
}

void DatagramDriver::Close(qsocket_t* sock)
{
    LANFunc(sock->landriver).CloseSocket(sock->socket);
}

int DatagramDriver::SendMessage(qsocket_t* sock, sizebuf_t* data)
{
    Common::Q_memcpy(sock->sendMessage.data(), data->data, data->cursize);
    sock->sendMessageLength = data->cursize;
    sock->canSend = false;
    return SendDatagramPacket(sock, sock->sendSequence++, false);
}

int DatagramDriver::SendUnreliableMessage(qsocket_t* sock, sizebuf_t* data)
{
    NetLanDriver& lan = LANFunc(sock->landriver);
    int packetLen = NET_HEADERSIZE + data->cursize;
    packetBuffer.length = Common::BigLong(packetLen | NETFLAG_UNRELIABLE);
    packetBuffer.sequence = Common::BigLong(sock->unreliableSendSequence++);
    Common::Q_memcpy(packetBuffer.data, data->data, data->cursize);
    if (lan.Write(sock->socket, (byte*)&packetBuffer, packetLen, &sock->addr) == -1) return -1;
    packetsSent++;
    return 1;
}

qboolean DatagramDriver::CanSendMessage(qsocket_t* sock)
{
    if (sock->sendNext) SendDatagramPacket(sock, sock->sendSequence++, false);
    return sock->canSend;
}

int DatagramDriver::GetMessage(qsocket_t* sock)
{
    NetLanDriver& lan = LANFunc(sock->landriver);
    if (!sock->canSend && (net_time - sock->lastSendTime) > 1.0) SendDatagramPacket(sock, sock->sendSequence - 1, true);

    int ret = 0;
    struct qsockaddr readaddr;
    while (1) {
        unsigned int length = lan.Read(sock->socket, (byte*)&packetBuffer, NET_DATAGRAMSIZE, &readaddr);
        if (length == 0) break;
        if (static_cast<int>(length) == -1) return -1;
        if (lan.AddrCompare(&readaddr, &sock->addr) != 0) continue;
        if (length < NET_HEADERSIZE) {
            shortPacketCount++;
            continue;
        }

        length = Common::BigLong(packetBuffer.length);
        unsigned int flags = length & (~NETFLAG_LENGTH_MASK);
        length &= NETFLAG_LENGTH_MASK;
        if (flags & NETFLAG_CTL) continue;

        unsigned int sequence = Common::BigLong(packetBuffer.sequence);
        packetsReceived++;
        if (flags & NETFLAG_UNRELIABLE) {
            if (sequence < sock->unreliableReceiveSequence) break;
            if (sequence != sock->unreliableReceiveSequence)
                droppedDatagrams += (sequence - sock->unreliableReceiveSequence);
            sock->unreliableReceiveSequence = sequence + 1;
            length -= NET_HEADERSIZE;
            Common::SZ_Clear(&net_message);
            Common::SZ_Write(&net_message, packetBuffer.data, length);
            ret = 2;
            break;
        }

        if (flags & NETFLAG_ACK) {
            if (sequence != (sock->sendSequence - 1)) continue;
            if (sequence == sock->ackSequence)
                sock->ackSequence++;
            else
                continue;
            sock->sendMessageLength -= MAX_DATAGRAM;
            if (sock->sendMessageLength > 0) {
                Common::Q_memcpy(
                    sock->sendMessage.data(), sock->sendMessage.data() + MAX_DATAGRAM, sock->sendMessageLength);
                sock->sendNext = true;
            } else {
                sock->sendMessageLength = 0;
                sock->canSend = true;
            }
            continue;
        }

        if (flags & NETFLAG_DATA) {
            packetBuffer.length = Common::BigLong(NET_HEADERSIZE | NETFLAG_ACK);
            packetBuffer.sequence = Common::BigLong(sequence);
            lan.Write(sock->socket, (byte*)&packetBuffer, NET_HEADERSIZE, &readaddr);
            if (sequence != sock->receiveSequence) {
                receivedDuplicateCount++;
                continue;
            }
            sock->receiveSequence++;
            length -= NET_HEADERSIZE;
            if (flags & NETFLAG_EOM) {
                Common::SZ_Clear(&net_message);
                Common::SZ_Write(&net_message, sock->receiveMessage.data(), sock->receiveMessageLength);
                Common::SZ_Write(&net_message, packetBuffer.data, length);
                sock->receiveMessageLength = 0;
                ret = 1;
                break;
            }
            Common::Q_memcpy(sock->receiveMessage.data() + sock->receiveMessageLength, packetBuffer.data, length);
            sock->receiveMessageLength += length;
            continue;
        }
    }
    if (sock->sendNext) SendDatagramPacket(sock, sock->sendSequence++, false);
    return ret;
}

qsocket_t* DatagramDriver::CheckNewConnections()
{
    qsocket_t* ret = nullptr;
    for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++) {
        if (!LANFunc(net_landriverlevel).IsInitialized()) continue;
        NetLanDriver& lan = LANFunc(net_landriverlevel);
        int acceptsock = lan.CheckNewConnections();
        if (acceptsock == -1) continue;

        Common::SZ_Clear(&net_message);
        struct qsockaddr clientaddr;
        int len = lan.Read(acceptsock, net_message.data, net_message.maxsize, &clientaddr);
        int control;
        if (!ReadControlHeader(len, control)) continue;

        auto SendReply = [&](byte repCmd, auto&& writePayload) {
            Common::SZ_Clear(&net_message);
            Common::MSG_WriteLong(&net_message, 0);
            Common::MSG_WriteByte(&net_message, repCmd);
            writePayload();
            WriteControlHeader(&net_message);
            lan.Write(acceptsock, net_message.data, net_message.cursize, &clientaddr);
            Common::SZ_Clear(&net_message);
        };

        int command = Common::MSG_ReadByte();
        if (command == CCREQ_SERVER_INFO) {
            if (Common::Q_strcmp(Common::MSG_ReadString(), "QUAKE") != 0) continue;
            struct qsockaddr newaddr;
            lan.GetSocketAddr(acceptsock, &newaddr);
            SendReply(CCREP_SERVER_INFO, [&]() {
                Common::MSG_WriteString(&net_message, lan.AddrToString(&newaddr));
                Common::MSG_WriteString(&net_message, hostname.string.c_str());
                Common::MSG_WriteString(&net_message, Server::sv.name.data());
                Common::MSG_WriteByte(&net_message, net_activeconnections);
                Common::MSG_WriteByte(&net_message, Server::svs.maxclients);
                Common::MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
            });
            continue;
        }

        if (command == CCREQ_PLAYER_INFO) {
            int pNum = Common::MSG_ReadByte(), activeNum = -1, cNum = 0;
            client_t* client = Server::svs.clients;
            for (; cNum < Server::svs.maxclients; cNum++, client++)
                if (client->active && ++activeNum == pNum) break;
            if (cNum == Server::svs.maxclients) continue;
            SendReply(CCREP_PLAYER_INFO, [&]() {
                Common::MSG_WriteByte(&net_message, pNum);
                Common::MSG_WriteString(&net_message, client->name.data());
                Common::MSG_WriteLong(&net_message, client->colors);
                Common::MSG_WriteLong(&net_message, (int)client->edict->v.frags);
                Common::MSG_WriteLong(&net_message, (int)(net_time - client->netconnection->connecttime));
                Common::MSG_WriteString(&net_message, client->netconnection->address);
            });
            continue;
        }

        if (command == CCREQ_RULE_INFO) {
            char* pName = Common::MSG_ReadString();
            cvar_t* var = *pName ? Cvar::FindVar(pName) : Cvar::state.vars;
            if (*pName && var) var = var->next;
            while (var && !var->server) var = var->next;
            SendReply(CCREP_RULE_INFO, [&]() {
                if (var) {
                    Common::MSG_WriteString(&net_message, var->name.c_str());
                    Common::MSG_WriteString(&net_message, var->string.c_str());
                }
            });
            continue;
        }

        if (command != CCREQ_CONNECT || Common::Q_strcmp(Common::MSG_ReadString(), "QUAKE") != 0) continue;

        if (Common::MSG_ReadByte() != NET_PROTOCOL_VERSION) {
            SendReply(CCREP_REJECT, [&]() { Common::MSG_WriteString(&net_message, "Incompatible version.\n"); });
            continue;
        }

        for (qsocket_t* s = net_activeSockets; s; s = s->next) {
            if (s->driver != net_driverlevel) continue;
            if (lan.AddrCompare(&clientaddr, &s->addr) == 0) {
                if (net_time - s->connecttime < 2.0) {
                    SendReply(CCREP_ACCEPT, [&]() {
                        struct qsockaddr newaddr;
                        lan.GetSocketAddr(s->socket, &newaddr);
                        Common::MSG_WriteLong(&net_message, lan.GetSocketPort(&newaddr));
                    });
                    return nullptr;
                }
                NET_Close(s);
                break;
            }
        }

        qsocket_t* sock = NET_NewQSocket();
        if (!sock) {
            SendReply(CCREP_REJECT, [&]() { Common::MSG_WriteString(&net_message, "Server is full.\n"); });
            continue;
        }

        int newsock = lan.OpenSocket(0);
        if (newsock == -1) {
            NET_FreeQSocket(sock);
            continue;
        }

        if (lan.Connect(newsock, &clientaddr) == -1) {
            lan.CloseSocket(newsock);
            NET_FreeQSocket(sock);
            continue;
        }

        sock->socket = newsock;
        sock->landriver = net_landriverlevel;
        sock->addr = clientaddr;
        Common::Q_strncpy(sock->address, lan.AddrToString(&clientaddr), sizeof(sock->address));
        SendReply(CCREP_ACCEPT, [&]() {
            struct qsockaddr newaddr;
            lan.GetSocketAddr(newsock, &newaddr);
            Common::MSG_WriteLong(&net_message, lan.GetSocketPort(&newaddr));
        });
        ret = sock;
        break;
    }
    return ret;
}

void DatagramDriver::SearchForHosts(qboolean xmit)
{
    for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++) {
        if (hostCacheCount == HOSTCACHESIZE) break;
        if (!LANFunc(net_landriverlevel).IsInitialized()) continue;
        NetLanDriver& lan = LANFunc(net_landriverlevel);

        struct qsockaddr readaddr, myaddr;
        lan.GetSocketAddr(lan.GetControlSocket(), &myaddr);
        if (xmit) {
            Common::SZ_Clear(&net_message);
            Common::MSG_WriteLong(&net_message, 0);
            Common::MSG_WriteByte(&net_message, CCREQ_SERVER_INFO);
            Common::MSG_WriteString(&net_message, "QUAKE");
            Common::MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
            WriteControlHeader(&net_message);
            lan.Broadcast(lan.GetControlSocket(), net_message.data, net_message.cursize);
            Common::SZ_Clear(&net_message);
        }

        int ret;
        while ((ret = lan.Read(lan.GetControlSocket(), net_message.data, net_message.maxsize, &readaddr)) > 0) {
            int control;
            if (!ReadControlHeader(ret, control)) continue;
            if (lan.AddrCompare(&readaddr, &myaddr) >= 0 || hostCacheCount == HOSTCACHESIZE) continue;
            if (Common::MSG_ReadByte() != CCREP_SERVER_INFO) continue;

            lan.GetAddrFromName(Common::MSG_ReadString(), &readaddr);
            int n;
            for (n = 0; n < hostCacheCount; n++)
                if (lan.AddrCompare(&readaddr, &hostcache[n].addr) == 0) break;
            if (n < hostCacheCount) continue;

            hostCacheCount++;
            Common::Q_strncpy(hostcache[n].name, Common::MSG_ReadString(), sizeof(hostcache[n].name));
            Common::Q_strncpy(hostcache[n].map, Common::MSG_ReadString(), sizeof(hostcache[n].map));
            hostcache[n].users = Common::MSG_ReadByte();
            hostcache[n].maxusers = Common::MSG_ReadByte();
            if (Common::MSG_ReadByte() != NET_PROTOCOL_VERSION) {
                Common::Q_strcpy(hostcache[n].cname, hostcache[n].name);
                hostcache[n].cname[14] = 0;
                Common::Q_strcpy(hostcache[n].name, "*");
                Common::Q_strcat(hostcache[n].name, hostcache[n].cname);
            }
            Common::Q_memcpy(&hostcache[n].addr, &readaddr, sizeof(struct qsockaddr));
            hostcache[n].driver = net_driverlevel;
            hostcache[n].ldriver = net_landriverlevel;
            Common::Q_strncpy(hostcache[n].cname, lan.AddrToString(&readaddr), sizeof(hostcache[n].cname));

            for (int i = 0; i < hostCacheCount; i++) {
                if (i == n) continue;
                if (Common::Q_strcasecmp(hostcache[n].name, hostcache[i].name) == 0) {
                    int len = Common::Q_strlen(hostcache[n].name);
                    if (len < 15 && hostcache[n].name[len - 1] > '8') {
                        hostcache[n].name[len] = '0';
                        hostcache[n].name[len + 1] = 0;
                    } else {
                        hostcache[n].name[len - 1]++;
                    }
                    i = -1;
                }
            }
        }
    }
}

qsocket_t* DatagramDriver::Connect(const char* host)
{
    for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++) {
        if (!LANFunc(net_landriverlevel).IsInitialized()) continue;
        NetLanDriver& lan = LANFunc(net_landriverlevel);

        struct qsockaddr sendaddr, readaddr;
        Common::Sys_Printf("_Datagram_Connect: connecting to '%s'...\n", host);
        if (lan.GetAddrFromName(host, &sendaddr) == -1) continue;

        int newsock = lan.OpenSocket(0);
        if (newsock == -1) continue;
        qsocket_t* sock = NET_NewQSocket();
        if (!sock) {
            lan.CloseSocket(newsock);
            return nullptr;
        }

        sock->socket = newsock;
        sock->landriver = net_landriverlevel;
        if (lan.Connect(newsock, &sendaddr) == -1) {
            NET_FreeQSocket(sock);
            lan.CloseSocket(newsock);
            continue;
        }

        Console::Con_Printf("trying...\n");
        Screen::GetScreenSystem().UpdateScreen();
        int ret = 0;
        const char* reason = nullptr;

        for (int reps = 0; reps < 3; reps++) {
            double start_time = SetNetTime();
            Common::SZ_Clear(&net_message);
            Common::MSG_WriteLong(&net_message, 0);
            Common::MSG_WriteByte(&net_message, CCREQ_CONNECT);
            Common::MSG_WriteString(&net_message, "QUAKE");
            Common::MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
            WriteControlHeader(&net_message);
            lan.Write(newsock, net_message.data, net_message.cursize, &sendaddr);
            Common::SZ_Clear(&net_message);

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
            Console::Con_Printf("still trying...\n");
            Screen::GetScreenSystem().UpdateScreen();
        }

        if (ret <= 0) {
            reason = (ret == 0) ? "No Response" : "Network Error";
            Console::Con_Printf("%s\n", reason);
            Menu::m_return_reason = reason;
        } else {
            ret = Common::MSG_ReadByte();
            if (ret == CCREP_REJECT) {
                reason = Common::MSG_ReadString();
                Console::Con_Printf(reason);
                Menu::m_return_reason = reason;
            } else if (ret == CCREP_ACCEPT) {
                Common::Q_memcpy(&sock->addr, &sendaddr, sizeof(struct qsockaddr));
                lan.SetSocketPort(&sock->addr, Common::MSG_ReadLong());
                lan.GetNameFromAddr(&sendaddr, sock->address);
                Console::Con_Printf("Connection accepted\n");
                sock->lastMessageTime = SetNetTime();
                if (lan.Connect(newsock, &sock->addr) != -1) {
                    Menu::m_return_onerror = false;
                    return sock;
                }
                reason = "Connect to Game failed";
                Console::Con_Printf("%s\n", reason);
                Menu::m_return_reason = reason;
            } else {
                reason = "Bad Response";
                Console::Con_Printf("%s\n", reason);
                Menu::m_return_reason = reason;
            }
        }

        NET_FreeQSocket(sock);
        lan.CloseSocket(newsock);
        if (Menu::m_return_onerror) {
            Keys::key_dest = Keys::key_menu;
            Menu::m_state = Menu::m_return_state;
            Menu::m_return_onerror = false;
        }
        return nullptr;
    }
    return nullptr;
}

} // namespace Net
