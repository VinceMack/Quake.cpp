// view_blend.hpp -- Palette Blending, Color Shifts, and Damage Flashes
#pragma once

#include "core/cvar.hpp"
#include "client/client_types.hpp"

namespace View {

extern cvar_t v_gamma;
extern cvar_t gl_cshiftpercent;
extern std::array<byte, 256> gammatable;

void V_InitBlend();
void V_ParseDamage();
void V_BonusFlash();
void V_SetContentsColor(int contents);
void V_UpdatePalette();

} // namespace View
