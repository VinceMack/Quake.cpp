// sw_edge.hpp -- Edge List Generation, Insertion, and Active Edge Scanline Rasterization
#pragma once

#include "quakedef.hpp"
#include "render/software/sw_local.hpp"

namespace Render {

void R_BeginEdgeFrame();
void R_InsertNewEdges(edge_t* edgestoadd, edge_t* edgelist);
void R_RemoveEdges(edge_t* pedge);
void R_StepActiveU(edge_t* pedge);
void R_CleanupSpan();
void R_LeadingEdge(edge_t* edge);
void R_LeadingEdgeBackwards(edge_t* edge);
void R_TrailingEdge(surf_t* surf, edge_t* edge);
void R_GenerateSpans();
void R_GenerateSpansBackward();
void R_ScanEdges();

} // namespace Render
