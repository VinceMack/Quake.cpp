// sw_sky.hpp -- Dynamic Scrolling Sky Surface Generation
#pragma once

#include "render/software/sw_local.hpp"

namespace Render {

extern int r_skymade;

void R_InitSky(texture_t* mt);
void R_MakeSky();
void R_SetSkyFrame();

} // namespace Render
