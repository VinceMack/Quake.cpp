// sw_alias_clip.hpp -- Alias Model (MDL) Polygon Frustum and Z-Clipping
#pragma once

#include "quakedef.hpp"
#include "render/software/sw_local.hpp"

namespace Render {

void R_AliasClipTriangle(mtriangle_t* ptri);

} // namespace Render
