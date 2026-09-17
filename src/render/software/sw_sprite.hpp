// sw_sprite.hpp -- Sprite Model Transformation, Frustum Clipping and Rasterization Setup
#pragma once

#include "render/software/sw_local.hpp"

namespace Render {

void R_RotateSprite(float beam_len);
int R_ClipSpriteFace(int nump, clipplane_t* pclipplane);
void R_SetupAndDrawSprite();
mspriteframe_t* R_GetSpriteframe(msprite_t* psprite);
void R_DrawSprite();

} // namespace Render
