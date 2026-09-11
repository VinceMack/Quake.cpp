// cl_demo.hpp -- Client Demo Recording and Playback Subsystem
#pragma once

#include "client/client_types.hpp"

namespace Client {

void CL_StopPlayback();
void CL_WriteDemoMessage();
[[nodiscard]] int CL_GetMessage();
void CL_Stop_f();
void CL_Record_f();
void CL_PlayDemo_f();
void CL_FinishTimeDemo();
void CL_TimeDemo_f();
void CL_NextDemo();

} // namespace Client
