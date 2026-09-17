// sw_vid.cpp -- SDL2 Video Output and Surface Management Implementation
#include "quakedef.hpp"
#include "render/software/sw_vid.hpp"

#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace Common;
using namespace Host;
using namespace Render;

namespace Vid {

viddef_t vid;

void VID_HandlePause()
{
}

#define BASEWIDTH (320 * 2)
#define BASEHEIGHT (200 * 2)

static SDL_Window* window = nullptr;
static SDL_Surface* screen = nullptr;
static std::vector<byte> video_storage; // z-buffer followed by the surface cache

SDL_Window* GetWindow() noexcept
{
    return window;
}

void VID_SetPalette(unsigned char* palette)
{
    int i;
    SDL_Color colors[256];
    for (i = 0; i < 256; ++i) {
        colors[i].r = *palette++;
        colors[i].g = *palette++;
        colors[i].b = *palette++;
        colors[i].a = 255;
    }
    if (screen && screen->format && screen->format->palette) {
        SDL_SetPaletteColors(screen->format->palette, colors, 0, 256);
    }
}

void VID_Init(unsigned char* palette)
{
    int pnum;
    byte* cache;
    int cachesize;
    Uint32 flags;
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        Sys_Error("VID: Couldn't load SDL Video: %s", SDL_GetError());
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
            fprintf(stderr, "Warning: VID: Couldn't load SDL Audio: %s\n", SDL_GetError());
        }
    }
    vid.width = BASEWIDTH;
    vid.height = BASEHEIGHT;
    vid.maxwarpwidth = WARP_WIDTH;
    vid.maxwarpheight = WARP_HEIGHT;
    if ((pnum = COM_CheckParm("-winsize"))) {
        if (pnum >= com_argc - 2) {
            Sys_Error("VID: -winsize <width> <height>\n");
        }
        vid.width = Q_atoi(com_argv[pnum + 1]);
        vid.height = Q_atoi(com_argv[pnum + 2]);
        if (!vid.width || !vid.height) {
            Sys_Error("VID: Bad window width/height\n");
        }
    }
    flags = 0;
    if (COM_CheckParm("-fullscreen")) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }
    window = SDL_CreateWindow("Quake.cpp",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        vid.width, vid.height, flags);
    if (!window) {
        Sys_Error("VID: Couldn't create window: %s\n", SDL_GetError());
    }
    screen = SDL_GetWindowSurface(window);
    if (!screen) {
        Sys_Error("VID: Couldn't get window surface: %s\n", SDL_GetError());
    }
    if (screen->format->BitsPerPixel != 8) {
        SDL_Surface* new_screen = SDL_CreateRGBSurface(0, vid.width, vid.height, 8, 0, 0, 0, 0);
        if (!new_screen) {
            Sys_Error("VID: Couldn't create 8-bit surface: %s\n", SDL_GetError());
        }
        SDL_Palette* pal = SDL_AllocPalette(256);
        if (!pal) {
            Sys_Error("VID: Couldn't allocate palette: %s\n", SDL_GetError());
        }
        SDL_SetSurfaceBlendMode(new_screen, SDL_BLENDMODE_NONE);
        SDL_SetSurfacePalette(new_screen, pal);
        screen = new_screen;
    }
    VID_SetPalette(palette);
    vid.conwidth = vid.width;
    vid.conheight = vid.height;
    vid.aspect = static_cast<float>(((float)vid.height / (float)vid.width) * (320.0 / 240.0));
    vid.numpages = 1;
    vid.colormap = host_colormap;
    vid.fullbright = 256 - LittleLong(*((int*)vid.colormap + 2048));
    vid.buffer = (pixel_t*)screen->pixels;
    vid.rowbytes = screen->pitch;
    vid.conbuffer = vid.buffer;
    vid.conrowbytes = vid.rowbytes;
    const size_t zbuffer_bytes = static_cast<size_t>(vid.width) * vid.height * sizeof(*d_pzbuffer);
    cachesize = D_SurfaceCacheForRes(vid.width, vid.height);
    video_storage.assign(zbuffer_bytes + static_cast<size_t>(cachesize), 0);
    d_pzbuffer = reinterpret_cast<short*>(video_storage.data());
    cache = video_storage.data() + zbuffer_bytes;
    D_InitCaches(cache, cachesize);
    SDL_ShowCursor(0);
}

void VID_Shutdown(void)
{
    if (window) {
        SDL_DestroyWindow(window);
        window = NULL;
    }
    SDL_Quit();
}

void VID_Update(vrect_t*)
{
    SDL_Surface* window_surface = SDL_GetWindowSurface(window);
    if (screen != window_surface) SDL_BlitSurface(screen, nullptr, window_surface, nullptr);
    SDL_UpdateWindowSurface(window);
}

void D_BeginDirectRect(int x, int y, byte* pbitmap, int width, int height)
{
    Uint8* offset;
    if (!screen) return;
    if (x < 0) x = screen->w + x - 1;
    offset = (Uint8*)screen->pixels + y * screen->pitch + x;
    while (height--) {
        memcpy(offset, pbitmap, width);
        offset += screen->pitch;
        pbitmap += width;
    }
}

void D_EndDirectRect(int x, int y, int width, int height)
{
    SDL_Rect rect;
    if (!screen || !window) return;
    if (x < 0) x = screen->w + x - 1;
    rect.x = x;
    rect.y = y;
    rect.w = width;
    rect.h = height;
    SDL_Surface* window_surface = SDL_GetWindowSurface(window);
    if (screen != window_surface) {
        SDL_BlitSurface(screen, &rect, window_surface, &rect);
    }
    SDL_UpdateWindowSurfaceRects(window, &rect, 1);
}

} // namespace Vid
