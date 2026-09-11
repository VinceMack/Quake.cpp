// sv_send.hpp -- Server Network Transmission, Client Messaging, and Client Prediction/Think
#pragma once

#include "server/server_types.hpp"

namespace Server {

void SV_StartParticle(const Vector3& org, const Vector3& dir, int color, int count);
void SV_StartSound(edict_t* entity, int channel, const char* sample, int vol, float attenuation);
void SV_SendServerinfo(client_t* client);
void SV_ConnectClient(int clientnum);
void SV_CheckForNewClients();
void SV_AddToFatPVS(const Vector3& org, mnode_t* node);
byte* SV_FatPVS(const Vector3& org);
void SV_WriteEntitiesToClient(edict_t* clent, sizebuf_t* msg);
void SV_CleanupEnts();
void SV_WriteClientdataToMessage(edict_t* ent, sizebuf_t* msg);
qboolean SV_SendClientDatagram(client_t* client);
void SV_UpdateToReliableMessages();
void SV_SendNop(client_t* client);
void SV_SendClientMessages();

void SV_SetIdealPitch();
void SV_ClientThink();
void SV_RunClients();
void SV_ClientPrintf(const char* fmt, ...);
void SV_BroadcastPrintf(const char* fmt, ...);
void SV_DropClient(bool crash);

} // namespace Server
