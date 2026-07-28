// view.hpp -- view rendering declarations (player eye, palette, gamma)
#pragma once

#include "cvar.hpp"
#include <EASTL/array.h>

namespace View {

extern cvar_t v_gamma;
extern eastl::array<byte, 256> gammatable;
extern cvar_t lcd_x;

void V_Init();
void V_RenderView();
[[nodiscard]] float V_CalcRoll(const Vector3& angles, const Vector3& velocity);
void V_UpdatePalette();
void V_StartPitchDrift();
void V_StopPitchDrift();
void V_Register();
void V_ParseDamage();
void V_SetContentsColor(int contents);

} // namespace View

