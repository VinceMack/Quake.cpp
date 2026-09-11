// cl_input.hpp -- Client Movement Input and Command Generation
#pragma once

#include "client/client_types.hpp"

namespace Client {

extern kbutton_t in_mlook, in_klook, in_left, in_right, in_forward, in_back;
extern kbutton_t in_lookup, in_lookdown, in_moveleft, in_moveright;
extern kbutton_t in_strafe, in_speed, in_use, in_jump, in_attack, in_up, in_down;
extern int in_impulse;

void CL_InitInput();
void CL_AdjustAngles();
void CL_BaseMove(usercmd_t* cmd);
void CL_SendMove(usercmd_t* cmd);
[[nodiscard]] float CL_KeyState(kbutton_t* key);

} // namespace Client
