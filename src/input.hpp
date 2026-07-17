// input.h -- external (non-keyboard) input devices
#pragma once

struct usercmd_t;

namespace Input {

void IN_Init();

void IN_Shutdown();

void IN_Commands();

void IN_Move(usercmd_t* cmd);

void IN_ClearStates();

} // namespace Input

