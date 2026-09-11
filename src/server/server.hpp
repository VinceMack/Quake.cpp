// server.hpp -- Server Lifecycle and Map Spawning
#pragma once

#include "server/server_types.hpp"

namespace Server {

void SV_Init();
void SV_SpawnServer(const char* server);
void SV_SaveSpawnparms();
void SV_SendReconnect();
void SV_CreateBaseline();
[[nodiscard]] int SV_ModelIndex(const char* name);

} // namespace Server
