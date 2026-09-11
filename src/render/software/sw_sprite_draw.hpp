// sw_sprite_draw.hpp -- Software Sprite and Particle Rasterization
#pragma once

#include "render/software/sw_local.hpp"

namespace Render {

void D_SpriteDrawSpans(sspan_t* pspan);
void D_SpriteScanLeftEdge();
void D_SpriteScanRightEdge();
void D_SpriteCalculateGradients();
void D_DrawSprite();

void D_StartParticles();
void D_EndParticles();
void D_DrawParticle(particle_t* pparticle);

} // namespace Render
