// sw_light.hpp -- BSP Lighting, Style Animation, and Dynamic Lights
#pragma once

#include "quakedef.hpp"
#include "render/software/sw_local.hpp"

namespace Render {

void R_AnimateLight();
void R_MarkLights(dlight_t* light, int bit, mnode_t* node);
void R_PushDlights();
int RecursiveLightPoint(mnode_t* node, const Vector3& start, const Vector3& end);
int R_LightPoint(const Vector3& p);

} // namespace Render
