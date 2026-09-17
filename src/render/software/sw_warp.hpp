// sw_warp.hpp -- Underwater Turbulent Warp and Sky Span Software Rasterizers
#pragma once

#include "render/software/sw_local.hpp"

namespace Render {

void R_InitTurb();
void D_DrawTurbulent8Span();
void D_WarpScreen();
void Turbulent8(espan_t* pspan);
void D_Sky_uv_To_st(int u, int v, fixed16_t* s, fixed16_t* t);
void D_DrawSkyScans8(espan_t* pspan);

} // namespace Render
