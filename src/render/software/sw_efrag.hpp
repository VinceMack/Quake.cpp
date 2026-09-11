// sw_efrag.hpp -- Entity Fragment Splitting and BSP Linking
#pragma once

#include "render/software/sw_local.hpp"

namespace Render {

void R_RemoveEfrags(entity_t* ent);
void R_SplitEntityOnNode(mnode_t* node);
void R_SplitEntityOnNode2(mnode_t* node);
void R_AddEfrags(entity_t* ent);
void R_StoreEfrags(efrag_t** ppefrag);

} // namespace Render
