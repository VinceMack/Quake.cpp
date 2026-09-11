// cl_main.hpp -- Client Subsystem Lifecycle, Connection, and Frame Orchestration
#pragma once

#include "client/client_types.hpp"

namespace Client {

[[nodiscard]] entity_t* CL_EntityNum(int num);
void CL_Init();
void CL_EstablishConnection(const char* host);
void CL_SignonReply();
void CL_ClearState();
void CL_Disconnect();
void CL_Disconnect_f();
int CL_ReadFromServer();
void CL_SendCmd();
void CL_RelinkEntities();
void CL_PrintEntities_f();

} // namespace Client
