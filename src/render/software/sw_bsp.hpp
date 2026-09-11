// sw_bsp.hpp -- BSP Traversal, Bmodel Rotation and Polygon Rasterization Setup
#pragma once

#include "quakedef.hpp"
#include "render/software/sw_local.hpp"

namespace Render {

void R_DrawCulledPolys();
void R_ZDrawSubmodelPolys(model_t* pmodel);
void R_EntityRotate(Vector3& vec);
void R_RotateBmodel();
void R_RecursiveClipBPoly(bedge_t* pedges, mnode_t* pnode, msurface_t* psurf);
void R_DrawSolidClippedSubmodelPolygons(model_t* pmodel);
void R_DrawSubmodelPolygons(model_t* pmodel, int clipflags);
void R_RecursiveWorldNode(mnode_t* node, int clipflags);
void R_RenderWorld();

} // namespace Render
