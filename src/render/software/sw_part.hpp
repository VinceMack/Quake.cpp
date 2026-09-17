// sw_part.hpp -- Particle System: Simulations, Explosions, Trails, and Rendering
#pragma once

#include "render/software/sw_local.hpp"

namespace Render {

void R_InitParticles();
void R_ClearParticles();
void R_DrawParticles();
void R_EntityParticles(entity_t* ent);
void R_ReadPointFile_f();
void R_ParseParticleEffect();
void R_ParticleExplosion(const Vector3& org);
void R_ParticleExplosion2(const Vector3& org, int colorStart, int colorLength);
void R_BlobExplosion(const Vector3& org);
void R_RunParticleEffect(const Vector3& org, const Vector3& dir, int color, int count);
void R_LavaSplash(const Vector3& org);
void R_TeleportSplash(const Vector3& org);
void R_RocketTrail(Vector3 start, const Vector3& end, int type);

} // namespace Render
