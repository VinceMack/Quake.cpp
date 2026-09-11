// sw_raster.hpp -- Software Span and Surface Rasterization
#pragma once

#include "render/software/sw_local.hpp"

namespace Render {

void D_Init();
void D_TurnZOn();
void D_SetupFrame();
void D_UpdateRects(vrect_t* prect);
void D_ViewChanged();
void D_DrawPoly();
int D_MipLevelForScale(float scale);
void D_DrawSolidSurface(surf_t* surf, int color);
void D_CalcGradients(msurface_t* pface);
void D_DrawSurfaces();
void D_DrawSpans8(espan_t* pspan);
void D_DrawZSpans(espan_t* pspan);

} // namespace Render
