// d_local.h:  private rasterization driver defs
#pragma once

#include "r_shared.hpp"

//
// TODO: fine-tune this; it's based on providing some overage even if there
// is a 2k-wide scan, with subdivision every 8, for 256 spans of 12 bytes each
//
#define SCANBUFFERPAD 0x1000

#define R_SKY_SMASK 0x007F0000
#define R_SKY_TMASK 0x007F0000

#define DS_SPAN_LIST_END -128

#define SURFCACHE_SIZE_AT_320X200 600 * 1024

typedef struct surfcache_s {
    struct surfcache_s* next;
    struct surfcache_s** owner; // NULL is an empty chunk of memory
    int lightadj[MAXLIGHTMAPS]; // checked for strobe flush
    int dlight;
    int size; // including header
    unsigned width;
    unsigned height; // DEBUG only needed for debug
    float mipscale;
    texture_t* texture; // checked for animating textures
    byte data[4];              // width*height elements
} surfcache_t;

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct sspan_s {
    int u, v, count;
} sspan_t;



namespace Render {

void D_DrawSpans8(espan_t* pspans);
void D_DrawSpans16(espan_t* pspans);
void D_DrawZSpans(espan_t* pspans);
void Turbulent8(espan_t* pspan);
void D_SpriteDrawSpans(sspan_t* pspan);

void D_DrawSkyScans8(espan_t* pspan);
void D_DrawSkyScans16(espan_t* pspan);

void R_ShowSubDiv(void);
surfcache_t* D_CacheSurface(msurface_t* surface, int mip_level);

extern short* d_pzbuffer;

} // namespace Render
