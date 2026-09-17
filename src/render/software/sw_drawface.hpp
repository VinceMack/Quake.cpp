// sw_drawface.hpp -- Polygon and Bmodel Face Clipping and Edge Emission
#pragma once

#include "render/software/sw_local.hpp"

namespace Render {

void R_EmitEdge(mvertex_t* pv0, mvertex_t* pv1);
void R_ClipEdge(mvertex_t* pv0, mvertex_t* pv1, clipplane_t* clip);
void R_EmitCachedEdge();
void R_RenderFace(msurface_t* fa, int clipflags);
void R_RenderBmodelFace(bedge_t* pedges, msurface_t* psurf);
void R_RenderPoly(msurface_t* fa, int clipflags);

} // namespace Render
