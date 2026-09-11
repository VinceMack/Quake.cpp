// chase.hpp -- Chase Camera Subsystem
#pragma once

#include "core/cvar.hpp"

namespace Client {

extern cvar_t chase_active;

void Chase_Init();
void Chase_Update();

} // namespace Client
