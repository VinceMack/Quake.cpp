// sw_frame.hpp -- Software Renderer Frame Setup, Viewport & Timing
#pragma once

#include "render/software/sw_local.hpp"

namespace Render {

void TransformVector(const Vector3& in, Vector3& out);
void R_TransformFrustum();
void R_SetUpFrustumIndexes();
void R_CheckVariables();
void R_SetupFrame();
void R_SetVrect(vrect_t* pvrectin, vrect_t* pvrect, int lineadj);
void R_ViewChanged(vrect_t* pvrect, int lineadj, float aspect);

void R_TimeRefresh_f();
void R_LineGraph(int x, int y, int h);
void R_TimeGraph();
void R_PrintAliasStats();
void R_PrintTimes();
void R_PrintDSpeeds();

} // namespace Render
