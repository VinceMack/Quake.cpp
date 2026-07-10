// view.h -- view rendering declarations (player eye, palette, gamma)
#pragma once

#include "cvar.hpp"

namespace View {

extern cvar_t v_gamma;

extern byte gammatable[256];
extern byte ramps[3][256];
extern float v_blend[4];

extern cvar_t lcd_x;

void V_Init(void);
void V_RenderView(void);
float V_CalcRoll(const Vector3& angles, const Vector3& velocity);
void V_UpdatePalette(void);
void V_StartPitchDrift(void);
void V_StopPitchDrift(void);
void V_Register(void);
void V_ParseDamage(void);
void V_SetContentsColor(int contents);

} // namespace View
