// sw_alias.hpp -- Alias Model (MDL) Transformation, Lighting, and Skin Setup
#pragma once

#include "quakedef.hpp"
#include "render/software/sw_local.hpp"

namespace Render {

void R_InitVertexNormals();
bool R_AliasCheckBBox();
void R_AliasTransformVector(const float* in, float* out);
void R_AliasPreparePoints();
void R_AliasSetUpTransform(int trivial_accept);
void R_AliasTransformFinalVert(finalvert_t* fv, auxvert_t* av, trivertx_t* pverts, stvert_t* pstverts);
void R_AliasTransformAndProjectFinalVerts(finalvert_t* fv, stvert_t* pstverts);
void R_AliasProjectFinalVert(finalvert_t* fv, auxvert_t* av);
void R_AliasPrepareUnclippedPoints();
void R_AliasSetupSkin();
void R_AliasSetupLighting(alight_t* plighting);
void R_AliasSetupFrame();
void R_AliasDrawModel(alight_t* plighting);

} // namespace Render
