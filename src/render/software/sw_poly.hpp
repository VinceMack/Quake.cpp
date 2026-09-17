// sw_poly.hpp -- Alias Model Triangle (Polyset) Rasterization
#pragma once

#include "render/software/sw_local.hpp"

namespace Render {

void D_PolysetDraw();
void D_PolysetDrawFinalVerts(finalvert_t* fv, int num_verts);
void D_DrawSubdiv();
void D_DrawNonSubdiv();
void D_PolysetUpdateTables();

} // namespace Render
