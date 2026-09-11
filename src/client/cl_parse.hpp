// cl_parse.hpp -- Server Message Parsing and Network Packet Decoding
#pragma once

#include "client/client_types.hpp"

namespace Client {

void CL_ParseServerMessage();
void CL_ParseServerInfo();
void CL_ParseUpdate(int bits);
void CL_ParseBaseline(entity_t* ent);
void CL_ParseClientdata(int bits);
void CL_ParseStatic();
void CL_ParseStaticSound();
void CL_ParseStartSoundPacket();
void CL_KeepaliveMessage();
void CL_NewTranslation(int slot);

} // namespace Client
