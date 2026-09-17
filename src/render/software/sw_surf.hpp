// sw_surf.hpp -- Surface Rasterization, Lightmaps, Dynamic Lights, and Surface Cache
#pragma once

#include "render/software/sw_local.hpp"

namespace Render {

void R_DrawSurfaceBlock8_mip0();
void R_DrawSurfaceBlock8_mip1();
void R_DrawSurfaceBlock8_mip2();
void R_DrawSurfaceBlock8_mip3();

void R_AddDynamicLights();
void R_BuildLightMap();
texture_t* R_TextureAnimation(texture_t* base);
void R_DrawSurface();

int D_SurfaceCacheForRes(int width, int height);
void D_CheckCacheGuard();
void D_ClearCacheGuard();
void D_InitCaches(void* buffer, int size);
void D_FlushCaches();
surfcache_t* D_SCAlloc(int width, int size);
surfcache_t* D_CacheSurface(msurface_t* surface, int mip_level);

} // namespace Render
