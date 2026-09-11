// sw_vid.hpp -- SDL2 Video Output and Surface Management
#pragma once

#include "core/types.hpp"
#include "render/render_types.hpp"
#include <SDL.h>

namespace Vid {

extern viddef_t vid;
extern unsigned short d_8to16table[256];
extern int VGA_width, VGA_height, VGA_rowbytes;
extern byte* VGA_pagebase;

void VID_HandlePause();
void VID_SetPalette(unsigned char* palette);
inline void VID_ShiftPalette(unsigned char* palette) { VID_SetPalette(palette); }
void VID_Init(unsigned char* palette);
void VID_Shutdown();
void VID_Update(vrect_t* rects);

void D_BeginDirectRect(int x, int y, byte* pbitmap, int width, int height);
void D_EndDirectRect(int x, int y, int width, int height);

[[nodiscard]] SDL_Window* GetWindow() noexcept;

} // namespace Vid

using Vid::VID_SetPalette;
using Vid::VID_ShiftPalette;
