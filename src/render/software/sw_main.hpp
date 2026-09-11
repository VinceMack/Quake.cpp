// sw_main.hpp -- Software Renderer Pipeline Orchestration
#pragma once

#include "render/software/sw_local.hpp"

namespace Render {

void R_InitTextures();
void R_Init();
void R_NewMap();

void R_MarkLeaves();
void R_DrawEntitiesOnList();
void R_DrawViewModel();
int R_BmodelCheckBBox(model_t* clmodel, float* minmaxs);
void R_DrawBEntitiesOnList();
void R_EdgeDrawing();
void R_RenderView_();
void R_RenderView();

} // namespace Render
