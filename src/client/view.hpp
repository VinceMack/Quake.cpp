// view.hpp -- 3D View Setup, Camera Orientation, Damage/Palette Blending
#pragma once

#include "core/cvar.hpp"
#include "client/client_types.hpp"

namespace View {

extern cvar_t v_gamma;
extern cvar_t lcd_x, lcd_yaw;
extern eastl::array<byte, 256> gammatable;

void V_Init();
void V_RenderView();
void V_ParseDamage();
void V_StopPitchDrift();
void V_StartPitchDrift();
void V_BonusFlash();
void V_UpdatePalette();
void V_SetContentsColor(int contents);

extern float v_dmg_time, v_dmg_roll, v_dmg_pitch;

} // namespace View
