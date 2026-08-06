// sys_render.cpp -- Subsystem Render Implementation
// Combines: renderer.cpp, rasterizer.cpp, draw.cpp, screen.cpp, view.cpp, sbar.cpp, model.cpp, vid_sdl.cpp

#include "quakedef.hpp"
#include <SDL.h>

#include <algorithm>
#include <cstring>
#include <cctype>
#include <cmath>
#include <charconv>

#include <EASTL/array.h>
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/fixed_string.h>
#include <EASTL/sort.h>

using namespace Client;
using namespace Common;
using namespace Console;
using namespace Render;
using namespace Draw;
using namespace Host;
using namespace Input;
using namespace Keys;
using namespace Math;
using namespace Menu;
using namespace Model;
using namespace Net;
using namespace VM;
using namespace Sbar;
using namespace Screen;
using namespace Server;
using namespace Audio;
using namespace Vid;
using namespace View;
using namespace Wad;
using namespace Cvar;
using namespace Cmd;


//=============================================================================
// 1. Video System & SDL Driver (from vid_sdl.cpp)
//=============================================================================

cvar_t _windowed_mouse = {"_windowed_mouse", "1", {}, {}, {}, {}};

namespace Vid {

viddef_t vid;
unsigned short d_8to16table[256];

void VID_HandlePause()
{
}

#define BASEWIDTH (320 * 2)
#define BASEHEIGHT (200 * 2)

int VGA_width, VGA_height, VGA_rowbytes;
byte* VGA_pagebase;

static SDL_Window* window = NULL;
static SDL_Surface* screen = NULL;

static qboolean mouse_avail;
static float mouse_x, mouse_y;
static int mouse_oldbuttonstate = 0;

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
    int pnum, chunk;
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

    VGA_width = vid.conwidth = vid.width;
    VGA_height = vid.conheight = vid.height;
    vid.aspect = static_cast<float>(((float)vid.height / (float)vid.width) * (320.0 / 240.0));
    vid.numpages = 1;
    vid.colormap = host_colormap;
    vid.fullbright = 256 - LittleLong(*((int*)vid.colormap + 2048));
    VGA_pagebase = vid.buffer = (pixel_t*)screen->pixels;
    VGA_rowbytes = vid.rowbytes = screen->pitch;
    vid.conbuffer = vid.buffer;
    vid.conrowbytes = vid.rowbytes;
    vid.direct = 0;

    chunk = vid.width * vid.height * sizeof(*d_pzbuffer);
    cachesize = D_SurfaceCacheForRes(vid.width, vid.height);
    chunk += cachesize;
    d_pzbuffer = (short *) Hunk_HighAllocName(chunk, "video");
    if (d_pzbuffer == NULL) {
        Sys_Error("Not enough memory for video mode\n");
    }

    cache = (byte*)d_pzbuffer + vid.width * vid.height * sizeof(*d_pzbuffer);
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

void VID_Update(vrect_t* rects)
{
    SDL_Rect* sdlrects;
    int n, i;
    vrect_t* rect;

    n = 0;
    for (rect = rects; rect; rect = rect->pnext) {
        ++n;
    }

    if (!(sdlrects = (SDL_Rect*)alloca(n * sizeof(*sdlrects)))) {
        Sys_Error("Out of memory");
    }

    i = 0;
    for (rect = rects; rect; rect = rect->pnext) {
        sdlrects[i].x = rect->x;
        sdlrects[i].y = rect->y;
        sdlrects[i].w = rect->width;
        sdlrects[i].h = rect->height;
        ++i;
    }

    SDL_Surface* window_surface = SDL_GetWindowSurface(window);
    if (screen != window_surface) {
        SDL_BlitSurface(screen, NULL, window_surface, NULL);
    }
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

namespace Common {

void Sys_SendKeyEvents(void)
{
    SDL_Event event;
    int sym, state;
    int modstate;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            sym = event.key.keysym.sym;
            state = event.key.state;
            modstate = SDL_GetModState();
            switch (sym) {
            case SDLK_KP_ENTER:
            case SDLK_RETURN: sym = K_ENTER; break;
            case SDLK_ESCAPE: sym = K_ESCAPE; break;
            case SDLK_DELETE: sym = K_DEL; break;
            case SDLK_BACKSPACE: sym = K_BACKSPACE; break;
            case SDLK_F1: sym = K_F1; break;
            case SDLK_F2: sym = K_F2; break;
            case SDLK_F3: sym = K_F3; break;
            case SDLK_F4: sym = K_F4; break;
            case SDLK_F5: sym = K_F5; break;
            case SDLK_F6: sym = K_F6; break;
            case SDLK_F7: sym = K_F7; break;
            case SDLK_F8: sym = K_F8; break;
            case SDLK_F9: sym = K_F9; break;
            case SDLK_F10: sym = K_F10; break;
            case SDLK_F11: sym = K_F11; break;
            case SDLK_F12: sym = K_F12; break;
            case SDLK_PAUSE: sym = K_PAUSE; break;
            case SDLK_UP: sym = K_UPARROW; break;
            case SDLK_DOWN: sym = K_DOWNARROW; break;
            case SDLK_RIGHT: sym = K_RIGHTARROW; break;
            case SDLK_LEFT: sym = K_LEFTARROW; break;
            case SDLK_INSERT: sym = K_INS; break;
            case SDLK_HOME: sym = K_HOME; break;
            case SDLK_END: sym = K_END; break;
            case SDLK_PAGEUP: sym = K_PGUP; break;
            case SDLK_PAGEDOWN: sym = K_PGDN; break;
            case SDLK_RSHIFT:
            case SDLK_LSHIFT: sym = K_SHIFT; break;
            case SDLK_RCTRL:
            case SDLK_LCTRL: sym = K_CTRL; break;
            case SDLK_RALT:
            case SDLK_LALT: sym = K_ALT; break;
            case SDLK_KP_0: sym = (modstate & KMOD_NUM) ? SDLK_0 : K_INS; break;
            case SDLK_KP_1: sym = (modstate & KMOD_NUM) ? SDLK_1 : K_END; break;
            case SDLK_KP_2: sym = (modstate & KMOD_NUM) ? SDLK_2 : K_DOWNARROW; break;
            case SDLK_KP_3: sym = (modstate & KMOD_NUM) ? SDLK_3 : K_PGDN; break;
            case SDLK_KP_4: sym = (modstate & KMOD_NUM) ? SDLK_4 : K_LEFTARROW; break;
            case SDLK_KP_5: sym = SDLK_5; break;
            case SDLK_KP_6: sym = (modstate & KMOD_NUM) ? SDLK_6 : K_RIGHTARROW; break;
            case SDLK_KP_7: sym = (modstate & KMOD_NUM) ? SDLK_7 : K_HOME; break;
            case SDLK_KP_8: sym = (modstate & KMOD_NUM) ? SDLK_8 : K_UPARROW; break;
            case SDLK_KP_9: sym = (modstate & KMOD_NUM) ? SDLK_9 : K_PGUP; break;
            }
            if (sym > 255) sym = 0;
            Key_Event(sym, state);
            break;

        case SDL_MOUSEMOTION:
            if (((unsigned)event.motion.x != (vid.width / 2)) || ((unsigned)event.motion.y != (vid.height / 2))) {
                mouse_x = static_cast<float>(event.motion.xrel * 10);
                mouse_y = static_cast<float>(event.motion.yrel * 10);
                if (((unsigned)event.motion.x < ((vid.width / 2) - (vid.width / 4))) || ((unsigned)event.motion.x > ((vid.width / 2) + (vid.width / 4))) || ((unsigned)event.motion.y < ((vid.height / 2) - (vid.height / 4))) || ((unsigned)event.motion.y > ((vid.height / 2) + (vid.height / 4)))) {
                    SDL_WarpMouseInWindow(window, vid.width / 2, vid.height / 2);
                }
            }
            break;

        case SDL_QUIT:
            CL_Disconnect();
            Host_ShutdownServer(false);
            Sys_Quit();
            break;

        default:
            break;
        }
    }
}

} // namespace Common

namespace Input {

void IN_Init(void)
{
    if (COM_CheckParm("-nomouse")) return;
    mouse_x = mouse_y = 0.0;
    mouse_avail = 1;
    Cvar::Register(&_windowed_mouse);
}

void IN_Shutdown(void)
{
    mouse_avail = 0;
}

void IN_Commands(void)
{
    int i, mouse_buttonstate;
    if (!mouse_avail) return;

    i = SDL_GetMouseState(NULL, NULL);
    mouse_buttonstate = (i & ~0x06) | ((i & 0x02) << 1) | ((i & 0x04) >> 1);
    for (i = 0; i < 3; i++) {
        if ((mouse_buttonstate & (1 << i)) && !(mouse_oldbuttonstate & (1 << i))) Key_Event(K_MOUSE1 + i, true);
        if (!(mouse_buttonstate & (1 << i)) && (mouse_oldbuttonstate & (1 << i))) Key_Event(K_MOUSE1 + i, false);
    }
    mouse_oldbuttonstate = mouse_buttonstate;
}

void IN_Move(usercmd_t* cmd)
{
    if (!mouse_avail) return;

    mouse_x *= sensitivity.value;
    mouse_y *= sensitivity.value;

    if ((in_strafe.state & 1) || (lookstrafe.value && ((in_mlook.state & 1) || _windowed_mouse.value))) {
        cmd->sidemove += m_side.value * mouse_x;
    } else {
        cl.viewangles[YAW] -= m_yaw.value * mouse_x;
    }

    if ((in_mlook.state & 1) || _windowed_mouse.value) {
        V_StopPitchDrift();
    }

    if (((in_mlook.state & 1) || _windowed_mouse.value) && !(in_strafe.state & 1)) {
        cl.viewangles[PITCH] += m_pitch.value * mouse_y;
        if (cl.viewangles[PITCH] > 80) cl.viewangles[PITCH] = 80;
        if (cl.viewangles[PITCH] < -70) cl.viewangles[PITCH] = -70;
    } else {
        if ((in_strafe.state & 1) && noclip_anglehack) {
            cmd->upmove -= m_forward.value * mouse_y;
        } else {
            cmd->forwardmove -= m_forward.value * mouse_y;
        }
    }

    mouse_x = mouse_y = 0.0;
}

} // namespace Input

//=============================================================================
// 2. 2D Drawing (from draw.cpp)
//=============================================================================

struct RectDesc {
    vrect_t rect;
    int width;
    int height;
    byte* ptexbytes;
    int rowbytes;
};

static RectDesc r_rectdesc;

namespace Draw {

byte* draw_chars;
qpic_t* draw_disc;
qpic_t* draw_backtile;

struct CachePic {
    eastl::string name;
    cache_user_t cache{};
};

static eastl::vector<eastl::unique_ptr<CachePic>> menu_cachepics;

qpic_t* Draw_CachePic(eastl::string_view path)
{
    for (const auto& pic : menu_cachepics) {
        if (eastl::string_view(pic->name.data(), pic->name.length()) == path) {
            if (auto* dat = static_cast<qpic_t*>(Cache_Check(&pic->cache))) {
                return dat;
            }
            eastl::string path_str(path.data(), path.length());
            COM_LoadCacheFile(path_str.c_str(), &pic->cache);
            auto* dat = static_cast<qpic_t*>(pic->cache.data);
            if (!dat) Sys_Error("Draw_CachePic: failed to load %s", path_str.c_str());
            SwapPic(dat);
            return dat;
        }
    }

    auto new_pic = eastl::make_unique<CachePic>();
    new_pic->name.assign(path.data(), path.length());
    auto* pic_ptr = new_pic.get();
    menu_cachepics.push_back(eastl::move(new_pic));

    eastl::string path_str(path.data(), path.length());
    COM_LoadCacheFile(path_str.c_str(), &pic_ptr->cache);
    auto* dat = static_cast<qpic_t*>(pic_ptr->cache.data);
    if (!dat) Sys_Error("Draw_CachePic: failed to load %s", path_str.c_str());
    SwapPic(dat);

    return dat;
}

void Draw_Init()
{
    draw_chars = (byte*)W_GetLumpName("conchars");
    draw_disc = (qpic_t*)W_GetLumpName("disc");
    draw_backtile = (qpic_t*)W_GetLumpName("backtile");

    r_rectdesc.width = draw_backtile->width;
    r_rectdesc.height = draw_backtile->height;
    r_rectdesc.ptexbytes = draw_backtile->data;
    r_rectdesc.rowbytes = draw_backtile->width;
}

void Draw_Character(int x, int y, int num)
{
    num &= 255;
    if (y <= -8) return;

    const int row = num >> 4;
    const int col = num & 15;
    const byte* source = draw_chars + (row << 10) + (col << 3);

    int drawline;
    if (y < 0) {
        drawline = 8 + y;
        source -= 128 * y;
        y = 0;
    } else {
        drawline = 8;
    }

    if (r_pixbytes == 1) {
        byte* dest = vid.conbuffer + y * vid.conrowbytes + x;
        while (drawline--) {
            for (int i = 0; i < 8; ++i) {
                if (source[i]) dest[i] = source[i];
            }
            source += 128;
            dest += vid.conrowbytes;
        }
    } else {
        auto* pusdest = (unsigned short*)((byte*)vid.conbuffer + y * vid.conrowbytes + (x << 1));
        while (drawline--) {
            for (int i = 0; i < 8; ++i) {
                if (source[i]) pusdest[i] = d_8to16table[source[i]];
            }
            source += 128;
            pusdest += (vid.conrowbytes >> 1);
        }
    }
}

void Draw_String(int x, int y, eastl::string_view str)
{
    for (const char c : str) {
        Draw_Character(x, y, c);
        x += 8;
    }
}

void Draw_Pic(int x, int y, qpic_t* pic)
{
    if ((x < 0) || (unsigned)(x + pic->width) > vid.width || (y < 0) || (unsigned)(y + pic->height) > vid.height) {
        Sys_Error("Draw_Pic: bad coordinates");
    }

    const byte* source = pic->data;
    if (r_pixbytes == 1) {
        byte* dest = vid.buffer + y * vid.rowbytes + x;
        for (int v = 0; v < pic->height; v++) {
            std::memcpy(dest, source, pic->width);
            dest += vid.rowbytes;
            source += pic->width;
        }
    } else {
        auto* pusdest = (unsigned short*)vid.buffer + y * (vid.rowbytes >> 1) + x;
        for (int v = 0; v < pic->height; v++) {
            for (int u = 0; u < pic->width; u++) {
                pusdest[u] = d_8to16table[source[u]];
            }
            pusdest += vid.rowbytes >> 1;
            source += pic->width;
        }
    }
}

void Draw_TransPic(int x, int y, qpic_t* pic)
{
    if (x < 0 || (unsigned)(x + pic->width) > vid.width || y < 0 || (unsigned)(y + pic->height) > vid.height) {
        Sys_Error("Draw_TransPic: bad coordinates");
    }

    const byte* source = pic->data;
    if (r_pixbytes == 1) {
        byte* dest = vid.buffer + y * vid.rowbytes + x;
        for (int v = 0; v < pic->height; v++) {
            for (int u = 0; u < pic->width; u++) {
                if (const byte tbyte = source[u]; tbyte != TRANSPARENT_COLOR) dest[u] = tbyte;
            }
            dest += vid.rowbytes;
            source += pic->width;
        }
    } else {
        auto* pusdest = (unsigned short*)vid.buffer + y * (vid.rowbytes >> 1) + x;
        for (int v = 0; v < pic->height; v++) {
            for (int u = 0; u < pic->width; u++) {
                if (const byte tbyte = source[u]; tbyte != TRANSPARENT_COLOR) pusdest[u] = d_8to16table[tbyte];
            }
            pusdest += vid.rowbytes >> 1;
            source += pic->width;
        }
    }
}

void Draw_TransPicTranslate(int x, int y, qpic_t* pic, const byte* translation)
{
    if (x < 0 || (unsigned)(x + pic->width) > vid.width || y < 0 || (unsigned)(y + pic->height) > vid.height) {
        Sys_Error("Draw_TransPic: bad coordinates");
    }

    const byte* source = pic->data;
    if (r_pixbytes == 1) {
        byte* dest = vid.buffer + y * vid.rowbytes + x;
        for (int v = 0; v < pic->height; v++) {
            for (int u = 0; u < pic->width; u++) {
                if (const byte tbyte = source[u]; tbyte != TRANSPARENT_COLOR) dest[u] = translation[tbyte];
            }
            dest += vid.rowbytes;
            source += pic->width;
        }
    } else {
        auto* pusdest = (unsigned short*)vid.buffer + y * (vid.rowbytes >> 1) + x;
        for (int v = 0; v < pic->height; v++) {
            for (int u = 0; u < pic->width; u++) {
                if (const byte tbyte = source[u]; tbyte != TRANSPARENT_COLOR) pusdest[u] = d_8to16table[tbyte];
            }
            pusdest += vid.rowbytes >> 1;
            source += pic->width;
        }
    }
}

void Draw_CharToConback(int num, byte* dest)
{
    const int row = num >> 4;
    const int col = num & 15;
    const byte* source = draw_chars + (row << 10) + (col << 3);
    int drawline = 8;

    while (drawline--) {
        for (int x = 0; x < 8; x++) {
            if (source[x]) dest[x] = 0x60 + source[x];
        }
        source += 128;
        dest += 320;
    }
}

void Draw_ConsoleBackground(int lines)
{
    qpic_t* conback = Draw_CachePic("gfx/conback.lmp");
    byte* dest = conback->data + 320 - 43 + 320 * 186;
    char ver[100];
    std::snprintf(ver, sizeof(ver), "%4.2f", VERSION);

    const eastl::string_view ver_view(ver);
    for (size_t x = 0; x < ver_view.length(); x++) {
        Draw_CharToConback(ver_view[x], dest + (x << 3));
    }

    if (r_pixbytes == 1) {
        dest = vid.conbuffer;
        for (int y = 0; y < lines; y++, dest += vid.conrowbytes) {
            const int v = (vid.conheight - lines + y) * 200 / vid.conheight;
            const byte* src = conback->data + v * 320;
            if (vid.conwidth == 320) {
                std::memcpy(dest, src, vid.conwidth);
            } else {
                int f = 0;
                const int fstep = 320 * 0x10000 / vid.conwidth;
                for (int x = 0; x < (int)vid.conwidth; x += 4) {
                    dest[x] = src[f >> 16]; f += fstep;
                    dest[x + 1] = src[f >> 16]; f += fstep;
                    dest[x + 2] = src[f >> 16]; f += fstep;
                    dest[x + 3] = src[f >> 16]; f += fstep;
                }
            }
        }
    } else {
        auto* pusdest = (unsigned short*)vid.conbuffer;
        for (int y = 0; y < lines; y++, pusdest += (vid.conrowbytes >> 1)) {
            const int v = (vid.conheight - lines + y) * 200 / vid.conheight;
            const byte* src = conback->data + v * 320;
            int f = 0;
            const int fstep = 320 * 0x10000 / vid.conwidth;
            for (int x = 0; x < (int)vid.conwidth; x += 4) {
                pusdest[x] = d_8to16table[src[f >> 16]]; f += fstep;
                pusdest[x + 1] = d_8to16table[src[f >> 16]]; f += fstep;
                pusdest[x + 2] = d_8to16table[src[f >> 16]]; f += fstep;
                pusdest[x + 3] = d_8to16table[src[f >> 16]]; f += fstep;
            }
        }
    }
}

void R_DrawRect8(const vrect_t* prect, int rowbytes, const byte* psrc, bool transparent)
{
    byte* pdest = vid.buffer + (prect->y * vid.rowbytes) + prect->x;
    const int srcdelta = rowbytes - prect->width;
    const int destdelta = vid.rowbytes - prect->width;

    if (transparent) {
        for (int i = 0; i < prect->height; i++) {
            for (int j = 0; j < prect->width; j++) {
                if (const byte t = *psrc; t != TRANSPARENT_COLOR) *pdest = t;
                psrc++; pdest++;
            }
            psrc += srcdelta; pdest += destdelta;
        }
    } else {
        for (int i = 0; i < prect->height; i++) {
            std::memcpy(pdest, psrc, prect->width);
            psrc += rowbytes; pdest += vid.rowbytes;
        }
    }
}

void R_DrawRect16(const vrect_t* prect, int rowbytes, const byte* psrc, bool transparent)
{
    auto* pdest = (unsigned short*)vid.buffer + (prect->y * (vid.rowbytes >> 1)) + prect->x;
    const int srcdelta = rowbytes - prect->width;
    const int destdelta = (vid.rowbytes >> 1) - prect->width;

    if (transparent) {
        for (int i = 0; i < prect->height; i++) {
            for (int j = 0; j < prect->width; j++) {
                if (const byte t = *psrc; t != TRANSPARENT_COLOR) *pdest = d_8to16table[t];
                psrc++; pdest++;
            }
            psrc += srcdelta; pdest += destdelta;
        }
    } else {
        for (int i = 0; i < prect->height; i++) {
            for (int j = 0; j < prect->width; j++) {
                *pdest = d_8to16table[*psrc];
                psrc++; pdest++;
            }
            psrc += srcdelta; pdest += destdelta;
        }
    }
}

void Draw_TileClear(int x, int y, int w, int h)
{
    r_rectdesc.rect.x = x;
    r_rectdesc.rect.y = y;
    r_rectdesc.rect.width = w;
    r_rectdesc.rect.height = h;

    vrect_t vr{};
    vr.y = r_rectdesc.rect.y;
    int height = r_rectdesc.rect.height;
    int tileoffsety = vr.y % r_rectdesc.height;

    while (height > 0) {
        vr.x = r_rectdesc.rect.x;
        int width = r_rectdesc.rect.width;

        if (tileoffsety != 0) vr.height = r_rectdesc.height - tileoffsety;
        else vr.height = r_rectdesc.height;

        if (vr.height > height) vr.height = height;
        int tileoffsetx = vr.x % r_rectdesc.width;

        while (width > 0) {
            if (tileoffsetx != 0) vr.width = r_rectdesc.width - tileoffsetx;
            else vr.width = r_rectdesc.width;

            if (vr.width > width) vr.width = width;

            const byte* psrc = r_rectdesc.ptexbytes + (tileoffsety * r_rectdesc.rowbytes) + tileoffsetx;
            if (r_pixbytes == 1) R_DrawRect8(&vr, r_rectdesc.rowbytes, psrc, false);
            else R_DrawRect16(&vr, r_rectdesc.rowbytes, psrc, false);

            vr.x += vr.width;
            width -= vr.width;
            tileoffsetx = 0;
        }

        vr.y += vr.height;
        height -= vr.height;
        tileoffsety = 0;
    }
}

void Draw_Fill(int x, int y, int w, int h, int c)
{
    if (r_pixbytes == 1) {
        byte* dest = vid.buffer + y * vid.rowbytes + x;
        for (int v = 0; v < h; v++, dest += vid.rowbytes) {
            std::fill_n(dest, w, static_cast<byte>(c));
        }
    } else {
        const auto uc = static_cast<unsigned short>(d_8to16table[c]);
        auto* pusdest = (unsigned short*)vid.buffer + y * (vid.rowbytes >> 1) + x;
        for (int v = 0; v < h; v++, pusdest += (vid.rowbytes >> 1)) {
            std::fill_n(pusdest, w, uc);
        }
    }
}

void Draw_FadeScreen()
{
    VID_UnlockBuffer();
    S_ExtraUpdate();
    VID_LockBuffer();

    for (int y = 0; y < static_cast<int>(vid.height); y++) {
        byte* pbuf = vid.buffer + vid.rowbytes * y;
        const int t = (y & 1) << 1;
        for (int x = 0; x < static_cast<int>(vid.width); x++) {
            if ((x & 3) != t) pbuf[x] = 0;
        }
    }

    VID_UnlockBuffer();
    S_ExtraUpdate();
    VID_LockBuffer();
}

void Draw_BeginDisc()
{
    D_BeginDirectRect(vid.width - 24, 0, draw_disc->data, 24, 24);
}

void Draw_EndDisc()
{
    D_EndDirectRect(vid.width - 24, 0, 24, 24);
}

} // namespace Draw

// Include renderer, rasterizer, model, sbar, screen, view implementations

// Subsystem Render Implementation Parts
// renderer.cpp -- merged renderer subsystem


using namespace Client;
using namespace Common;
using namespace Console;
using namespace Render;
using namespace Draw;
using namespace Host;
using namespace Input;
using namespace Keys;
using namespace Math;
using namespace Menu;
using namespace Model;
using namespace Net;
using namespace VM;
using namespace Sbar;
using namespace Screen;
using namespace Server;
using namespace Audio;
using namespace Vid;
using namespace View;
using namespace Wad;
using namespace Cvar;
using namespace Cmd;

#include <EASTL/vector.h>
#include <EASTL/array.h>

namespace Render {

namespace {
    int r_bmodelactive;
    mnode_t* r_pefragtopnode;
    Vector3 r_emins, r_emaxs;
    int r_dlightframecount;
    int c_faceclip;
    eastl::array<clipplane_t, 4> view_clipplanes{};
    edge_t* auxedges;
    edge_t *r_edges, *edge_p, *edge_max;
    eastl::array<edge_t*, MAXHEIGHT> newedges{};
    eastl::array<edge_t*, MAXHEIGHT> removeedges{};
    int r_currentkey;
    edge_t edge_head;
    edge_t edge_tail;
    edge_t edge_aftertail;
    Vector3 r_entorigin;
    float entity_rotation[3][3];
    Vector3 r_worldmodelorg;
    int r_currentbkey;

    mdl_t* pmdl;
    aliashdr_t* paliashdr;
    finalvert_t* pfinalverts;
    auxvert_t* pauxverts;
    int r_amodels_drawn;
    int a_skinwidth;
    float r_time1;
    int r_numallocatededges;
    int r_outofsurfaces;
    int r_outofedges;
    bool r_dowarpold, r_viewchanged;
    int numbtofpolys;
    btofpoly_t* pbtofpolys;
    mvertex_t* r_pcurrentvertbase;
    int r_maxsurfsseen, r_maxedgesseen, r_cnumsurfs;
    bool r_surfsonstack;
    int r_clipflags;
    bool r_fov_greater_than_90;
    float aliasxscale, aliasyscale, aliasxcenter, aliasycenter;
    float screenAspect;
    float verticalFieldOfView;
    float xOrigin, yOrigin;
    eastl::array<mplane_t, 4> screenedge{};
    int r_visframecount;
    int r_polycount;
    int r_wholepolycount;
    eastl::array<int*, 4> pfrustum_indexes{};
    eastl::array<int, 4 * 6> r_frustum_indexes{};
    mleaf_t *r_viewleaf, *r_oldviewleaf;
    float r_aliastransition, r_resfudge;
    float dp_time1, dp_time2, db_time1, db_time2, rw_time1, rw_time2;
    float se_time1, se_time2, de_time1, de_time2, dv_time1, dv_time2;
    cvar_t r_draworder = { "r_draworder", "0", {}, {}, {}, {} };
    cvar_t r_speeds = { "r_speeds", "0", {}, {}, {}, {} };
    cvar_t r_timegraph = { "r_timegraph", "0", {}, {}, {}, {} };
    cvar_t r_graphheight = { "r_graphheight", "10", {}, {}, {}, {} };
    cvar_t r_waterwarp = { "r_waterwarp", "1", {}, {}, {}, {} };
    cvar_t r_fullbright = { "r_fullbright", "0", {}, {}, {}, {} };
    cvar_t r_drawentities = { "r_drawentities", "1", {}, {}, {}, {} };
    cvar_t r_aliasstats = { "r_polymodelstats", "0", {}, {}, {}, {} };
    cvar_t r_dspeeds = { "r_dspeeds", "0", {}, {}, {}, {} };
    cvar_t r_ambient = { "r_ambient", "0", {}, {}, {}, {} };
    cvar_t r_reportsurfout = { "r_reportsurfout", "0", {}, {}, {}, {} };
    cvar_t r_maxsurfs = { "r_maxsurfs", "0", {}, {}, {}, {} };
    cvar_t r_numsurfs = { "r_numsurfs", "0", {}, {}, {}, {} };
    cvar_t r_reportedgeout = { "r_reportedgeout", "0", {}, {}, {}, {} };
    cvar_t r_maxedges = { "r_maxedges", "0", {}, {}, {}, {} };
    cvar_t r_numedges = { "r_numedges", "0", {}, {}, {}, {} };
}

// ============================================================
// Content from: src\r_vars.cpp
// ============================================================
// r_vars.cpp: global refresh variables


// all global and static refresh variables are collected in a contiguous block
// to avoid cache conflicts.

//-------------------------------------------------------
// global refresh variables
//-------------------------------------------------------


// ============================================================
// Content from: src\r_efrag.cpp
// ============================================================


//===========================================================================

/*
===============================================================================

					ENTITY FRAGMENT FUNCTIONS

===============================================================================
*/

efrag_t** lastlink;

entity_t* r_addent;

/*
================
R_RemoveEfrags

Call when removing an object from the world or moving it to another position
================
*/
void R_RemoveEfrags(entity_t* ent)
{
    efrag_t *ef, *old, *walk, **prev;

    ef = ent->efrag;

    while (ef) {
        prev = &ef->leaf->efrags;
        while (1) {
            walk = *prev;
            if (!walk) {
                break;
            }

            if (walk == ef) { // remove this fragment
                *prev = ef->leafnext;
                break;
            } else {
                prev = &walk->leafnext;
            }
        }

        old = ef;
        ef = ef->entnext;

        // put it on the free list
        old->entnext = cl.free_efrags;
        cl.free_efrags = old;
    }

    ent->efrag = nullptr;
}

/*
===================
R_SplitEntityOnNode
===================
*/
void R_SplitEntityOnNode(mnode_t* node)
{
    efrag_t* ef;
    mplane_t* splitplane;
    mleaf_t* leaf;
    int sides;

    if (node->contents == CONTENTS_SOLID) {
        return;
    }

    // add an efrag if the node is a leaf

    if (node->contents < 0) {
        if (!r_pefragtopnode) {
            r_pefragtopnode = node;
        }

        leaf = (mleaf_t*)node;

        // grab an efrag off the free list
        ef = cl.free_efrags;
        if (!ef) {
            Con_Printf("Too many efrags!\n");

            return; // no free fragments...
        }

        cl.free_efrags = cl.free_efrags->entnext;

        ef->entity = r_addent;

        // add the entity link
        *lastlink = ef;
        lastlink = &ef->entnext;
        ef->entnext = nullptr;

        // set the leaf links
        ef->leaf = leaf;
        ef->leafnext = leaf->efrags;
        leaf->efrags = ef;

        return;
    }

    // NODE_MIXED

    splitplane = node->plane;
    sides = BOX_ON_PLANE_SIDE(r_emins, r_emaxs, splitplane);

    if (sides == 3) {
        // split on this plane
        // if this is the first splitter of this bmodel, remember it
        if (!r_pefragtopnode) {
            r_pefragtopnode = node;
        }
    }

    // recurse down the contacted sides
    if (sides & 1) {
        R_SplitEntityOnNode(node->children[0]);
    }

    if (sides & 2) {
        R_SplitEntityOnNode(node->children[1]);
    }
}

/*
===================
R_SplitEntityOnNode2
===================
*/
void R_SplitEntityOnNode2(mnode_t* node)
{
    mplane_t* splitplane;
    int sides;

    if (node->visframe != r_visframecount) {
        return;
    }

    if (node->contents < 0) {
        if (node->contents != CONTENTS_SOLID) {
            r_pefragtopnode = node; // we've reached a non-solid leaf, so it's
        }

        //  visible and not BSP clipped
        return;
    }

    splitplane = node->plane;
    sides = BOX_ON_PLANE_SIDE(r_emins, r_emaxs, splitplane);

    if (sides == 3) {
        // remember first splitter
        r_pefragtopnode = node;

        return;
    }

    // not split yet; recurse down the contacted side
    if (sides & 1) {
        R_SplitEntityOnNode2(node->children[0]);
    } else {
        R_SplitEntityOnNode2(node->children[1]);
    }
}

/*
===========
R_AddEfrags
===========
*/
void R_AddEfrags(entity_t* ent)
{
    model_t* entmodel;
    int i;

    if (!ent->model) {
        return;
    }

    if (ent == cl_entities) {
        return; // never add the world
    }

    r_addent = ent;

    lastlink = &ent->efrag;
    r_pefragtopnode = nullptr;

    entmodel = ent->model;

    for (i = 0; i < 3; i++) {
        r_emins[i] = ent->origin[i] + entmodel->mins[i];
        r_emaxs[i] = ent->origin[i] + entmodel->maxs[i];
    }

    R_SplitEntityOnNode(cl.worldmodel->nodes);

    ent->topnode = r_pefragtopnode;
}

/*
================
R_StoreEfrags

================
*/
void R_StoreEfrags(efrag_t** ppefrag)
{
    entity_t* pent;
    model_t* clmodel;
    efrag_t* pefrag;

    while ((pefrag = *ppefrag) != nullptr) {
        pent = pefrag->entity;
        clmodel = pent->model;

        switch (clmodel->type) {
        case mod_alias:
        case mod_brush:
        case mod_sprite:
            pent = pefrag->entity;

            if ((pent->visframe != r_framecount) && (cl_numvisedicts < MAX_VISEDICTS)) {
                cl_visedicts[cl_numvisedicts++] = pent;

                // mark that we've recorded this entity for this frame
                pent->visframe = r_framecount;
            }

            ppefrag = &pefrag->leafnext;
            break;

        default:
            Sys_Error("R_StoreEfrags: Bad entity type %d\n", clmodel->type);
        }
    }
}


// ============================================================
// Content from: src\r_light.cpp
// ============================================================


/*
==================
R_AnimateLight
==================
*/
void R_AnimateLight(void)
{
    int i, j, k;

    //
    // light animations
    // 'm' is normal light, 'a' is no light, 'z' is double bright
    i = static_cast<int>(cl.time * 10);
    for (j = 0; j < MAX_LIGHTSTYLES; j++) {
        if (!cl_lightstyle[j].length) {
            d_lightstylevalue[j] = 256;
            continue;
        }

        k = i % cl_lightstyle[j].length;
        k = cl_lightstyle[j].map[k] - 'a';
        k = k * 22;
        d_lightstylevalue[j] = k;
    }
}

/*
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/

/*
=============
R_MarkLights
=============
*/
void R_MarkLights(dlight_t* light, int bit, mnode_t* node)
{
    mplane_t* splitplane;
    float dist;
    msurface_t* surf;
    int i;

    if (node->contents < 0) {
        return;
    }

    splitplane = node->plane;
    dist = DotProduct(light->origin, splitplane->normal) - splitplane->dist;

    if (dist > light->radius) {
        R_MarkLights(light, bit, node->children[0]);

        return;
    }

    if (dist < -light->radius) {
        R_MarkLights(light, bit, node->children[1]);

        return;
    }

    // mark the polygons
    surf = cl.worldmodel->surfaces + node->firstsurface;
    for (i = 0; i < node->numsurfaces; i++, surf++) {
        if (surf->dlightframe != r_dlightframecount) {
            surf->dlightbits = 0;
            surf->dlightframe = r_dlightframecount;
        }

        surf->dlightbits |= bit;
    }

    R_MarkLights(light, bit, node->children[0]);
    R_MarkLights(light, bit, node->children[1]);
}

/*
=============
R_PushDlights
=============
*/
void R_PushDlights(void)
{
    int i;
    dlight_t* l;

    r_dlightframecount = r_framecount + 1; // because the count hasn't
    //  advanced yet for this frame
    l = cl_dlights;

    for (i = 0; i < MAX_DLIGHTS; i++, l++) {
        if (l->die < cl.time || !l->radius) {
            continue;
        }

        R_MarkLights(l, 1 << i, cl.worldmodel->nodes);
    }
}

/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

int RecursiveLightPoint(mnode_t* node, const Vector3& start, const Vector3& end)
{
    int r;
    float front, back, frac;
    int side;
    mplane_t* plane;
    Vector3 mid;
    msurface_t* surf;
    int s, t, ds, dt;
    int i;
    mtexinfo_t* tex;
    byte* lightmap;
    unsigned scale;
    int maps;

    if (node->contents < 0) {
        return -1; // didn't hit anything
    }

    // calculate mid point

    // FIXME: optimize for axial
    plane = node->plane;
    front = start.dot(plane->normal) - plane->dist;
    back = end.dot(plane->normal) - plane->dist;
    side = front < 0;

    if ((back < 0) == side) {
        return RecursiveLightPoint(node->children[side], start, end);
    }

    frac = front / (front - back);
    mid = start + (end - start) * frac;

    // go down front side
    r = RecursiveLightPoint(node->children[side], start, mid);
    if (r >= 0) {
        return r; // hit something
    }

    if ((back < 0) == side) {
        return -1; // didn't hit anuthing
    }

    // check for impact on this node

    surf = cl.worldmodel->surfaces + node->firstsurface;
    for (i = 0; i < node->numsurfaces; i++, surf++) {
        if (surf->flags & SURF_DRAWTILED) {
            continue; // no lightmaps
        }

        tex = surf->texinfo;

        s = static_cast<int>(mid.dot(tex->vecs[0]) + tex->vecs[0][3]);
        t = static_cast<int>(mid.dot(tex->vecs[1]) + tex->vecs[1][3]);
        ;

        if (s < surf->texturemins[0] || t < surf->texturemins[1]) {
            continue;
        }

        ds = s - surf->texturemins[0];
        dt = t - surf->texturemins[1];

        if (ds > surf->extents[0] || dt > surf->extents[1]) {
            continue;
        }

        if (!surf->samples) {
            return 0;
        }

        ds >>= 4;
        dt >>= 4;

        lightmap = surf->samples;
        r = 0;
        if (lightmap) {
            lightmap += dt * ((surf->extents[0] >> 4) + 1) + ds;

            for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++) {
                scale = d_lightstylevalue[surf->styles[maps]];
                r += *lightmap * scale;
                lightmap += ((surf->extents[0] >> 4) + 1) * ((surf->extents[1] >> 4) + 1);
            }

            r >>= 8;
        }

        return r;
    }

    // go down back side
    return RecursiveLightPoint(node->children[!side], mid, end);
}

int R_LightPoint(const Vector3& p)
{
    Vector3 end;
    int r;

    if (!cl.worldmodel->lightdata) {
        return 255;
    }

    end = p - Vector3(0.0f, 0.0f, 2048.0f);

    r = RecursiveLightPoint(cl.worldmodel->nodes, p, end);

    if (r == -1) {
        r = 0;
    }

    if (r < r_refdef.ambientlight) {
        r = r_refdef.ambientlight;
    }

    return r;
}


// ============================================================
// Content from: src\r_part.cpp
// ============================================================


#define MAX_PARTICLES 2048 // default max # of particles at one
//  time
#define ABSOLUTE_MIN_PARTICLES 512 // no fewer than this no matter what's
//  on the command line

constexpr eastl::array<int, 8> ramp1 = { 0x6f, 0x6d, 0x6b, 0x69, 0x67, 0x65, 0x63, 0x61 };
constexpr eastl::array<int, 8> ramp2 = { 0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x68, 0x66 };
constexpr eastl::array<int, 8> ramp3 = { 0x6d, 0x6b, 6, 5, 4, 3, 0, 0 };

particle_t* active_particles = nullptr;
particle_t* free_particles = nullptr;

static inline particle_t* AllocParticle()
{
    if (!free_particles) return nullptr;
    particle_t* p = free_particles;
    free_particles = p->next;
    p->next = active_particles;
    active_particles = p;
    return p;
}

eastl::vector<particle_t> particles;
int r_numparticles = 0;

Vector3 r_pright, r_pup, r_ppn;


/*
===============
R_InitParticles
===============
*/
void R_InitParticles(void)
{
    int i;

    i = COM_CheckParm("-particles");

    if (i) {
        r_numparticles = (int)(Q_atoi(com_argv[i + 1]));
        if (r_numparticles < ABSOLUTE_MIN_PARTICLES) {
            r_numparticles = ABSOLUTE_MIN_PARTICLES;
        }
    } else {
        r_numparticles = MAX_PARTICLES;
    }

    particles.resize(r_numparticles);
}


/*
===============
R_EntityParticles
===============
*/

#define NUMVERTEXNORMALS 162
eastl::array<eastl::array<float, 3>, NUMVERTEXNORMALS> r_avertexnormals{};
eastl::array<Vector3, NUMVERTEXNORMALS> avelocities{};
float beamlength = 16;

void R_EntityParticles(entity_t* ent)
{
    int i;
    particle_t* p;
    float angle;
    float sp, sy, cp, cy;
    Vector3 forward;
    float dist;

    dist = 64;

    if (!avelocities[0].x) {
        for (int j = 0; j < NUMVERTEXNORMALS; j++) {
            for (i = 0; i < 3; i++) {
                avelocities[j][i] = (rand() & 255) * 0.01f;
            }
        }
    }

    for (i = 0; i < NUMVERTEXNORMALS; i++) {
        angle = static_cast<float>(cl.time * avelocities[i].x);
        sy = sin(angle);
        cy = cos(angle);
        angle = static_cast<float>(cl.time * avelocities[i].y);
        sp = sin(angle);
        cp = cos(angle);

        forward.x = cp * cy;
        forward.y = cp * sy;
        forward.z = -sp;

        if (!free_particles) {
            return;
        }

        p = free_particles;
        free_particles = p->next;
        p->next = active_particles;
        active_particles = p;

        p->die = static_cast<float>(cl.time + 0.01);
        p->color = static_cast<float>(0x6f);
        p->type = ptype_t::Explode;

        p->org = ent->origin + Vector3(r_avertexnormals[i][0], r_avertexnormals[i][1], r_avertexnormals[i][2]) * dist + forward * beamlength;
    }
}

/*
===============
R_ClearParticles
===============
*/
void R_ClearParticles(void)
{
    int i;

    free_particles = particles.data();
    active_particles = nullptr;

    for (i = 0; i < r_numparticles - 1; i++) {
        particles[i].next = &particles[i + 1];
    }
    particles[r_numparticles - 1].next = nullptr;
}

void R_ReadPointFile_f(void)
{
    FILE* f;
    Vector3 org;
    int r;
    int c;
    particle_t* p;
    char name[MAX_OSPATH];

    sprintf_s(name, sizeof(name), "maps/%s.pts", sv.name);

    COM_FOpenFile(name, &f);
    if (!f) {
        Con_Printf("couldn't open %s\n", name);

        return;
    }

    Con_Printf("Reading %s...\n", name);
    c = 0;
    for (;;) {
        r = fscanf_s(f, "%f %f %f\n", &org[0], &org[1], &org[2]);
        if (r != 3) {
            break;
        }

        c++;

        if (!free_particles) {
            Con_Printf("Not enough free particles\n");
            break;
        }

        p = free_particles;
        free_particles = p->next;
        p->next = active_particles;
        active_particles = p;

        p->die = 99999.0f;
        p->color = static_cast<float>((-c) & 15);
        p->type = ptype_t::Static;
        p->vel = vec3_origin;
        p->org = org;
    }

    fclose(f);
    Con_Printf("%i points read\n", c);
}

/*
===============
R_ParseParticleEffect

Parse an effect out of the server message
===============
*/
void R_ParseParticleEffect(void)
{
    Vector3 org, dir;
    int count, msgcount, color;

    org.x = MSG_ReadCoord();
    org.y = MSG_ReadCoord();
    org.z = MSG_ReadCoord();

    dir.x = MSG_ReadChar() * (1.0f / 16.0f);
    dir.y = MSG_ReadChar() * (1.0f / 16.0f);
    dir.z = MSG_ReadChar() * (1.0f / 16.0f);

    msgcount = MSG_ReadByte();
    color = MSG_ReadByte();

    if (msgcount == 255) {
        count = 1024;
    } else {
        count = msgcount;
    }

    R_RunParticleEffect(org, dir, color, count);
}

void R_ParticleExplosion(const Vector3& org)
{
    for (int i = 0; i < 1024; i++) {
        particle_t* p = AllocParticle();
        if (!p) return;

        p->die = static_cast<float>(cl.time + 5);
        p->color = static_cast<float>(ramp1[0]);
        p->ramp = static_cast<float>(rand() & 3);
        p->type = (i & 1) ? ptype_t::Explode : ptype_t::Explode2;
        p->org = org + Vector3(static_cast<float>((rand() % 32) - 16), static_cast<float>((rand() % 32) - 16), static_cast<float>((rand() % 32) - 16));
        p->vel = Vector3(static_cast<float>((rand() % 512) - 256), static_cast<float>((rand() % 512) - 256), static_cast<float>((rand() % 512) - 256));
    }
}

void R_ParticleExplosion2(const Vector3& org, int colorStart, int colorLength)
{
    int colorMod = 0;
    for (int i = 0; i < 512; i++) {
        particle_t* p = AllocParticle();
        if (!p) return;

        p->die = static_cast<float>(cl.time + 0.3);
        p->color = static_cast<float>(colorStart + (colorMod++ % colorLength));
        p->type = ptype_t::Blob;
        p->org = org + Vector3(static_cast<float>((rand() % 32) - 16), static_cast<float>((rand() % 32) - 16), static_cast<float>((rand() % 32) - 16));
        p->vel = Vector3(static_cast<float>((rand() % 512) - 256), static_cast<float>((rand() % 512) - 256), static_cast<float>((rand() % 512) - 256));
    }
}

void R_BlobExplosion(const Vector3& org)
{
    for (int i = 0; i < 1024; i++) {
        particle_t* p = AllocParticle();
        if (!p) return;

        p->die = static_cast<float>(cl.time + 1 + (rand() & 8) * 0.05);
        if (i & 1) {
            p->type = ptype_t::Blob;
            p->color = static_cast<float>(66 + rand() % 6);
        } else {
            p->type = ptype_t::Blob2;
            p->color = static_cast<float>(150 + rand() % 6);
        }
        p->org = org + Vector3(static_cast<float>((rand() % 32) - 16), static_cast<float>((rand() % 32) - 16), static_cast<float>((rand() % 32) - 16));
        p->vel = Vector3(static_cast<float>((rand() % 512) - 256), static_cast<float>((rand() % 512) - 256), static_cast<float>((rand() % 512) - 256));
    }
}

void R_RunParticleEffect(const Vector3& org, const Vector3& dir, int color, int count)
{
    for (int i = 0; i < count; i++) {
        particle_t* p = AllocParticle();
        if (!p) return;

        if (count == 1024) {
            p->die = static_cast<float>(cl.time + 5);
            p->color = static_cast<float>(ramp1[0]);
            p->ramp = static_cast<float>(rand() & 3);
            p->type = (i & 1) ? ptype_t::Explode : ptype_t::Explode2;
            p->org = org + Vector3(static_cast<float>((rand() % 32) - 16), static_cast<float>((rand() % 32) - 16), static_cast<float>((rand() % 32) - 16));
            p->vel = Vector3(static_cast<float>((rand() % 512) - 256), static_cast<float>((rand() % 512) - 256), static_cast<float>((rand() % 512) - 256));
        } else {
            p->die = static_cast<float>(cl.time + 0.1 * (rand() % 5));
            p->color = static_cast<float>((color & ~7) + (rand() & 7));
            p->type = ptype_t::SlowGrav;
            p->org = org + Vector3(static_cast<float>((rand() & 15) - 8), static_cast<float>((rand() & 15) - 8), static_cast<float>((rand() & 15) - 8));
            p->vel = dir * 15;
        }
    }
}

void R_LavaSplash(const Vector3& org)
{
    for (int i = -16; i < 16; i++) {
        for (int j = -16; j < 16; j++) {
            particle_t* p = AllocParticle();
            if (!p) return;

            p->die = static_cast<float>(cl.time + 2 + (rand() & 31) * 0.02);
            p->color = static_cast<float>(224 + (rand() & 7));
            p->type = ptype_t::SlowGrav;

            Vector3 dir(static_cast<float>(j * 8 + (rand() & 7)), static_cast<float>(i * 8 + (rand() & 7)), 256.0f);
            p->org = org + Vector3(dir.x, dir.y, static_cast<float>(rand() & 63));
            dir.normalize();
            p->vel = dir * static_cast<float>(50 + (rand() & 63));
        }
    }
}

void R_TeleportSplash(const Vector3& org)
{
    for (int i = -16; i < 16; i += 4) {
        for (int j = -16; j < 16; j += 4) {
            for (int k = -24; k < 32; k += 4) {
                particle_t* p = AllocParticle();
                if (!p) return;

                p->die = static_cast<float>(cl.time + 0.2 + (rand() & 7) * 0.02);
                p->color = static_cast<float>(7 + (rand() & 7));
                p->type = ptype_t::SlowGrav;

                Vector3 dir(static_cast<float>(j * 8), static_cast<float>(i * 8), static_cast<float>(k * 8));
                p->org = org + Vector3(static_cast<float>(i + (rand() & 3)), static_cast<float>(j + (rand() & 3)), static_cast<float>(k + (rand() & 3)));
                dir.normalize();
                p->vel = dir * static_cast<float>(50 + (rand() & 63));
            }
        }
    }
}

void R_RocketTrail(Vector3 start, const Vector3& end, int type)
{
    Vector3 vec;
    float len;
    particle_t* p;
    int dec;
    static int tracercount;

    vec = end - start;
    len = vec.normalize();
    if (type < 128) {
        dec = 3;
    } else {
        dec = 1;
        type -= 128;
    }

    while (len > 0) {
        len -= dec;

        particle_t* p = AllocParticle();
        if (!p) return;

        p->vel = vec3_origin;
        p->die = static_cast<float>(cl.time + 2);


        switch (type) {
        case 0: // rocket trail
            p->ramp = static_cast<float>(rand() & 3);
            p->color = static_cast<float>(ramp3[(int)p->ramp]);
            p->type = ptype_t::Fire;
            p->org = start + Vector3(static_cast<float>((rand() % 6) - 3), static_cast<float>((rand() % 6) - 3), static_cast<float>((rand() % 6) - 3));
            break;

        case 1: // smoke smoke
            p->ramp = static_cast<float>((rand() & 3) + 2);
            p->color = static_cast<float>(ramp3[(int)p->ramp]);
            p->type = ptype_t::Fire;
            p->org = start + Vector3(static_cast<float>((rand() % 6) - 3), static_cast<float>((rand() % 6) - 3), static_cast<float>((rand() % 6) - 3));
            break;

        case 2: // blood
            p->type = ptype_t::Grav;
            p->color = static_cast<float>(67 + (rand() & 3));
            p->org = start + Vector3(static_cast<float>((rand() % 6) - 3), static_cast<float>((rand() % 6) - 3), static_cast<float>((rand() % 6) - 3));
            break;

        case 3:
        case 5: // tracer
            p->die = static_cast<float>(cl.time + 0.5);
            p->type = ptype_t::Static;
            if (type == 3) {
                p->color = static_cast<float>(52 + ((tracercount & 4) << 1));
            } else {
                p->color = static_cast<float>(230 + ((tracercount & 4) << 1));
            }

            tracercount++;

            p->org = start;
            if (tracercount & 1) {
                p->vel.x = 30 * vec.y;
                p->vel.y = 30 * -vec.x;
                p->vel.z = 0;
            } else {
                p->vel.x = 30 * -vec.y;
                p->vel.y = 30 * vec.x;
                p->vel.z = 0;
            }

            break;

        case 4: // slight blood
            p->type = ptype_t::Grav;
            p->color = static_cast<float>(67 + (rand() & 3));
            p->org = start + Vector3(static_cast<float>((rand() % 6) - 3), static_cast<float>((rand() % 6) - 3), static_cast<float>((rand() % 6) - 3));
            len -= 3;
            break;

        case 6: // voor trail
            p->color = static_cast<float>(9 * 16 + 8 + (rand() & 3));
            p->type = ptype_t::Static;
            p->die = static_cast<float>(cl.time + 0.3);
            p->org = start + Vector3(static_cast<float>((rand() & 15) - 8), static_cast<float>((rand() & 15) - 8), static_cast<float>((rand() & 15) - 8));
            break;
        }

        start += vec;
    }
}

/*
===============
R_DrawParticles
===============
*/
void R_DrawParticles(void)
{
    particle_t *p, *kill;
    float grav;
    int i;
    float time2, time3;
    float time1;
    float dvel;
    float frametime;

    D_StartParticles();

    VectorScale(vright, xscaleshrink, r_pright);
    VectorScale(vup, yscaleshrink, r_pup);
    VectorCopy(vpn, r_ppn);
    frametime = static_cast<float>(cl.time - cl.oldtime);
    time3 = frametime * 15;
    time2 = frametime * 10; // 15;
    time1 = frametime * 5;
    grav = static_cast<float>(frametime * sv_gravity.value * 0.05);
    dvel = 4 * frametime;

    for (;;) {
        kill = active_particles;
        if (kill && kill->die < cl.time) {
            active_particles = kill->next;
            kill->next = free_particles;
            free_particles = kill;
            continue;
        }

        break;
    }

    for (p = active_particles; p; p = p->next) {
        for (;;) {
            kill = p->next;
            if (kill && kill->die < cl.time) {
                p->next = kill->next;
                kill->next = free_particles;
                free_particles = kill;
                continue;
            }

            break;
        }

        D_DrawParticle(p);
        p->org[0] += p->vel[0] * frametime;
        p->org[1] += p->vel[1] * frametime;
        p->org[2] += p->vel[2] * frametime;

        switch (p->type) {
        case ptype_t::Static:
            break;
        case ptype_t::Fire:
            p->ramp += time1;
            if (p->ramp >= 6) {
                p->die = -1;
            } else {
                p->color = static_cast<float>(ramp3[(int)p->ramp]);
            }

            p->vel[2] += grav;
            break;

        case ptype_t::Explode:
            p->ramp += time2;
            if (p->ramp >= 8) {
                p->die = -1;
            } else {
                p->color = static_cast<float>(ramp1[(int)p->ramp]);
            }

            for (i = 0; i < 3; i++) {
                p->vel[i] += p->vel[i] * dvel;
            }
            p->vel[2] -= grav;
            break;

        case ptype_t::Explode2:
            p->ramp += time3;
            if (p->ramp >= 8) {
                p->die = -1;
            } else {
                p->color = static_cast<float>(ramp2[(int)p->ramp]);
            }

            for (i = 0; i < 3; i++) {
                p->vel[i] -= p->vel[i] * frametime;
            }
            p->vel[2] -= grav;
            break;

        case ptype_t::Blob:
            for (i = 0; i < 3; i++) {
                p->vel[i] += p->vel[i] * dvel;
            }
            p->vel[2] -= grav;
            break;

        case ptype_t::Blob2:
            for (i = 0; i < 2; i++) {
                p->vel[i] -= p->vel[i] * dvel;
            }
            p->vel[2] -= grav;
            break;

        case ptype_t::Grav:
        case ptype_t::SlowGrav:
            p->vel[2] -= grav;
            break;
        }
    }

    D_EndParticles();
}


// ============================================================
// Content from: src\r_sky.cpp
// ============================================================


int iskyspeed = 8;
int iskyspeed2 = 2;
float skyspeed, skyspeed2;

float skytime;

byte* r_skysource;

int r_skymade;
int r_skydirect; // not used?

// TODO: clean up these routines

eastl::array<byte, 128 * 131> bottomsky{};
eastl::array<byte, 128 * 131> bottommask{};
alignas(unsigned) eastl::array<byte, 128 * 256> newsky{}; // newsky and topsky both pack in here, 128 bytes

//  of newsky on the left of each scan, 128 bytes
//  of topsky on the right, because the low-level
//  drawers need 256-byte scan widths

/*
=============
R_InitSky

A sky texture is 256*128, with the right side being a masked overlay
==============
*/
void R_InitSky(texture_t* mt)
{
    int i, j;
    byte* src;

    src = reinterpret_cast<byte*>(mt) + mt->offsets[0];

    for (i = 0; i < 128; i++) {
        for (j = 0; j < 128; j++) {
            newsky[(i * 256) + j + 128] = src[i * 256 + j + 128];
        }
    }

    for (i = 0; i < 128; i++) {
        for (j = 0; j < 131; j++) {
            if (src[i * 256 + (j & 0x7F)]) {
                bottomsky[(i * 131) + j] = src[i * 256 + (j & 0x7F)];
                bottommask[(i * 131) + j] = 0;
            } else {
                bottomsky[(i * 131) + j] = 0;
                bottommask[(i * 131) + j] = 0xff;
            }
        }
    }

    r_skysource = newsky.data();
}

/*
=================
R_MakeSky
=================
*/
void R_MakeSky(void)
{
    int x, y;
    int ofs, baseofs;
    int xshift, yshift;
    unsigned* pnewsky;
    static int xlast = -1, ylast = -1;

    xshift = static_cast<int>(skytime * skyspeed);
    yshift = static_cast<int>(skytime * skyspeed);

    if ((xshift == xlast) && (yshift == ylast)) {
        return;
    }

    xlast = xshift;
    ylast = yshift;

    pnewsky = reinterpret_cast<unsigned*>(newsky.data());

    for (y = 0; y < SKYSIZE; y++) {
        baseofs = ((y + yshift) & SKYMASK) * 131;

#if UNALIGNED_OK

        for (x = 0; x < SKYSIZE; x += 4) {
            ofs = baseofs + ((x + xshift) & SKYMASK);

            // PORT: unaligned dword access to bottommask and bottomsky

            *pnewsky = (*(pnewsky + (128 / sizeof(unsigned))) & *reinterpret_cast<unsigned*>(&bottommask[ofs])) | *reinterpret_cast<unsigned*>(&bottomsky[ofs]);
            pnewsky++;
        }

#else

        for (x = 0; x < SKYSIZE; x++) {
            ofs = baseofs + ((x + xshift) & SKYMASK);

            *reinterpret_cast<byte*>(pnewsky) = (*(reinterpret_cast<byte*>(pnewsky) + 128) & bottommask[ofs]) | bottomsky[ofs];
            pnewsky = reinterpret_cast<unsigned*>(reinterpret_cast<byte*>(pnewsky) + 1);
        }

#endif

        pnewsky += 128 / sizeof(unsigned);
    }

    r_skymade = 1;
}

/*
=============
R_SetSkyFrame
==============
*/
void R_SetSkyFrame(void)
{
    int g, s1, s2;
    float temp;

    skyspeed = static_cast<float>(iskyspeed);
    skyspeed2 = static_cast<float>(iskyspeed2);

    g = GreatestCommonDivisor(iskyspeed, iskyspeed2);
    s1 = iskyspeed / g;
    s2 = iskyspeed2 / g;
    temp = static_cast<float>(SKYSIZE * s1 * s2);

    skytime = static_cast<float>(cl.time - ((int)(cl.time / temp) * temp));

    r_skymade = 0;
}


// ============================================================
// Content from: src\r_surf.cpp
// ============================================================
// r_surf.cpp: surface-related refresh code


drawsurf_t r_drawsurf;

int lightleft, sourcesstep, blocksize, sourcetstep;
int lightdelta, lightdeltastep;
int lightright, lightleftstep, lightrightstep, blockdivshift;
unsigned blockdivmask;
void* prowdestbase;
unsigned char* pbasesource;
int surfrowbytes; // used by ASM files
unsigned* r_lightptr;
int r_stepback;
int r_lightwidth;
int r_numhblocks, r_numvblocks;
unsigned char *r_source, *r_sourcemax;

void R_DrawSurfaceBlock8_mip0(void);
void R_DrawSurfaceBlock8_mip1(void);
void R_DrawSurfaceBlock8_mip2(void);
void R_DrawSurfaceBlock8_mip3(void);

static void (*surfmiptable[4])(void) = {
    R_DrawSurfaceBlock8_mip0, R_DrawSurfaceBlock8_mip1,
    R_DrawSurfaceBlock8_mip2, R_DrawSurfaceBlock8_mip3
};

unsigned blocklights[18 * 18];

/*
===============
R_AddDynamicLights
===============
*/
void R_AddDynamicLights(void)
{
    msurface_t* surf;
    int lnum;
    int sd, td;
    float dist, rad, minlight;
    Vector3 impact, local;
    int s, t;
    int smax, tmax;
    mtexinfo_t* tex;

    surf = r_drawsurf.surf;
    smax = (surf->extents[0] >> 4) + 1;
    tmax = (surf->extents[1] >> 4) + 1;
    tex = surf->texinfo;

    for (lnum = 0; lnum < MAX_DLIGHTS; lnum++) {
        if (!(surf->dlightbits & (1 << lnum))) {
            continue; // not lit by this light
        }

        rad = cl_dlights[lnum].radius;
        dist = cl_dlights[lnum].origin.dot(surf->plane->normal) - surf->plane->dist;
        rad -= fabs(dist);
        minlight = cl_dlights[lnum].minlight;
        if (rad < minlight) {
            continue;
        }

        minlight = rad - minlight;

        impact = cl_dlights[lnum].origin - surf->plane->normal * dist;

        local.x = impact.dot(tex->vecs[0]) + tex->vecs[0][3];
        local.y = impact.dot(tex->vecs[1]) + tex->vecs[1][3];

        local.x -= surf->texturemins[0];
        local.y -= surf->texturemins[1];

        for (t = 0; t < tmax; t++) {
            td = static_cast<int>(local.y - t * 16);
            if (td < 0) {
                td = -td;
            }

            for (s = 0; s < smax; s++) {
                sd = static_cast<int>(local.x - s * 16);
                if (sd < 0) {
                    sd = -sd;
                }

                if (sd > td) {
                    dist = static_cast<float>(sd + (td >> 1));
                } else {
                    dist = static_cast<float>(td + (sd >> 1));
                }

                if (dist < minlight)
                {
                    blocklights[t * smax + s] += static_cast<unsigned int>((rad - dist) * 256);
                }
            }
        }
    }
}

/*
===============
R_BuildLightMap

Combine and scale multiple lightmaps into the 8.8 format in blocklights
===============
*/
void R_BuildLightMap(void)
{
    int smax, tmax;
    int t;
    int i, size;
    byte* lightmap;
    unsigned scale;
    int maps;
    msurface_t* surf;

    surf = r_drawsurf.surf;

    smax = (surf->extents[0] >> 4) + 1;
    tmax = (surf->extents[1] >> 4) + 1;
    size = smax * tmax;
    lightmap = surf->samples;

    if (r_fullbright.value || !cl.worldmodel->lightdata) {
        for (i = 0; i < size; i++) {
            blocklights[i] = 0;
        }

        return;
    }

    // clear to ambient
    for (i = 0; i < size; i++) {
        blocklights[i] = r_refdef.ambientlight << 8;
    }

    // add all the lightmaps
    if (lightmap) {
        for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++) {
            scale = r_drawsurf.lightadj[maps]; // 8.8 fraction
            for (i = 0; i < size; i++) {
                blocklights[i] += lightmap[i] * scale;
            }
            lightmap += size; // skip to next lightmap
        }
    }

    // add all the dynamic lights
    if (surf->dlightframe == r_framecount) {
        R_AddDynamicLights();
    }

    // bound, invert, and shift
    for (i = 0; i < size; i++) {
        t = (255 * 256 - (int)blocklights[i]) >> (8 - VID_CBITS);

        if (t < (1 << 6)) {
            t = (1 << 6);
        }

        blocklights[i] = t;
    }
}

/*
===============
R_TextureAnimation

Returns the proper texture for a given time and base texture
===============
*/
texture_t* R_TextureAnimation(texture_t* base)
{
    int reletive;
    int count;

    if (currententity->frame) {
        if (base->alternate_anims) {
            base = base->alternate_anims;
        }
    }

    if (!base->anim_total) {
        return base;
    }

    reletive = (int)(cl.time * 10) % base->anim_total;

    count = 0;
    while (base->anim_min > reletive || base->anim_max <= reletive) {
        base = base->anim_next;
        if (!base) {
            Sys_Error("R_TextureAnimation: broken cycle");
        }

        if (++count > 100) {
            Sys_Error("R_TextureAnimation: infinite cycle");
        }
    }

    return base;
}

/*
===============
R_DrawSurface
===============
*/
void R_DrawSurface(void)
{
    unsigned char* basetptr;
    int smax, tmax, twidth;
    int u;
    int soffset, basetoffset, texwidth;
    int horzblockstep;
    unsigned char* pcolumndest;
    void (*pblockdrawer)(void);
    texture_t* mt;

    // calculate the lightings
    R_BuildLightMap();

    surfrowbytes = r_drawsurf.rowbytes;

    mt = r_drawsurf.texture;

    r_source = reinterpret_cast<byte*>(mt) + mt->offsets[r_drawsurf.surfmip];

    // the fractional light values should range from 0 to (VID_GRADES - 1) << 16
    // from a source range of 0 - 255

    texwidth = mt->width >> r_drawsurf.surfmip;

    blocksize = 16 >> r_drawsurf.surfmip;
    blockdivshift = 4 - r_drawsurf.surfmip;
    blockdivmask = (1 << blockdivshift) - 1;

    r_lightwidth = (r_drawsurf.surf->extents[0] >> 4) + 1;

    r_numhblocks = r_drawsurf.surfwidth >> blockdivshift;
    r_numvblocks = r_drawsurf.surfheight >> blockdivshift;

    //==============================

    if (r_pixbytes == 1) {
        pblockdrawer = surfmiptable[r_drawsurf.surfmip];
        // TODO: only needs to be set when there is a display settings change
        horzblockstep = blocksize;
    } else {
        pblockdrawer = R_DrawSurfaceBlock16;
        // TODO: only needs to be set when there is a display settings change
        horzblockstep = blocksize << 1;
    }

    smax = mt->width >> r_drawsurf.surfmip;
    twidth = texwidth;
    tmax = mt->height >> r_drawsurf.surfmip;
    sourcetstep = texwidth;
    r_stepback = tmax * twidth;

    r_sourcemax = r_source + (tmax * smax);

    soffset = r_drawsurf.surf->texturemins[0];
    basetoffset = r_drawsurf.surf->texturemins[1];

    // << 16 components are to guarantee positive values for %
    soffset = ((soffset >> r_drawsurf.surfmip) + (smax << 16)) % smax;
    basetptr = &r_source[(
        (((basetoffset >> r_drawsurf.surfmip) + (tmax << 16)) % tmax) * twidth)];

    pcolumndest = r_drawsurf.surfdat;

    for (u = 0; u < r_numhblocks; u++) {
        r_lightptr = blocklights + u;

        prowdestbase = pcolumndest;

        pbasesource = basetptr + soffset;

        (*pblockdrawer)();

        soffset = soffset + blocksize;
        if (soffset >= smax) {
            soffset = 0;
        }

        pcolumndest += horzblockstep;
    }
}

//=============================================================================

template<int Shift>
static inline void R_DrawSurfaceBlock8_mip_T()
{
    constexpr int BlockCount = 1 << Shift;
    const auto* psource = reinterpret_cast<const unsigned char*>(pbasesource);
    auto* prowdest = reinterpret_cast<unsigned char*>(prowdestbase);
    const auto* colormap = reinterpret_cast<const unsigned char*>(vid.colormap);

    for (int v = 0; v < r_numvblocks; v++) {
        lightleft = r_lightptr[0];
        lightright = r_lightptr[1];
        r_lightptr += r_lightwidth;
        lightleftstep = (r_lightptr[0] - lightleft) >> Shift;
        lightrightstep = (r_lightptr[1] - lightright) >> Shift;

        for (int i = 0; i < BlockCount; i++) {
            const int lightstep = (lightleft - lightright) >> Shift;
            int light = lightright;

            for (int b = BlockCount - 1; b >= 0; b--) {
                prowdest[b] = colormap[(light & 0xFF00) + psource[b]];
                light += lightstep;
            }

            psource += sourcetstep;
            lightright += lightrightstep;
            lightleft += lightleftstep;
            prowdest += surfrowbytes;
        }

        if (psource >= r_sourcemax) {
            psource -= r_stepback;
        }
    }
}

void R_DrawSurfaceBlock8_mip0() { R_DrawSurfaceBlock8_mip_T<4>(); }
void R_DrawSurfaceBlock8_mip1() { R_DrawSurfaceBlock8_mip_T<3>(); }
void R_DrawSurfaceBlock8_mip2() { R_DrawSurfaceBlock8_mip_T<2>(); }
void R_DrawSurfaceBlock8_mip3() { R_DrawSurfaceBlock8_mip_T<1>(); }


/*
================
R_DrawSurfaceBlock16

FIXME: make this work
================
*/
void R_DrawSurfaceBlock16(void)
{
    int k;
    unsigned char* psource;
    int lighttemp, lightstep, light;
    unsigned short* prowdest;

    prowdest = (unsigned short*)prowdestbase;

    for (k = 0; k < blocksize; k++) {
        unsigned short* pdest;
        unsigned char pix;
        int b;

        psource = pbasesource;
        lighttemp = lightright - lightleft;
        lightstep = lighttemp >> blockdivshift;

        light = lightleft;
        pdest = prowdest;

        for (b = 0; b < blocksize; b++) {
            pix = *psource;
            *pdest = vid.colormap16[(light & 0xFF00) + pix];
            psource += sourcesstep;
            pdest++;
            light += lightstep;
        }

        pbasesource += sourcetstep;
        lightright += lightrightstep;
        lightleft += lightleftstep;
        prowdest = (unsigned short*)((size_t)prowdest + surfrowbytes);
    }

    prowdestbase = prowdest;
}


// ============================================================
// Content from: src\r_misc.cpp
// ============================================================


/*
===============
R_CheckVariables
===============
*/
void R_CheckVariables(void)
{
    static float oldbright;

    if (r_fullbright.value != oldbright) {
        oldbright = r_fullbright.value;
        D_FlushCaches(); // so all lighting changes
    }
}

/*
====================
R_TimeRefresh_f

For program optimization
====================
*/
void R_TimeRefresh_f(void)
{
    int i;
    float start, stop, time;
    int startangle;
    vrect_t vr;

    startangle = static_cast<int>(r_refdef.viewangles[1]);

    start = static_cast<float>(Sys_FloatTime());
    for (i = 0; i < 128; i++) {
        r_refdef.viewangles[1] = static_cast<float>(i / 128.0 * 360.0);

        VID_LockBuffer();

        R_RenderView();

        VID_UnlockBuffer();

        vr.x = r_refdef.vrect.x;
        vr.y = r_refdef.vrect.y;
        vr.width = r_refdef.vrect.width;
        vr.height = r_refdef.vrect.height;
        vr.pnext = nullptr;
        VID_Update(&vr);
    }
    stop = static_cast<float>(Sys_FloatTime());
    time = stop - start;
    Con_Printf("%f seconds (%f fps)\n", time, 128 / time);

    r_refdef.viewangles[1] = static_cast<float>(startangle);
}

/*
================
R_LineGraph

Only called by R_DisplayTime
================
*/
void R_LineGraph(int x, int y, int h)
{
    int i;
    byte* dest;
    int s;

    // FIXME: should be disabled on no-buffer adapters, or should be in the driver

    x += r_refdef.vrect.x;
    y += r_refdef.vrect.y;

    dest = vid.buffer + vid.rowbytes * y + x;

    s = static_cast<int>(r_graphheight.value);

    if (h > s) {
        h = s;
    }

    for (i = 0; i < h; i++, dest -= vid.rowbytes * 2) {
        dest[0] = 0xff;
        *(dest - vid.rowbytes) = 0x30;
    }
    for (; i < s; i++, dest -= vid.rowbytes * 2) {
        dest[0] = 0x30;
        *(dest - vid.rowbytes) = 0x30;
    }
}

/*
==============
R_TimeGraph

Performance monitoring tool
==============
*/
#define MAX_TIMINGS 100
extern float mouse_x, mouse_y;

void R_TimeGraph(void)
{
    static int timex;
    int a;
    float r_time2;
    static byte r_timings[MAX_TIMINGS];
    int x;

    r_time2 = static_cast<float>(Sys_FloatTime());

    a = static_cast<int>((r_time2 - r_time1) / 0.01);
    //a = fabs(mouse_y * 0.05);
    //a = (int)((r_refdef.vieworg[2] + 1024)/1)%(int)r_graphheight.value;
    //a = fabs(velocity[0])/20;
    //a = ((int)fabs(origin[0])/8)%20;
    //a = (cl.idealpitch + 30)/5;
    r_timings[timex] = static_cast<byte>(a);
    a = timex;

    if (r_refdef.vrect.width <= MAX_TIMINGS) {
        x = r_refdef.vrect.width - 1;
    } else {
        x = r_refdef.vrect.width - (r_refdef.vrect.width - MAX_TIMINGS) / 2;
    }

    do {
        R_LineGraph(x, r_refdef.vrect.height - 2, r_timings[a]);
        if (x == 0) {
            break; // screen too small to hold entire thing
        }

        x--;
        a--;
        if (a == -1) {
            a = MAX_TIMINGS - 1;
        }
    } while (a != timex);

    timex = (timex + 1) % MAX_TIMINGS;
}

/*
=============
R_PrintAliasStats
================
*/
void R_PrintAliasStats(void)
{
    Con_Printf("%3i polygon model drawn\n", r_amodels_drawn);
}

/*
=============
R_PrintTimes
=============
*/
void R_PrintTimes(void)
{
    float r_time2;
    float ms;

    r_time2 = static_cast<float>(Sys_FloatTime());

    ms = static_cast<float>(1000 * (r_time2 - r_time1));

    Con_Printf("%5.1f ms %3i/%3i/%3i poly %3i surf\n", ms, c_faceclip,
        r_polycount, r_drawnpolycount, c_surf);
    c_surf = 0;
}

/*
=============
R_PrintDSpeeds
=============
*/
void R_PrintDSpeeds(void)
{
    float ms, dp_time, r_time2, rw_time, db_time, se_time, de_time, dv_time;

    r_time2 = static_cast<float>(Sys_FloatTime());

    dp_time = static_cast<float>((dp_time2 - dp_time1) * 1000);
    rw_time = (rw_time2 - rw_time1) * 1000;
    db_time = (db_time2 - db_time1) * 1000;
    se_time = (se_time2 - se_time1) * 1000;
    de_time = (de_time2 - de_time1) * 1000;
    dv_time = (dv_time2 - dv_time1) * 1000;
    ms = (r_time2 - r_time1) * 1000;

    Con_Printf("%3i %4.1fp %3iw %4.1fb %3is %4.1fe %4.1fv\n", (int)ms, dp_time,
        (int)rw_time, db_time, (int)se_time, de_time, dv_time);
}

/*
===================
R_TransformFrustum
===================
*/
void R_TransformFrustum(void)
{
    int i;
    Vector3 v, v2;

    for (i = 0; i < 4; i++) {
        v = Vector3(screenedge[i].normal.z, -screenedge[i].normal.x, screenedge[i].normal.y);

        v2 = vright * v.y + vup * v.z + vpn * v.x;

        view_clipplanes[i].normal = v2;

        view_clipplanes[i].dist = modelorg.dot(v2);
    }
}

/*
================
TransformVector
================
*/
void TransformVector(const Vector3& in, Vector3& out)
{
    out.x = in.dot(vright);
    out.y = in.dot(vup);
    out.z = in.dot(vpn);
}

/*
===============
R_SetUpFrustumIndexes
===============
*/
void R_SetUpFrustumIndexes(void)
{
    int i, j, *pindex;

    pindex = r_frustum_indexes.data();

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 3; j++) {
            if (view_clipplanes[i].normal[j] < 0) {
                pindex[j] = j;
                pindex[j + 3] = j + 3;
            } else {
                pindex[j] = j + 3;
                pindex[j + 3] = j;
            }
        }

        // FIXME: do just once at start
        pfrustum_indexes[i] = pindex;
        pindex += 6;
    }
}

/*
===============
R_SetupFrame
===============
*/
void R_SetupFrame(void)
{
    int edgecount;
    vrect_t vrect;
    float w, h;

    // don't allow cheats in multiplayer
    if (cl.maxclients > 1) {
        Cvar::Set("r_draworder", "0");
        Cvar::Set("r_fullbright", "0");
        Cvar::Set("r_ambient", "0");
        Cvar::Set("r_drawflat", "0");
    }

    if (r_numsurfs.value) {
        if ((surface_p - surfaces) > r_maxsurfsseen) {
            r_maxsurfsseen = static_cast<int>(surface_p - surfaces);
        }

        Con_Printf("Used %d of %d surfs; %d max\n", surface_p - surfaces,
            surf_max - surfaces, r_maxsurfsseen);
    }

    if (r_numedges.value) {
        edgecount = static_cast<int>(edge_p - r_edges);

        if (edgecount > r_maxedgesseen) {
            r_maxedgesseen = edgecount;
        }

        Con_Printf("Used %d of %d edges; %d max\n", edgecount, r_numallocatededges,
            r_maxedgesseen);
    }

    r_refdef.ambientlight = static_cast<int>(r_ambient.value);

    if (r_refdef.ambientlight < 0) {
        r_refdef.ambientlight = 0;
    }

    if (!sv.active) {
        r_draworder.value = 0; // don't let cheaters look behind walls
    }

    R_CheckVariables();

    R_AnimateLight();

    r_framecount++;

    numbtofpolys = 0;

    // build the transformation matrix for the given view angles
    VectorCopy(r_refdef.vieworg, modelorg);
    VectorCopy(r_refdef.vieworg, r_origin);

    AngleVectors(r_refdef.viewangles, vpn, vright, vup);

    // current viewleaf
    r_oldviewleaf = r_viewleaf;
    r_viewleaf = Mod_PointInLeaf(r_origin, cl.worldmodel);

    r_dowarpold = r_dowarp;
    r_dowarp = r_waterwarp.value && (r_viewleaf->contents <= CONTENTS_WATER);

    if ((r_dowarp != r_dowarpold) || r_viewchanged || lcd_x.value) {
        if (r_dowarp) {
            if (((int)vid.width <= vid.maxwarpwidth) && ((int)vid.height <= vid.maxwarpheight)) {
                vrect.x = 0;
                vrect.y = 0;
                vrect.width = vid.width;
                vrect.height = vid.height;

                R_ViewChanged(&vrect, sb_lines, vid.aspect);
            } else {
                w = static_cast<float>(vid.width);
                h = static_cast<float>(vid.height);

                if (w > vid.maxwarpwidth) {
                    h *= static_cast<float>(vid.maxwarpwidth) / w;
                    w = static_cast<float>(vid.maxwarpwidth);
                }

                if (h > vid.maxwarpheight) {
                    h = static_cast<float>(vid.maxwarpheight);
                    w *= static_cast<float>(vid.maxwarpheight) / h;
                }

                vrect.x = 0;
                vrect.y = 0;
                vrect.width = (int)w;
                vrect.height = (int)h;

                R_ViewChanged(
                    &vrect, static_cast<int>(static_cast<float>(sb_lines) * (h / static_cast<float>(vid.height))),
                    vid.aspect * (h / w) * (static_cast<float>(vid.width) / static_cast<float>(vid.height)));
            }
        } else {
            vrect.x = 0;
            vrect.y = 0;
            vrect.width = vid.width;
            vrect.height = vid.height;

            R_ViewChanged(&vrect, sb_lines, vid.aspect);
        }

        r_viewchanged = false;
    }

    // start off with just the four screen edge clip planes
    R_TransformFrustum();

    // save base values
    VectorCopy(vpn, base_vpn);
    VectorCopy(vright, base_vright);
    VectorCopy(vup, base_vup);
    VectorCopy(modelorg, base_modelorg);

    R_SetSkyFrame();

    R_SetUpFrustumIndexes();

    r_cache_thrash = false;

    // clear frame counts
    c_faceclip = 0;
    d_spanpixcount = 0;
    r_polycount = 0;
    r_drawnpolycount = 0;
    r_wholepolycount = 0;
    r_amodels_drawn = 0;
    r_outofsurfaces = 0;
    r_outofedges = 0;

    D_SetupFrame();
}


// ============================================================
// Content from: src\r_draw.cpp
// ============================================================


#define MAXLEFTCLIPEDGES 100

// !!! if these are changed, they must be changed in asm_draw.h too !!!
#define FULLY_CLIPPED_CACHED 0x80000000
#define FRAMECOUNT_MASK 0x7FFFFFFF

unsigned int cacheoffset;

zpointdesc_t r_zpointdesc;

polydesc_t r_polydesc;

clipplane_t* entity_clipplanes;

medge_t* r_pedge;

bool r_leftclipped, r_rightclipped;
static bool makeleftedge, makerightedge;
bool r_nearzionly;

eastl::array<int, SIN_BUFFER_SIZE> sintable;
eastl::array<int, SIN_BUFFER_SIZE> intsintable;

mvertex_t r_leftenter, r_leftexit;
mvertex_t r_rightenter, r_rightexit;

typedef struct {
    float u, v;
    int ceilv;
} evert_t;

int r_emitted;
float r_nearzi;
float r_u1, r_v1, r_lzi1;
int r_ceilv1;

bool r_lastvertvalid;

/*
================
R_EmitEdge
================
*/
void R_EmitEdge(mvertex_t* pv0, mvertex_t* pv1)
{
    edge_t *edge, *pcheck;
    int64_t u_check; // Changed from int to int64_t
    float u, u_step;
    Vector3 local, transformed;
    float* world;
    int v, v2, ceilv0;
    float scale, lzi0, u0, v0;
    int side;

    if (r_lastvertvalid) {
        u0 = r_u1;
        v0 = r_v1;
        lzi0 = r_lzi1;
        ceilv0 = r_ceilv1;
    } else {
        world = &pv0->position[0];

        // transform and project
        local = Vector3(world) - modelorg;
        TransformVector(local, transformed);

        if (transformed.z < NEAR_CLIP) {
            transformed.z = (vec_t)NEAR_CLIP;
        }

        lzi0 = static_cast<float>(1.0 / transformed.z);

        // FIXME: build x/yscale into transform?
        scale = xscale * lzi0;
        u0 = (xcenter + scale * transformed.x);
        if (u0 < r_refdef.fvrectx_adj) {
            u0 = r_refdef.fvrectx_adj;
        }

        if (u0 > r_refdef.fvrectright_adj) {
            u0 = r_refdef.fvrectright_adj;
        }

        scale = yscale * lzi0;
        v0 = (ycenter - scale * transformed.y);
        if (v0 < r_refdef.fvrecty_adj) {
            v0 = r_refdef.fvrecty_adj;
        }

        if (v0 > r_refdef.fvrectbottom_adj) {
            v0 = r_refdef.fvrectbottom_adj;
        }

        ceilv0 = (int)ceil(v0);
    }

    world = &pv1->position[0];

    // transform and project
    local = Vector3(world) - modelorg;
    TransformVector(local, transformed);

    if (transformed.z < NEAR_CLIP) {
        transformed.z = (vec_t)NEAR_CLIP;
    }

    r_lzi1 = static_cast<float>(1.0 / transformed.z);

    scale = xscale * r_lzi1;
    r_u1 = (xcenter + scale * transformed.x);
    if (r_u1 < r_refdef.fvrectx_adj) {
        r_u1 = r_refdef.fvrectx_adj;
    }

    if (r_u1 > r_refdef.fvrectright_adj) {
        r_u1 = r_refdef.fvrectright_adj;
    }

    scale = yscale * r_lzi1;
    r_v1 = (ycenter - scale * transformed.y);
    if (r_v1 < r_refdef.fvrecty_adj) {
        r_v1 = r_refdef.fvrecty_adj;
    }

    if (r_v1 > r_refdef.fvrectbottom_adj) {
        r_v1 = r_refdef.fvrectbottom_adj;
    }

    if (r_lzi1 > lzi0) {
        lzi0 = r_lzi1;
    }

    if (lzi0 > r_nearzi) { // for mipmap finding
        r_nearzi = lzi0;
    }

    // for right edges, all we want is the effect on 1/z
    if (r_nearzionly) {
        return;
    }

    r_emitted = 1;

    r_ceilv1 = (int)ceil(r_v1);

    // create the edge
    if (ceilv0 == r_ceilv1) {
        // we cache unclipped horizontal edges as fully clipped
        if (cacheoffset != 0x7FFFFFFF) {
            cacheoffset = FULLY_CLIPPED_CACHED | (r_framecount & FRAMECOUNT_MASK);
        }

        return; // horizontal edge
    }

    side = ceilv0 > r_ceilv1;

    edge = edge_p++;

    edge->owner = r_pedge;

    edge->nearzi = lzi0;

    if (side == 0) {
        // trailing edge (go from p1 to p2)
        v = ceilv0;
        v2 = r_ceilv1 - 1;

        edge->surfs[0] = static_cast<unsigned short>(surface_p - surfaces);
        edge->surfs[1] = 0;

        u_step = ((r_u1 - u0) / (r_v1 - v0));
        u = u0 + ((float)v - v0) * u_step;
    } else {
        // leading edge (go from p2 to p1)
        v2 = ceilv0 - 1;
        v = r_ceilv1;

        edge->surfs[0] = 0;
        edge->surfs[1] = static_cast<unsigned short>(surface_p - surfaces);

        u_step = ((u0 - r_u1) / (v0 - r_v1));
        u = r_u1 + ((float)v - r_v1) * u_step;
    }

    edge->u_step = static_cast<int64_t>(u_step * 0x100000);
    edge->u = static_cast<int64_t>(u * 0x100000 + 0xFFFFF);

    // we need to do this to avoid stepping off the edges if a very nearly
    // horizontal edge is less than epsilon above a scan, and numeric error causes
    // it to incorrectly extend to the scan, and the extension of the line goes off
    // the edge of the screen
    // FIXME: is this actually needed?
    if (edge->u < r_refdef.vrect_x_adj_shift20) {
        edge->u = r_refdef.vrect_x_adj_shift20;
    }

    if (edge->u > r_refdef.vrectright_adj_shift20) {
        edge->u = r_refdef.vrectright_adj_shift20;
    }

    //
    // sort the edge in normally
    //
    u_check = edge->u;
    if (edge->surfs[0]) {
        u_check++; // sort trailers after leaders
    }

    if (!newedges[v] || newedges[v]->u >= u_check) {
        edge->next = newedges[v];
        newedges[v] = edge;
    } else {
        pcheck = newedges[v];
        while (pcheck->next && pcheck->next->u < u_check) {
            pcheck = pcheck->next;
        }
        edge->next = pcheck->next;
        pcheck->next = edge;
    }

    edge->nextremove = removeedges[v2];
    removeedges[v2] = edge;
}

/*
================
R_ClipEdge
================
*/
void R_ClipEdge(mvertex_t* pv0, mvertex_t* pv1, clipplane_t* clip)
{
    float d0, d1, f;
    mvertex_t clipvert;

    if (clip) {
        do {
            d0 = DotProduct(pv0->position, clip->normal) - clip->dist;
            d1 = DotProduct(pv1->position, clip->normal) - clip->dist;

            if (d0 >= 0) {
                // point 0 is unclipped
                if (d1 >= 0) {
                    // both points are unclipped
                    continue;
                }

                // only point 1 is clipped

                // we don't cache clipped edges
                cacheoffset = 0x7FFFFFFF;

                f = d0 / (d0 - d1);
                clipvert.position[0] = pv0->position[0] + f * (pv1->position[0] - pv0->position[0]);
                clipvert.position[1] = pv0->position[1] + f * (pv1->position[1] - pv0->position[1]);
                clipvert.position[2] = pv0->position[2] + f * (pv1->position[2] - pv0->position[2]);

                if (clip->leftedge) {
                    r_leftclipped = true;
                    r_leftexit = clipvert;
                } else if (clip->rightedge) {
                    r_rightclipped = true;
                    r_rightexit = clipvert;
                }

                R_ClipEdge(pv0, &clipvert, clip->next);

                return;
            } else {
                // point 0 is clipped
                if (d1 < 0) {
                    // both points are clipped
                    // we do cache fully clipped edges
                    if (!r_leftclipped) {
                        cacheoffset = FULLY_CLIPPED_CACHED | (r_framecount & FRAMECOUNT_MASK);
                    }

                    return;
                }

                // only point 0 is clipped
                r_lastvertvalid = false;

                // we don't cache partially clipped edges
                cacheoffset = 0x7FFFFFFF;

                f = d0 / (d0 - d1);
                clipvert.position[0] = pv0->position[0] + f * (pv1->position[0] - pv0->position[0]);
                clipvert.position[1] = pv0->position[1] + f * (pv1->position[1] - pv0->position[1]);
                clipvert.position[2] = pv0->position[2] + f * (pv1->position[2] - pv0->position[2]);

                if (clip->leftedge) {
                    r_leftclipped = true;
                    r_leftenter = clipvert;
                } else if (clip->rightedge) {
                    r_rightclipped = true;
                    r_rightenter = clipvert;
                }

                R_ClipEdge(&clipvert, pv1, clip->next);

                return;
            }
        } while ((clip = clip->next) != nullptr);
    }

    // add the edge
    R_EmitEdge(pv0, pv1);
}

/*
================
R_EmitCachedEdge
================
*/
void R_EmitCachedEdge(void)
{
    edge_t* pedge_t;

    pedge_t = (edge_t*)((size_t)r_edges + r_pedge->cachededgeoffset);

    if (!pedge_t->surfs[0]) {
        pedge_t->surfs[0] = static_cast<unsigned short>(surface_p - surfaces);
    } else {
        pedge_t->surfs[1] = static_cast<unsigned short>(surface_p - surfaces);
    }

    if (pedge_t->nearzi > r_nearzi) { // for mipmap finding
        r_nearzi = pedge_t->nearzi;
    }

    r_emitted = 1;
}

/*
================
R_RenderFace
================
*/
void R_RenderFace(msurface_t* fa, int clipflags)
{
    int i, lindex;
    unsigned mask;
    mplane_t* pplane;
    float distinv;
    Vector3 p_normal;
    medge_t *pedges;
    static medge_t tedge;
    clipplane_t* pclip;

    // skip out if no more surfs
    if ((surface_p) >= surf_max) {
        r_outofsurfaces++;

        return;
    }

    // ditto if not enough edges left, or switch to auxedges if possible
    if ((edge_p + fa->numedges + 4) >= edge_max) {
        r_outofedges += fa->numedges;

        return;
    }

    c_faceclip++;

    // set up clip planes
    pclip = nullptr;

    for (i = 3, mask = 0x08; i >= 0; i--, mask >>= 1) {
        if (clipflags & mask) {
            view_clipplanes[i].next = pclip;
            pclip = &view_clipplanes[i];
        }
    }

    // push the edges through
    r_emitted = 0;
    r_nearzi = 0;
    r_nearzionly = false;
    makeleftedge = makerightedge = false;
    pedges = currententity->model->edges;
    r_lastvertvalid = false;

    for (i = 0; i < fa->numedges; i++) {
        lindex = currententity->model->surfedges[fa->firstedge + i];

        if (lindex > 0) {
            r_pedge = &pedges[lindex];

            // if the edge is cached, we can just reuse the edge
            if (!insubmodel) {
                if (r_pedge->cachededgeoffset & FULLY_CLIPPED_CACHED) {
                    if ((r_pedge->cachededgeoffset & FRAMECOUNT_MASK) == (unsigned int)r_framecount) {
                        r_lastvertvalid = false;
                        continue;
                    }
                } else {
                    if ((((size_t)edge_p - (size_t)r_edges) > r_pedge->cachededgeoffset) && (((edge_t*)((size_t)r_edges + r_pedge->cachededgeoffset))->owner == r_pedge)) {
                        R_EmitCachedEdge();
                        r_lastvertvalid = false;
                        continue;
                    }
                }
            }

            // assume it's cacheable
            cacheoffset = static_cast<unsigned int>(reinterpret_cast<byte*>(edge_p) - reinterpret_cast<byte*>(r_edges));
            r_leftclipped = r_rightclipped = false;
            R_ClipEdge(&r_pcurrentvertbase[r_pedge->v[0]],
                &r_pcurrentvertbase[r_pedge->v[1]], pclip);
            r_pedge->cachededgeoffset = cacheoffset;

            if (r_leftclipped) {
                makeleftedge = true;
            }

            if (r_rightclipped) {
                makerightedge = true;
            }

            r_lastvertvalid = true;
        } else {
            lindex = -lindex;
            r_pedge = &pedges[lindex];
            // if the edge is cached, we can just reuse the edge
            if (!insubmodel) {
                if (r_pedge->cachededgeoffset & FULLY_CLIPPED_CACHED) {
                    if ((r_pedge->cachededgeoffset & FRAMECOUNT_MASK) == (unsigned int)r_framecount) {
                        r_lastvertvalid = false;
                        continue;
                    }
                } else {
                    // it's cached if the cached edge is valid and is owned
                    // by this medge_t
                    if ((((size_t)edge_p - (size_t)r_edges) > r_pedge->cachededgeoffset) && (((edge_t*)((size_t)r_edges + r_pedge->cachededgeoffset))->owner == r_pedge)) {
                        R_EmitCachedEdge();
                        r_lastvertvalid = false;
                        continue;
                    }
                }
            }

            // assume it's cacheable
            cacheoffset = static_cast<unsigned int>(reinterpret_cast<byte*>(edge_p) - reinterpret_cast<byte*>(r_edges));
            r_leftclipped = r_rightclipped = false;
            R_ClipEdge(&r_pcurrentvertbase[r_pedge->v[1]],
                &r_pcurrentvertbase[r_pedge->v[0]], pclip);
            r_pedge->cachededgeoffset = cacheoffset;

            if (r_leftclipped) {
                makeleftedge = true;
            }

            if (r_rightclipped) {
                makerightedge = true;
            }

            r_lastvertvalid = true;
        }
    }

    // if there was a clip off the left edge, add that edge too
    // FIXME: faster to do in screen space?
    // FIXME: share clipped edges?
    if (makeleftedge) {
        r_pedge = &tedge;
        r_lastvertvalid = false;
        R_ClipEdge(&r_leftexit, &r_leftenter, pclip->next);
    }

    // if there was a clip off the right edge, get the right r_nearzi
    if (makerightedge) {
        r_pedge = &tedge;
        r_lastvertvalid = false;
        r_nearzionly = true;
        R_ClipEdge(&r_rightexit, &r_rightenter, view_clipplanes[1].next);
    }

    // if no edges made it out, return without posting the surface
    if (!r_emitted) {
        return;
    }

    r_polycount++;

    surface_p->data = (void*)fa;
    surface_p->nearzi = r_nearzi;
    surface_p->flags = fa->flags;
    surface_p->insubmodel = insubmodel;
    surface_p->spanstate = 0;
    surface_p->entity = currententity;
    surface_p->key = r_currentkey++;
    surface_p->spans = nullptr;

    pplane = fa->plane;
    // FIXME: cache this?
    TransformVector(pplane->normal, p_normal);
    // FIXME: cache this?
    distinv = static_cast<float>(1.0 / (pplane->dist - modelorg.dot(pplane->normal)));

    surface_p->d_zistepu = p_normal.x * xscaleinv * distinv;
    surface_p->d_zistepv = -p_normal.y * yscaleinv * distinv;
    surface_p->d_ziorigin = p_normal.z * distinv - xcenter * surface_p->d_zistepu - ycenter * surface_p->d_zistepv;

    //JDC	VectorCopy (r_worldmodelorg, surface_p->modelorg);
    surface_p++;
}

/*
================
R_RenderBmodelFace
================
*/
void R_RenderBmodelFace(bedge_t* pedges, msurface_t* psurf)
{
    int i;
    unsigned mask;
    mplane_t* pplane;
    float distinv;
    Vector3 p_normal;
    static medge_t tedge;
    clipplane_t* pclip;

    // skip out if no more surfs
    if (surface_p >= surf_max) {
        r_outofsurfaces++;

        return;
    }

    // ditto if not enough edges left, or switch to auxedges if possible
    if ((edge_p + psurf->numedges + 4) >= edge_max) {
        r_outofedges += psurf->numedges;

        return;
    }

    c_faceclip++;

    // this is a dummy to give the caching mechanism someplace to write to
    r_pedge = &tedge;

    // set up clip planes
    pclip = nullptr;

    for (i = 3, mask = 0x08; i >= 0; i--, mask >>= 1) {
        if (r_clipflags & mask) {
            view_clipplanes[i].next = pclip;
            pclip = &view_clipplanes[i];
        }
    }

    // push the edges through
    r_emitted = 0;
    r_nearzi = 0;
    r_nearzionly = false;
    makeleftedge = makerightedge = false;
    // FIXME: keep clipped bmodel edges in clockwise order so last vertex caching
    // can be used?
    r_lastvertvalid = false;

    for (; pedges; pedges = pedges->pnext) {
        r_leftclipped = r_rightclipped = false;
        R_ClipEdge(pedges->v[0], pedges->v[1], pclip);

        if (r_leftclipped) {
            makeleftedge = true;
        }

        if (r_rightclipped) {
            makerightedge = true;
        }
    }

    // if there was a clip off the left edge, add that edge too
    // FIXME: faster to do in screen space?
    // FIXME: share clipped edges?
    if (makeleftedge) {
        r_pedge = &tedge;
        R_ClipEdge(&r_leftexit, &r_leftenter, pclip->next);
    }

    // if there was a clip off the right edge, get the right r_nearzi
    if (makerightedge) {
        r_pedge = &tedge;
        r_nearzionly = true;
        R_ClipEdge(&r_rightexit, &r_rightenter, view_clipplanes[1].next);
    }

    // if no edges made it out, return without posting the surface
    if (!r_emitted) {
        return;
    }

    r_polycount++;

    surface_p->data = (void*)psurf;
    surface_p->nearzi = r_nearzi;
    surface_p->flags = psurf->flags;
    surface_p->insubmodel = true;
    surface_p->spanstate = 0;
    surface_p->entity = currententity;
    surface_p->key = r_currentbkey;
    surface_p->spans = nullptr;

    pplane = psurf->plane;
    // FIXME: cache this?
    TransformVector(pplane->normal, p_normal);
    // FIXME: cache this?
    distinv = static_cast<float>(1.0 / (pplane->dist - modelorg.dot(pplane->normal)));

    surface_p->d_zistepu = p_normal.x * xscaleinv * distinv;
    surface_p->d_zistepv = -p_normal.y * yscaleinv * distinv;
    surface_p->d_ziorigin = p_normal.z * distinv - xcenter * surface_p->d_zistepu - ycenter * surface_p->d_zistepv;

    //JDC	VectorCopy (r_worldmodelorg, surface_p->modelorg);
    surface_p++;
}

/*
================
R_RenderPoly
================
*/
void R_RenderPoly(msurface_t* fa, int clipflags)
{
    int i, lindex, lnumverts, s_axis, t_axis;
    float dist, lastdist, lzi, scale, u, v, frac;
    unsigned mask;
    Vector3 local, transformed;
    clipplane_t* pclip;
    medge_t* pedges;
    mplane_t* pplane;
    mvertex_t verts[2][100]; //FIXME: do real number
    polyvert_t pverts[100];  //FIXME: do real number, safely
    int vertpage, newverts, newpage, lastvert;
    bool visible;

    // FIXME: clean this up and make it faster
    // FIXME: guard against running out of vertices

    s_axis = t_axis = 0; // keep compiler happy

    // set up clip planes
    pclip = nullptr;

    for (i = 3, mask = 0x08; i >= 0; i--, mask >>= 1) {
        if (clipflags & mask) {
            view_clipplanes[i].next = pclip;
            pclip = &view_clipplanes[i];
        }
    }

    // reconstruct the polygon
    // FIXME: these should be precalculated and loaded off disk
    pedges = currententity->model->edges;
    lnumverts = fa->numedges;
    vertpage = 0;

    for (i = 0; i < lnumverts; i++) {
        lindex = currententity->model->surfedges[fa->firstedge + i];

        if (lindex > 0) {
            r_pedge = &pedges[lindex];
            verts[0][i] = r_pcurrentvertbase[r_pedge->v[0]];
        } else {
            r_pedge = &pedges[-lindex];
            verts[0][i] = r_pcurrentvertbase[r_pedge->v[1]];
        }
    }

    // clip the polygon, done if not visible
    while (pclip) {
        lastvert = lnumverts - 1;
        lastdist = Vector3(verts[vertpage][lastvert].position).dot(pclip->normal) - pclip->dist;

        visible = false;
        newverts = 0;
        newpage = vertpage ^ 1;

        for (i = 0; i < lnumverts; i++) {
            dist = Vector3(verts[vertpage][i].position).dot(pclip->normal) - pclip->dist;

            if ((lastdist > 0) != (dist > 0)) {
                frac = dist / (dist - lastdist);
                Vector3 interp = Vector3(verts[vertpage][i].position) + (Vector3(verts[vertpage][lastvert].position) - Vector3(verts[vertpage][i].position)) * frac;
                verts[newpage][newverts].position[0] = interp.x;
                verts[newpage][newverts].position[1] = interp.y;
                verts[newpage][newverts].position[2] = interp.z;
                newverts++;
            }

            if (dist >= 0) {
                verts[newpage][newverts] = verts[vertpage][i];
                newverts++;
                visible = true;
            }

            lastvert = i;
            lastdist = dist;
        }

        if (!visible || (newverts < 3)) {
            return;
        }

        lnumverts = newverts;
        vertpage ^= 1;
        pclip = pclip->next;
    }

    // transform and project, remembering the z values at the vertices and
    // r_nearzi, and extract the s and t coordinates at the vertices
    pplane = fa->plane;
    switch (pplane->type) {
    case PLANE_X:
    case PLANE_ANYX:
        s_axis = 1;
        t_axis = 2;
        break;
    case PLANE_Y:
    case PLANE_ANYY:
        s_axis = 0;
        t_axis = 2;
        break;
    case PLANE_Z:
    case PLANE_ANYZ:
        s_axis = 0;
        t_axis = 1;
        break;
    }

    r_nearzi = 0;

    for (i = 0; i < lnumverts; i++) {
        // transform and project
        local = Vector3(verts[vertpage][i].position) - modelorg;
        TransformVector(local, transformed);

        if (transformed.z < NEAR_CLIP) {
            transformed.z = (vec_t)NEAR_CLIP;
        }

        lzi = static_cast<float>(1.0 / transformed.z);

        if (lzi > r_nearzi) { // for mipmap finding
            r_nearzi = lzi;
        }

        // FIXME: build x/yscale into transform?
        scale = xscale * lzi;
        u = (xcenter + scale * transformed.x);
        if (u < r_refdef.fvrectx_adj) {
            u = r_refdef.fvrectx_adj;
        }

        if (u > r_refdef.fvrectright_adj) {
            u = r_refdef.fvrectright_adj;
        }

        scale = yscale * lzi;
        v = (ycenter - scale * transformed.y);
        if (v < r_refdef.fvrecty_adj) {
            v = r_refdef.fvrecty_adj;
        }

        if (v > r_refdef.fvrectbottom_adj) {
            v = r_refdef.fvrectbottom_adj;
        }

        pverts[i].u = u;
        pverts[i].v = v;
        pverts[i].zi = lzi;
        pverts[i].s = verts[vertpage][i].position[s_axis];
        pverts[i].t = verts[vertpage][i].position[t_axis];
    }

    // build the polygon descriptor, including fa, r_nearzi, and u, v, s, t, and z
    // for each vertex
    r_polydesc.numverts = lnumverts;
    r_polydesc.nearzi = r_nearzi;
    r_polydesc.pcurrentface = fa;
    r_polydesc.pverts = pverts;

    // draw the polygon
    D_DrawPoly();
}

/*
================
R_ZDrawSubmodelPolys
================
*/
void R_ZDrawSubmodelPolys(model_t* pmodel)
{
    int i, numsurfaces;
    msurface_t* psurf;
    float dot;
    mplane_t* pplane;

    psurf = &pmodel->surfaces[pmodel->firstmodelsurface];
    numsurfaces = pmodel->nummodelsurfaces;

    for (i = 0; i < numsurfaces; i++, psurf++) {
        // find which side of the node we are on
        pplane = psurf->plane;

        dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

        // draw the polygon
        if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) || (!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON))) {
            // FIXME: use bounding-box-based frustum clipping info?
            R_RenderPoly(psurf, 15);
        }
    }
}


// ============================================================
// Content from: src\r_edge.cpp
// ============================================================

#include <limits.h>


surf_t *surfaces, *surface_p, *surf_max;

// surfaces are generated in back to front order by the bsp, so if a surf
// pointer is greater than another one, it should be drawn in front
// surfaces[1] is the background, and is used as the active surface stack

espan_t *span_p, *max_span_p;

extern int screenwidth;

int current_iv;

int64_t edge_head_u_shift20, edge_tail_u_shift20; // Changed from int to int64_t

static void (*pdrawfunc)(void);

edge_t edge_sentinel;

float edge_fv;

void R_GenerateSpans(void);
void R_GenerateSpansBackward(void);

void R_LeadingEdge(edge_t* edge);
void R_LeadingEdgeBackwards(edge_t* edge);
void R_TrailingEdge(surf_t* surf, edge_t* edge);

//=============================================================================

/*
==============
R_DrawCulledPolys
==============
*/
void R_DrawCulledPolys(void)
{
    surf_t* s;
    msurface_t* pface;

    currententity = &cl_entities[0];

    if (r_worldpolysbacktofront) {
        for (s = surface_p - 1; s > &surfaces[1]; s--) {
            if (!s->spans) {
                continue;
            }

            if (!(s->flags & SURF_DRAWBACKGROUND)) {
                pface = (msurface_t*)s->data;
                R_RenderPoly(pface, 15);
            }
        }
    } else {
        for (s = &surfaces[1]; s < surface_p; s++) {
            if (!s->spans) {
                continue;
            }

            if (!(s->flags & SURF_DRAWBACKGROUND)) {
                pface = (msurface_t*)s->data;
                R_RenderPoly(pface, 15);
            }
        }
    }
}

/*
==============
R_BeginEdgeFrame
==============
*/
void R_BeginEdgeFrame(void)
{
    int v;

    edge_p = r_edges;
    edge_max = &r_edges[r_numallocatededges];

    surface_p = &surfaces[2]; // background is surface 1,
    //  surface 0 is a dummy
    surfaces[1].spans = nullptr; // no background spans yet
    surfaces[1].flags = SURF_DRAWBACKGROUND;

    // put the background behind everything in the world
    if (r_draworder.value) {
        pdrawfunc = R_GenerateSpansBackward;
        surfaces[1].key = 0;
        r_currentkey = 1;
    } else {
        pdrawfunc = R_GenerateSpans;
        surfaces[1].key = 0x7FFFFFFF;
        r_currentkey = 0;
    }

    // FIXME: set with memset
    for (v = r_refdef.vrect.y; v < r_refdef.vrectbottom; v++) {
        newedges[v] = removeedges[v] = nullptr;
    }
}

/*
==============
R_InsertNewEdges

Adds the edges in the linked list edgestoadd, adding them to the edges in the
linked list edgelist.  edgestoadd is assumed to be sorted on u, and non-empty (this is actually newedges[v]).  edgelist is assumed to be sorted on u, with a
sentinel at the end (actually, this is the active edge table starting at
edge_head.next).
==============
*/
void R_InsertNewEdges(edge_t* edgestoadd, edge_t* edgelist)
{
    edge_t* next_edge;

    do {
        next_edge = edgestoadd->next;
    edgesearch:
        if (edgelist->u >= edgestoadd->u) {
            goto addedge;
        }

        edgelist = edgelist->next;
        if (edgelist->u >= edgestoadd->u) {
            goto addedge;
        }

        edgelist = edgelist->next;
        if (edgelist->u >= edgestoadd->u) {
            goto addedge;
        }

        edgelist = edgelist->next;
        if (edgelist->u >= edgestoadd->u) {
            goto addedge;
        }

        edgelist = edgelist->next;
        goto edgesearch;

        // insert edgestoadd before edgelist
    addedge:
        edgestoadd->next = edgelist;
        edgestoadd->prev = edgelist->prev;
        edgelist->prev->next = edgestoadd;
        edgelist->prev = edgestoadd;
    } while ((edgestoadd = next_edge) != nullptr);
}

/*
==============
R_RemoveEdges
==============
*/
void R_RemoveEdges(edge_t* pedge)
{
    do {
        pedge->next->prev = pedge->prev;
        pedge->prev->next = pedge->next;
    } while ((pedge = pedge->nextremove) != nullptr);
}

/*
==============
R_StepActiveU
==============
*/
void R_StepActiveU(edge_t* pedge)
{
    edge_t *pnext_edge, *pwedge;

    while (1) {
    nextedge:
        pedge->u += pedge->u_step;
        if (pedge->u < pedge->prev->u) {
            goto pushback;
        }

        pedge = pedge->next;

        pedge->u += pedge->u_step;
        if (pedge->u < pedge->prev->u) {
            goto pushback;
        }

        pedge = pedge->next;

        pedge->u += pedge->u_step;
        if (pedge->u < pedge->prev->u) {
            goto pushback;
        }

        pedge = pedge->next;

        pedge->u += pedge->u_step;
        if (pedge->u < pedge->prev->u) {
            goto pushback;
        }

        pedge = pedge->next;

        goto nextedge;

    pushback:
        if (pedge == &edge_aftertail) {
            return;
        }

        // push it back to keep it sorted
        pnext_edge = pedge->next;

        // pull the edge out of the edge list
        pedge->next->prev = pedge->prev;
        pedge->prev->next = pedge->next;

        // find out where the edge goes in the edge list
        pwedge = pedge->prev->prev;

        while (pwedge->u > pedge->u) {
            pwedge = pwedge->prev;
        }

        // put the edge back into the edge list
        pedge->next = pwedge->next;
        pedge->prev = pwedge;
        pedge->next->prev = pedge;
        pwedge->next = pedge;

        pedge = pnext_edge;
        if (pedge == &edge_tail) {
            return;
        }
    }
}

/*
==============
R_CleanupSpan
==============
*/
void R_CleanupSpan()
{
    surf_t* surf;
    int iu;
    espan_t* span;

    // now that we've reached the right edge of the screen, we're done with any
    // unfinished surfaces, so emit a span for whatever's on top
    surf = surfaces[1].next;
    iu = static_cast<int>(edge_tail_u_shift20);
    if (iu > surf->last_u) {
        span = span_p++;
        span->u = surf->last_u;
        span->count = iu - span->u;
        span->v = current_iv;
        span->pnext = surf->spans;
        surf->spans = span;
    }

    // reset spanstate for all surfaces in the surface stack
    do {
        surf->spanstate = 0;
        surf = surf->next;
    } while (surf != &surfaces[1]);
}

/*
==============
R_LeadingEdgeBackwards
==============
*/
void R_LeadingEdgeBackwards(edge_t* edge)
{
    espan_t* span;
    surf_t *surf, *surf2;
    int iu;

    // it's adding a new surface in, so find the correct place
    surf = &surfaces[edge->surfs[1]];

    // don't start a span if this is an inverted span, with the end
    // edge preceding the start edge (that is, we've already seen the
    // end edge)
    if (++surf->spanstate == 1) {
        surf2 = surfaces[1].next;

        if (surf->key > surf2->key) {
            goto newtop;
        }

        // if it's two surfaces on the same plane, the one that's already
        // active is in front, so keep going unless it's a bmodel
        if (surf->insubmodel && (surf->key == surf2->key)) {
            // must be two bmodels in the same leaf; don't care, because they'll
            // never be farthest anyway
            goto newtop;
        }

    continue_search:

        do {
            surf2 = surf2->next;
        } while (surf->key < surf2->key);

        if (surf->key == surf2->key) {
            // if it's two surfaces on the same plane, the one that's already
            // active is in front, so keep going unless it's a bmodel
            if (!surf->insubmodel) {
                goto continue_search;
            }

            // must be two bmodels in the same leaf; don't care which is really
            // in front, because they'll never be farthest anyway
        }

        goto gotposition;

    newtop:
        // emit a span (obscures current top)
        iu = static_cast<int>(edge->u >> 20);

        if (iu > surf2->last_u) {
            span = span_p++;
            span->u = surf2->last_u;
            span->count = iu - span->u;
            span->v = current_iv;
            span->pnext = surf2->spans;
            surf2->spans = span;
        }

        // set last_u on the new span
        surf->last_u = iu;

    gotposition:
        // insert before surf2
        surf->next = surf2;
        surf->prev = surf2->prev;
        surf2->prev->next = surf;
        surf2->prev = surf;
    }
}

/*
==============
R_TrailingEdge
==============
*/
void R_TrailingEdge(surf_t* surf, edge_t* edge)
{
    espan_t* span;
    int iu;

    // don't generate a span if this is an inverted span, with the end
    // edge preceding the start edge (that is, we haven't seen the
    // start edge yet)
    if (--surf->spanstate == 0) {
        if (surf->insubmodel) {
            r_bmodelactive--;
        }

        if (surf == surfaces[1].next) {
            // emit a span (current top going away)
            iu = static_cast<int>(edge->u >> 20);
            if (iu > surf->last_u) {
                span = span_p++;
                span->u = surf->last_u;
                span->count = iu - span->u;
                span->v = current_iv;
                span->pnext = surf->spans;
                surf->spans = span;
            }

            // set last_u on the surface below
            surf->next->last_u = iu;
        }

        surf->prev->next = surf->next;
        surf->next->prev = surf->prev;
    }
}

/*
==============
R_LeadingEdge
==============
*/
void R_LeadingEdge(edge_t* edge)
{
    espan_t* span;
    surf_t *surf, *surf2;
    int iu;
    double fu, newzi, testzi, newzitop, newzibottom;

    if (edge->surfs[1]) {
        // it's adding a new surface in, so find the correct place
        surf = &surfaces[edge->surfs[1]];

        // don't start a span if this is an inverted span, with the end
        // edge preceding the start edge (that is, we've already seen the
        // end edge)
        if (++surf->spanstate == 1) {
            if (surf->insubmodel) {
                r_bmodelactive++;
            }

            surf2 = surfaces[1].next;

            if (surf->key < surf2->key) {
                goto newtop;
            }

            // if it's two surfaces on the same plane, the one that's already
            // active is in front, so keep going unless it's a bmodel
            if (surf->insubmodel && (surf->key == surf2->key)) {
                // must be two bmodels in the same leaf; sort on 1/z
                fu = static_cast<float>(edge->u - 0xFFFFF) * (1.0f / 0x100000);
                newzi = surf->d_ziorigin + edge_fv * surf->d_zistepv + fu * surf->d_zistepu;
                newzibottom = newzi * 0.99f;

                testzi = surf2->d_ziorigin + edge_fv * surf2->d_zistepv + fu * surf2->d_zistepu;

                if (newzibottom >= testzi) {
                    goto newtop;
                }

                newzitop = newzi * 1.01;
                if (newzitop >= testzi) {
                    if (surf->d_zistepu >= surf2->d_zistepu) {
                        goto newtop;
                    }
                }
            }

        continue_search:

            do {
                surf2 = surf2->next;
            } while (surf->key > surf2->key);

            if (surf->key == surf2->key) {
                // if it's two surfaces on the same plane, the one that's already
                // active is in front, so keep going unless it's a bmodel
                if (!surf->insubmodel) {
                    goto continue_search;
                }

                // must be two bmodels in the same leaf; sort on 1/z
                fu = static_cast<float>(edge->u - 0xFFFFF) * (1.0f / 0x100000);
                newzi = surf->d_ziorigin + edge_fv * surf->d_zistepv + fu * surf->d_zistepu;
                newzibottom = newzi * 0.99f;

                testzi = surf2->d_ziorigin + edge_fv * surf2->d_zistepv + fu * surf2->d_zistepu;

                if (newzibottom >= testzi) {
                    goto gotposition;
                }

                newzitop = newzi * 1.01;
                if (newzitop >= testzi) {
                    if (surf->d_zistepu >= surf2->d_zistepu) {
                        goto gotposition;
                    }
                }

                goto continue_search;
            }

            goto gotposition;

        newtop:
            // emit a span (obscures current top)
            iu = static_cast<int>(edge->u >> 20);

            if (iu > surf2->last_u) {
                span = span_p++;
                span->u = surf2->last_u;
                span->count = iu - span->u;
                span->v = current_iv;
                span->pnext = surf2->spans;
                surf2->spans = span;
            }

            // set last_u on the new span
            surf->last_u = iu;

        gotposition:
            // insert before surf2
            surf->next = surf2;
            surf->prev = surf2->prev;
            surf2->prev->next = surf;
            surf2->prev = surf;
        }
    }
}

/*
==============
R_GenerateSpans
==============
*/
void R_GenerateSpans(void)
{
    edge_t* edge;
    surf_t* surf;

    r_bmodelactive = 0;

    // clear active surfaces to just the background surface
    surfaces[1].next = surfaces[1].prev = &surfaces[1];
    surfaces[1].last_u = static_cast<int>(edge_head_u_shift20);

    // generate spans
    for (edge = edge_head.next; edge != &edge_tail; edge = edge->next) {
        if (edge->surfs[0]) {
            // it has a left surface, so a surface is going away for this span
            surf = &surfaces[edge->surfs[0]];

            R_TrailingEdge(surf, edge);

            if (!edge->surfs[1]) {
                continue;
            }
        }

        R_LeadingEdge(edge);
    }

    R_CleanupSpan();
}

/*
==============
R_GenerateSpansBackward
==============
*/
void R_GenerateSpansBackward(void)
{
    edge_t* edge;

    r_bmodelactive = 0;

    // clear active surfaces to just the background surface
    surfaces[1].next = surfaces[1].prev = &surfaces[1];
    surfaces[1].last_u = static_cast<int>(edge_head_u_shift20);

    // generate spans
    for (edge = edge_head.next; edge != &edge_tail; edge = edge->next) {
        if (edge->surfs[0]) {
            R_TrailingEdge(&surfaces[edge->surfs[0]], edge);
        }

        if (edge->surfs[1]) {
            R_LeadingEdgeBackwards(edge);
        }
    }

    R_CleanupSpan();
}

/*
==============
R_ScanEdges

Input:
newedges[] array
	this has links to edges, which have links to surfaces

Output:
Each surface has a linked list of its visible spans
==============
*/
void R_ScanEdges(void)
{
    int iv, bottom;
    byte basespans[MAXSPANS * sizeof(espan_t) + CACHE_SIZE];
    espan_t* basespan_p;
    surf_t* s;

    basespan_p = (espan_t*)((size_t)(basespans + CACHE_SIZE - 1) & ~(size_t)(CACHE_SIZE - 1));
    max_span_p = &basespan_p[MAXSPANS - r_refdef.vrect.width];

    span_p = basespan_p;

    // clear active edges to just the background edges around the whole screen
    // FIXME: most of this only needs to be set up once
    edge_head.u = (int64_t)r_refdef.vrect.x << 20;
    edge_head_u_shift20 = edge_head.u >> 20;
    edge_head.u_step = 0;
    edge_head.prev = nullptr;
    edge_head.next = &edge_tail;
    edge_head.surfs[0] = 0;
    edge_head.surfs[1] = 1;

    edge_tail.u = ((int64_t)r_refdef.vrectright << 20) + 0xFFFFF;
    edge_tail_u_shift20 = edge_tail.u >> 20;
    edge_tail.u_step = 0;
    edge_tail.prev = &edge_head;
    edge_tail.next = &edge_aftertail;
    edge_tail.surfs[0] = 1;
    edge_tail.surfs[1] = 0;

    edge_aftertail.u = -1; // force a move
    edge_aftertail.u_step = 0;
    edge_aftertail.next = &edge_sentinel;
    edge_aftertail.prev = &edge_tail;

    // FIXME: do we need this now that we clamp x in r_draw.cpp?
    edge_sentinel.u = UINT_MAX; // make sure nothing sorts past this
    edge_sentinel.prev = &edge_aftertail;

    //
    // process all scan lines
    //
    bottom = r_refdef.vrectbottom - 1;

    for (iv = r_refdef.vrect.y; iv < bottom; iv++) {
        current_iv = iv;
        edge_fv = (float)iv;

        // mark that the head (background start) span is pre-included
        surfaces[1].spanstate = 1;

        if (newedges[iv]) {
            R_InsertNewEdges(newedges[iv], edge_head.next);
        }

        (*pdrawfunc)();

        // flush the span list if we can't be sure we have enough spans left for
        // the next scan
        if (span_p >= max_span_p) {
            VID_UnlockBuffer();
            S_ExtraUpdate(); // don't let sound get messed up if going slow
            VID_LockBuffer();

            if (r_drawculledpolys) {
                R_DrawCulledPolys();
            } else {
                D_DrawSurfaces();
            }

            // clear the surface span pointers
            for (s = &surfaces[1]; s < surface_p; s++) {
                s->spans = nullptr;
            }

            span_p = basespan_p;
        }

        if (removeedges[iv]) {
            R_RemoveEdges(removeedges[iv]);
        }

        if (edge_head.next != &edge_tail) {
            R_StepActiveU(edge_head.next);
        }
    }

    // do the last scan (no need to step or sort or remove on the last scan)

    current_iv = iv;
    edge_fv = (float)iv;

    // mark that the head (background start) span is pre-included
    surfaces[1].spanstate = 1;

    if (newedges[iv]) {
        R_InsertNewEdges(newedges[iv], edge_head.next);
    }

    (*pdrawfunc)();

    // draw whatever's left in the span list
    if (r_drawculledpolys) {
        R_DrawCulledPolys();
    } else {
        D_DrawSurfaces();
    }
}


// ============================================================
// Content from: src\r_bsp.cpp
// ============================================================


//
// current entity info
//
bool insubmodel;
entity_t* currententity;
Vector3 modelorg, base_modelorg;

typedef enum { touchessolid,
    drawnode,
    nodrawnode } solidstate_t;

#define MAX_BMODEL_VERTS 500  // 6K
#define MAX_BMODEL_EDGES 1000 // 12K

static mvertex_t* pbverts;
static bedge_t* pbedges;
static int numbverts, numbedges;

static mvertex_t *pfrontenter, *pfrontexit;

static bool makeclippededge;

//===========================================================================

/*
================
R_EntityRotate
================
*/
void R_EntityRotate(Vector3& vec)
{
    Vector3 tvec;

    tvec = vec;
    vec.x = Vector3(entity_rotation[0][0], entity_rotation[0][1], entity_rotation[0][2]).dot(tvec);
    vec.y = Vector3(entity_rotation[1][0], entity_rotation[1][1], entity_rotation[1][2]).dot(tvec);
    vec.z = Vector3(entity_rotation[2][0], entity_rotation[2][1], entity_rotation[2][2]).dot(tvec);
}

/*
================
R_RotateBmodel
================
*/
void R_RotateBmodel(void)
{
    float angle, s, c, temp1[3][3], temp2[3][3], temp3[3][3];

    // TODO: should use a look-up table
    // TODO: should really be stored with the entity instead of being reconstructed
    // TODO: could cache lazily, stored in the entity
    // TODO: share work with R_SetUpAliasTransform

    // yaw
    angle = currententity->angles[YAW];
    angle = static_cast<float>(angle * M_PI * 2 / 360);
    s = sin(angle);
    c = cos(angle);

    temp1[0][0] = c;
    temp1[0][1] = s;
    temp1[0][2] = 0;
    temp1[1][0] = -s;
    temp1[1][1] = c;
    temp1[1][2] = 0;
    temp1[2][0] = 0;
    temp1[2][1] = 0;
    temp1[2][2] = 1;

    // pitch
    angle = currententity->angles[PITCH];
    angle = static_cast<float>(angle * M_PI * 2 / 360);
    s = sin(angle);
    c = cos(angle);

    temp2[0][0] = c;
    temp2[0][1] = 0;
    temp2[0][2] = -s;
    temp2[1][0] = 0;
    temp2[1][1] = 1;
    temp2[1][2] = 0;
    temp2[2][0] = s;
    temp2[2][1] = 0;
    temp2[2][2] = c;

    R_ConcatRotations(temp2, temp1, temp3);

    // roll
    angle = currententity->angles[ROLL];
    angle = static_cast<float>(angle * M_PI * 2 / 360);
    s = sin(angle);
    c = cos(angle);

    temp1[0][0] = 1;
    temp1[0][1] = 0;
    temp1[0][2] = 0;
    temp1[1][0] = 0;
    temp1[1][1] = c;
    temp1[1][2] = s;
    temp1[2][0] = 0;
    temp1[2][1] = -s;
    temp1[2][2] = c;

    R_ConcatRotations(temp1, temp3, entity_rotation);

    //
    // rotate modelorg and the transformation matrix
    //
    R_EntityRotate(modelorg);
    R_EntityRotate(vpn);
    R_EntityRotate(vright);
    R_EntityRotate(vup);

    R_TransformFrustum();
}

/*
================
R_RecursiveClipBPoly
================
*/
void R_RecursiveClipBPoly(bedge_t* pedges, mnode_t* pnode, msurface_t* psurf)
{
    bedge_t *psideedges[2], *pnextedge, *ptedge;
    int i, side, lastside;
    float dist, frac, lastdist;
    mplane_t *splitplane, tplane;
    mvertex_t *pvert, *plastvert, *ptvert;
    mnode_t* pn;

    psideedges[0] = psideedges[1] = nullptr;

    makeclippededge = false;

    // transform the BSP plane into model space
    // FIXME: cache these?
    splitplane = pnode->plane;
    tplane.dist = splitplane->dist - DotProduct(r_entorigin, splitplane->normal);
    tplane.normal[0] = DotProduct(entity_rotation[0], splitplane->normal);
    tplane.normal[1] = DotProduct(entity_rotation[1], splitplane->normal);
    tplane.normal[2] = DotProduct(entity_rotation[2], splitplane->normal);

    // clip edges to BSP plane
    for (; pedges; pedges = pnextedge) {
        pnextedge = pedges->pnext;

        // set the status for the last point as the previous point
        // FIXME: cache this stuff somehow?
        plastvert = pedges->v[0];
        lastdist = DotProduct(plastvert->position, tplane.normal) - tplane.dist;

        if (lastdist > 0) {
            lastside = 0;
        } else {
            lastside = 1;
        }

        pvert = pedges->v[1];

        dist = DotProduct(pvert->position, tplane.normal) - tplane.dist;

        if (dist > 0) {
            side = 0;
        } else {
            side = 1;
        }

        if (side != lastside) {
            // clipped
            if (numbverts >= MAX_BMODEL_VERTS) {
                return;
            }

            // generate the clipped vertex
            frac = lastdist / (lastdist - dist);
            ptvert = &pbverts[numbverts++];
            ptvert->position[0] = plastvert->position[0] + frac * (pvert->position[0] - plastvert->position[0]);
            ptvert->position[1] = plastvert->position[1] + frac * (pvert->position[1] - plastvert->position[1]);
            ptvert->position[2] = plastvert->position[2] + frac * (pvert->position[2] - plastvert->position[2]);

            // split into two edges, one on each side, and remember entering
            // and exiting points
            // FIXME: share the clip edge by having a winding direction flag?
            if (numbedges >= (MAX_BMODEL_EDGES - 1)) {
                Con_Printf("Out of edges for bmodel\n");

                return;
            }

            ptedge = &pbedges[numbedges];
            ptedge->pnext = psideedges[lastside];
            psideedges[lastside] = ptedge;
            ptedge->v[0] = plastvert;
            ptedge->v[1] = ptvert;

            ptedge = &pbedges[numbedges + 1];
            ptedge->pnext = psideedges[side];
            psideedges[side] = ptedge;
            ptedge->v[0] = ptvert;
            ptedge->v[1] = pvert;

            numbedges += 2;

            if (side == 0) {
                // entering for front, exiting for back
                pfrontenter = ptvert;
                makeclippededge = true;
            } else {
                pfrontexit = ptvert;
                makeclippededge = true;
            }
        } else {
            // add the edge to the appropriate side
            pedges->pnext = psideedges[side];
            psideedges[side] = pedges;
        }
    }

    // if anything was clipped, reconstitute and add the edges along the clip
    // plane to both sides (but in opposite directions)
    if (makeclippededge) {
        if (numbedges >= (MAX_BMODEL_EDGES - 2)) {
            Con_Printf("Out of edges for bmodel\n");

            return;
        }

        ptedge = &pbedges[numbedges];
        ptedge->pnext = psideedges[0];
        psideedges[0] = ptedge;
        ptedge->v[0] = pfrontexit;
        ptedge->v[1] = pfrontenter;

        ptedge = &pbedges[numbedges + 1];
        ptedge->pnext = psideedges[1];
        psideedges[1] = ptedge;
        ptedge->v[0] = pfrontenter;
        ptedge->v[1] = pfrontexit;

        numbedges += 2;
    }

    // draw or recurse further
    for (i = 0; i < 2; i++) {
        if (psideedges[i]) {
            // draw if we've reached a non-solid leaf, done if all that's left is a
            // solid leaf, and continue down the tree if it's not a leaf
            pn = pnode->children[i];

            // we're done with this branch if the node or leaf isn't in the PVS
            if (pn->visframe == r_visframecount) {
                if (pn->contents < 0) {
                    if (pn->contents != CONTENTS_SOLID) {
                        r_currentbkey = ((mleaf_t*)pn)->key;
                        R_RenderBmodelFace(psideedges[i], psurf);
                    }
                } else {
                    R_RecursiveClipBPoly(psideedges[i], pnode->children[i], psurf);
                }
            }
        }
    }
}

/*
================
R_DrawSolidClippedSubmodelPolygons
================
*/
void R_DrawSolidClippedSubmodelPolygons(model_t* pmodel)
{
    int i, j, lindex;
    vec_t dot;
    msurface_t* psurf;
    int numsurfaces;
    mplane_t* pplane;
    mvertex_t bverts[MAX_BMODEL_VERTS];
    bedge_t bedges[MAX_BMODEL_EDGES], *pbedge;
    medge_t *pedge, *pedges;

    // FIXME: use bounding-box-based frustum clipping info?

    psurf = &pmodel->surfaces[pmodel->firstmodelsurface];
    numsurfaces = pmodel->nummodelsurfaces;
    pedges = pmodel->edges;

    for (i = 0; i < numsurfaces; i++, psurf++) {
        // find which side of the node we are on
        pplane = psurf->plane;

        dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

        // draw the polygon
        if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) || (!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON))) {
            // FIXME: use bounding-box-based frustum clipping info?

            // copy the edges to bedges, flipping if necessary so always
            // clockwise winding
            // FIXME: if edges and vertices get caches, these assignments must move
            // outside the loop, and overflow checking must be done here
            pbverts = bverts;
            pbedges = bedges;
            numbverts = numbedges = 0;

            if (psurf->numedges > 0) {
                pbedge = &bedges[numbedges];
                numbedges += psurf->numedges;

                for (j = 0; j < psurf->numedges; j++) {
                    lindex = pmodel->surfedges[psurf->firstedge + j];

                    if (lindex > 0) {
                        pedge = &pedges[lindex];
                        pbedge[j].v[0] = &r_pcurrentvertbase[pedge->v[0]];
                        pbedge[j].v[1] = &r_pcurrentvertbase[pedge->v[1]];
                    } else {
                        lindex = -lindex;
                        pedge = &pedges[lindex];
                        pbedge[j].v[0] = &r_pcurrentvertbase[pedge->v[1]];
                        pbedge[j].v[1] = &r_pcurrentvertbase[pedge->v[0]];
                    }

                    pbedge[j].pnext = &pbedge[j + 1];
                }

                pbedge[j - 1].pnext = nullptr; // mark end of edges

                R_RecursiveClipBPoly(pbedge, currententity->topnode, psurf);
            } else {
                Sys_Error("no edges in bmodel");
            }
        }
    }
}

/*
================
R_DrawSubmodelPolygons
================
*/
void R_DrawSubmodelPolygons(model_t* pmodel, int clipflags)
{
    int i;
    vec_t dot;
    msurface_t* psurf;
    int numsurfaces;
    mplane_t* pplane;

    // FIXME: use bounding-box-based frustum clipping info?

    psurf = &pmodel->surfaces[pmodel->firstmodelsurface];
    numsurfaces = pmodel->nummodelsurfaces;

    for (i = 0; i < numsurfaces; i++, psurf++) {
        // find which side of the node we are on
        pplane = psurf->plane;

        dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

        // draw the polygon
        if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) || (!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON))) {
            r_currentkey = ((mleaf_t*)currententity->topnode)->key;

            // FIXME: use bounding-box-based frustum clipping info?
            R_RenderFace(psurf, clipflags);
        }
    }
}

/*
================
R_RecursiveWorldNode
================
*/
void R_RecursiveWorldNode(mnode_t* node, int clipflags)
{
    int i, c, side, *pindex;
    Vector3 acceptpt, rejectpt;
    mplane_t* plane;
    msurface_t *surf, **mark;
    mleaf_t* pleaf;
    double d, dot;

    if (node->contents == CONTENTS_SOLID) {
        return; // solid
    }

    if (node->visframe != r_visframecount) {
        return;
    }

    // cull the clipping planes if not trivial accept
    // FIXME: the compiler is doing a lousy job of optimizing here; it could be
    //  twice as fast in ASM
    if (clipflags) {
        for (i = 0; i < 4; i++) {
            if (!(clipflags & (1 << i))) {
                continue; // don't need to clip against it
            }

            // generate accept and reject points
            // FIXME: do with fast look-ups or integer tests based on the sign bit
            // of the floating point values

            pindex = pfrustum_indexes[i];

            rejectpt = Vector3((float)node->minmaxs[pindex[0]], (float)node->minmaxs[pindex[1]], (float)node->minmaxs[pindex[2]]);

            d = rejectpt.dot(view_clipplanes[i].normal);
            d -= view_clipplanes[i].dist;

            if (d <= 0) {
                return;
            }

            acceptpt = Vector3((float)node->minmaxs[pindex[3 + 0]], (float)node->minmaxs[pindex[3 + 1]], (float)node->minmaxs[pindex[3 + 2]]);

            d = acceptpt.dot(view_clipplanes[i].normal);
            d -= view_clipplanes[i].dist;

            if (d >= 0) {
                clipflags &= ~(1 << i); // node is entirely on screen
            }
        }
    }

    // if a leaf node, draw stuff
    if (node->contents < 0) {
        pleaf = (mleaf_t*)node;

        mark = pleaf->firstmarksurface;
        c = pleaf->nummarksurfaces;

        if (c) {
            do {
                (*mark)->visframe = r_framecount;
                mark++;
            } while (--c);
        }

        // deal with model fragments in this leaf
        if (pleaf->efrags) {
            R_StoreEfrags(&pleaf->efrags);
        }

        pleaf->key = r_currentkey;
        r_currentkey++; // all bmodels in a leaf share the same key
    } else {
        // node is just a decision point, so go down the apropriate sides

        // find which side of the node we are on
        plane = node->plane;

        switch (plane->type) {
        case PLANE_X:
            dot = modelorg[0] - plane->dist;
            break;
        case PLANE_Y:
            dot = modelorg[1] - plane->dist;
            break;
        case PLANE_Z:
            dot = modelorg[2] - plane->dist;
            break;
        default:
            dot = DotProduct(modelorg, plane->normal) - plane->dist;
            break;
        }

        if (dot >= 0) {
            side = 0;
        } else {
            side = 1;
        }

        // recurse down the children, front side first
        R_RecursiveWorldNode(node->children[side], clipflags);

        // draw stuff
        c = node->numsurfaces;

        if (c) {
            surf = cl.worldmodel->surfaces + node->firstsurface;

            if (dot < -BACKFACE_EPSILON) {
                do {
                    if ((surf->flags & SURF_PLANEBACK) && (surf->visframe == r_framecount)) {
                        if (r_drawpolys) {
                            if (r_worldpolysbacktofront) {
                                if (numbtofpolys < MAX_BTOFPOLYS) {
                                    pbtofpolys[numbtofpolys].clipflags = clipflags;
                                    pbtofpolys[numbtofpolys].psurf = surf;
                                    numbtofpolys++;
                                }
                            } else {
                                R_RenderPoly(surf, clipflags);
                            }
                        } else {
                            R_RenderFace(surf, clipflags);
                        }
                    }

                    surf++;
                } while (--c);
            } else if (dot > BACKFACE_EPSILON) {
                do {
                    if (!(surf->flags & SURF_PLANEBACK) && (surf->visframe == r_framecount)) {
                        if (r_drawpolys) {
                            if (r_worldpolysbacktofront) {
                                if (numbtofpolys < MAX_BTOFPOLYS) {
                                    pbtofpolys[numbtofpolys].clipflags = clipflags;
                                    pbtofpolys[numbtofpolys].psurf = surf;
                                    numbtofpolys++;
                                }
                            } else {
                                R_RenderPoly(surf, clipflags);
                            }
                        } else {
                            R_RenderFace(surf, clipflags);
                        }
                    }

                    surf++;
                } while (--c);
            }

            // all surfaces on the same node share the same sequence number
            r_currentkey++;
        }

        // recurse down the back side
        R_RecursiveWorldNode(node->children[!side], clipflags);
    }
}

/*
================
R_RenderWorld
================
*/
void R_RenderWorld(void)
{
    int i;
    model_t* clmodel;
    eastl::array<btofpoly_t, MAX_BTOFPOLYS> btofpolys{};

    pbtofpolys = btofpolys.data();

    currententity = &cl_entities[0];
    VectorCopy(r_origin, modelorg);
    clmodel = currententity->model;
    r_pcurrentvertbase = clmodel->vertexes;

    R_RecursiveWorldNode(clmodel->nodes, 15);

    // if the driver wants the polygons back to front, play the visible ones back
    // in that order
    if (r_worldpolysbacktofront) {
        for (i = numbtofpolys - 1; i >= 0; i--) {
            R_RenderPoly(btofpolys[i].psurf, btofpolys[i].clipflags);
        }
    }
}


// ============================================================
// Content from: src\r_sprite.cpp
// ============================================================


static int clip_current;
static vec5_t clip_verts[2][MAXWORKINGVERTS];
static int sprite_width, sprite_height;

spritedesc_t r_spritedesc;

/*
================
R_RotateSprite
================
*/
void R_RotateSprite(float beam_len)
{
    Vector3 vec;

    if (beam_len == 0.0) {
        return;
    }

    vec = r_spritedesc.vpn * -beam_len;
    r_entorigin += vec;
    modelorg -= vec;
}

/*
=============
R_ClipSpriteFace

Clips the winding at clip_verts[clip_current] and changes clip_current
Throws out the back side
==============
*/
int R_ClipSpriteFace(int nump, clipplane_t* pclipplane)
{
    int i, outcount;
    float dists[MAXWORKINGVERTS + 1];
    float frac, clipdist, *pclipnormal;
    float *in, *instep, *outstep, *vert2;

    clipdist = pclipplane->dist;
    pclipnormal = pclipplane->normal;

    // calc dists
    if (clip_current) {
        in = clip_verts[1][0];
        outstep = clip_verts[0][0];
        clip_current = 0;
    } else {
        in = clip_verts[0][0];
        outstep = clip_verts[1][0];
        clip_current = 1;
    }

    instep = in;
    for (i = 0; i < nump; i++, instep += sizeof(vec5_t) / sizeof(float)) {
        dists[i] = DotProduct(instep, pclipnormal) - clipdist;
    }

    // handle wraparound case
    dists[nump] = dists[0];
    Q_memcpy(instep, in, sizeof(vec5_t));

    // clip the winding
    instep = in;
    outcount = 0;

    for (i = 0; i < nump; i++, instep += sizeof(vec5_t) / sizeof(float)) {
        if (dists[i] >= 0) {
            Q_memcpy(outstep, instep, sizeof(vec5_t));
            outstep += sizeof(vec5_t) / sizeof(float);
            outcount++;
        }

        if (dists[i] == 0 || dists[i + 1] == 0) {
            continue;
        }

        if ((dists[i] > 0) == (dists[i + 1] > 0)) {
            continue;
        }

        // split it into a new vertex
        frac = dists[i] / (dists[i] - dists[i + 1]);

        vert2 = instep + sizeof(vec5_t) / sizeof(float);

        outstep[0] = instep[0] + frac * (vert2[0] - instep[0]);
        outstep[1] = instep[1] + frac * (vert2[1] - instep[1]);
        outstep[2] = instep[2] + frac * (vert2[2] - instep[2]);
        outstep[3] = instep[3] + frac * (vert2[3] - instep[3]);
        outstep[4] = instep[4] + frac * (vert2[4] - instep[4]);

        outstep += sizeof(vec5_t) / sizeof(float);
        outcount++;
    }

    return outcount;
}

/*
================
R_SetupAndDrawSprite
================
*/
void R_SetupAndDrawSprite()
{
    int i, nump;
    float dot, scale, *pv;
    vec5_t* pverts;
    Vector3 left, up, right, down, transformed, local;
    emitpoint_t outverts[MAXWORKINGVERTS + 1], *pout;

    dot = r_spritedesc.vpn.dot(modelorg);

    // backface cull
    if (dot >= 0) {
        return;
    }

    // build the sprite poster in worldspace
    right = r_spritedesc.vright * r_spritedesc.pspriteframe->right;
    up = r_spritedesc.vup * r_spritedesc.pspriteframe->up;
    left = r_spritedesc.vright * r_spritedesc.pspriteframe->left;
    down = r_spritedesc.vup * r_spritedesc.pspriteframe->down;

    pverts = clip_verts[0];

    pverts[0][0] = r_entorigin.x + up.x + left.x;
    pverts[0][1] = r_entorigin.y + up.y + left.y;
    pverts[0][2] = r_entorigin.z + up.z + left.z;
    pverts[0][3] = 0;
    pverts[0][4] = 0;

    pverts[1][0] = r_entorigin.x + up.x + right.x;
    pverts[1][1] = r_entorigin.y + up.y + right.y;
    pverts[1][2] = r_entorigin.z + up.z + right.z;
    pverts[1][3] = static_cast<float>(sprite_width);
    pverts[1][4] = 0;

    pverts[2][0] = r_entorigin.x + down.x + right.x;
    pverts[2][1] = r_entorigin.y + down.y + right.y;
    pverts[2][2] = r_entorigin.z + down.z + right.z;
    pverts[2][3] = static_cast<float>(sprite_width);
    pverts[2][4] = static_cast<float>(sprite_height);

    pverts[3][0] = r_entorigin.x + down.x + left.x;
    pverts[3][1] = r_entorigin.y + down.y + left.y;
    pverts[3][2] = r_entorigin.z + down.z + left.z;
    pverts[3][3] = 0;
    pverts[3][4] = static_cast<float>(sprite_height);

    // clip to the frustum in worldspace
    nump = 4;
    clip_current = 0;

    for (i = 0; i < 4; i++) {
        nump = R_ClipSpriteFace(nump, &view_clipplanes[i]);
        if (nump < 3) {
            return;
        }

        if (nump >= MAXWORKINGVERTS) {
            Sys_Error("R_SetupAndDrawSprite: too many points");
        }
    }

    // transform vertices into viewspace and project
    pv = &clip_verts[clip_current][0][0];
    r_spritedesc.nearzi = -999999;

    for (i = 0; i < nump; i++) {
        local = Vector3(pv[0], pv[1], pv[2]) - r_origin;
        TransformVector(local, transformed);

        if (transformed.z < NEAR_CLIP) {
            transformed.z = (vec_t)NEAR_CLIP;
        }

        pout = &outverts[i];
        pout->zi = static_cast<float>(1.0 / transformed.z);
        if (pout->zi > r_spritedesc.nearzi) {
            r_spritedesc.nearzi = pout->zi;
        }

        pout->s = pv[3];
        pout->t = pv[4];

        scale = xscale * pout->zi;
        pout->u = (xcenter + scale * transformed.x);

        scale = yscale * pout->zi;
        pout->v = (ycenter - scale * transformed.y);

        pv += sizeof(vec5_t) / sizeof(*pv);
    }

    // draw it
    r_spritedesc.nump = nump;
    r_spritedesc.pverts = outverts;
    D_DrawSprite();
}

/*
================
R_GetSpriteframe
================
*/
mspriteframe_t* R_GetSpriteframe(msprite_t* psprite)
{
    mspritegroup_t* pspritegroup;
    mspriteframe_t* pspriteframe;
    int i, numframes, frame;
    float *pintervals, fullinterval, targettime, time;

    frame = currententity->frame;

    if ((frame >= psprite->numframes) || (frame < 0)) {
        Con_Printf("R_DrawSprite: no such frame %d\n", frame);
        frame = 0;
    }

    if (psprite->frames[frame].type == spriteframetype_t::SPR_SINGLE) {
        pspriteframe = psprite->frames[frame].frameptr;
    } else {
        pspritegroup = (mspritegroup_t*)psprite->frames[frame].frameptr;
        pintervals = pspritegroup->intervals;
        numframes = pspritegroup->numframes;
        fullinterval = pintervals[numframes - 1];

        time = static_cast<float>(cl.time + currententity->syncbase);

        // when loading in Mod_LoadSpriteGroup, we guaranteed all interval values
        // are positive, so we don't have to worry about division by 0
        targettime = time - ((int)(time / fullinterval)) * fullinterval;

        for (i = 0; i < (numframes - 1); i++) {
            if (pintervals[i] > targettime) {
                break;
            }
        }

        pspriteframe = pspritegroup->frames[i];
    }

    return pspriteframe;
}

/*
================
R_DrawSprite
================
*/
void R_DrawSprite(void)
{
    msprite_t* psprite;
    Vector3 tvec;
    float dot, angle, sr, cr;

    psprite = (msprite_t*)currententity->model->cache.data;

    r_spritedesc.pspriteframe = R_GetSpriteframe(psprite);

    sprite_width = r_spritedesc.pspriteframe->width;
    sprite_height = r_spritedesc.pspriteframe->height;

    // TODO: make this caller-selectable
    if (psprite->type == SPR_FACING_UPRIGHT) {
        // generate the sprite's axes, with vup straight up in worldspace, and
        // r_spritedesc.vright perpendicular to modelorg.
        // This will not work if the view direction is very close to straight up or
        // down, because the cross product will be between two nearly parallel
        // vectors and starts to approach an undefined state, so we don't draw if
        // the two vectors are less than 1 degree apart
        tvec = -modelorg;
        tvec.normalize();
        dot = tvec.z; // same as DotProduct (tvec, r_spritedesc.vup) because
        //  r_spritedesc.vup is 0, 0, 1
        if ((dot > 0.999848) || (dot < -0.999848)) { // cos(1 degree) = 0.999848
            return;
        }

        r_spritedesc.vup = Vector3(0, 0, 1);
        r_spritedesc.vright = Vector3(tvec.y, -tvec.x, 0);
        r_spritedesc.vright.normalize();
        r_spritedesc.vpn = Vector3(-r_spritedesc.vright.y, r_spritedesc.vright.x, 0);
        // CrossProduct (r_spritedesc.vright, r_spritedesc.vup,
        //  r_spritedesc.vpn)
    } else if (psprite->type == SPR_VP_PARALLEL) {
        // generate the sprite's axes, completely parallel to the viewplane. There
        // are no problem situations, because the sprite is always in the same
        // position relative to the viewer
        r_spritedesc.vup = vup;
        r_spritedesc.vright = vright;
        r_spritedesc.vpn = vpn;
    } else if (psprite->type == SPR_VP_PARALLEL_UPRIGHT) {
        // generate the sprite's axes, with vup straight up in worldspace, and
        // r_spritedesc.vright parallel to the viewplane.
        // This will not work if the view direction is very close to straight up or
        // down, because the cross product will be between two nearly parallel
        // vectors and starts to approach an undefined state, so we don't draw if
        // the two vectors are less than 1 degree apart
        dot = vpn.z; // same as DotProduct (vpn, r_spritedesc.vup) because
        //  r_spritedesc.vup is 0, 0, 1
        if ((dot > 0.999848) || (dot < -0.999848)) { // cos(1 degree) = 0.999848
            return;
        }

        r_spritedesc.vup = Vector3(0, 0, 1);
        r_spritedesc.vright = Vector3(vpn.y, -vpn.x, 0);
        r_spritedesc.vright.normalize();
        r_spritedesc.vpn = Vector3(-r_spritedesc.vright.y, r_spritedesc.vright.x, 0);
        // CrossProduct (r_spritedesc.vright, r_spritedesc.vup,
        //  r_spritedesc.vpn)
    } else if (psprite->type == SPR_ORIENTED) {
        // generate the sprite's axes, according to the sprite's world orientation
        AngleVectors(currententity->angles, r_spritedesc.vpn, r_spritedesc.vright,
            r_spritedesc.vup);
    } else if (psprite->type == SPR_VP_PARALLEL_ORIENTED) {
        // generate the sprite's axes, parallel to the viewplane, but rotated in
        // that plane around the center according to the sprite entity's roll
        // angle. So vpn stays the same, but vright and vup rotate
        angle = static_cast<float>(currententity->angles[ROLL] * (M_PI * 2 / 360));
        sr = sin(angle);
        cr = cos(angle);

        r_spritedesc.vpn = vpn;
        r_spritedesc.vright = vright * cr + vup * sr;
        r_spritedesc.vup = vright * -sr + vup * cr;
    } else {
        Sys_Error("R_DrawSprite: Bad sprite type %d", psprite->type);
    }

    R_RotateSprite(psprite->beamlength);

    R_SetupAndDrawSprite();
}


// ============================================================
// Content from: src\r_alias.cpp
// ============================================================
// r_alias.cpp: routines for setting up to draw alias models

// right now, but that should move)

#define LIGHT_MIN 5 // lowest light value we'll allow, to avoid the
//  need for inner-loop light clamping

affinetridesc_t r_affinetridesc;

void* acolormap; // FIXME: should go away

trivertx_t* r_apverts;

Vector3 r_plightvec;
int r_ambientlight;
float r_shadelight;
static float ziscale;
static model_t* pmodel;

static Vector3 alias_forward, alias_right, alias_up;

static maliasskindesc_t* pskindesc;
int r_anumverts;

float aliastransform[3][4];

typedef struct {
    int index0;
    int index1;
} aedge_t;

static aedge_t aedges[12] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 },
    { 6, 7 }, { 7, 4 }, { 0, 5 }, { 1, 4 }, { 2, 7 }, { 3, 6 } };

/*
================
R_InitVertexNormals

Generate the 162 vertex normals by subdividing an icosahedron.
This reproduces the exact set and ordering from the original
Quake Pascal tool used to create anorms.h.
================
*/
void R_InitVertexNormals(void)
{
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    const float X = 0.525731112119133606f;
    const float Z = 0.850650808352039932f;

    const float verts[12][3] = {
        {-X, 0.0f, Z}, {X, 0.0f, Z}, {-X, 0.0f, -Z}, {X, 0.0f, -Z},
        {0.0f, Z, X}, {0.0f, Z, -X}, {0.0f, -Z, X}, {0.0f, -Z, -X},
        {Z, X, 0.0f}, {-Z, X, 0.0f}, {Z, -X, 0.0f}, {-Z, -X, 0.0f},
    };

    const int faces[20][3] = {
        {0, 4, 1}, {0, 9, 4}, {9, 5, 4}, {4, 5, 8}, {4, 8, 1},
        {8, 10, 1}, {8, 3, 10}, {5, 3, 8}, {5, 2, 3}, {2, 7, 3},
        {7, 10, 3}, {7, 6, 10}, {7, 11, 6}, {11, 0, 6}, {0, 1, 6},
        {6, 1, 10}, {9, 0, 11}, {9, 11, 2}, {9, 2, 5}, {7, 2, 11}
    };

    const int subdiv = 4;
    const int max_low_i = subdiv / 2 - 1;

    float temp[400][3];
    int num_temp = 0;

    for (int f = 0; f < 20; f++) {
        const float* a = verts[faces[f][0]];
        const float* b = verts[faces[f][1]];
        const float* c = verts[faces[f][2]];

        // High i part (i >= n/2)
        for (int i = subdiv; i >= subdiv / 2; i--) {
            for (int j = subdiv - i; j >= 0; j--) {
                int k = subdiv - i - j;
                float px = (i * a[0] + j * b[0] + k * c[0]) / subdiv;
                float py = (i * a[1] + j * b[1] + k * c[1]) / subdiv;
                float pz = (i * a[2] + j * b[2] + k * c[2]) / subdiv;
                float len = std::sqrt(px * px + py * py + pz * pz);
                if (len > 0) {
                    temp[num_temp][0] = px / len;
                    temp[num_temp][1] = py / len;
                    temp[num_temp][2] = pz / len;
                    num_temp++;
                }
            }
        }

        // Low i part, high j (j >= n/2)
        for (int j = subdiv; j >= subdiv / 2; j--) {
            int max_i = (max_low_i < subdiv - j) ? max_low_i : (subdiv - j);
            for (int i = max_i; i >= 0; i--) {
                int k = subdiv - i - j;
                float px = (i * a[0] + j * b[0] + k * c[0]) / subdiv;
                float py = (i * a[1] + j * b[1] + k * c[1]) / subdiv;
                float pz = (i * a[2] + j * b[2] + k * c[2]) / subdiv;
                float len = std::sqrt(px * px + py * py + pz * pz);
                if (len > 0) {
                    temp[num_temp][0] = px / len;
                    temp[num_temp][1] = py / len;
                    temp[num_temp][2] = pz / len;
                    num_temp++;
                }
            }
        }

        // Low i part, low j (j < n/2)
        for (int j = 0; j < subdiv / 2; j++) {
            int max_i = (max_low_i < subdiv - j) ? max_low_i : (subdiv - j);
            for (int i = 0; i <= max_i; i++) {
                int k = subdiv - i - j;
                float px = (i * a[0] + j * b[0] + k * c[0]) / subdiv;
                float py = (i * a[1] + j * b[1] + k * c[1]) / subdiv;
                float pz = (i * a[2] + j * b[2] + k * c[2]) / subdiv;
                float len = std::sqrt(px * px + py * py + pz * pz);
                if (len > 0) {
                    temp[num_temp][0] = px / len;
                    temp[num_temp][1] = py / len;
                    temp[num_temp][2] = pz / len;
                    num_temp++;
                }
            }
        }
    }

    // Dedup preserving first occurrence (order matches original)
    int out_idx = 0;
    for (int i = 0; i < num_temp && out_idx < NUMVERTEXNORMALS; i++) {
        int key_x = (int)std::round(temp[i][0] * 10000.0f);
        int key_y = (int)std::round(temp[i][1] * 10000.0f);
        int key_z = (int)std::round(temp[i][2] * 10000.0f);

        bool dup = false;
        for (int j = 0; j < out_idx; j++) {
            int jx = (int)std::round(r_avertexnormals[j][0] * 10000.0f);
            int jy = (int)std::round(r_avertexnormals[j][1] * 10000.0f);
            int jz = (int)std::round(r_avertexnormals[j][2] * 10000.0f);
            if (jx == key_x && jy == key_y && jz == key_z) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            r_avertexnormals[out_idx][0] = temp[i][0];
            r_avertexnormals[out_idx][1] = temp[i][1];
            r_avertexnormals[out_idx][2] = temp[i][2];
            out_idx++;
        }
    }
}

void R_AliasTransformAndProjectFinalVerts(finalvert_t* fv, stvert_t* pstverts);
void R_AliasSetUpTransform(int trivial_accept);
void R_AliasTransformVector(const float* in, float* out);
void R_AliasTransformFinalVert(finalvert_t* fv,
    auxvert_t* av,
    trivertx_t* pverts,
    stvert_t* pstverts);
void R_AliasProjectFinalVert(finalvert_t* fv, auxvert_t* av);

/*
================
R_AliasCheckBBox
================
*/
bool R_AliasCheckBBox(void)
{
    int i, flags, frame, numv;
    aliashdr_t* pahdr;
    float zi, basepts[8][3], v0, v1, frac;
    finalvert_t *pv0, *pv1, viewpts[16];
    auxvert_t *pa0, *pa1, viewaux[16];
    maliasframedesc_t* pframedesc;
    bool zclipped, zfullyclipped;
    unsigned anyclip, allclip;
    int minz;

    // expand, rotate, and translate points into worldspace

    currententity->trivial_accept = 0;
    pmodel = currententity->model;
    pahdr = (aliashdr_t*)Mod_Extradata(pmodel);
    pmdl = reinterpret_cast<mdl_t*>(reinterpret_cast<byte*>(pahdr) + pahdr->model);

    R_AliasSetUpTransform(0);

    // construct the base bounding box for this frame
    frame = currententity->frame;
    // TODO: don't repeat this check when drawing?
    if ((frame >= pmdl->numframes) || (frame < 0)) {
        Con_DPrintf("No such frame %d %s\n", frame, pmodel->name);
        frame = 0;
    }

    pframedesc = &pahdr->frames[frame];

    // x worldspace coordinates
    basepts[0][0] = basepts[1][0] = basepts[2][0] = basepts[3][0] = (float)pframedesc->bboxmin.v[0];
    basepts[4][0] = basepts[5][0] = basepts[6][0] = basepts[7][0] = (float)pframedesc->bboxmax.v[0];

    // y worldspace coordinates
    basepts[0][1] = basepts[3][1] = basepts[5][1] = basepts[6][1] = (float)pframedesc->bboxmin.v[1];
    basepts[1][1] = basepts[2][1] = basepts[4][1] = basepts[7][1] = (float)pframedesc->bboxmax.v[1];

    // z worldspace coordinates
    basepts[0][2] = basepts[1][2] = basepts[4][2] = basepts[5][2] = (float)pframedesc->bboxmin.v[2];
    basepts[2][2] = basepts[3][2] = basepts[6][2] = basepts[7][2] = (float)pframedesc->bboxmax.v[2];

    zclipped = false;
    zfullyclipped = true;

    minz = 9999;
    for (i = 0; i < 8; i++) {
        R_AliasTransformVector(&basepts[i][0], &viewaux[i].fv[0]);

        if (viewaux[i].fv[2] < ALIAS_Z_CLIP_PLANE) {
            // we must clip points that are closer than the near clip plane
            viewpts[i].flags = ALIAS_Z_CLIP;
            zclipped = true;
        } else {
            if (viewaux[i].fv[2] < minz) {
                minz = static_cast<int>(viewaux[i].fv[2]);
            }

            viewpts[i].flags = 0;
            zfullyclipped = false;
        }
    }

    if (zfullyclipped) {
        return false; // everything was near-z-clipped
    }

    numv = 8;

    if (zclipped) {
        // organize points by edges, use edges to get new points (possible trivial
        // reject)
        for (i = 0; i < 12; i++) {
            // edge endpoints
            pv0 = &viewpts[aedges[i].index0];
            pv1 = &viewpts[aedges[i].index1];
            pa0 = &viewaux[aedges[i].index0];
            pa1 = &viewaux[aedges[i].index1];

            // if one end is clipped and the other isn't, make a new point
            if (pv0->flags ^ pv1->flags) {
                frac = (ALIAS_Z_CLIP_PLANE - pa0->fv[2]) / (pa1->fv[2] - pa0->fv[2]);
                viewaux[numv].fv[0] = pa0->fv[0] + (pa1->fv[0] - pa0->fv[0]) * frac;
                viewaux[numv].fv[1] = pa0->fv[1] + (pa1->fv[1] - pa0->fv[1]) * frac;
                viewaux[numv].fv[2] = ALIAS_Z_CLIP_PLANE;
                viewpts[numv].flags = 0;
                numv++;
            }
        }
    }

    // project the vertices that remain after clipping
    anyclip = 0;
    allclip = ALIAS_XY_CLIP_MASK;

    // TODO: probably should do this loop in ASM, especially if we use floats
    for (i = 0; i < numv; i++) {
        // we don't need to bother with vertices that were z-clipped
        if (viewpts[i].flags & ALIAS_Z_CLIP) {
            continue;
        }

        zi = static_cast<float>(1.0 / viewaux[i].fv[2]);

        // FIXME: do with chop mode in ASM, or convert to float
        v0 = (viewaux[i].fv[0] * xscale * zi) + xcenter;
        v1 = (viewaux[i].fv[1] * yscale * zi) + ycenter;

        flags = 0;

        if (v0 < r_refdef.fvrectx) {
            flags |= ALIAS_LEFT_CLIP;
        }

        if (v1 < r_refdef.fvrecty) {
            flags |= ALIAS_TOP_CLIP;
        }

        if (v0 > r_refdef.fvrectright) {
            flags |= ALIAS_RIGHT_CLIP;
        }

        if (v1 > r_refdef.fvrectbottom) {
            flags |= ALIAS_BOTTOM_CLIP;
        }

        anyclip |= flags;
        allclip &= flags;
    }

    if (allclip) {
        return false; // trivial reject off one side
    }

    currententity->trivial_accept = !anyclip & !zclipped;

    if (currententity->trivial_accept) {
        if (minz > (r_aliastransition + (pmdl->size * r_resfudge))) {
            currententity->trivial_accept |= 2;
        }
    }

    return true;
}

/*
================
R_AliasTransformVector
================
*/
void R_AliasTransformVector(const float* in, float* out)
{
    out[0] = DotProduct(in, aliastransform[0]) + aliastransform[0][3];
    out[1] = DotProduct(in, aliastransform[1]) + aliastransform[1][3];
    out[2] = DotProduct(in, aliastransform[2]) + aliastransform[2][3];
}

/*
================
R_AliasPreparePoints

General clipped case
================
*/
void R_AliasPreparePoints(void)
{
    int i;
    stvert_t* pstverts;
    finalvert_t* fv;
    auxvert_t* av;
    mtriangle_t* ptri;
    finalvert_t* paclip_fv[3];

    pstverts = reinterpret_cast<stvert_t*>(reinterpret_cast<byte*>(paliashdr) + paliashdr->stverts);
    r_anumverts = pmdl->numverts;
    fv = pfinalverts;
    av = pauxverts;

    for (i = 0; i < r_anumverts; i++, fv++, av++, r_apverts++, pstverts++) {
        R_AliasTransformFinalVert(fv, av, r_apverts, pstverts);
        if (av->fv[2] < ALIAS_Z_CLIP_PLANE) {
            fv->flags |= ALIAS_Z_CLIP;
        } else {
            R_AliasProjectFinalVert(fv, av);

            if (fv->v[0] < r_refdef.aliasvrect.x) {
                fv->flags |= ALIAS_LEFT_CLIP;
            }

            if (fv->v[1] < r_refdef.aliasvrect.y) {
                fv->flags |= ALIAS_TOP_CLIP;
            }

            if (fv->v[0] > r_refdef.aliasvrectright) {
                fv->flags |= ALIAS_RIGHT_CLIP;
            }

            if (fv->v[1] > r_refdef.aliasvrectbottom) {
                fv->flags |= ALIAS_BOTTOM_CLIP;
            }
        }
    }

    //
    // clip and draw all triangles
    //
    r_affinetridesc.numtriangles = 1;

    ptri = reinterpret_cast<mtriangle_t*>(reinterpret_cast<byte*>(paliashdr) + paliashdr->triangles);
    for (i = 0; i < pmdl->numtris; i++, ptri++) {
        paclip_fv[0] = &pfinalverts[ptri->vertindex[0]];
        paclip_fv[1] = &pfinalverts[ptri->vertindex[1]];
        paclip_fv[2] = &pfinalverts[ptri->vertindex[2]];

        if (paclip_fv[0]->flags & paclip_fv[1]->flags & paclip_fv[2]->flags & (ALIAS_XY_CLIP_MASK | ALIAS_Z_CLIP)) {
            continue; // completely clipped
        }

        if (!((paclip_fv[0]->flags | paclip_fv[1]->flags | paclip_fv[2]->flags) & (ALIAS_XY_CLIP_MASK | ALIAS_Z_CLIP))) { // totally unclipped
            r_affinetridesc.pfinalverts = pfinalverts;
            r_affinetridesc.ptriangles = ptri;
            D_PolysetDraw();
        } else { // partially clipped
            R_AliasClipTriangle(ptri);
        }
    }
}

/*
================
R_AliasSetUpTransform
================
*/
void R_AliasSetUpTransform(int trivial_accept)
{
    int i;
    float rotationmatrix[3][4], t2matrix[3][4];
    static float tmatrix[3][4];
    static float viewmatrix[3][4];
    Vector3 angles;

    // TODO: should really be stored with the entity instead of being reconstructed
    // TODO: should use a look-up table
    // TODO: could cache lazily, stored in the entity

    angles[ROLL] = currententity->angles[ROLL];
    angles[PITCH] = -currententity->angles[PITCH];
    angles[YAW] = currententity->angles[YAW];
    AngleVectors(angles, alias_forward, alias_right, alias_up);

    tmatrix[0][0] = pmdl->scale[0];
    tmatrix[1][1] = pmdl->scale[1];
    tmatrix[2][2] = pmdl->scale[2];

    tmatrix[0][3] = pmdl->scale_origin[0];
    tmatrix[1][3] = pmdl->scale_origin[1];
    tmatrix[2][3] = pmdl->scale_origin[2];

    // TODO: can do this with simple matrix rearrangement

    for (i = 0; i < 3; i++) {
        t2matrix[i][0] = alias_forward[i];
        t2matrix[i][1] = -alias_right[i];
        t2matrix[i][2] = alias_up[i];
    }

    t2matrix[0][3] = -modelorg[0];
    t2matrix[1][3] = -modelorg[1];
    t2matrix[2][3] = -modelorg[2];

    // FIXME: can do more efficiently than full concatenation
    R_ConcatTransforms(t2matrix, tmatrix, rotationmatrix);

    // TODO: should be global, set when vright, etc., set
    VectorCopy(vright, viewmatrix[0]);
    VectorCopy(vup, viewmatrix[1]);
    VectorInverse(viewmatrix[1]);
    VectorCopy(vpn, viewmatrix[2]);

    //	viewmatrix[0][3] = 0;
    //	viewmatrix[1][3] = 0;
    //	viewmatrix[2][3] = 0;

    R_ConcatTransforms(viewmatrix, rotationmatrix, aliastransform);

    // do the scaling up of x and y to screen coordinates as part of the transform
    // for the unclipped case (it would mess up clipping in the clipped case).
    // Also scale down z, so 1/z is scaled 31 bits for free, and scale down x and y
    // correspondingly so the projected x and y come out right
    // FIXME: make this work for clipped case too?
    if (trivial_accept) {
        for (i = 0; i < 4; i++) {
            aliastransform[0][i] *= aliasxscale * static_cast<float>(1.0 / ((float)0x8000 * 0x10000));
            aliastransform[1][i] *= aliasyscale * static_cast<float>(1.0 / ((float)0x8000 * 0x10000));
            aliastransform[2][i] *= static_cast<float>(1.0 / ((float)0x8000 * 0x10000));
        }
    }
}

/*
================
R_AliasTransformFinalVert
================
*/
void R_AliasTransformFinalVert(finalvert_t* fv,
    auxvert_t* av,
    trivertx_t* pverts,
    stvert_t* pstverts)
{
    int temp;
    float lightcos, *plightnormal;

    av->fv[0] = DotProduct(pverts->v, aliastransform[0]) + aliastransform[0][3];
    av->fv[1] = DotProduct(pverts->v, aliastransform[1]) + aliastransform[1][3];
    av->fv[2] = DotProduct(pverts->v, aliastransform[2]) + aliastransform[2][3];

    fv->v[2] = pstverts->s;
    fv->v[3] = pstverts->t;

    fv->flags = pstverts->onseam;

    // lighting
    plightnormal = r_avertexnormals[pverts->lightnormalindex].data();
    lightcos = DotProduct(plightnormal, r_plightvec);
    temp = r_ambientlight;

    if (lightcos < 0) {
        temp += (int)(r_shadelight * lightcos);

        // clamp; because we limited the minimum ambient and shading light, we
        // don't have to clamp low light, just bright
        if (temp < 0) {
            temp = 0;
        }
    }

    fv->v[4] = temp;
}

/*
================
R_AliasTransformAndProjectFinalVerts
================
*/
void R_AliasTransformAndProjectFinalVerts(finalvert_t* fv, stvert_t* pstverts)
{
    int i, temp;
    float lightcos, *plightnormal, zi;
    trivertx_t* pverts;

    pverts = r_apverts;

    for (i = 0; i < r_anumverts; i++, fv++, pverts++, pstverts++) {
        // transform and project
        zi = static_cast<float>(1.0 / (DotProduct(pverts->v, aliastransform[2]) + aliastransform[2][3]));

        // x, y, and z are scaled down by 1/2**31 in the transform, so 1/z is
        // scaled up by 1/2**31, and the scaling cancels out for x and y in the
        // projection
        fv->v[5] = static_cast<int>(zi);

        fv->v[0] = static_cast<int>(((DotProduct(pverts->v, aliastransform[0]) + aliastransform[0][3]) * zi) + aliasxcenter);
        fv->v[1] = static_cast<int>(((DotProduct(pverts->v, aliastransform[1]) + aliastransform[1][3]) * zi) + aliasycenter);

        fv->v[2] = pstverts->s;
        fv->v[3] = pstverts->t;
        fv->flags = pstverts->onseam;

        // lighting
        plightnormal = r_avertexnormals[pverts->lightnormalindex].data();
        lightcos = DotProduct(plightnormal, r_plightvec);
        temp = r_ambientlight;

        if (lightcos < 0) {
            temp += (int)(r_shadelight * lightcos);

            // clamp; because we limited the minimum ambient and shading light, we
            // don't have to clamp low light, just bright
            if (temp < 0) {
                temp = 0;
            }
        }

        fv->v[4] = temp;
    }
}

/*
================
R_AliasProjectFinalVert
================
*/
void R_AliasProjectFinalVert(finalvert_t* fv, auxvert_t* av)
{
    float zi;

    // project points
    zi = static_cast<float>(1.0 / av->fv[2]);

    fv->v[5] = static_cast<int>(zi * ziscale);

    fv->v[0] = static_cast<int>((av->fv[0] * aliasxscale * zi) + aliasxcenter);
    fv->v[1] = static_cast<int>((av->fv[1] * aliasyscale * zi) + aliasycenter);
}

/*
================
R_AliasPrepareUnclippedPoints
================
*/
void R_AliasPrepareUnclippedPoints(void)
{
    stvert_t* pstverts;
    finalvert_t* fv;

    pstverts = reinterpret_cast<stvert_t*>(reinterpret_cast<byte*>(paliashdr) + paliashdr->stverts);
    r_anumverts = pmdl->numverts;
    // FIXME: just use pfinalverts directly?
    fv = pfinalverts;

    R_AliasTransformAndProjectFinalVerts(fv, pstverts);

    if (r_affinetridesc.drawtype) {
        D_PolysetDrawFinalVerts(fv, r_anumverts);
    }

    r_affinetridesc.pfinalverts = pfinalverts;
    r_affinetridesc.ptriangles = reinterpret_cast<mtriangle_t*>(reinterpret_cast<byte*>(paliashdr) + paliashdr->triangles);
    r_affinetridesc.numtriangles = pmdl->numtris;

    D_PolysetDraw();
}

/*
===============
R_AliasSetupSkin
===============
*/
void R_AliasSetupSkin(void)
{
    int skinnum;
    int i, numskins;
    maliasskingroup_t* paliasskingroup;
    float *pskinintervals, fullskininterval;
    float skintargettime, skintime;

    skinnum = currententity->skinnum;
    if ((skinnum >= pmdl->numskins) || (skinnum < 0)) {
        Con_DPrintf("R_AliasSetupSkin: no such skin # %d\n", skinnum);
        skinnum = 0;
    }

    pskindesc = reinterpret_cast<maliasskindesc_t*>(reinterpret_cast<byte*>(paliashdr) + paliashdr->skindesc) + skinnum;
    a_skinwidth = pmdl->skinwidth;

    if (pskindesc->type == aliasskintype_t::ALIAS_SKIN_GROUP) {
        paliasskingroup = reinterpret_cast<maliasskingroup_t*>(reinterpret_cast<byte*>(paliashdr) + pskindesc->skin);
        pskinintervals = reinterpret_cast<float*>(reinterpret_cast<byte*>(paliashdr) + paliasskingroup->intervals);
        numskins = paliasskingroup->numskins;
        fullskininterval = pskinintervals[numskins - 1];

        skintime = static_cast<float>(cl.time + currententity->syncbase);

        // when loading in Mod_LoadAliasSkinGroup, we guaranteed all interval
        // values are positive, so we don't have to worry about division by 0
        skintargettime = skintime - ((int)(skintime / fullskininterval)) * fullskininterval;

        for (i = 0; i < (numskins - 1); i++) {
            if (pskinintervals[i] > skintargettime) {
                break;
            }
        }

        pskindesc = &paliasskingroup->skindescs[i];
    }

    r_affinetridesc.pskindesc = pskindesc;
    r_affinetridesc.pskin = reinterpret_cast<void*>(reinterpret_cast<byte*>(paliashdr) + pskindesc->skin);
    r_affinetridesc.skinwidth = a_skinwidth;
    r_affinetridesc.seamfixupX16 = (a_skinwidth >> 1) << 16;
    r_affinetridesc.skinheight = pmdl->skinheight;
}

/*
================
R_AliasSetupLighting
================
*/
void R_AliasSetupLighting(alight_t* plighting)
{
    // guarantee that no vertex will ever be lit below LIGHT_MIN, so we don't have
    // to clamp off the bottom
    r_ambientlight = plighting->ambientlight;

    if (r_ambientlight < LIGHT_MIN) {
        r_ambientlight = LIGHT_MIN;
    }

    r_ambientlight = (255 - r_ambientlight) << VID_CBITS;

    if (r_ambientlight < LIGHT_MIN) {
        r_ambientlight = LIGHT_MIN;
    }

    r_shadelight = static_cast<float>(plighting->shadelight);

    if (r_shadelight < 0) {
        r_shadelight = 0;
    }

    r_shadelight *= VID_GRADES;

    // rotate the lighting vector into the model's frame of reference
    r_plightvec[0] = DotProduct(plighting->plightvec, alias_forward);
    r_plightvec[1] = -DotProduct(plighting->plightvec, alias_right);
    r_plightvec[2] = DotProduct(plighting->plightvec, alias_up);
}

/*
=================
R_AliasSetupFrame

set r_apverts
=================
*/
void R_AliasSetupFrame(void)
{
    int frame;
    int i, numframes;
    maliasgroup_t* paliasgroup;
    float *pintervals, fullinterval, targettime, time;

    frame = currententity->frame;
    if ((frame >= pmdl->numframes) || (frame < 0)) {
        Con_DPrintf("R_AliasSetupFrame: no such frame %d\n", frame);
        frame = 0;
    }

    if (paliashdr->frames[frame].type == aliasframetype_t::ALIAS_SINGLE) {
        r_apverts = reinterpret_cast<trivertx_t*>(reinterpret_cast<byte*>(paliashdr) + paliashdr->frames[frame].frame);

        return;
    }

    paliasgroup = reinterpret_cast<maliasgroup_t*>(reinterpret_cast<byte*>(paliashdr) + paliashdr->frames[frame].frame);
    pintervals = reinterpret_cast<float*>(reinterpret_cast<byte*>(paliashdr) + paliasgroup->intervals);
    numframes = paliasgroup->numframes;
    fullinterval = pintervals[numframes - 1];

    time = static_cast<float>(cl.time + currententity->syncbase);

    //
    // when loading in Mod_LoadAliasGroup, we guaranteed all interval values
    // are positive, so we don't have to worry about division by 0
    //
    targettime = time - ((int)(time / fullinterval)) * fullinterval;

    for (i = 0; i < (numframes - 1); i++) {
        if (pintervals[i] > targettime) {
            break;
        }
    }

    r_apverts = reinterpret_cast<trivertx_t*>(reinterpret_cast<byte*>(paliashdr) + paliasgroup->frames[i].frame);
}

/*
================
R_AliasDrawModel
================
*/
void R_AliasDrawModel(alight_t* plighting)
{
    finalvert_t
        finalverts[MAXALIASVERTS + ((CACHE_SIZE - 1) / sizeof(finalvert_t)) + 1];
    auxvert_t auxverts[MAXALIASVERTS];

    r_amodels_drawn++;

    // cache align
    pfinalverts = (finalvert_t*)(((size_t)&finalverts[0] + CACHE_SIZE - 1) & ~(size_t)(CACHE_SIZE - 1));
    pauxverts = &auxverts[0];

    paliashdr = (aliashdr_t*)Mod_Extradata(currententity->model);
    pmdl = reinterpret_cast<mdl_t*>(reinterpret_cast<byte*>(paliashdr) + paliashdr->model);

    R_AliasSetupSkin();
    R_AliasSetUpTransform(currententity->trivial_accept);
    R_AliasSetupLighting(plighting);
    R_AliasSetupFrame();

    if (!currententity->colormap) {
        Sys_Error("R_AliasDrawModel: !currententity->colormap");
    }

    r_affinetridesc.drawtype = (currententity->trivial_accept == 3) && r_recursiveaffinetriangles;

    if (r_affinetridesc.drawtype) {
        D_PolysetUpdateTables(); // FIXME: precalc...
    }

    acolormap = currententity->colormap;

    if (currententity != &cl.viewent) {
        ziscale = (float)0x8000 * (float)0x10000;
    } else {
        ziscale = (float)0x8000 * (float)0x10000 * 3.0;
    }

    if (currententity->trivial_accept) {
        R_AliasPrepareUnclippedPoints();
    } else {
        R_AliasPreparePoints();
    }
}


// ============================================================
// Content from: src\r_aclip.cpp
// ============================================================
// r_aclip.cpp: clip routines for drawing Alias models directly to the screen


static finalvert_t aclip_fv[2][8];
static auxvert_t aclip_av[8];

void R_AliasProjectFinalVert(finalvert_t* fv, auxvert_t* av);
void R_Alias_clip_top(finalvert_t* pfv0, finalvert_t* pfv1, finalvert_t* out);
void R_Alias_clip_bottom(finalvert_t* pfv0,
    finalvert_t* pfv1,
    finalvert_t* out);
void R_Alias_clip_left(finalvert_t* pfv0, finalvert_t* pfv1, finalvert_t* out);
void R_Alias_clip_right(finalvert_t* pfv0, finalvert_t* pfv1, finalvert_t* out);

/*
================
R_Alias_clip_z

pfv0 is the unclipped vertex, pfv1 is the z-clipped vertex
================
*/
void R_Alias_clip_z(finalvert_t* pfv0, finalvert_t* pfv1, finalvert_t* out)
{
    float scale;
    auxvert_t *pav0, *pav1, avout;

    pav0 = &aclip_av[pfv0 - &aclip_fv[0][0]];
    pav1 = &aclip_av[pfv1 - &aclip_fv[0][0]];

    if (pfv0->v[1] >= pfv1->v[1]) {
        scale = (ALIAS_Z_CLIP_PLANE - pav0->fv[2]) / (pav1->fv[2] - pav0->fv[2]);

        avout.fv[0] = pav0->fv[0] + (pav1->fv[0] - pav0->fv[0]) * scale;
        avout.fv[1] = pav0->fv[1] + (pav1->fv[1] - pav0->fv[1]) * scale;
        avout.fv[2] = ALIAS_Z_CLIP_PLANE;

        out->v[2] = static_cast<int>(pfv0->v[2] + (pfv1->v[2] - pfv0->v[2]) * scale);
        out->v[3] = static_cast<int>(pfv0->v[3] + (pfv1->v[3] - pfv0->v[3]) * scale);
        out->v[4] = static_cast<int>(pfv0->v[4] + (pfv1->v[4] - pfv0->v[4]) * scale);
    } else {
        scale = (ALIAS_Z_CLIP_PLANE - pav1->fv[2]) / (pav0->fv[2] - pav1->fv[2]);

        avout.fv[0] = pav1->fv[0] + (pav0->fv[0] - pav1->fv[0]) * scale;
        avout.fv[1] = pav1->fv[1] + (pav0->fv[1] - pav1->fv[1]) * scale;
        avout.fv[2] = ALIAS_Z_CLIP_PLANE;

        out->v[2] = static_cast<int>(pfv1->v[2] + (pfv0->v[2] - pfv1->v[2]) * scale);
        out->v[3] = static_cast<int>(pfv1->v[3] + (pfv0->v[3] - pfv1->v[3]) * scale);
        out->v[4] = static_cast<int>(pfv1->v[4] + (pfv0->v[4] - pfv1->v[4]) * scale);
    }

    R_AliasProjectFinalVert(out, &avout);

    if (out->v[0] < r_refdef.aliasvrect.x) {
        out->flags |= ALIAS_LEFT_CLIP;
    }

    if (out->v[1] < r_refdef.aliasvrect.y) {
        out->flags |= ALIAS_TOP_CLIP;
    }

    if (out->v[0] > r_refdef.aliasvrectright) {
        out->flags |= ALIAS_RIGHT_CLIP;
    }

    if (out->v[1] > r_refdef.aliasvrectbottom) {
        out->flags |= ALIAS_BOTTOM_CLIP;
    }
}

void R_Alias_clip_left(finalvert_t* pfv0, finalvert_t* pfv1, finalvert_t* out)
{
    float scale;
    int i;

    if (pfv0->v[1] >= pfv1->v[1]) {
        scale = static_cast<float>(r_refdef.aliasvrect.x - pfv0->v[0]) / (pfv1->v[0] - pfv0->v[0]);
        for (i = 0; i < 6; i++) {
            out->v[i] = static_cast<int>(pfv0->v[i] + (pfv1->v[i] - pfv0->v[i]) * scale + 0.5);
        }
    } else {
        scale = static_cast<float>(r_refdef.aliasvrect.x - pfv1->v[0]) / (pfv0->v[0] - pfv1->v[0]);
        for (i = 0; i < 6; i++) {
            out->v[i] = static_cast<int>(pfv1->v[i] + (pfv0->v[i] - pfv1->v[i]) * scale + 0.5);
        }
    }
}

void R_Alias_clip_right(finalvert_t* pfv0,
    finalvert_t* pfv1,
    finalvert_t* out)
{
    float scale;
    int i;

    if (pfv0->v[1] >= pfv1->v[1]) {
        scale = static_cast<float>(r_refdef.aliasvrectright - pfv0->v[0]) / (pfv1->v[0] - pfv0->v[0]);
        for (i = 0; i < 6; i++) {
            out->v[i] = static_cast<int>(pfv0->v[i] + (pfv1->v[i] - pfv0->v[i]) * scale + 0.5);
        }
    } else {
        scale = static_cast<float>(r_refdef.aliasvrectright - pfv1->v[0]) / (pfv0->v[0] - pfv1->v[0]);
        for (i = 0; i < 6; i++) {
            out->v[i] = static_cast<int>(pfv1->v[i] + (pfv0->v[i] - pfv1->v[i]) * scale + 0.5);
        }
    }
}

void R_Alias_clip_top(finalvert_t* pfv0, finalvert_t* pfv1, finalvert_t* out)
{
    float scale;
    int i;

    if (pfv0->v[1] >= pfv1->v[1]) {
        scale = static_cast<float>(r_refdef.aliasvrect.y - pfv0->v[1]) / (pfv1->v[1] - pfv0->v[1]);
        for (i = 0; i < 6; i++) {
            out->v[i] = static_cast<int>(pfv0->v[i] + (pfv1->v[i] - pfv0->v[i]) * scale + 0.5);
        }
    } else {
        scale = static_cast<float>(r_refdef.aliasvrect.y - pfv1->v[1]) / (pfv0->v[1] - pfv1->v[1]);
        for (i = 0; i < 6; i++) {
            out->v[i] = static_cast<int>(pfv1->v[i] + (pfv0->v[i] - pfv1->v[i]) * scale + 0.5);
        }
    }
}

void R_Alias_clip_bottom(finalvert_t* pfv0,
    finalvert_t* pfv1,
    finalvert_t* out)
{
    float scale;
    int i;

    if (pfv0->v[1] >= pfv1->v[1]) {
        scale = static_cast<float>(r_refdef.aliasvrectbottom - pfv0->v[1]) / (pfv1->v[1] - pfv0->v[1]);

        for (i = 0; i < 6; i++) {
            out->v[i] = static_cast<int>(pfv0->v[i] + (pfv1->v[i] - pfv0->v[i]) * scale + 0.5);
        }
    } else {
        scale = static_cast<float>(r_refdef.aliasvrectbottom - pfv1->v[1]) / (pfv0->v[1] - pfv1->v[1]);

        for (i = 0; i < 6; i++) {
            out->v[i] = static_cast<int>(pfv1->v[i] + (pfv0->v[i] - pfv1->v[i]) * scale + 0.5);
        }
    }
}

int R_AliasClip(finalvert_t* in,
    finalvert_t* out,
    int flag,
    int count,
    void (*clip)(finalvert_t* pfv0,
        finalvert_t* pfv1,
        finalvert_t* out))
{
    int i, j, k;
    int flags, oldflags;

    j = count - 1;
    k = 0;
    for (i = 0; i < count; j = i, i++) {
        oldflags = in[j].flags & flag;
        flags = in[i].flags & flag;

        if (flags && oldflags) {
            continue;
        }

        if (oldflags ^ flags) {
            clip(&in[j], &in[i], &out[k]);
            out[k].flags = 0;
            if (out[k].v[0] < r_refdef.aliasvrect.x) {
                out[k].flags |= ALIAS_LEFT_CLIP;
            }

            if (out[k].v[1] < r_refdef.aliasvrect.y) {
                out[k].flags |= ALIAS_TOP_CLIP;
            }

            if (out[k].v[0] > r_refdef.aliasvrectright) {
                out[k].flags |= ALIAS_RIGHT_CLIP;
            }

            if (out[k].v[1] > r_refdef.aliasvrectbottom) {
                out[k].flags |= ALIAS_BOTTOM_CLIP;
            }

            k++;
        }

        if (!flags) {
            out[k] = in[i];
            k++;
        }
    }

    return k;
}

/*
================
R_AliasClipTriangle
================
*/
void R_AliasClipTriangle(mtriangle_t* ptri)
{
    int i, k, pingpong;
    mtriangle_t mtri;
    unsigned clipflags;

    // copy vertexes and fix seam texture coordinates
    if (ptri->facesfront) {
        aclip_fv[0][0] = pfinalverts[ptri->vertindex[0]];
        aclip_fv[0][1] = pfinalverts[ptri->vertindex[1]];
        aclip_fv[0][2] = pfinalverts[ptri->vertindex[2]];
    } else {
        for (i = 0; i < 3; i++) {
            aclip_fv[0][i] = pfinalverts[ptri->vertindex[i]];

            if (!ptri->facesfront && (aclip_fv[0][i].flags & ALIAS_ONSEAM)) {
                aclip_fv[0][i].v[2] += r_affinetridesc.seamfixupX16;
            }
        }
    }

    // clip
    clipflags = aclip_fv[0][0].flags | aclip_fv[0][1].flags | aclip_fv[0][2].flags;

    if (clipflags & ALIAS_Z_CLIP) {
        for (i = 0; i < 3; i++) {
            aclip_av[i] = pauxverts[ptri->vertindex[i]];
        }

        k = R_AliasClip(aclip_fv[0], aclip_fv[1], ALIAS_Z_CLIP, 3, R_Alias_clip_z);
        if (k == 0) {
            return;
        }

        pingpong = 1;
        clipflags = aclip_fv[1][0].flags | aclip_fv[1][1].flags | aclip_fv[1][2].flags;
    } else {
        pingpong = 0;
        k = 3;
    }

    if (clipflags & ALIAS_LEFT_CLIP) {
        k = R_AliasClip(aclip_fv[pingpong], aclip_fv[pingpong ^ 1], ALIAS_LEFT_CLIP, k,
            R_Alias_clip_left);
        if (k == 0) {
            return;
        }

        pingpong ^= 1;
    }

    if (clipflags & ALIAS_RIGHT_CLIP) {
        k = R_AliasClip(aclip_fv[pingpong], aclip_fv[pingpong ^ 1], ALIAS_RIGHT_CLIP, k,
            R_Alias_clip_right);
        if (k == 0) {
            return;
        }

        pingpong ^= 1;
    }

    if (clipflags & ALIAS_BOTTOM_CLIP) {
        k = R_AliasClip(aclip_fv[pingpong], aclip_fv[pingpong ^ 1], ALIAS_BOTTOM_CLIP, k,
            R_Alias_clip_bottom);
        if (k == 0) {
            return;
        }

        pingpong ^= 1;
    }

    if (clipflags & ALIAS_TOP_CLIP) {
        k = R_AliasClip(aclip_fv[pingpong], aclip_fv[pingpong ^ 1], ALIAS_TOP_CLIP, k,
            R_Alias_clip_top);
        if (k == 0) {
            return;
        }

        pingpong ^= 1;
    }

    for (i = 0; i < k; i++) {
        if (aclip_fv[pingpong][i].v[0] < r_refdef.aliasvrect.x) {
            aclip_fv[pingpong][i].v[0] = r_refdef.aliasvrect.x;
        } else if (aclip_fv[pingpong][i].v[0] > r_refdef.aliasvrectright) {
            aclip_fv[pingpong][i].v[0] = r_refdef.aliasvrectright;
        }

        if (aclip_fv[pingpong][i].v[1] < r_refdef.aliasvrect.y) {
            aclip_fv[pingpong][i].v[1] = r_refdef.aliasvrect.y;
        } else if (aclip_fv[pingpong][i].v[1] > r_refdef.aliasvrectbottom) {
            aclip_fv[pingpong][i].v[1] = r_refdef.aliasvrectbottom;
        }

        aclip_fv[pingpong][i].flags = 0;
    }

    // draw triangles
    mtri.facesfront = ptri->facesfront;
    r_affinetridesc.ptriangles = &mtri;
    r_affinetridesc.pfinalverts = aclip_fv[pingpong];

    // FIXME: do all at once as trifan?
    mtri.vertindex[0] = 0;
    for (i = 1; i < k - 1; i++) {
        mtri.vertindex[1] = i;
        mtri.vertindex[2] = i + 1;
        D_PolysetDraw();
    }
}


// ============================================================
// Content from: src\r_main.cpp
// ============================================================


//define	PASSAGES

void* colormap;
Vector3 viewlightvec;
alight_t r_viewlighting = { 128, 192, viewlightvec };
bool r_drawpolys;
bool r_drawculledpolys;
bool r_worldpolysbacktofront;
bool r_recursiveaffinetriangles = true;
int r_pixbytes = 1;
float r_aliasuvscale = 1.0;

bool r_dowarp;

int c_surf;

byte* r_warpbuffer;

byte* r_stack_start;

//
// view origin
//
Vector3 vup, base_vup;
Vector3 vpn, base_vpn;
Vector3 vright, base_vright;
Vector3 r_origin;

//
// screen size info
//
refdef_t r_refdef;
float xcenter, ycenter;
float xscale, yscale;
float xscaleinv, yscaleinv;
float xscaleshrink, yscaleshrink;
int screenwidth;

float pixelAspect;

//
// refresh flags
//
int r_framecount = 1; // so frame counts initialized to 0 don't match
int d_spanpixcount;
int r_drawnpolycount;

#define VIEWMODNAME_LENGTH 256
char viewmodname[VIEWMODNAME_LENGTH + 1];
int modcount;



texture_t* r_notexture_mip;

eastl::array<int, 256> d_lightstylevalue; // 8.8 fraction of base light value

void R_MarkLeaves(void);

cvar_t r_clearcolor = { "r_clearcolor", "2", {}, {}, {}, {} };
cvar_t r_drawviewmodel = { "r_drawviewmodel", "1", {}, {}, {}, {} };
cvar_t r_drawflat = { "r_drawflat", "0", {}, {}, {}, {} };
cvar_t r_aliastransbase = { "r_aliastransbase", "200", {}, {}, {}, {} };
cvar_t r_aliastransadj = { "r_aliastransadj", "100", {}, {}, {}, {} };

void CreatePassages(void);
void SetVisibilityByPassages(void);

/*
==================
R_InitTextures
==================
*/
void R_InitTextures(void)
{
    int x, y, m;
    byte* dest;

    // create a simple checkerboard texture for the default
    r_notexture_mip = static_cast<texture_t*>(Hunk_Alloc(
        sizeof(texture_t) + 16 * 16 + 8 * 8 + 4 * 4 + 2 * 2, "notexture"));

    r_notexture_mip->width = r_notexture_mip->height = 16;
    r_notexture_mip->offsets[0] = sizeof(texture_t);
    r_notexture_mip->offsets[1] = r_notexture_mip->offsets[0] + 16 * 16;
    r_notexture_mip->offsets[2] = r_notexture_mip->offsets[1] + 8 * 8;
    r_notexture_mip->offsets[3] = r_notexture_mip->offsets[2] + 4 * 4;

    for (m = 0; m < 4; m++) {
        dest = (byte*)r_notexture_mip + r_notexture_mip->offsets[m];
        for (y = 0; y < (16 >> m); y++) {
            for (x = 0; x < (16 >> m); x++) {
                if ((y < (8 >> m)) ^ (x < (8 >> m))) {
                    *dest++ = 0;
                } else {
                    *dest++ = 0xff;
                }
            }
        }
    }
}

/*
===============
R_Init
===============
*/
void R_Init(void)
{
    int dummy;

    // get stack position so we can guess if we are going to overflow
    r_stack_start = (byte*)&dummy;

    R_InitTurb();
    R_InitVertexNormals();

    Cmd::AddCommand("timerefresh", R_TimeRefresh_f);
    Cmd::AddCommand("pointfile", R_ReadPointFile_f);

    Cvar::Register(&r_draworder);
    Cvar::Register(&r_speeds);
    Cvar::Register(&r_timegraph);
    Cvar::Register(&r_graphheight);
    Cvar::Register(&r_drawflat);
    Cvar::Register(&r_ambient);
    Cvar::Register(&r_clearcolor);
    Cvar::Register(&r_waterwarp);
    Cvar::Register(&r_fullbright);
    Cvar::Register(&r_drawentities);
    Cvar::Register(&r_drawviewmodel);
    Cvar::Register(&r_aliasstats);
    Cvar::Register(&r_dspeeds);
    Cvar::Register(&r_reportsurfout);
    Cvar::Register(&r_maxsurfs);
    Cvar::Register(&r_numsurfs);
    Cvar::Register(&r_reportedgeout);
    Cvar::Register(&r_maxedges);
    Cvar::Register(&r_numedges);
    Cvar::Register(&r_aliastransbase);
    Cvar::Register(&r_aliastransadj);

    Cvar::SetValue("r_maxedges", (float)NUMSTACKEDGES);
    Cvar::SetValue("r_maxsurfs", (float)NUMSTACKSURFACES);

    view_clipplanes[0].leftedge = true;
    view_clipplanes[1].rightedge = true;
    view_clipplanes[1].leftedge = view_clipplanes[2].leftedge = view_clipplanes[3].leftedge = false;
    view_clipplanes[0].rightedge = view_clipplanes[2].rightedge = view_clipplanes[3].rightedge = false;

    r_refdef.xOrigin = XCENTERING;
    r_refdef.yOrigin = YCENTERING;

    R_InitParticles();
    D_Init();
}

/*
===============
R_NewMap
===============
*/
void R_NewMap(void)
{
    int i;

    // clear out efrags in case the level hasn't been reloaded
    // FIXME: is this one short?
    for (i = 0; i < cl.worldmodel->numleafs; i++) {
        cl.worldmodel->leafs[i].efrags = nullptr;
    }

    r_viewleaf = nullptr;
    R_ClearParticles();

    r_cnumsurfs = static_cast<int>(r_maxsurfs.value);

    if (r_cnumsurfs <= MINSURFACES) {
        r_cnumsurfs = MINSURFACES;
    }

    if (r_cnumsurfs > NUMSTACKSURFACES) {
        surfaces = static_cast<surf_t*>(Hunk_Alloc(r_cnumsurfs * sizeof(surf_t), "surfaces"));
        surface_p = surfaces;
        surf_max = &surfaces[r_cnumsurfs];
        r_surfsonstack = false;
        // surface 0 doesn't really exist; it's just a dummy because index 0
        // is used to indicate no edge attached to surface
        surfaces--;
    } else {
        r_surfsonstack = true;
    }

    r_maxedgesseen = 0;
    r_maxsurfsseen = 0;

    r_numallocatededges = static_cast<int>(r_maxedges.value);

    if (r_numallocatededges < MINEDGES) {
        r_numallocatededges = MINEDGES;
    }

    if (r_numallocatededges <= NUMSTACKEDGES) {
        auxedges = nullptr;
    } else {
        auxedges = static_cast<edge_t*>(Hunk_Alloc(r_numallocatededges * sizeof(edge_t), "edges"));
    }

    r_dowarpold = false;
    r_viewchanged = false;
}

/*
===============
R_SetVrect
===============
*/
void R_SetVrect(vrect_t* pvrectin, vrect_t* pvrect, int lineadj)
{
    int h;
    float size;

    size = Screen::GetScreenSystem().GetViewsize().value > 100 ? 100 : Screen::GetScreenSystem().GetViewsize().value;
    if (cl.intermission) {
        size = 100;
        lineadj = 0;
    }

    size /= 100;

    h = pvrectin->height - lineadj;
    pvrect->width = static_cast<int>(pvrectin->width * size);
    if (pvrect->width < 96) {
        size = static_cast<float>(96.0 / pvrectin->width);
        pvrect->width = 96; // min for icons
    }

    pvrect->width &= ~7;
    pvrect->height = static_cast<int>(pvrectin->height * size);
    if (pvrect->height > pvrectin->height - lineadj) {
        pvrect->height = pvrectin->height - lineadj;
    }

    pvrect->height &= ~1;

    pvrect->x = (pvrectin->width - pvrect->width) / 2;
    pvrect->y = (h - pvrect->height) / 2;

    {
        if (lcd_x.value) {
            pvrect->y >>= 1;
            pvrect->height >>= 1;
        }
    }
}

/*
===============
R_ViewChanged

Called every time the vid structure or r_refdef changes.
Guaranteed to be called before the first refresh
===============
*/
void R_ViewChanged(vrect_t* pvrect, int lineadj, float aspect)
{
    int i;
    float res_scale;

    r_viewchanged = true;

    R_SetVrect(pvrect, &r_refdef.vrect, lineadj);

    r_refdef.horizontalFieldOfView = static_cast<float>(2.0 * tan(r_refdef.fov_x / 360 * M_PI));
    r_refdef.fvrectx = static_cast<float>(r_refdef.vrect.x);
    r_refdef.fvrectx_adj = static_cast<float>(r_refdef.vrect.x) - 0.5f;
    r_refdef.vrect_x_adj_shift20 = ((int64_t)r_refdef.vrect.x << 20) + (1 << 19) - 1;
    r_refdef.fvrecty = static_cast<float>(r_refdef.vrect.y);
    r_refdef.fvrecty_adj = static_cast<float>(r_refdef.vrect.y) - 0.5f;
    r_refdef.vrectright = r_refdef.vrect.x + r_refdef.vrect.width;
    r_refdef.vrectright_adj_shift20 = ((int64_t)r_refdef.vrectright << 20) + (1 << 19) - 1;
    r_refdef.fvrectright = static_cast<float>(r_refdef.vrectright);
    r_refdef.fvrectright_adj = static_cast<float>(r_refdef.vrectright) - 0.5f;
    r_refdef.vrectrightedge = static_cast<float>(r_refdef.vrectright) - 0.99f;
    r_refdef.vrectbottom = r_refdef.vrect.y + r_refdef.vrect.height;
    r_refdef.fvrectbottom = static_cast<float>(r_refdef.vrectbottom);
    r_refdef.fvrectbottom_adj = static_cast<float>(r_refdef.vrectbottom) - 0.5f;

    r_refdef.aliasvrect.x = (int)(r_refdef.vrect.x * r_aliasuvscale);
    r_refdef.aliasvrect.y = (int)(r_refdef.vrect.y * r_aliasuvscale);
    r_refdef.aliasvrect.width = (int)(r_refdef.vrect.width * r_aliasuvscale);
    r_refdef.aliasvrect.height = (int)(r_refdef.vrect.height * r_aliasuvscale);
    r_refdef.aliasvrectright = r_refdef.aliasvrect.x + r_refdef.aliasvrect.width;
    r_refdef.aliasvrectbottom = r_refdef.aliasvrect.y + r_refdef.aliasvrect.height;

    pixelAspect = aspect;
    xOrigin = r_refdef.xOrigin;
    yOrigin = r_refdef.yOrigin;

    screenAspect = r_refdef.vrect.width * pixelAspect / r_refdef.vrect.height;
    // 320*200 1.0 pixelAspect = 1.6 screenAspect
    // 320*240 1.0 pixelAspect = 1.3333 screenAspect
    // proper 320*200 pixelAspect = 0.8333333

    verticalFieldOfView = r_refdef.horizontalFieldOfView / screenAspect;

    // values for perspective projection
    // if math were exact, the values would range from 0.5 to to range+0.5
    // hopefully they wll be in the 0.000001 to range+.999999 and truncate
    // the polygon rasterization will never render in the first row or column
    // but will definately render in the [range] row and column, so adjust the
    // buffer origin to get an exact edge to edge fill
    xcenter = (static_cast<float>(r_refdef.vrect.width) * static_cast<float>(XCENTERING)) + static_cast<float>(r_refdef.vrect.x) - 0.5f;
    aliasxcenter = xcenter * r_aliasuvscale;
    ycenter = (static_cast<float>(r_refdef.vrect.height) * static_cast<float>(YCENTERING)) + static_cast<float>(r_refdef.vrect.y) - 0.5f;
    aliasycenter = ycenter * r_aliasuvscale;

    xscale = static_cast<float>(r_refdef.vrect.width) / r_refdef.horizontalFieldOfView;
    aliasxscale = xscale * r_aliasuvscale;
    xscaleinv = 1.0f / xscale;
    yscale = xscale * pixelAspect;
    aliasyscale = yscale * r_aliasuvscale;
    yscaleinv = 1.0f / yscale;
    xscaleshrink = static_cast<float>(r_refdef.vrect.width - 6) / r_refdef.horizontalFieldOfView;
    yscaleshrink = xscaleshrink * pixelAspect;

    // left side clip
    screenedge[0].normal[0] = static_cast<float>(-1.0 / (xOrigin * r_refdef.horizontalFieldOfView));
    screenedge[0].normal[1] = 0;
    screenedge[0].normal[2] = 1;
    screenedge[0].type = PLANE_ANYZ;

    // right side clip
    screenedge[1].normal[0] = static_cast<float>(1.0 / ((1.0 - xOrigin) * r_refdef.horizontalFieldOfView));
    screenedge[1].normal[1] = 0;
    screenedge[1].normal[2] = 1;
    screenedge[1].type = PLANE_ANYZ;

    // top side clip
    screenedge[2].normal[0] = 0;
    screenedge[2].normal[1] = static_cast<float>(-1.0 / (yOrigin * verticalFieldOfView));
    screenedge[2].normal[2] = 1;
    screenedge[2].type = PLANE_ANYZ;

    // bottom side clip
    screenedge[3].normal[0] = 0;
    screenedge[3].normal[1] = static_cast<float>(1.0 / ((1.0 - yOrigin) * verticalFieldOfView));
    screenedge[3].normal[2] = 1;
    screenedge[3].type = PLANE_ANYZ;

    for (i = 0; i < 4; i++) {
        VectorNormalize(screenedge[i].normal);
    }

    res_scale = static_cast<float>(sqrt(static_cast<double>(r_refdef.vrect.width * r_refdef.vrect.height) / (320.0 * 152.0)) * (2.0 / r_refdef.horizontalFieldOfView));
    r_aliastransition = r_aliastransbase.value * res_scale;
    r_resfudge = r_aliastransadj.value * res_scale;

    if (Screen::GetScreenSystem().GetFov().value <= 90.0) {
        r_fov_greater_than_90 = false;
    } else {
        r_fov_greater_than_90 = true;
    }

    D_ViewChanged();
}

/*
===============
R_MarkLeaves
===============
*/
void R_MarkLeaves(void)
{
    byte* vis;
    mnode_t* node;
    int i;

    if (r_oldviewleaf == r_viewleaf) {
        return;
    }

    r_visframecount++;
    r_oldviewleaf = r_viewleaf;

    vis = Mod_LeafPVS(r_viewleaf, cl.worldmodel);

    for (i = 0; i < cl.worldmodel->numleafs; i++) {
        if (vis[i >> 3] & (1 << (i & 7))) {
            node = (mnode_t*)&cl.worldmodel->leafs[i + 1];
            do {
                if (node->visframe == r_visframecount) {
                    break;
                }

                node->visframe = r_visframecount;
                node = node->parent;
            } while (node);
        }
    }
}

/*
=============
R_DrawEntitiesOnList
=============
*/
void R_DrawEntitiesOnList(void)
{
    int i, j;
    int lnum;
    alight_t lighting;
    // FIXME: remove and do real lighting
    float lightvec[3] = { -1, 0, 0 };
    Vector3 dist;
    float add;

    if (!r_drawentities.value) {
        return;
    }

    for (i = 0; i < cl_numvisedicts; i++) {
        currententity = cl_visedicts[i];

        if (currententity == &cl_entities[cl.viewentity]) {
            continue; // don't draw the player
        }

        switch (currententity->model->type) {
        case mod_sprite:
            r_entorigin = currententity->origin;
            modelorg = r_origin - r_entorigin;
            R_DrawSprite();
            break;

        case mod_alias:
            r_entorigin = currententity->origin;
            modelorg = r_origin - r_entorigin;

            // see if the bounding box lets us trivially reject, also sets
            // trivial accept status
            if (R_AliasCheckBBox()) {
                j = R_LightPoint(currententity->origin);

                lighting.ambientlight = j;
                lighting.shadelight = j;

                lighting.plightvec = lightvec;

                for (lnum = 0; lnum < MAX_DLIGHTS; lnum++) {
                    if (cl_dlights[lnum].die >= cl.time) {
                        dist = currententity->origin - cl_dlights[lnum].origin;
                        add = cl_dlights[lnum].radius - dist.length();

                        if (add > 0) {
                            lighting.ambientlight += static_cast<int>(add);
                        }
                    }
                }

                // clamp lighting so it doesn't overbright as much
                if (lighting.ambientlight > 128) {
                    lighting.ambientlight = 128;
                }

                if (lighting.ambientlight + lighting.shadelight > 192) {
                    lighting.shadelight = 192 - lighting.ambientlight;
                }

                R_AliasDrawModel(&lighting);
            }

            break;

        default:
            break;
        }
    }
}

/*
=============
R_DrawViewModel
=============
*/
void R_DrawViewModel(void)
{
    // FIXME: remove and do real lighting
    float lightvec[3] = { -1, 0, 0 };
    int j;
    int lnum;
    Vector3 dist;
    float add;
    dlight_t* dl;

    if (!r_drawviewmodel.value || r_fov_greater_than_90) {
        return;
    }

    if (cl.items & IT_INVISIBILITY) {
        return;
    }

    if (cl.stats[STAT_HEALTH] <= 0) {
        return;
    }

    currententity = &cl.viewent;
    if (!currententity->model) {
        return;
    }

    r_entorigin = currententity->origin;
    modelorg = r_origin - r_entorigin;

    viewlightvec = -vup;

    j = R_LightPoint(currententity->origin);

    if (j < 24) {
        j = 24; // allways give some light on gun
    }

    r_viewlighting.ambientlight = j;
    r_viewlighting.shadelight = j;

    // add dynamic lights
    for (lnum = 0; lnum < MAX_DLIGHTS; lnum++) {
        dl = &cl_dlights[lnum];
        if (!dl->radius) {
            continue;
        }

        if (!dl->radius) {
            continue;
        }

        if (dl->die < cl.time) {
            continue;
        }

        dist = currententity->origin - dl->origin;
        add = dl->radius - dist.length();
        if (add > 0) {
            r_viewlighting.ambientlight += static_cast<int>(add);
        }
    }

    // clamp lighting so it doesn't overbright as much
    if (r_viewlighting.ambientlight > 128) {
        r_viewlighting.ambientlight = 128;
    }

    if (r_viewlighting.ambientlight + r_viewlighting.shadelight > 192) {
        r_viewlighting.shadelight = 192 - r_viewlighting.ambientlight;
    }

    r_viewlighting.plightvec = lightvec;


    R_AliasDrawModel(&r_viewlighting);
}

/*
=============
R_BmodelCheckBBox
=============
*/
int R_BmodelCheckBBox(model_t* clmodel, float* minmaxs)
{
    int i, *pindex, clipflags;
    Vector3 acceptpt, rejectpt;
    double d;

    clipflags = 0;

    if (currententity->angles[0] || currententity->angles[1] || currententity->angles[2]) {
        for (i = 0; i < 4; i++) {
            d = currententity->origin.dot(view_clipplanes[i].normal);
            d -= view_clipplanes[i].dist;

            if (d <= -clmodel->radius) {
                return BMODEL_FULLY_CLIPPED;
            }

            if (d <= clmodel->radius) {
                clipflags |= (1 << i);
            }
        }
    } else {
        for (i = 0; i < 4; i++) {
            // generate accept and reject points
            // FIXME: do with fast look-ups or integer tests based on the sign bit
            // of the floating point values

            pindex = pfrustum_indexes[i];

            rejectpt = Vector3(minmaxs[pindex[0]], minmaxs[pindex[1]], minmaxs[pindex[2]]);

            d = rejectpt.dot(view_clipplanes[i].normal);
            d -= view_clipplanes[i].dist;

            if (d <= 0) {
                return BMODEL_FULLY_CLIPPED;
            }

            acceptpt = Vector3(minmaxs[pindex[3 + 0]], minmaxs[pindex[3 + 1]], minmaxs[pindex[3 + 2]]);

            d = acceptpt.dot(view_clipplanes[i].normal);
            d -= view_clipplanes[i].dist;

            if (d <= 0) {
                clipflags |= (1 << i);
            }
        }
    }

    return clipflags;
}

/*
=============
R_DrawBEntitiesOnList
=============
*/
void R_DrawBEntitiesOnList(void)
{
    int i, k, clipflags;
    Vector3 oldorigin;
    model_t* clmodel;
    float minmaxs[6];

    if (!r_drawentities.value) {
        return;
    }

    oldorigin = modelorg;
    insubmodel = true;
    r_dlightframecount = r_framecount;

    for (i = 0; i < cl_numvisedicts; i++) {
        currententity = cl_visedicts[i];

        switch (currententity->model->type) {
        case mod_brush:

            clmodel = currententity->model;

            // see if the bounding box lets us trivially reject, also sets
            // trivial accept status
            minmaxs[0] = currententity->origin.x + clmodel->mins[0];
            minmaxs[1] = currententity->origin.y + clmodel->mins[1];
            minmaxs[2] = currententity->origin.z + clmodel->mins[2];
            minmaxs[3] = currententity->origin.x + clmodel->maxs[0];
            minmaxs[4] = currententity->origin.y + clmodel->maxs[1];
            minmaxs[5] = currententity->origin.z + clmodel->maxs[2];

            clipflags = R_BmodelCheckBBox(clmodel, minmaxs);

            if (clipflags != BMODEL_FULLY_CLIPPED) {
                r_entorigin = currententity->origin;
                modelorg = r_origin - r_entorigin;
                // FIXME: is this needed?
                r_worldmodelorg = modelorg;

                r_pcurrentvertbase = clmodel->vertexes;

                // FIXME: stop transforming twice
                R_RotateBmodel();

                // calculate dynamic lighting for bmodel if it's not an
                // instanced model
                if (clmodel->firstmodelsurface != 0) {
                    for (k = 0; k < MAX_DLIGHTS; k++) {
                        if ((cl_dlights[k].die < cl.time) || (!cl_dlights[k].radius)) {
                            continue;
                        }

                        R_MarkLights(&cl_dlights[k], 1 << k,
                            clmodel->nodes + clmodel->hulls[0].firstclipnode);
                    }
                }

                // if the driver wants polygons, deliver those. Z-buffering is on
                // at this point, so no clipping to the world tree is needed, just
                // frustum clipping
                if (r_drawpolys | r_drawculledpolys) {
                    R_ZDrawSubmodelPolys(clmodel);
                } else {
                    r_pefragtopnode = nullptr;

                    r_emins = Vector3(minmaxs[0], minmaxs[1], minmaxs[2]);
                    r_emaxs = Vector3(minmaxs[3], minmaxs[4], minmaxs[5]);

                    R_SplitEntityOnNode2(cl.worldmodel->nodes);

                    if (r_pefragtopnode) {
                        currententity->topnode = r_pefragtopnode;

                        if (r_pefragtopnode->contents >= 0) {
                            // not a leaf; has to be clipped to the world BSP
                            r_clipflags = clipflags;
                            R_DrawSolidClippedSubmodelPolygons(clmodel);
                        } else {
                            // falls entirely in one leaf, so we just put all the
                            // edges in the edge list and let 1/z sorting handle
                            // drawing order
                            R_DrawSubmodelPolygons(clmodel, clipflags);
                        }

                        currententity->topnode = nullptr;
                    }
                }

                // put back world rotation and frustum clipping
                // FIXME: R_RotateBmodel should just work off base_vxx
                vpn = base_vpn;
                vup = base_vup;
                vright = base_vright;
                modelorg = base_modelorg;
                modelorg = oldorigin;
                R_TransformFrustum();
            }

            break;

        default:
            break;
        }
    }

    insubmodel = false;
}

/*
================
R_EdgeDrawing
================
*/
void R_EdgeDrawing(void)
{
    edge_t ledges[NUMSTACKEDGES + ((CACHE_SIZE - 1) / sizeof(edge_t)) + 1];
    surf_t lsurfs[NUMSTACKSURFACES + ((CACHE_SIZE - 1) / sizeof(surf_t)) + 1];

    if (auxedges) {
        r_edges = auxedges;
    } else {
        r_edges = (edge_t*)(((size_t)&ledges[0] + CACHE_SIZE - 1) & ~(size_t)(CACHE_SIZE - 1));
    }

    if (r_surfsonstack) {
        surfaces = (surf_t*)(((size_t)&lsurfs[0] + CACHE_SIZE - 1) & ~(size_t)(CACHE_SIZE - 1));
        surf_max = &surfaces[r_cnumsurfs];
        // surface 0 doesn't really exist; it's just a dummy because index 0
        // is used to indicate no edge attached to surface
        surfaces--;
    }

    R_BeginEdgeFrame();

    if (r_dspeeds.value) {
        rw_time1 = static_cast<float>(Sys_FloatTime());
    }

    R_RenderWorld();

    if (r_drawculledpolys) {
        R_ScanEdges();
    }

    // only the world can be drawn back to front with no z reads or compares, just
    // z writes, so have the driver turn z compares on now
    D_TurnZOn();

    if (r_dspeeds.value) {
        rw_time2 = static_cast<float>(Sys_FloatTime());
        db_time1 = rw_time2;
    }

    R_DrawBEntitiesOnList();

    if (r_dspeeds.value) {
        db_time2 = static_cast<float>(Sys_FloatTime());
        se_time1 = db_time2;
    }

    if (!r_dspeeds.value) {
        VID_UnlockBuffer();
        S_ExtraUpdate(); // don't let sound get messed up if going slow
        VID_LockBuffer();
    }

    if (!(r_drawpolys | r_drawculledpolys)) {
        R_ScanEdges();
    }
}

/*
================
R_RenderView

r_refdef must be set before the first call
================
*/
void R_RenderView_(void)
{
    eastl::array<byte, WARP_WIDTH * WARP_HEIGHT> warpbuffer{};

    r_warpbuffer = warpbuffer.data();

    if (r_timegraph.value || r_speeds.value || r_dspeeds.value) {
        r_time1 = static_cast<float>(Sys_FloatTime());
    }

    R_SetupFrame();

    R_MarkLeaves(); // done here so we know if we're in water

    // make FDIV fast. This reduces timing precision after we've been running for a
    // while, so we don't do it globally.  This also sets chop mode, and we do it
    // here so that setup stuff like the refresh area calculations match what's
    // done in screen.cpp
    Sys_LowFPPrecision();

    if (!cl_entities[0].model || !cl.worldmodel) {
        Sys_Error("R_RenderView: nullptr worldmodel");
    }

    if (!r_dspeeds.value) {
        VID_UnlockBuffer();
        S_ExtraUpdate(); // don't let sound get messed up if going slow
        VID_LockBuffer();
    }

    R_EdgeDrawing();

    if (!r_dspeeds.value) {
        VID_UnlockBuffer();
        S_ExtraUpdate(); // don't let sound get messed up if going slow
        VID_LockBuffer();
    }

    if (r_dspeeds.value) {
        se_time2 = static_cast<float>(Sys_FloatTime());
        de_time1 = se_time2;
    }

    R_DrawEntitiesOnList();

    if (r_dspeeds.value) {
        de_time2 = static_cast<float>(Sys_FloatTime());
        dv_time1 = de_time2;
    }

    R_DrawViewModel();

    if (r_dspeeds.value) {
        dv_time2 = static_cast<float>(Sys_FloatTime());
        dp_time1 = static_cast<float>(Sys_FloatTime());
    }

    R_DrawParticles();

    if (r_dspeeds.value) {
        dp_time2 = static_cast<float>(Sys_FloatTime());
    }

    if (r_dowarp) {
        D_WarpScreen();
    }

    V_SetContentsColor(r_viewleaf->contents);

    if (r_timegraph.value) {
        R_TimeGraph();
    }

    if (r_aliasstats.value) {
        R_PrintAliasStats();
    }

    if (r_speeds.value) {
        R_PrintTimes();
    }

    if (r_dspeeds.value) {
        R_PrintDSpeeds();
    }

    if (r_reportsurfout.value && r_outofsurfaces) {
        Con_Printf("Short %d surfaces\n", r_outofsurfaces);
    }

    if (r_reportedgeout.value && r_outofedges) {
        Con_Printf("Short roughly %d edges\n", r_outofedges * 2 / 3);
    }

    // back to high floating-point precision
    Sys_HighFPPrecision();
}

void R_RenderView(void)
{
    int dummy;
    int delta;

    delta = static_cast<int>(reinterpret_cast<byte*>(&dummy) - r_stack_start);
    if (delta < -10000 || delta > 10000) {
        Sys_Error("R_RenderView: called without enough stack");
    }

    if (Hunk_LowMark() & 3) {
        Sys_Error("Hunk is missaligned");
    }

    if (reinterpret_cast<size_t>(&dummy) & 3) {
        Sys_Error("Stack is missaligned");
    }

    if (reinterpret_cast<size_t>(&r_warpbuffer) & 3) {
        Sys_Error("Globals are missaligned");
    }

    R_RenderView_();
}

/*
================
R_InitTurb
================
*/
void R_InitTurb(void)
{
    int i;

    for (i = 0; i < (SIN_BUFFER_SIZE); i++) {
        sintable[i] = static_cast<int>(AMP + sin(i * 3.14159 * 2 / CYCLE) * AMP);
        intsintable[i] = static_cast<int>(AMP2 + sin(i * 3.14159 * 2 / CYCLE) * AMP2); // AMP2, not 20
    }
}

} // namespace Render


// rasterizer.cpp -- merged software rasterization driver
//
// Merged from: d_vars.cpp, d_init.cpp, d_modech.cpp, d_edge.cpp,
//              d_scan.cpp, d_sky.cpp, d_surf.cpp, d_sprite.cpp, d_polyse.cpp

#include <tuple>
#include <utility>

using namespace Client;
using namespace Common;
using namespace Console;
using namespace Render;
using namespace Draw;
using namespace Host;
using namespace Input;
using namespace Keys;
using namespace Math;
using namespace Menu;
using namespace Model;
using namespace Net;
using namespace VM;
using namespace Sbar;
using namespace Screen;
using namespace Server;
using namespace Audio;
using namespace Vid;
using namespace View;
using namespace Wad;
using namespace Cvar;
using namespace Cmd;


namespace Render {

// ==============================================================
// Global variables (TU-local via anonymous namespace)
// ==============================================================

namespace {

// d_vars.cpp
float d_sdivzstepu = 0.0f, d_tdivzstepu = 0.0f, d_zistepu = 0.0f;
float d_sdivzstepv = 0.0f, d_tdivzstepv = 0.0f, d_zistepv = 0.0f;
float d_sdivzorigin = 0.0f, d_tdivzorigin = 0.0f, d_ziorigin = 0.0f;

fixed16_t sadjust = 0, tadjust = 0, bbextents = 0, bbextentt = 0;

pixel_t* cacheblock = nullptr;
int cachewidth = 0;
pixel_t* d_viewbuffer = nullptr;

// d_init.cpp
constexpr int NUM_MIPS = 4;

cvar_t d_subdiv16 = { "d_subdiv16", "1", false, false, 0.0f, nullptr };
cvar_t d_mipcap = { "d_mipcap", "0", false, false, 0.0f, nullptr };
cvar_t d_mipscale = { "d_mipscale", "1", false, false, 0.0f, nullptr };

surfcache_t* d_initial_rover = nullptr;
qboolean d_roverwrapped = false;
int d_minmip = 0;
eastl::array<float, NUM_MIPS - 1> d_scalemip{};

constexpr eastl::array<float, NUM_MIPS - 1> basemip = { 1.0f, 0.5f * 0.8f, 0.25f * 0.8f };

void (*d_drawspans)(espan_t* pspan) = nullptr;

// d_modech.cpp
int d_vrectx = 0, d_vrecty = 0, d_vrectright_particle = 0, d_vrectbottom_particle = 0;

int d_y_aspect_shift = 0, d_pix_min = 0, d_pix_max = 0, d_pix_shift = 0;

eastl::array<int, MAXHEIGHT> d_scantable{};
eastl::array<short*, MAXHEIGHT> zspantable{};

// d_edge.cpp
int miplevel = 0;

float scale_for_mip = 0.0f;
int ubasestep = 0, errorterm = 0, erroradjustup = 0, erroradjustdown = 0;

Vector3 transformed_modelorg{};

// d_surf.cpp
float surfscale = 0.0f;

int sc_size = 0;
surfcache_t* sc_rover = nullptr;
surfcache_t* sc_base = nullptr;

// d_scan.cpp
unsigned char* r_turb_pbase = nullptr;
unsigned char* r_turb_pdest = nullptr;
fixed16_t r_turb_s = 0, r_turb_t = 0, r_turb_sstep = 0, r_turb_tstep = 0;
int* r_turb_turb = nullptr;
int r_turb_spancount = 0;

struct edgetable {
    int isflattop = 0;
    int numleftedges = 0;
    const eastl::array<int, 6>* pleftedgevert0 = nullptr;
    const eastl::array<int, 6>* pleftedgevert1 = nullptr;
    const eastl::array<int, 6>* pleftedgevert2 = nullptr;
    int numrightedges = 0;
    const eastl::array<int, 6>* prightedgevert0 = nullptr;
    const eastl::array<int, 6>* prightedgevert1 = nullptr;
    const eastl::array<int, 6>* prightedgevert2 = nullptr;
};

struct spanpackage_t {
    void* pdest = nullptr;
    short* pz = nullptr;
    int count = 0;
    byte* ptex = nullptr;
    int sfrac = 0, tfrac = 0, light = 0, zi = 0;
};

eastl::array<int, 6> r_p0{}, r_p1{}, r_p2{};

byte* d_pcolormap = nullptr;
int d_aflatcolor = 0;
int d_xdenom = 0;

const edgetable* pedgetable = nullptr;

// edgetables references r_p0, r_p1, r_p2 so must come after them
const edgetable edgetables[12] = {
    { 0, 1, &r_p0, &r_p2, nullptr, 2, &r_p0, &r_p1, &r_p2 },
    { 0, 2, &r_p1, &r_p0, &r_p2, 1, &r_p1, &r_p2, nullptr },
    { 1, 1, &r_p0, &r_p2, nullptr, 1, &r_p1, &r_p2, nullptr },
    { 0, 1, &r_p1, &r_p0, nullptr, 2, &r_p1, &r_p2, &r_p0 },
    { 0, 2, &r_p0, &r_p2, &r_p1, 1, &r_p0, &r_p1, nullptr },
    { 0, 1, &r_p2, &r_p1, nullptr, 1, &r_p2, &r_p0, nullptr },
    { 0, 1, &r_p2, &r_p1, nullptr, 2, &r_p2, &r_p0, &r_p1 },
    { 0, 2, &r_p2, &r_p1, &r_p0, 1, &r_p2, &r_p0, nullptr },
    { 0, 1, &r_p1, &r_p0, nullptr, 1, &r_p1, &r_p2, nullptr },
    { 1, 1, &r_p2, &r_p1, nullptr, 1, &r_p0, &r_p1, nullptr },
    { 1, 1, &r_p1, &r_p0, nullptr, 1, &r_p2, &r_p0, nullptr },
    { 0, 1, &r_p0, &r_p2, nullptr, 1, &r_p0, &r_p1, nullptr },
};

int a_sstepxfrac = 0, a_tstepxfrac = 0, r_lstepx = 0, a_ststepxwhole = 0;
int r_sstepx = 0, r_tstepx = 0, r_lstepy = 0, r_sstepy = 0, r_tstepy = 0;
int r_zistepx = 0, r_zistepy = 0;
int d_aspancount = 0, d_countextrastep = 0;

spanpackage_t* a_spans = nullptr;
spanpackage_t* d_pedgespanpackage = nullptr;
int ystart = 0;
byte* d_pdest = nullptr;
byte* d_ptex = nullptr;
short* d_pz = nullptr;
int d_sfrac = 0, d_tfrac = 0, d_light = 0, d_zi = 0;
int d_ptexextrastep = 0, d_sfracextrastep = 0;
int d_tfracextrastep = 0, d_lightextrastep = 0, d_pdestextrastep = 0;
int d_lightbasestep = 0, d_pdestbasestep = 0, d_ptexbasestep = 0;
int d_sfracbasestep = 0, d_tfracbasestep = 0;
int d_ziextrastep = 0, d_zibasestep = 0;
int d_pzextrastep = 0, d_pzbasestep = 0;

eastl::array<byte*, MAX_LBM_HEIGHT> skintable{};
int skinwidth = 0;
byte* skinstart = nullptr;

// d_sprite.cpp
int minindex = 0, maxindex = 0;

sspan_t* sprite_spans = nullptr;

} // namespace

// Shared global variables (visible to other TUs)
unsigned int d_zrowbytes = 0;
unsigned int d_zwidth = 0;
qboolean r_cache_thrash = false;


// ==============================================================
// Forward declarations for internal functions
// ==============================================================

void D_DrawTurbulent8Span();
void D_PolysetDrawSpans8(spanpackage_t* pspanpackage);
void D_PolysetCalcGradients(int skinwidth);
void D_DrawSubdiv();
void D_DrawNonSubdiv();
void D_PolysetRecursiveTriangle(const eastl::array<int, 6>* p1, const eastl::array<int, 6>* p2, const eastl::array<int, 6>* p3);
void D_PolysetSetEdgeTable();
void D_RasterizeAliasPolySmooth();
void D_PolysetScanLeftEdge(int height);

// ==============================================================
// d_init.cpp -- rasterization driver initialization
// ==============================================================

void D_Init(void)
{
    r_skydirect = 1;

    Cvar::Register(&d_subdiv16);
    Cvar::Register(&d_mipcap);
    Cvar::Register(&d_mipscale);

    r_drawpolys = false;
    r_worldpolysbacktofront = false;
    r_recursiveaffinetriangles = true;
    r_pixbytes = 1;
    r_aliasuvscale = 1.0;
}

void D_TurnZOn()
{
}

void D_SetupFrame()
{
    if (r_dowarp) {
        d_viewbuffer = r_warpbuffer;
    } else {
        d_viewbuffer = vid.buffer;
    }

    if (r_dowarp) {
        screenwidth = WARP_WIDTH;
    } else {
        screenwidth = vid.rowbytes;
    }

    d_roverwrapped = false;
    d_initial_rover = sc_rover;

    d_minmip = static_cast<int>(d_mipcap.value);
    if (d_minmip > 3) {
        d_minmip = 3;
    } else if (d_minmip < 0) {
        d_minmip = 0;
    }

    for (size_t i = 0; i < (NUM_MIPS - 1); ++i) {
        d_scalemip[i] = basemip[i] * d_mipscale.value;
    }

    d_drawspans = D_DrawSpans8;
    d_aflatcolor = 0;
}

void D_UpdateRects(vrect_t* prect)
{
    UNUSED(prect);
}

// ==============================================================
// d_modech.cpp -- called when mode has just changed
// ==============================================================

void D_ViewChanged()
{
    int rowbytes;

    if (r_dowarp) {
        rowbytes = WARP_WIDTH;
    } else {
        rowbytes = vid.rowbytes;
    }

    scale_for_mip = xscale;
    if (yscale > xscale) {
        scale_for_mip = yscale;
    }

    d_zrowbytes = vid.width * 2;
    d_zwidth = vid.width;

    d_pix_min = r_refdef.vrect.width / 320;
    if (d_pix_min < 1) {
        d_pix_min = 1;
    }

    d_pix_max = static_cast<int>(static_cast<float>(r_refdef.vrect.width) / (320.0f / 4.0f) + 0.5f);
    d_pix_shift = 8 - static_cast<int>(static_cast<float>(r_refdef.vrect.width) / 320.0f + 0.5f);
    if (d_pix_max < 1) {
        d_pix_max = 1;
    }

    if (pixelAspect > 1.4) {
        d_y_aspect_shift = 1;
    } else {
        d_y_aspect_shift = 0;
    }

    d_vrectx = r_refdef.vrect.x;
    d_vrecty = r_refdef.vrect.y;
    d_vrectright_particle = r_refdef.vrectright - d_pix_max;
    d_vrectbottom_particle = r_refdef.vrectbottom - (d_pix_max << d_y_aspect_shift);

    {
        for (unsigned i = 0; i < vid.height; ++i) {
            d_scantable[i] = i * rowbytes;
            zspantable[i] = d_pzbuffer + i * d_zwidth;
        }
    }
}

// ==============================================================
// d_edge.cpp -- software edge rendering (mipmapping and texture stepping)
// ==============================================================

void D_DrawPoly()
{
}

int D_MipLevelForScale(float scale)
{
    int lmiplevel;

    if (scale >= d_scalemip[0]) {
        lmiplevel = 0;
    } else if (scale >= d_scalemip[1]) {
        lmiplevel = 1;
    } else if (scale >= d_scalemip[2]) {
        lmiplevel = 2;
    } else {
        lmiplevel = 3;
    }

    if (lmiplevel < d_minmip) {
        lmiplevel = d_minmip;
    }

    return lmiplevel;
}

void D_DrawSolidSurface(surf_t* surf, int color)
{
    espan_t* span;
    byte* pdest;
    int u, u2, pix;

    pix = (color << 24) | (color << 16) | (color << 8) | color;
    for (span = surf->spans; span; span = span->pnext) {
        pdest = reinterpret_cast<byte*>(d_viewbuffer) + screenwidth * span->v;
        u = span->u;
        u2 = span->u + span->count - 1;
        pdest[u] = static_cast<byte>(pix);

        if (u2 - u < 8) {
            for (u++; u <= u2; u++) {
                pdest[u] = static_cast<byte>(pix);
            }
        } else {
            for (u++; u & 3; u++) {
                pdest[u] = static_cast<byte>(pix);
            }

            u2 -= 4;
            for (; u <= u2; u += 4) {
                *reinterpret_cast<int*>(pdest + u) = pix;
            }
            u2 += 4;
            for (; u <= u2; u++) {
                pdest[u] = static_cast<byte>(pix);
            }
        }
    }
}

void D_CalcGradients(msurface_t* pface)
{
    float mipscale;
    Vector3 p_temp1;
    Vector3 p_saxis, p_taxis;
    float t;

    mipscale = 1.0f / static_cast<float>(1 << miplevel);

    TransformVector(pface->texinfo->vecs[0], p_saxis);
    TransformVector(pface->texinfo->vecs[1], p_taxis);

    t = xscaleinv * mipscale;
    d_sdivzstepu = p_saxis.x * t;
    d_tdivzstepu = p_taxis.x * t;

    t = yscaleinv * mipscale;
    d_sdivzstepv = -p_saxis.y * t;
    d_tdivzstepv = -p_taxis.y * t;

    d_sdivzorigin = p_saxis.z * mipscale - xcenter * d_sdivzstepu - ycenter * d_sdivzstepv;
    d_tdivzorigin = p_taxis.z * mipscale - xcenter * d_tdivzstepu - ycenter * d_tdivzstepv;

    p_temp1 = transformed_modelorg * mipscale;

    t = 0x10000 * mipscale;
    sadjust = static_cast<fixed16_t>(p_temp1.dot(p_saxis) * 0x10000 + 0.5f - ((pface->texturemins[0] << 16) >> miplevel) + pface->texinfo->vecs[0][3] * t);
    tadjust = static_cast<fixed16_t>(p_temp1.dot(p_taxis) * 0x10000 + 0.5f - ((pface->texturemins[1] << 16) >> miplevel) + pface->texinfo->vecs[1][3] * t);

    bbextents = ((pface->extents[0] << 16) >> miplevel) - 1;
    bbextentt = ((pface->extents[1] << 16) >> miplevel) - 1;
}

void D_DrawSurfaces()
{
    surf_t* s;
    msurface_t* pface;
    surfcache_t* pcurrentcache;
    Vector3 world_transformed_modelorg;
    Vector3 local_modelorg;

    currententity = &cl_entities[0];
    TransformVector(modelorg, transformed_modelorg);
    world_transformed_modelorg = transformed_modelorg;

    if (r_drawflat.value) {
        for (s = &surfaces[1]; s < surface_p; s++) {
            if (!s->spans) {
                continue;
            }

            d_zistepu = s->d_zistepu;
            d_zistepv = s->d_zistepv;
            d_ziorigin = s->d_ziorigin;

            D_DrawSolidSurface(s, static_cast<int>(reinterpret_cast<uintptr_t>(s->data) & 0xFF));
            D_DrawZSpans(s->spans);
        }
    } else {
        for (s = &surfaces[1]; s < surface_p; s++) {
            if (!s->spans) {
                continue;
            }

            r_drawnpolycount++;

            d_zistepu = s->d_zistepu;
            d_zistepv = s->d_zistepv;
            d_ziorigin = s->d_ziorigin;

            if (s->flags & SURF_DRAWSKY) {
                if (!r_skymade) {
                    R_MakeSky();
                }

                D_DrawSkyScans8(s->spans);
                D_DrawZSpans(s->spans);
            } else if (s->flags & SURF_DRAWBACKGROUND) {
                d_zistepu = 0;
                d_zistepv = 0;
                d_ziorigin = -0.9f;

                D_DrawSolidSurface(s, static_cast<int>(r_clearcolor.value) & 0xFF);
                D_DrawZSpans(s->spans);
            } else if (s->flags & SURF_DRAWTURB) {
                pface = reinterpret_cast<msurface_t*>(s->data);
                miplevel = 0;
                cacheblock = reinterpret_cast<pixel_t*>(reinterpret_cast<byte*>(pface->texinfo->texture) + pface->texinfo->texture->offsets[0]);
                cachewidth = 64;

                if (s->insubmodel) {
                    currententity = s->entity;
                    local_modelorg = r_origin - currententity->origin;
                    TransformVector(local_modelorg, transformed_modelorg);

                    R_RotateBmodel();
                }

                D_CalcGradients(pface);
                Turbulent8(s->spans);
                D_DrawZSpans(s->spans);

                if (s->insubmodel) {
                    currententity = &cl_entities[0];
                    transformed_modelorg = world_transformed_modelorg;
                    vpn = base_vpn;
                    vup = base_vup;
                    vright = base_vright;
                    modelorg = base_modelorg;
                    R_TransformFrustum();
                }
            } else {
                if (s->insubmodel) {
                    currententity = s->entity;
                    local_modelorg = r_origin - currententity->origin;
                    TransformVector(local_modelorg, transformed_modelorg);

                    R_RotateBmodel();
                }

                pface = reinterpret_cast<msurface_t*>(s->data);
                miplevel = D_MipLevelForScale(s->nearzi * scale_for_mip * pface->texinfo->mipadjust);

                pcurrentcache = D_CacheSurface(pface, miplevel);

                cacheblock = reinterpret_cast<pixel_t*>(pcurrentcache->data);
                cachewidth = pcurrentcache->width;

                D_CalcGradients(pface);

                (*d_drawspans)(s->spans);

                D_DrawZSpans(s->spans);

                if (s->insubmodel) {
                    currententity = &cl_entities[0];
                    transformed_modelorg = world_transformed_modelorg;
                    vpn = base_vpn;
                    vup = base_vup;
                    vright = base_vright;
                    modelorg = base_modelorg;
                    R_TransformFrustum();
                }
            }
        }
    }
}

// ==============================================================
// d_scan.cpp -- portable C scan-level rasterization code
// ==============================================================

void D_DrawTurbulent8Span()
{
    int sturb, tturb;

    do {
        sturb = ((r_turb_s + r_turb_turb[(r_turb_t >> 16) & (CYCLE - 1)]) >> 16) & 63;
        tturb = ((r_turb_t + r_turb_turb[(r_turb_s >> 16) & (CYCLE - 1)]) >> 16) & 63;
        *r_turb_pdest++ = *(r_turb_pbase + (tturb << 6) + sturb);
        r_turb_s += r_turb_sstep;
        r_turb_t += r_turb_tstep;
    } while (--r_turb_spancount > 0);
}

void D_WarpScreen()
{
    int w, h;
    int u, v;
    byte* dest;
    int* turb;
    int* col;
    byte** row;
    eastl::array<byte*, MAXHEIGHT + (AMP2 * 2)> rowptr{};
    eastl::array<int, MAXWIDTH + (AMP2 * 2)> column{};
    float wratio, hratio;
    const auto& scr_vrect = GetScreenSystem().GetVrect();

    w = r_refdef.vrect.width;
    h = r_refdef.vrect.height;

    wratio = static_cast<float>(w) / static_cast<float>(scr_vrect.width);
    hratio = static_cast<float>(h) / static_cast<float>(scr_vrect.height);

    for (v = 0; v < scr_vrect.height + AMP2 * 2; v++) {
        rowptr[v] = reinterpret_cast<byte*>(d_viewbuffer) + (r_refdef.vrect.y * screenwidth) + (screenwidth * static_cast<int>(static_cast<float>(v) * hratio * static_cast<float>(h) / static_cast<float>(h + AMP2 * 2)));
    }

    for (u = 0; u < scr_vrect.width + AMP2 * 2; u++) {
        column[u] = r_refdef.vrect.x + static_cast<int>(static_cast<float>(u) * wratio * static_cast<float>(w) / static_cast<float>(w + AMP2 * 2));
    }

    turb = intsintable.data() + (static_cast<int>(cl.time * SPEED) & (CYCLE - 1));
    dest = reinterpret_cast<byte*>(vid.buffer) + scr_vrect.y * vid.rowbytes + scr_vrect.x;

    for (v = 0; v < scr_vrect.height; v++, dest += vid.rowbytes) {
        col = &column[turb[v]];
        row = &rowptr[v];

        for (u = 0; u < scr_vrect.width; u += 4) {
            dest[u + 0] = row[turb[u + 0]][col[u + 0]];
            dest[u + 1] = row[turb[u + 1]][col[u + 1]];
            dest[u + 2] = row[turb[u + 2]][col[u + 2]];
            dest[u + 3] = row[turb[u + 3]][col[u + 3]];
        }
    }
}

void Turbulent8(espan_t* pspan)
{
    int count;
    fixed16_t snext, tnext;
    float sdivz, tdivz, zi, z, du, dv, spancountminus1;
    float sdivz16stepu, tdivz16stepu, zi16stepu;

    r_turb_turb = sintable.data() + (static_cast<int>(cl.time * SPEED) & (CYCLE - 1));

    r_turb_sstep = 0;
    r_turb_tstep = 0;

    r_turb_pbase = reinterpret_cast<unsigned char*>(cacheblock);

    sdivz16stepu = d_sdivzstepu * 16;
    tdivz16stepu = d_tdivzstepu * 16;
    zi16stepu = d_zistepu * 16;

    do {
        r_turb_pdest = reinterpret_cast<unsigned char*>(reinterpret_cast<byte*>(d_viewbuffer) + (screenwidth * pspan->v) + pspan->u);

        count = pspan->count;

        du = static_cast<float>(pspan->u);
        dv = static_cast<float>(pspan->v);

        sdivz = d_sdivzorigin + dv * d_sdivzstepv + du * d_sdivzstepu;
        tdivz = d_tdivzorigin + dv * d_tdivzstepv + du * d_tdivzstepu;
        zi = d_ziorigin + dv * d_zistepv + du * d_zistepu;
        z = 0x10000 / zi;

        r_turb_s = static_cast<int>(sdivz * z) + sadjust;
        if (r_turb_s > bbextents) {
            r_turb_s = bbextents;
        } else if (r_turb_s < 0) {
            r_turb_s = 0;
        }

        r_turb_t = static_cast<int>(tdivz * z) + tadjust;
        if (r_turb_t > bbextentt) {
            r_turb_t = bbextentt;
        } else if (r_turb_t < 0) {
            r_turb_t = 0;
        }

        do {
            if (count >= 16) {
                r_turb_spancount = 16;
            } else {
                r_turb_spancount = count;
            }

            count -= r_turb_spancount;

            if (count) {
                sdivz += sdivz16stepu;
                tdivz += tdivz16stepu;
                zi += zi16stepu;
                z = 0x10000 / zi;

                snext = static_cast<int>(sdivz * z) + sadjust;
                if (snext > bbextents) {
                    snext = bbextents;
                } else if (snext < 16) {
                    snext = 16;
                }

                tnext = static_cast<int>(tdivz * z) + tadjust;
                if (tnext > bbextentt) {
                    tnext = bbextentt;
                } else if (tnext < 16) {
                    tnext = 16;
                }

                r_turb_sstep = (snext - r_turb_s) >> 4;
                r_turb_tstep = (tnext - r_turb_t) >> 4;
            } else {
                spancountminus1 = static_cast<float>(r_turb_spancount - 1);
                sdivz += d_sdivzstepu * spancountminus1;
                tdivz += d_tdivzstepu * spancountminus1;
                zi += d_zistepu * spancountminus1;
                z = 0x10000 / zi;
                snext = static_cast<int>(sdivz * z) + sadjust;
                if (snext > bbextents) {
                    snext = bbextents;
                } else if (snext < 16) {
                    snext = 16;
                }

                tnext = static_cast<int>(tdivz * z) + tadjust;
                if (tnext > bbextentt) {
                    tnext = bbextentt;
                } else if (tnext < 16) {
                    tnext = 16;
                }

                if (r_turb_spancount > 1) {
                    r_turb_sstep = (snext - r_turb_s) / (r_turb_spancount - 1);
                    r_turb_tstep = (tnext - r_turb_t) / (r_turb_spancount - 1);
                }
            }

            r_turb_s = r_turb_s & ((CYCLE << 16) - 1);
            r_turb_t = r_turb_t & ((CYCLE << 16) - 1);

            D_DrawTurbulent8Span();

            r_turb_s = snext;
            r_turb_t = tnext;

        } while (count > 0);

    } while ((pspan = pspan->pnext) != nullptr);
}

void D_DrawSpans8(espan_t* pspan)
{
    int count, spancount;
    unsigned char *pbase, *pdest;
    fixed16_t s, t, snext, tnext, sstep, tstep;
    float sdivz, tdivz, zi, z, du, dv, spancountminus1;
    float sdivz8stepu, tdivz8stepu, zi8stepu;

    sstep = 0;
    tstep = 0;

    pbase = reinterpret_cast<unsigned char*>(cacheblock);

    sdivz8stepu = d_sdivzstepu * 8;
    tdivz8stepu = d_tdivzstepu * 8;
    zi8stepu = d_zistepu * 8;

    do {
        pdest = reinterpret_cast<unsigned char*>(reinterpret_cast<byte*>(d_viewbuffer) + (screenwidth * pspan->v) + pspan->u);

        count = pspan->count;

        du = static_cast<float>(pspan->u);
        dv = static_cast<float>(pspan->v);

        sdivz = d_sdivzorigin + dv * d_sdivzstepv + du * d_sdivzstepu;
        tdivz = d_tdivzorigin + dv * d_tdivzstepv + du * d_tdivzstepu;
        zi = d_ziorigin + dv * d_zistepv + du * d_zistepu;
        z = 0x10000 / zi;

        s = static_cast<int>(sdivz * z) + sadjust;
        if (s > bbextents) {
            s = bbextents;
        } else if (s < 0) {
            s = 0;
        }

        t = static_cast<int>(tdivz * z) + tadjust;
        if (t > bbextentt) {
            t = bbextentt;
        } else if (t < 0) {
            t = 0;
        }

        do {
            if (count >= 8) {
                spancount = 8;
            } else {
                spancount = count;
            }

            count -= spancount;

            if (count) {
                sdivz += sdivz8stepu;
                tdivz += tdivz8stepu;
                zi += zi8stepu;
                z = 0x10000 / zi;

                snext = static_cast<int>(sdivz * z) + sadjust;
                if (snext > bbextents) {
                    snext = bbextents;
                } else if (snext < 8) {
                    snext = 8;
                }

                tnext = static_cast<int>(tdivz * z) + tadjust;
                if (tnext > bbextentt) {
                    tnext = bbextentt;
                } else if (tnext < 8) {
                    tnext = 8;
                }

                sstep = (snext - s) >> 3;
                tstep = (tnext - t) >> 3;
            } else {
                spancountminus1 = static_cast<float>(spancount - 1);
                sdivz += d_sdivzstepu * spancountminus1;
                tdivz += d_tdivzstepu * spancountminus1;
                zi += d_zistepu * spancountminus1;
                z = 0x10000 / zi;
                snext = static_cast<int>(sdivz * z) + sadjust;
                if (snext > bbextents) {
                    snext = bbextents;
                } else if (snext < 8) {
                    snext = 8;
                }

                tnext = static_cast<int>(tdivz * z) + tadjust;
                if (tnext > bbextentt) {
                    tnext = bbextentt;
                } else if (tnext < 8) {
                    tnext = 8;
                }

                if (spancount > 1) {
                    sstep = (snext - s) / (spancount - 1);
                    tstep = (tnext - t) / (spancount - 1);
                }
            }

            do {
                *pdest++ = *(pbase + (s >> 16) + (t >> 16) * cachewidth);
                s += sstep;
                t += tstep;
            } while (--spancount > 0);

            s = snext;
            t = tnext;

        } while (count > 0);

    } while ((pspan = pspan->pnext) != nullptr);
}

void D_DrawZSpans(espan_t* pspan)
{
    int count, doublecount, izistep;
    int izi;
    short* pdest;
    unsigned ltemp;
    double zi;
    float du, dv;

    izistep = static_cast<int>(d_zistepu * 0x8000 * 0x10000);

    do {
        pdest = d_pzbuffer + (d_zwidth * pspan->v) + pspan->u;

        count = pspan->count;

        du = static_cast<float>(pspan->u);
        dv = static_cast<float>(pspan->v);

        zi = d_ziorigin + dv * d_zistepv + du * d_zistepu;
        izi = static_cast<int>(zi * 0x8000 * 0x10000);

        if (reinterpret_cast<uintptr_t>(pdest) & 0x02) {
            *pdest++ = static_cast<short>(izi >> 16);
            izi += izistep;
            count--;
        }

        if ((doublecount = count >> 1) > 0) {
            do {
                ltemp = izi >> 16;
                izi += izistep;
                ltemp |= izi & 0xFFFF0000;
                izi += izistep;
                *reinterpret_cast<int*>(pdest) = ltemp;
                pdest += 2;
            } while (--doublecount > 0);
        }

        if (count & 1) {
            *pdest = static_cast<short>(izi >> 16);
        }

    } while ((pspan = pspan->pnext) != nullptr);
}

// ==============================================================
// d_sky.cpp -- software sky texture coordinate calculation
// ==============================================================

constexpr int SKY_SPAN_SHIFT = 5;
constexpr int SKY_SPAN_MAX = 1 << SKY_SPAN_SHIFT;

void D_Sky_uv_To_st(int u, int v, fixed16_t* s, fixed16_t* t)
{
    float wu, wv, temp;
    Vector3 end;

    if (r_refdef.vrect.width >= r_refdef.vrect.height) {
        temp = static_cast<float>(r_refdef.vrect.width);
    } else {
        temp = static_cast<float>(r_refdef.vrect.height);
    }

    wu = 8192.0f * static_cast<float>(u - (static_cast<int>(vid.width) >> 1)) / temp;
    wv = 8192.0f * static_cast<float>((static_cast<int>(vid.height) >> 1) - v) / temp;

    end = vpn * 4096.0f + vright * wu + vup * wv;
    end.z *= 3.0f;
    end.normalize();

    temp = skytime * skyspeed;
    *s = static_cast<int>((temp + 6.0f * (static_cast<float>(SKYSIZE) / 2.0f - 1.0f) * end.x) * 65536.0f);
    *t = static_cast<int>((temp + 6.0f * (static_cast<float>(SKYSIZE) / 2.0f - 1.0f) * end.y) * 65536.0f);
}

void D_DrawSkyScans8(espan_t* pspan)
{
    int count, spancount, u, v;
    unsigned char* pdest;
    fixed16_t s, t, snext = 0, tnext = 0, sstep, tstep;
    float spancountminus1;

    sstep = 0;
    tstep = 0;

    do {
        pdest = reinterpret_cast<unsigned char*>(reinterpret_cast<byte*>(d_viewbuffer) + (screenwidth * pspan->v) + pspan->u);

        count = pspan->count;

        u = pspan->u;
        v = pspan->v;
        D_Sky_uv_To_st(u, v, &s, &t);

        do {
            if (count >= SKY_SPAN_MAX) {
                spancount = SKY_SPAN_MAX;
            } else {
                spancount = count;
            }

            count -= spancount;

            if (count) {
                u += spancount;

                D_Sky_uv_To_st(u, v, &snext, &tnext);

                sstep = (snext - s) >> SKY_SPAN_SHIFT;
                tstep = (tnext - t) >> SKY_SPAN_SHIFT;
            } else {
                spancountminus1 = static_cast<float>(spancount - 1);

                if (spancountminus1 > 0) {
                    u += static_cast<int>(spancountminus1);
                    D_Sky_uv_To_st(u, v, &snext, &tnext);

                    sstep = static_cast<fixed16_t>((snext - s) / spancountminus1);
                    tstep = static_cast<fixed16_t>((tnext - t) / spancountminus1);
                }
            }

            do {
                *pdest++ = r_skysource[((t & R_SKY_TMASK) >> 8) + ((s & R_SKY_SMASK) >> 16)];
                s += sstep;
                t += tstep;
            } while (--spancount > 0);

            s = snext;
            t = tnext;

        } while (count > 0);

    } while ((pspan = pspan->pnext) != nullptr);
}

// ==============================================================
// d_surf.cpp -- rasterization driver surface heap manager
// ==============================================================

constexpr int GUARDSIZE = 4;

int D_SurfaceCacheForRes(int width, int height)
{
    int size, pix;

    if (COM_CheckParm("-surfcachesize")) {
        size = Q_atoi(com_argv[COM_CheckParm("-surfcachesize") + 1]) * 1024;

        return size;
    }

    size = SURFCACHE_SIZE_AT_320X200;

    pix = width * height;
    if (pix > 64000) {
        size += (pix - 64000) * 3;
    }

    return size;
}

void D_CheckCacheGuard()
{
    byte* s;
    int i;

    s = reinterpret_cast<byte*>(sc_base) + sc_size;
    for (i = 0; i < GUARDSIZE; i++) {
        if (s[i] != static_cast<byte>(i)) {
            Sys_Error("D_CheckCacheGuard: failed");
        }
    }
}

void D_ClearCacheGuard()
{
    byte* s;
    int i;

    s = reinterpret_cast<byte*>(sc_base) + sc_size;
    for (i = 0; i < GUARDSIZE; i++) {
        s[i] = static_cast<byte>(i);
    }
}

void D_InitCaches(void* buffer, int size)
{
    if (!msg_suppress_1) {
        Con_Printf("%ik surface cache\n", size / 1024);
    }

    sc_size = size - GUARDSIZE;
    sc_base = reinterpret_cast<surfcache_t*>(buffer);
    sc_rover = sc_base;

    sc_base->next = nullptr;
    sc_base->owner = nullptr;
    sc_base->size = sc_size;

    D_ClearCacheGuard();
}

void D_FlushCaches()
{
    surfcache_t* c;

    if (!sc_base) {
        return;
    }

    for (c = sc_base; c; c = c->next) {
        if (c->owner) {
            *c->owner = nullptr;
        }
    }

    sc_rover = sc_base;
    sc_base->next = nullptr;
    sc_base->owner = nullptr;
    sc_base->size = sc_size;
}

surfcache_t* D_SCAlloc(int width, int size)
{
    surfcache_t* new_surf;
    qboolean wrapped_this_time;

    if ((width < 0) || (width > 256)) {
        Sys_Error("D_SCAlloc: bad cache width %d\n", width);
    }

    if ((size <= 0) || (size > 0x10000)) {
        Sys_Error("D_SCAlloc: bad cache size %d\n", size);
    }

    size = static_cast<int>(offsetof(surfcache_t, data) + size);
    size = (size + 3) & ~3;
    if (size > sc_size) {
        Sys_Error("D_SCAlloc: %i > cache size", size);
    }

    wrapped_this_time = false;

    if (!sc_rover || reinterpret_cast<byte*>(sc_rover) - reinterpret_cast<byte*>(sc_base) > sc_size - size) {
        if (sc_rover) {
            wrapped_this_time = true;
        }

        sc_rover = sc_base;
    }

    new_surf = sc_rover;
    if (sc_rover->owner) {
        *sc_rover->owner = nullptr;
    }

    while (new_surf->size < size) {
        sc_rover = sc_rover->next;
        if (!sc_rover) {
            Sys_Error("D_SCAlloc: hit the end of memory");
        }

        if (sc_rover->owner) {
            *sc_rover->owner = nullptr;
        }

        new_surf->size += sc_rover->size;
        new_surf->next = sc_rover->next;
    }

    if (new_surf->size - size > 256) {
        sc_rover = reinterpret_cast<surfcache_t*>(reinterpret_cast<byte*>(new_surf) + size);
        sc_rover->size = new_surf->size - size;
        sc_rover->next = new_surf->next;
        sc_rover->width = 0;
        sc_rover->owner = nullptr;
        new_surf->next = sc_rover;
        new_surf->size = size;
    } else {
        sc_rover = new_surf->next;
    }

    new_surf->width = width;
    if (width > 0) {
        new_surf->height = (size - sizeof(*new_surf) + sizeof(new_surf->data)) / width;
    }

    new_surf->owner = nullptr;

    if (d_roverwrapped) {
        if (wrapped_this_time || (sc_rover >= d_initial_rover)) {
            r_cache_thrash = true;
        }
    } else if (wrapped_this_time) {
        d_roverwrapped = true;
    }

    D_CheckCacheGuard();

    return new_surf;
}

surfcache_t* D_CacheSurface(msurface_t* surface, int mip_level)
{
    surfcache_t* cache;

    r_drawsurf.texture = R_TextureAnimation(surface->texinfo->texture);
    r_drawsurf.lightadj[0] = d_lightstylevalue[surface->styles[0]];
    r_drawsurf.lightadj[1] = d_lightstylevalue[surface->styles[1]];
    r_drawsurf.lightadj[2] = d_lightstylevalue[surface->styles[2]];
    r_drawsurf.lightadj[3] = d_lightstylevalue[surface->styles[3]];

    cache = surface->cachespots[mip_level];

    if (cache && !cache->dlight && surface->dlightframe != r_framecount && cache->texture == r_drawsurf.texture && cache->lightadj[0] == r_drawsurf.lightadj[0] && cache->lightadj[1] == r_drawsurf.lightadj[1] && cache->lightadj[2] == r_drawsurf.lightadj[2] && cache->lightadj[3] == r_drawsurf.lightadj[3]) {
        return cache;
    }

    surfscale = 1.0f / (1 << mip_level);
    r_drawsurf.surfmip = mip_level;
    r_drawsurf.surfwidth = surface->extents[0] >> mip_level;
    r_drawsurf.rowbytes = r_drawsurf.surfwidth;
    r_drawsurf.surfheight = surface->extents[1] >> mip_level;

    if (!cache) {
        cache = D_SCAlloc(r_drawsurf.surfwidth,
            r_drawsurf.surfwidth * r_drawsurf.surfheight);
        surface->cachespots[mip_level] = cache;
        cache->owner = &surface->cachespots[mip_level];
        cache->mipscale = surfscale;
    }

    if (surface->dlightframe == r_framecount) {
        cache->dlight = 1;
    } else {
        cache->dlight = 0;
    }

    r_drawsurf.surfdat = reinterpret_cast<pixel_t*>(cache->data);

    cache->texture = r_drawsurf.texture;
    cache->lightadj[0] = r_drawsurf.lightadj[0];
    cache->lightadj[1] = r_drawsurf.lightadj[1];
    cache->lightadj[2] = r_drawsurf.lightadj[2];
    cache->lightadj[3] = r_drawsurf.lightadj[3];

    r_drawsurf.surf = surface;

    c_surf++;
    R_DrawSurface();

    return surface->cachespots[mip_level];
}

// ==============================================================
// d_sprite.cpp -- software sprite rasterization
// ==============================================================

void D_SpriteDrawSpans(sspan_t* pspan)
{
    int count, spancount, izistep;
    int izi;
    byte *pbase, *pdest;
    fixed16_t s, t, snext, tnext, sstep, tstep;
    float sdivz, tdivz, zi, z, du, dv, spancountminus1;
    float sdivz8stepu, tdivz8stepu, zi8stepu;
    byte btemp;
    short* pz;

    sstep = 0;
    tstep = 0;

    pbase = cacheblock;

    sdivz8stepu = d_sdivzstepu * 8;
    tdivz8stepu = d_tdivzstepu * 8;
    zi8stepu = d_zistepu * 8;

    izistep = static_cast<int>(d_zistepu * 0x8000 * 0x10000);

    do {
        pdest = reinterpret_cast<byte*>(d_viewbuffer) + (screenwidth * pspan->v) + pspan->u;
        pz = d_pzbuffer + (d_zwidth * pspan->v) + pspan->u;

        count = pspan->count;

        if (count <= 0) {
            goto NextSpan;
        }

        du = static_cast<float>(pspan->u);
        dv = static_cast<float>(pspan->v);

        sdivz = d_sdivzorigin + dv * d_sdivzstepv + du * d_sdivzstepu;
        tdivz = d_tdivzorigin + dv * d_tdivzstepv + du * d_tdivzstepu;
        zi = d_ziorigin + dv * d_zistepv + du * d_zistepu;
        z = 0x10000 / zi;
        izi = static_cast<int>(zi * 0x8000 * 0x10000);

        s = static_cast<int>(sdivz * z) + sadjust;
        if (s > bbextents) {
            s = bbextents;
        } else if (s < 0) {
            s = 0;
        }

        t = static_cast<int>(tdivz * z) + tadjust;
        if (t > bbextentt) {
            t = bbextentt;
        } else if (t < 0) {
            t = 0;
        }

        do {
            if (count >= 8) {
                spancount = 8;
            } else {
                spancount = count;
            }

            count -= spancount;

            if (count) {
                sdivz += sdivz8stepu;
                tdivz += tdivz8stepu;
                zi += zi8stepu;
                z = 0x10000 / zi;

                snext = static_cast<int>(sdivz * z) + sadjust;
                if (snext > bbextents) {
                    snext = bbextents;
                } else if (snext < 8) {
                    snext = 8;
                }

                tnext = static_cast<int>(tdivz * z) + tadjust;
                if (tnext > bbextentt) {
                    tnext = bbextentt;
                } else if (tnext < 8) {
                    tnext = 8;
                }

                sstep = (snext - s) >> 3;
                tstep = (tnext - t) >> 3;
            } else {
                spancountminus1 = static_cast<float>(spancount - 1);
                sdivz += d_sdivzstepu * spancountminus1;
                tdivz += d_tdivzstepu * spancountminus1;
                zi += d_zistepu * spancountminus1;
                z = 0x10000 / zi;
                snext = static_cast<int>(sdivz * z) + sadjust;
                if (snext > bbextents) {
                    snext = bbextents;
                } else if (snext < 8) {
                    snext = 8;
                }

                tnext = static_cast<int>(tdivz * z) + tadjust;
                if (tnext > bbextentt) {
                    tnext = bbextentt;
                } else if (tnext < 8) {
                    tnext = 8;
                }

                if (spancount > 1) {
                    sstep = (snext - s) / (spancount - 1);
                    tstep = (tnext - t) / (spancount - 1);
                }
            }

            do {
                btemp = *(pbase + (s >> 16) + (t >> 16) * cachewidth);
                if (btemp != 255) {
                    if (*pz <= static_cast<short>(izi >> 16)) {
                        *pz = static_cast<short>(izi >> 16);
                        *pdest = btemp;
                    }
                }

                izi += izistep;
                pdest++;
                pz++;
                s += sstep;
                t += tstep;
            } while (--spancount > 0);

            s = snext;
            t = tnext;

        } while (count > 0);

    NextSpan:
        pspan++;

    } while (pspan->count != DS_SPAN_LIST_END);
}

void D_SpriteScanLeftEdge()
{
    int i, v, itop, ibottom, lmaxindex;
    emitpoint_t *pvert, *pnext;
    sspan_t* pspan;
    float du, dv, vtop, vbottom, slope;
    fixed16_t u, u_step;

    pspan = sprite_spans;
    i = minindex;
    if (i == 0) {
        i = r_spritedesc.nump;
    }

    lmaxindex = maxindex;
    if (lmaxindex == 0) {
        lmaxindex = r_spritedesc.nump;
    }

    vtop = ceil(r_spritedesc.pverts[i].v);

    do {
        pvert = &r_spritedesc.pverts[i];
        pnext = pvert - 1;

        vbottom = ceil(pnext->v);

        if (vtop < vbottom) {
            du = pnext->u - pvert->u;
            dv = pnext->v - pvert->v;
            slope = du / dv;
            u_step = static_cast<fixed16_t>(slope * 65536.0f);
            u = static_cast<fixed16_t>((pvert->u + (slope * (vtop - pvert->v))) * 65536.0f) + (65536 - 1);
            itop = static_cast<int>(vtop);
            ibottom = static_cast<int>(vbottom);

            for (v = itop; v < ibottom; v++) {
                pspan->u = u >> 16;
                pspan->v = v;
                u += u_step;
                pspan++;
            }
        }

        vtop = vbottom;

        i--;
        if (i == 0) {
            i = r_spritedesc.nump;
        }

    } while (i != lmaxindex);
}

void D_SpriteScanRightEdge()
{
    int i, v, itop, ibottom;
    emitpoint_t *pvert, *pnext;
    sspan_t* pspan;
    float du, dv, vtop, vbottom, slope, uvert, unext, vvert, vnext;
    fixed16_t u, u_step;

    pspan = sprite_spans;
    i = minindex;

    vvert = r_spritedesc.pverts[i].v;
    if (vvert < r_refdef.fvrecty_adj) {
        vvert = r_refdef.fvrecty_adj;
    }

    if (vvert > r_refdef.fvrectbottom_adj) {
        vvert = r_refdef.fvrectbottom_adj;
    }

    vtop = ceil(vvert);

    do {
        pvert = &r_spritedesc.pverts[i];
        pnext = pvert + 1;

        vnext = pnext->v;
        if (vnext < r_refdef.fvrecty_adj) {
            vnext = r_refdef.fvrecty_adj;
        }

        if (vnext > r_refdef.fvrectbottom_adj) {
            vnext = r_refdef.fvrectbottom_adj;
        }

        vbottom = ceil(vnext);

        if (vtop < vbottom) {
            uvert = pvert->u;
            if (uvert < r_refdef.fvrectx_adj) {
                uvert = r_refdef.fvrectx_adj;
            }

            if (uvert > r_refdef.fvrectright_adj) {
                uvert = r_refdef.fvrectright_adj;
            }

            unext = pnext->u;
            if (unext < r_refdef.fvrectx_adj) {
                unext = r_refdef.fvrectx_adj;
            }

            if (unext > r_refdef.fvrectright_adj) {
                unext = r_refdef.fvrectright_adj;
            }

            du = unext - uvert;
            dv = vnext - vvert;
            slope = du / dv;
            u_step = static_cast<fixed16_t>(slope * 65536.0f);
            u = static_cast<fixed16_t>((uvert + (slope * (vtop - vvert))) * 65536.0f) + (65536 - 1);
            itop = static_cast<int>(vtop);
            ibottom = static_cast<int>(vbottom);

            for (v = itop; v < ibottom; v++) {
                pspan->count = (u >> 16) - pspan->u;
                u += u_step;
                pspan++;
            }
        }

        vtop = vbottom;
        vvert = vnext;

        i++;
        if (i == r_spritedesc.nump) {
            i = 0;
        }

    } while (i != maxindex);

    pspan->count = DS_SPAN_LIST_END;
}

void D_SpriteCalculateGradients()
{
    Vector3 p_normal, p_saxis, p_taxis, p_temp1;
    float distinv;

    TransformVector(r_spritedesc.vpn, p_normal);
    TransformVector(r_spritedesc.vright, p_saxis);
    TransformVector(r_spritedesc.vup, p_taxis);
    p_taxis = -p_taxis;

    distinv = 1.0f / (-modelorg.dot(r_spritedesc.vpn));

    d_sdivzstepu = p_saxis.x * xscaleinv;
    d_tdivzstepu = p_taxis.x * xscaleinv;

    d_sdivzstepv = -p_saxis.y * yscaleinv;
    d_tdivzstepv = -p_taxis.y * yscaleinv;

    d_zistepu = p_normal.x * xscaleinv * distinv;
    d_zistepv = -p_normal.y * yscaleinv * distinv;

    d_sdivzorigin = p_saxis.z - xcenter * d_sdivzstepu - ycenter * d_sdivzstepv;
    d_tdivzorigin = p_taxis.z - xcenter * d_tdivzstepu - ycenter * d_tdivzstepv;
    d_ziorigin = p_normal.z * distinv - xcenter * d_zistepu - ycenter * d_zistepv;

    TransformVector(modelorg, p_temp1);

    sadjust = static_cast<fixed16_t>(p_temp1.dot(p_saxis) * 65536.0f + 0.5f) - (-(cachewidth >> 1) << 16);
    tadjust = static_cast<fixed16_t>(p_temp1.dot(p_taxis) * 65536.0f + 0.5f) - (-(sprite_height >> 1) << 16);

    bbextents = (cachewidth << 16) - 1;
    bbextentt = (sprite_height << 16) - 1;
}

void D_DrawSprite()
{
    int i, nump;
    float ymin, ymax;
    emitpoint_t* pverts;
    eastl::array<sspan_t, MAXHEIGHT + 1> spans{};

    sprite_spans = spans.data();

    ymin = 999999.9f;
    ymax = -999999.9f;
    pverts = r_spritedesc.pverts;

    for (i = 0; i < r_spritedesc.nump; i++) {
        if (pverts->v < ymin) {
            ymin = pverts->v;
            minindex = i;
        }

        if (pverts->v > ymax) {
            ymax = pverts->v;
            maxindex = i;
        }

        pverts++;
    }

    ymin = ceil(ymin);
    ymax = ceil(ymax);

    if (ymin >= ymax) {
        return;
    }

    cachewidth = r_spritedesc.pspriteframe->width;
    sprite_height = r_spritedesc.pspriteframe->height;
    cacheblock = reinterpret_cast<byte*>(&r_spritedesc.pspriteframe->pixels[0]);

    nump = r_spritedesc.nump;
    pverts = r_spritedesc.pverts;
    pverts[nump] = pverts[0];

    D_SpriteCalculateGradients();
    D_SpriteScanLeftEdge();
    D_SpriteScanRightEdge();
    D_SpriteDrawSpans(sprite_spans);
}

// ==============================================================
// d_polyse.cpp -- routines for drawing sets of polygons sharing the same
//                 texture (used for Alias models)
// ==============================================================

constexpr int DPS_MAXSPANS = MAXHEIGHT + 1;

void D_PolysetDraw()
{
    alignas(CACHE_SIZE) spanpackage_t spans[DPS_MAXSPANS + 1];

    a_spans = spans;

    if (r_affinetridesc.drawtype) {
        D_DrawSubdiv();
    } else {
        D_DrawNonSubdiv();
    }
}

void D_PolysetDrawFinalVerts(finalvert_t* fv, int num_verts)
{
    int i, z;
    short* zbuf;

    for (i = 0; i < num_verts; i++, fv++) {
        if ((fv->v[0] < r_refdef.vrectright) && (fv->v[1] < r_refdef.vrectbottom)) {
            z = fv->v[5] >> 16;
            zbuf = zspantable[fv->v[1]] + fv->v[0];
            if (z >= *zbuf) {
                int pix;

                *zbuf = static_cast<short>(z);
                pix = skintable[fv->v[3] >> 16][fv->v[2] >> 16];
                pix = reinterpret_cast<byte*>(acolormap)[pix + (fv->v[4] & 0xFF00)];
                d_viewbuffer[d_scantable[fv->v[1]] + fv->v[0]] = static_cast<pixel_t>(pix);
            }
        }
    }
}

void D_DrawSubdiv()
{
    mtriangle_t* ptri;
    finalvert_t *pfv, *index0, *index1, *index2;
    int i;
    int lnumtriangles;

    pfv = r_affinetridesc.pfinalverts;
    ptri = r_affinetridesc.ptriangles;
    lnumtriangles = r_affinetridesc.numtriangles;

    for (i = 0; i < lnumtriangles; i++) {
        index0 = pfv + ptri[i].vertindex[0];
        index1 = pfv + ptri[i].vertindex[1];
        index2 = pfv + ptri[i].vertindex[2];

        if (((index0->v[1] - index1->v[1]) * (index0->v[0] - index2->v[0]) - (index0->v[0] - index1->v[0]) * (index0->v[1] - index2->v[1])) >= 0) {
            continue;
        }

        d_pcolormap = &reinterpret_cast<byte*>(acolormap)[index0->v[4] & 0xFF00];

        if (ptri[i].facesfront) {
            D_PolysetRecursiveTriangle(&index0->v, &index1->v, &index2->v);
        } else {
            int s0, s1, s2;

            s0 = index0->v[2];
            s1 = index1->v[2];
            s2 = index2->v[2];

            if (index0->flags & ALIAS_ONSEAM) {
                index0->v[2] += r_affinetridesc.seamfixupX16;
            }

            if (index1->flags & ALIAS_ONSEAM) {
                index1->v[2] += r_affinetridesc.seamfixupX16;
            }

            if (index2->flags & ALIAS_ONSEAM) {
                index2->v[2] += r_affinetridesc.seamfixupX16;
            }

            D_PolysetRecursiveTriangle(&index0->v, &index1->v, &index2->v);

            index0->v[2] = s0;
            index1->v[2] = s1;
            index2->v[2] = s2;
        }
    }
}

void D_DrawNonSubdiv()
{
    mtriangle_t* ptri;
    finalvert_t *pfv, *index0, *index1, *index2;
    int i;
    int lnumtriangles;

    pfv = r_affinetridesc.pfinalverts;
    ptri = r_affinetridesc.ptriangles;
    lnumtriangles = r_affinetridesc.numtriangles;

    for (i = 0; i < lnumtriangles; i++, ptri++) {
        index0 = pfv + ptri->vertindex[0];
        index1 = pfv + ptri->vertindex[1];
        index2 = pfv + ptri->vertindex[2];

        d_xdenom = (index0->v[1] - index1->v[1]) * (index0->v[0] - index2->v[0]) - (index0->v[0] - index1->v[0]) * (index0->v[1] - index2->v[1]);

        if (d_xdenom >= 0) {
            continue;
        }

        r_p0 = index0->v;
        r_p1 = index1->v;
        r_p2 = index2->v;

        if (!ptri->facesfront) {
            if (index0->flags & ALIAS_ONSEAM) {
                r_p0[2] += r_affinetridesc.seamfixupX16;
            }

            if (index1->flags & ALIAS_ONSEAM) {
                r_p1[2] += r_affinetridesc.seamfixupX16;
            }

            if (index2->flags & ALIAS_ONSEAM) {
                r_p2[2] += r_affinetridesc.seamfixupX16;
            }
        }

        D_PolysetSetEdgeTable();
        D_RasterizeAliasPolySmooth();
    }
}

void D_PolysetRecursiveTriangle(const eastl::array<int, 6>* lp1, const eastl::array<int, 6>* lp2, const eastl::array<int, 6>* lp3)
{
    const eastl::array<int, 6>* temp;
    int d;
    eastl::array<int, 6> new_poly{};
    int z;
    short* zbuf;

    d = (*lp2)[0] - (*lp1)[0];
    if (d < -1 || d > 1) {
        goto split;
    }

    d = (*lp2)[1] - (*lp1)[1];
    if (d < -1 || d > 1) {
        goto split;
    }

    d = (*lp3)[0] - (*lp2)[0];
    if (d < -1 || d > 1) {
        goto split2;
    }

    d = (*lp3)[1] - (*lp2)[1];
    if (d < -1 || d > 1) {
        goto split2;
    }

    d = (*lp1)[0] - (*lp3)[0];
    if (d < -1 || d > 1) {
        goto split3;
    }

    d = (*lp1)[1] - (*lp3)[1];
    if (d < -1 || d > 1) {
    split3:
        temp = lp1;
        lp1 = lp3;
        lp3 = lp2;
        lp2 = temp;

        goto split;
    }

    return;

split2:
    temp = lp1;
    lp1 = lp2;
    lp2 = lp3;
    lp3 = temp;

split:
    new_poly[0] = ((*lp1)[0] + (*lp2)[0]) >> 1;
    new_poly[1] = ((*lp1)[1] + (*lp2)[1]) >> 1;
    new_poly[2] = ((*lp1)[2] + (*lp2)[2]) >> 1;
    new_poly[3] = ((*lp1)[3] + (*lp2)[3]) >> 1;
    new_poly[5] = ((*lp1)[5] + (*lp2)[5]) >> 1;

    if ((*lp2)[1] > (*lp1)[1]) {
        goto nodraw;
    }

    if (((*lp2)[1] == (*lp1)[1]) && ((*lp2)[0] < (*lp1)[0])) {
        goto nodraw;
    }

    z = new_poly[5] >> 16;
    zbuf = zspantable[new_poly[1]] + new_poly[0];
    if (z >= *zbuf) {
        int pix;

        *zbuf = static_cast<short>(z);
        pix = d_pcolormap[skintable[new_poly[3] >> 16][new_poly[2] >> 16]];
        d_viewbuffer[d_scantable[new_poly[1]] + new_poly[0]] = static_cast<pixel_t>(pix);
    }

nodraw:
    D_PolysetRecursiveTriangle(lp3, lp1, &new_poly);
    D_PolysetRecursiveTriangle(lp3, &new_poly, lp2);
}

void D_PolysetUpdateTables()
{
    int i;
    byte* s;

    if (r_affinetridesc.skinwidth != skinwidth || r_affinetridesc.pskin != skinstart) {
        skinwidth = r_affinetridesc.skinwidth;
        skinstart = reinterpret_cast<byte*>(r_affinetridesc.pskin);
        s = skinstart;
        for (i = 0; i < MAX_LBM_HEIGHT; i++, s += skinwidth) {
            skintable[i] = s;
        }
    }
}

void D_PolysetScanLeftEdge(int height)
{
    do {
        d_pedgespanpackage->pdest = d_pdest;
        d_pedgespanpackage->pz = d_pz;
        d_pedgespanpackage->count = d_aspancount;
        d_pedgespanpackage->ptex = d_ptex;

        d_pedgespanpackage->sfrac = d_sfrac;
        d_pedgespanpackage->tfrac = d_tfrac;

        d_pedgespanpackage->light = d_light;
        d_pedgespanpackage->zi = d_zi;

        d_pedgespanpackage++;

        errorterm += erroradjustup;
        if (errorterm >= 0) {
            d_pdest += d_pdestextrastep;
            d_pz += d_pzextrastep;
            d_aspancount += d_countextrastep;
            d_ptex += d_ptexextrastep;
            d_sfrac += d_sfracextrastep;
            d_ptex += d_sfrac >> 16;

            d_sfrac &= 0xFFFF;
            d_tfrac += d_tfracextrastep;
            if (d_tfrac & 0x10000) {
                d_ptex += r_affinetridesc.skinwidth;
                d_tfrac &= 0xFFFF;
            }

            d_light += d_lightextrastep;
            d_zi += d_ziextrastep;
            errorterm -= erroradjustdown;
        } else {
            d_pdest += d_pdestbasestep;
            d_pz += d_pzbasestep;
            d_aspancount += ubasestep;
            d_ptex += d_ptexbasestep;
            d_sfrac += d_sfracbasestep;
            d_ptex += d_sfrac >> 16;
            d_sfrac &= 0xFFFF;
            d_tfrac += d_tfracbasestep;
            if (d_tfrac & 0x10000) {
                d_ptex += r_affinetridesc.skinwidth;
                d_tfrac &= 0xFFFF;
            }

            d_light += d_lightbasestep;
            d_zi += d_zibasestep;
        }
    } while (--height);
}

void D_PolysetSetUpForLineScan(fixed8_t startvertu,
    fixed8_t startvertv,
    fixed8_t endvertu,
    fixed8_t endvertv)
{
    double dm, dn;
    int tm, tn;

    errorterm = -1;

    tm = endvertu - startvertu;
    tn = endvertv - startvertv;

    dm = static_cast<double>(tm);
    dn = static_cast<double>(tn);

    std::tie(ubasestep, erroradjustup) = FloorDivMod(dm, dn);

    erroradjustdown = tn;
}

void D_PolysetCalcGradients(int s_width)
{
    float xstepdenominv, ystepdenominv, t0, t1;
    float p01_minus_p21, p11_minus_p21, p00_minus_p20, p10_minus_p20;

    p00_minus_p20 = static_cast<float>(r_p0[0] - r_p2[0]);
    p01_minus_p21 = static_cast<float>(r_p0[1] - r_p2[1]);
    p10_minus_p20 = static_cast<float>(r_p1[0] - r_p2[0]);
    p11_minus_p21 = static_cast<float>(r_p1[1] - r_p2[1]);

    xstepdenominv = 1.0f / static_cast<float>(d_xdenom);

    ystepdenominv = -xstepdenominv;

    t0 = static_cast<float>(r_p0[4] - r_p2[4]);
    t1 = static_cast<float>(r_p1[4] - r_p2[4]);
    r_lstepx = static_cast<int>(ceil((t1 * p01_minus_p21 - t0 * p11_minus_p21) * xstepdenominv));
    r_lstepy = static_cast<int>(ceil((t1 * p00_minus_p20 - t0 * p10_minus_p20) * ystepdenominv));

    t0 = static_cast<float>(r_p0[2] - r_p2[2]);
    t1 = static_cast<float>(r_p1[2] - r_p2[2]);
    r_sstepx = static_cast<int>((t1 * p01_minus_p21 - t0 * p11_minus_p21) * xstepdenominv);
    r_sstepy = static_cast<int>((t1 * p00_minus_p20 - t0 * p10_minus_p20) * ystepdenominv);

    t0 = static_cast<float>(r_p0[3] - r_p2[3]);
    t1 = static_cast<float>(r_p1[3] - r_p2[3]);
    r_tstepx = static_cast<int>((t1 * p01_minus_p21 - t0 * p11_minus_p21) * xstepdenominv);
    r_tstepy = static_cast<int>((t1 * p00_minus_p20 - t0 * p10_minus_p20) * ystepdenominv);

    t0 = static_cast<float>(r_p0[5] - r_p2[5]);
    t1 = static_cast<float>(r_p1[5] - r_p2[5]);
    r_zistepx = static_cast<int>((t1 * p01_minus_p21 - t0 * p11_minus_p21) * xstepdenominv);
    r_zistepy = static_cast<int>((t1 * p00_minus_p20 - t0 * p10_minus_p20) * ystepdenominv);

    a_sstepxfrac = r_sstepx & 0xFFFF;
    a_tstepxfrac = r_tstepx & 0xFFFF;
    a_ststepxwhole = s_width * (r_tstepx >> 16) + (r_sstepx >> 16);
}

void D_PolysetDrawSpans8(spanpackage_t* pspanpackage)
{
    int lcount;
    byte* lpdest;
    byte* lptex;
    int lsfrac, ltfrac;
    int llight;
    int lzi;
    short* lpz;

    do {
        lcount = d_aspancount - pspanpackage->count;

        errorterm += erroradjustup;
        if (errorterm >= 0) {
            d_aspancount += d_countextrastep;
            errorterm -= erroradjustdown;
        } else {
            d_aspancount += ubasestep;
        }

        if (lcount) {
            lpdest = reinterpret_cast<byte*>(pspanpackage->pdest);
            lptex = pspanpackage->ptex;
            lpz = pspanpackage->pz;
            lsfrac = pspanpackage->sfrac;
            ltfrac = pspanpackage->tfrac;
            llight = pspanpackage->light;
            lzi = pspanpackage->zi;

            do {
                if ((lzi >> 16) >= *lpz) {
                    *lpdest = reinterpret_cast<byte*>(acolormap)[*lptex + (llight & 0xFF00)];
                    *lpz = static_cast<short>(lzi >> 16);
                }

                lpdest++;
                lzi += r_zistepx;
                lpz++;
                llight += r_lstepx;
                lptex += a_ststepxwhole;
                lsfrac += a_sstepxfrac;
                lptex += lsfrac >> 16;
                lsfrac &= 0xFFFF;
                ltfrac += a_tstepxfrac;
                if (ltfrac & 0x10000) {
                    lptex += r_affinetridesc.skinwidth;
                    ltfrac &= 0xFFFF;
                }
            } while (--lcount);
        }

        pspanpackage++;
    } while (pspanpackage->count != -999999);
}

void D_RasterizeAliasPolySmooth()
{
    int initialleftheight, initialrightheight;
    const eastl::array<int, 6> *plefttop, *prighttop, *pleftbottom, *prightbottom;
    int working_lstepx, originalcount;

    plefttop = pedgetable->pleftedgevert0;
    prighttop = pedgetable->prightedgevert0;

    pleftbottom = pedgetable->pleftedgevert1;
    prightbottom = pedgetable->prightedgevert1;

    initialleftheight = (*pleftbottom)[1] - (*plefttop)[1];
    initialrightheight = (*prightbottom)[1] - (*prighttop)[1];

    D_PolysetCalcGradients(r_affinetridesc.skinwidth);

    d_pedgespanpackage = a_spans;

    ystart = (*plefttop)[1];
    d_aspancount = (*plefttop)[0] - (*prighttop)[0];

    d_ptex = reinterpret_cast<byte*>(r_affinetridesc.pskin) + ((*plefttop)[2] >> 16) + ((*plefttop)[3] >> 16) * r_affinetridesc.skinwidth;
    d_sfrac = (*plefttop)[2] & 0xFFFF;
    d_tfrac = (*plefttop)[3] & 0xFFFF;
    d_light = (*plefttop)[4];
    d_zi = (*plefttop)[5];

    d_pdest = reinterpret_cast<byte*>(d_viewbuffer) + ystart * screenwidth + (*plefttop)[0];
    d_pz = d_pzbuffer + ystart * d_zwidth + (*plefttop)[0];

    if (initialleftheight == 1) {
        d_pedgespanpackage->pdest = d_pdest;
        d_pedgespanpackage->pz = d_pz;
        d_pedgespanpackage->count = d_aspancount;
        d_pedgespanpackage->ptex = d_ptex;

        d_pedgespanpackage->sfrac = d_sfrac;
        d_pedgespanpackage->tfrac = d_tfrac;

        d_pedgespanpackage->light = d_light;
        d_pedgespanpackage->zi = d_zi;

        d_pedgespanpackage++;
    } else {
        D_PolysetSetUpForLineScan((*plefttop)[0], (*plefttop)[1], (*pleftbottom)[0],
            (*pleftbottom)[1]);

        d_pzbasestep = d_zwidth + ubasestep;
        d_pzextrastep = d_pzbasestep + 1;

        d_pdestbasestep = screenwidth + ubasestep;
        d_pdestextrastep = d_pdestbasestep + 1;

        if (ubasestep < 0) {
            working_lstepx = r_lstepx - 1;
        } else {
            working_lstepx = r_lstepx;
        }

        d_countextrastep = ubasestep + 1;
        d_ptexbasestep = ((r_sstepy + r_sstepx * ubasestep) >> 16) + ((r_tstepy + r_tstepx * ubasestep) >> 16) * r_affinetridesc.skinwidth;
        d_sfracbasestep = (r_sstepy + r_sstepx * ubasestep) & 0xFFFF;
        d_tfracbasestep = (r_tstepy + r_tstepx * ubasestep) & 0xFFFF;
        d_lightbasestep = r_lstepy + working_lstepx * ubasestep;
        d_zibasestep = r_zistepy + r_zistepx * ubasestep;

        d_ptexextrastep = ((r_sstepy + r_sstepx * d_countextrastep) >> 16) + ((r_tstepy + r_tstepx * d_countextrastep) >> 16) * r_affinetridesc.skinwidth;
        d_sfracextrastep = (r_sstepy + r_sstepx * d_countextrastep) & 0xFFFF;
        d_tfracextrastep = (r_tstepy + r_tstepx * d_countextrastep) & 0xFFFF;
        d_lightextrastep = d_lightbasestep + working_lstepx;
        d_ziextrastep = d_zibasestep + r_zistepx;

        D_PolysetScanLeftEdge(initialleftheight);
    }

    if (pedgetable->numleftedges == 2) {
        int height;

        plefttop = pleftbottom;
        pleftbottom = pedgetable->pleftedgevert2;

        height = (*pleftbottom)[1] - (*plefttop)[1];

        ystart = (*plefttop)[1];
        d_aspancount = (*plefttop)[0] - (*prighttop)[0];
        d_ptex = reinterpret_cast<byte*>(r_affinetridesc.pskin) + ((*plefttop)[2] >> 16) + ((*plefttop)[3] >> 16) * r_affinetridesc.skinwidth;
        d_sfrac = 0;
        d_tfrac = 0;
        d_light = (*plefttop)[4];
        d_zi = (*plefttop)[5];

        d_pdest = reinterpret_cast<byte*>(d_viewbuffer) + ystart * screenwidth + (*plefttop)[0];
        d_pz = d_pzbuffer + ystart * d_zwidth + (*plefttop)[0];

        if (height == 1) {
            d_pedgespanpackage->pdest = d_pdest;
            d_pedgespanpackage->pz = d_pz;
            d_pedgespanpackage->count = d_aspancount;
            d_pedgespanpackage->ptex = d_ptex;

            d_pedgespanpackage->sfrac = d_sfrac;
            d_pedgespanpackage->tfrac = d_tfrac;

            d_pedgespanpackage->light = d_light;
            d_pedgespanpackage->zi = d_zi;

            d_pedgespanpackage++;
        } else {
            D_PolysetSetUpForLineScan((*plefttop)[0], (*plefttop)[1], (*pleftbottom)[0],
                (*pleftbottom)[1]);

            d_pdestbasestep = screenwidth + ubasestep;
            d_pdestextrastep = d_pdestbasestep + 1;
            d_pzbasestep = d_zwidth + ubasestep;
            d_pzextrastep = d_pzbasestep + 1;

            if (ubasestep < 0) {
                working_lstepx = r_lstepx - 1;
            } else {
                working_lstepx = r_lstepx;
            }

            d_countextrastep = ubasestep + 1;
            d_ptexbasestep = ((r_sstepy + r_sstepx * ubasestep) >> 16) + ((r_tstepy + r_tstepx * ubasestep) >> 16) * r_affinetridesc.skinwidth;
            d_sfracbasestep = (r_sstepy + r_sstepx * ubasestep) & 0xFFFF;
            d_tfracbasestep = (r_tstepy + r_tstepx * ubasestep) & 0xFFFF;
            d_lightbasestep = r_lstepy + working_lstepx * ubasestep;
            d_zibasestep = r_zistepy + r_zistepx * ubasestep;

            d_ptexextrastep = ((r_sstepy + r_sstepx * d_countextrastep) >> 16) + ((r_tstepy + r_tstepx * d_countextrastep) >> 16) * r_affinetridesc.skinwidth;
            d_sfracextrastep = (r_sstepy + r_sstepx * d_countextrastep) & 0xFFFF;
            d_tfracextrastep = (r_tstepy + r_tstepx * d_countextrastep) & 0xFFFF;
            d_lightextrastep = d_lightbasestep + working_lstepx;
            d_ziextrastep = d_zibasestep + r_zistepx;

            D_PolysetScanLeftEdge(height);
        }
    }

    d_pedgespanpackage = a_spans;

    D_PolysetSetUpForLineScan((*prighttop)[0], (*prighttop)[1], (*prightbottom)[0],
        (*prightbottom)[1]);
    d_aspancount = 0;
    d_countextrastep = ubasestep + 1;
    originalcount = a_spans[initialrightheight].count;
    a_spans[initialrightheight].count = -999999;
    D_PolysetDrawSpans8(a_spans);

    if (pedgetable->numrightedges == 2) {
        int height;
        spanpackage_t* pstart;

        pstart = a_spans + initialrightheight;
        pstart->count = originalcount;

        d_aspancount = (*prightbottom)[0] - (*prighttop)[0];

        prighttop = prightbottom;
        prightbottom = pedgetable->prightedgevert2;

        height = (*prightbottom)[1] - (*prighttop)[1];

        D_PolysetSetUpForLineScan((*prighttop)[0], (*prighttop)[1], (*prightbottom)[0],
            (*prightbottom)[1]);

        d_countextrastep = ubasestep + 1;
        a_spans[initialrightheight + height].count = -999999;
        D_PolysetDrawSpans8(pstart);
    }
}

void D_PolysetSetEdgeTable()
{
    int edgetableindex;

    edgetableindex = 0;

    if (r_p0[1] >= r_p1[1]) {
        if (r_p0[1] == r_p1[1]) {
            if (r_p0[1] < r_p2[1]) {
                pedgetable = &edgetables[2];
            } else {
                pedgetable = &edgetables[5];
            }

            return;
        } else {
            edgetableindex = 1;
        }
    }

    if (r_p0[1] == r_p2[1]) {
        if (edgetableindex) {
            pedgetable = &edgetables[8];
        } else {
            pedgetable = &edgetables[9];
        }

        return;
    } else if (r_p1[1] == r_p2[1]) {
        if (edgetableindex) {
            pedgetable = &edgetables[10];
        } else {
            pedgetable = &edgetables[11];
        }

        return;
    }

    if (r_p0[1] > r_p2[1]) {
        edgetableindex += 2;
    }

    if (r_p1[1] > r_p2[1]) {
        edgetableindex += 4;
    }

    pedgetable = &edgetables[edgetableindex];
}

// ==============================================================
// d_part.cpp -- software particle drawing
// ==============================================================

void D_EndParticles()
{
}

void D_StartParticles()
{
}

void D_DrawParticle(particle_t* pparticle)
{
    Vector3 local, transformed;
    float zi;
    byte* pdest;
    short* pz;
    int i, izi, pix, count, u, v;

    local = pparticle->org - r_origin;

    transformed.x = local.dot(r_pright);
    transformed.y = local.dot(r_pup);
    transformed.z = local.dot(r_ppn);

    if (transformed.z < PARTICLE_Z_CLIP) {
        return;
    }

    zi = 1.0f / transformed.z;
    u = static_cast<int>(xcenter + zi * transformed.x + 0.5f);
    v = static_cast<int>(ycenter - zi * transformed.y + 0.5f);

    if ((v > d_vrectbottom_particle) || (u > d_vrectright_particle) || (v < d_vrecty) || (u < d_vrectx)) {
        return;
    }

    pz = d_pzbuffer + (d_zwidth * v) + u;
    pdest = reinterpret_cast<byte*>(d_viewbuffer) + d_scantable[v] + u;
    izi = static_cast<int>(zi * 32768.0f);

    pix = izi >> d_pix_shift;

    if (pix < d_pix_min) {
        pix = d_pix_min;
    } else if (pix > d_pix_max) {
        pix = d_pix_max;
    }

    switch (pix) {
    case 1:
        count = 1 << d_y_aspect_shift;

        for (; count; count--, pz += d_zwidth, pdest += screenwidth) {
            if (pz[0] <= izi) {
                pz[0] = static_cast<short>(izi);
                pdest[0] = static_cast<byte>(pparticle->color);
            }
        }
        break;

    case 2:
        count = 2 << d_y_aspect_shift;

        for (; count; count--, pz += d_zwidth, pdest += screenwidth) {
            if (pz[0] <= izi) {
                pz[0] = static_cast<short>(izi);
                pdest[0] = static_cast<byte>(pparticle->color);
            }

            if (pz[1] <= izi) {
                pz[1] = static_cast<short>(izi);
                pdest[1] = static_cast<byte>(pparticle->color);
            }
        }
        break;

    case 3:
        count = 3 << d_y_aspect_shift;

        for (; count; count--, pz += d_zwidth, pdest += screenwidth) {
            if (pz[0] <= izi) {
                pz[0] = static_cast<short>(izi);
                pdest[0] = static_cast<byte>(pparticle->color);
            }

            if (pz[1] <= izi) {
                pz[1] = static_cast<short>(izi);
                pdest[1] = static_cast<byte>(pparticle->color);
            }

            if (pz[2] <= izi) {
                pz[2] = static_cast<short>(izi);
                pdest[2] = static_cast<byte>(pparticle->color);
            }
        }
        break;

    case 4:
        count = 4 << d_y_aspect_shift;

        for (; count; count--, pz += d_zwidth, pdest += screenwidth) {
            if (pz[0] <= izi) {
                pz[0] = static_cast<short>(izi);
                pdest[0] = static_cast<byte>(pparticle->color);
            }

            if (pz[1] <= izi) {
                pz[1] = static_cast<short>(izi);
                pdest[1] = static_cast<byte>(pparticle->color);
            }

            if (pz[2] <= izi) {
                pz[2] = static_cast<short>(izi);
                pdest[2] = static_cast<byte>(pparticle->color);
            }

            if (pz[3] <= izi) {
                pz[3] = static_cast<short>(izi);
                pdest[3] = static_cast<byte>(pparticle->color);
            }
        }
        break;

    default:
        count = pix << d_y_aspect_shift;

        for (; count; count--, pz += d_zwidth, pdest += screenwidth) {
            for (i = 0; i < pix; i++) {
                if (pz[i] <= izi) {
                    pz[i] = static_cast<short>(izi);
                    pdest[i] = static_cast<byte>(pparticle->color);
                }
            }
        }
        break;
    }
}

} // namespace Render

// model.cpp -- model loading and caching

// models are the only shared resource between a client and server running
// on the same machine.

#include <EASTL/array.h>
#include <EASTL/vector.h>
#include <EASTL/string.h>
#include <EASTL/algorithm.h>
#include <cmath>
#include <cstring>

using namespace Client;
using namespace Common;
using namespace Console;
using namespace Render;
using namespace Draw;
using namespace Host;
using namespace Input;
using namespace Keys;
using namespace Math;
using namespace Menu;
using namespace Model;
using namespace Net;
using namespace VM;
using namespace Sbar;
using namespace Screen;
using namespace Server;
using namespace Audio;
using namespace Vid;
using namespace View;
using namespace Wad;
using namespace Cvar;
using namespace Cmd;


namespace Model {

static model_t* loadmodel = nullptr;
static char loadname[32] = {}; // for hunk tags

void Mod_LoadSpriteModel(model_t* mod, void* buffer);
void Mod_LoadBrushModel(model_t* mod, void* buffer);
void Mod_LoadAliasModel(model_t* mod, void* buffer);
model_t* Mod_LoadModel(model_t* mod, qboolean crash);

static byte mod_novis[MAX_MAP_LEAFS / 8];

constexpr int MAX_MOD_KNOWN = 256;
static eastl::array<model_t, MAX_MOD_KNOWN> mod_known;
static int mod_numknown = 0;

// values for model_t's needload
constexpr int NL_PRESENT = 0;
constexpr int NL_NEEDS_LOADED = 1;
constexpr int NL_UNREFERENCED = 2;

static byte* mod_base = nullptr;

/*
===============
Mod_Init
===============
*/
void Mod_Init(void)
{
    std::memset(mod_novis, 0xff, sizeof(mod_novis));
    Cmd::AddCommand("modellist", Mod_Print);
}

/*
===============
Mod_Extradata

Caches the data if needed
===============
*/
void* Mod_Extradata(model_t* mod)
{
    if (!mod) {
        Sys_Error("Mod_Extradata: NULL mod");
    }

    void* r = Cache_Check(&mod->cache);
    if (r) {
        return r;
    }

    Mod_LoadModel(mod, true);

    if (!mod->cache.data) {
        Sys_Error("Mod_Extradata: caching failed");
    }

    return mod->cache.data;
}

/*
===============
Mod_PointInLeaf
===============
*/
mleaf_t* Mod_PointInLeaf(const Vector3& p, model_t* model)
{
    if (!model || !model->nodes) {
        Sys_Error("Mod_PointInLeaf: bad model");
    }

    mnode_t* node = model->nodes;
    while (true) {
        if (node->contents < 0) {
            return reinterpret_cast<mleaf_t*>(node);
        }

        mplane_t* plane = node->plane;
        float d = p.dot(plane->normal) - plane->dist;
        if (d > 0) {
            node = node->children[0];
        } else {
            node = node->children[1];
        }
    }

    return nullptr; // never reached
}

/*
===================
Mod_DecompressVis
===================
*/
byte* Mod_DecompressVis(byte* in, model_t* model)
{
    static byte decompressed[MAX_MAP_LEAFS / 8];
    
    int row = (model->numleafs + 7) >> 3;
    byte* out = decompressed;

    if (!in) { // no vis info, so make all visible
        while (row) {
            *out++ = 0xff;
            row--;
        }

        return decompressed;
    }

    do {
        if (*in) {
            *out++ = *in++;
            continue;
        }

        int c = in[1];
        in += 2;
        while (c) {
            *out++ = 0;
            c--;
        }
    } while (out - decompressed < row);

    return decompressed;
}

byte* Mod_LeafPVS(mleaf_t* leaf, model_t* model)
{
    if (leaf == model->leafs) {
        return mod_novis;
    }

    return Mod_DecompressVis(leaf->compressed_vis, model);
}

/*
===================
Mod_ClearAll
===================
*/
void Mod_ClearAll(void)
{
    for (int i = 0; i < mod_numknown; ++i) {
        model_t& mod = mod_known[i];
        if (mod.type != mod_alias) {
            mod.needload = NL_NEEDS_LOADED;
        }
    }
}




/*
==================
Mod_FindName
==================
*/
model_t* Mod_FindName(const char* name)
{
    if (!name || !name[0]) {
        Sys_Error("Mod_ForName: NULL name");
    }

    model_t* avail = nullptr;

    // search the currently loaded models
    for (int i = 0; i < mod_numknown; ++i) {
        model_t* mod = &mod_known[i];
        if (std::strcmp(mod->name, name) == 0) {
            return mod;
        }

        if (mod->needload == NL_UNREFERENCED) {
            if (!avail || mod->type != mod_alias) {
                avail = mod;
            }
        }
    }

    model_t* mod = nullptr;
    if (mod_numknown == MAX_MOD_KNOWN) {
        if (avail) {
            mod = avail;
            if (mod->type == mod_alias) {
                if (Cache_Check(&mod->cache)) {
                    Cache_Free(&mod->cache);
                }
            }
        } else {
            Sys_Error("mod_numknown == MAX_MOD_KNOWN");
        }
    } else {
        mod = &mod_known[mod_numknown];
        mod_numknown++;
    }

    strcpy_s(mod->name, sizeof(mod->name), name);
    mod->needload = NL_NEEDS_LOADED;

    return mod;
}

/*
==================
Mod_TouchModel
==================
*/
void Mod_TouchModel(char* name)
{
    model_t* mod = Mod_FindName(name);

    if (mod->needload == NL_PRESENT) {
        if (mod->type == mod_alias) {
            Cache_Check(&mod->cache);
        }
    }
}

/*
==================
Mod_LoadModel

Loads a model into the cache
==================
*/
model_t* Mod_LoadModel(model_t* mod, qboolean crash)
{
    if (mod->type == mod_alias) {
        if (Cache_Check(&mod->cache)) {
            mod->needload = NL_PRESENT;
            return mod;
        }
    } else {
        if (mod->needload == NL_PRESENT) {
            return mod;
        }
    }

    // load the file
    byte stackbuf[1024]; // avoid dirtying the cache heap
    unsigned* buf = reinterpret_cast<unsigned*>(COM_LoadStackFile(mod->name, stackbuf, sizeof(stackbuf)));
    if (!buf) {
        if (crash) {
            Sys_Error("Mod_NumForName: %s not found", mod->name);
        }
        return nullptr;
    }

    // allocate a new model
    COM_FileBase(mod->name, loadname);

    loadmodel = mod;

    // fill it in
    mod->needload = NL_PRESENT;

    switch (LittleLong(*buf)) {
    case IDPOLYHEADER:
        Mod_LoadAliasModel(mod, buf);
        break;

    case IDSPRITEHEADER:
        Mod_LoadSpriteModel(mod, buf);
        break;

    default:
        Mod_LoadBrushModel(mod, buf);
        break;
    }

    return mod;
}

/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/
model_t* Mod_ForName(const char* name, qboolean crash)
{
    model_t* mod = Mod_FindName(name);
    return Mod_LoadModel(mod, crash);
}

/*
===============================================================================

					BRUSHMODEL LOADING

===============================================================================
*/

/*
================
Mod_LoadTextures
================
*/
void Mod_LoadTextures(lump_t* l)
{
    if (!l->filelen) {
        loadmodel->textures = nullptr;
        return;
    }

    dmiptexlump_t* m = reinterpret_cast<dmiptexlump_t*>(mod_base + l->fileofs);
    m->nummiptex = LittleLong(m->nummiptex);

    loadmodel->numtextures = m->nummiptex;
    loadmodel->textures_owner.resize(m->nummiptex);
    loadmodel->textures = loadmodel->textures_owner.data();

    loadmodel->texture_allocations.clear();

    for (int i = 0; i < m->nummiptex; i++) {
        m->dataofs[i] = LittleLong(m->dataofs[i]);
        if (m->dataofs[i] == -1) {
            loadmodel->textures[i] = nullptr;
            continue;
        }

        miptex_t* mt = reinterpret_cast<miptex_t*>(reinterpret_cast<byte*>(m) + m->dataofs[i]);
        mt->width = LittleLong(mt->width);
        mt->height = LittleLong(mt->height);
        for (int j = 0; j < MIPLEVELS; j++) {
            mt->offsets[j] = LittleLong(mt->offsets[j]);
        }

        if ((mt->width & 15) || (mt->height & 15)) {
            Sys_Error("Texture %s is not 16 aligned", mt->name);
        }

        int pixels = mt->width * mt->height / 64 * 85;
        int texture_size = sizeof(texture_t) + pixels;
        
        loadmodel->texture_allocations.emplace_back();
        auto& tex_buf = loadmodel->texture_allocations.back();
        tex_buf.resize(texture_size);
        
        texture_t* tx = reinterpret_cast<texture_t*>(tex_buf.data());
        loadmodel->textures[i] = tx;

        std::memcpy(tx->name, mt->name, sizeof(tx->name));
        tx->width = mt->width;
        tx->height = mt->height;
        for (int j = 0; j < MIPLEVELS; j++) {
            tx->offsets[j] = mt->offsets[j] + sizeof(texture_t) - sizeof(miptex_t);
        }
        // the pixels immediately follow the structures
        std::memcpy(tx + 1, mt + 1, pixels);

        if (Q_strncmp(mt->name, "sky", 3) == 0) {
            R_InitSky(tx);
        }
    }

    // sequence the animations
    texture_t* anims[10];
    texture_t* altanims[10];

    for (int i = 0; i < m->nummiptex; i++) {
        texture_t* tx = loadmodel->textures[i];
        if (!tx || tx->name[0] != '+') {
            continue;
        }

        if (tx->anim_next) {
            continue; // allready sequenced
        }

        // find the number of frames in the animation
        std::memset(anims, 0, sizeof(anims));
        std::memset(altanims, 0, sizeof(altanims));

        int max = tx->name[1];
        if (max >= 'a' && max <= 'z') {
            max -= 'a' - 'A';
        }

        int altmax = 0;
        if (max >= '0' && max <= '9') {
            max -= '0';
            anims[max] = tx;
            max++;
        } else if (max >= 'A' && max <= 'J') {
            altmax = max - 'A';
            max = 0;
            altanims[altmax] = tx;
            altmax++;
        } else {
            Sys_Error("Bad animating texture %s", tx->name);
        }

        for (int j = i + 1; j < m->nummiptex; j++) {
            texture_t* tx2 = loadmodel->textures[j];
            if (!tx2 || tx2->name[0] != '+') {
                continue;
            }

            if (std::strcmp(tx2->name + 2, tx->name + 2) != 0) {
                continue;
            }

            int num = tx2->name[1];
            if (num >= 'a' && num <= 'z') {
                num -= 'a' - 'A';
            }

            if (num >= '0' && num <= '9') {
                num -= '0';
                anims[num] = tx2;
                if (num + 1 > max) {
                    max = num + 1;
                }
            } else if (num >= 'A' && num <= 'J') {
                num = num - 'A';
                altanims[num] = tx2;
                if (num + 1 > altmax) {
                    altmax = num + 1;
                }
            } else {
                Sys_Error("Bad animating texture %s", tx->name);
            }
        }

        constexpr int ANIM_CYCLE = 2;
        // link them all together
        for (int j = 0; j < max; j++) {
            texture_t* tx2 = anims[j];
            if (!tx2) {
                Sys_Error("Missing frame %i of %s", j, tx->name);
            }

            tx2->anim_total = max * ANIM_CYCLE;
            tx2->anim_min = j * ANIM_CYCLE;
            tx2->anim_max = (j + 1) * ANIM_CYCLE;
            tx2->anim_next = anims[(j + 1) % max];
            if (altmax) {
                tx2->alternate_anims = altanims[0];
            }
        }
        for (int j = 0; j < altmax; j++) {
            texture_t* tx2 = altanims[j];
            if (!tx2) {
                Sys_Error("Missing frame %i of %s", j, tx->name);
            }

            tx2->anim_total = altmax * ANIM_CYCLE;
            tx2->anim_min = j * ANIM_CYCLE;
            tx2->anim_max = (j + 1) * ANIM_CYCLE;
            tx2->anim_next = altanims[(j + 1) % altmax];
            if (max) {
                tx2->alternate_anims = anims[0];
            }
        }
    }
}

/*
================
Mod_LoadLighting
================
*/
void Mod_LoadLighting(lump_t* l)
{
    if (!l->filelen) {
        loadmodel->lightdata = nullptr;
        return;
    }

    loadmodel->lightdata_owner.resize(l->filelen);
    loadmodel->lightdata = loadmodel->lightdata_owner.data();
    std::memcpy(loadmodel->lightdata, mod_base + l->fileofs, l->filelen);
}

/*
================
Mod_LoadVisibility
================
*/
void Mod_LoadVisibility(lump_t* l)
{
    if (!l->filelen) {
        loadmodel->visdata = nullptr;
        return;
    }

    loadmodel->visdata_owner.resize(l->filelen);
    loadmodel->visdata = loadmodel->visdata_owner.data();
    std::memcpy(loadmodel->visdata, mod_base + l->fileofs, l->filelen);
}

/*
================
Mod_LoadEntities
================
*/
void Mod_LoadEntities(lump_t* l)
{
    if (!l->filelen) {
        loadmodel->entities = nullptr;
        return;
    }

    loadmodel->entities_owner.resize(l->filelen);
    loadmodel->entities = loadmodel->entities_owner.data();
    std::memcpy(loadmodel->entities, mod_base + l->fileofs, l->filelen);
}

/*
================
Mod_LoadVertexes
================
*/
void Mod_LoadVertexes(lump_t* l)
{
    if (l->filelen % sizeof(dvertex_t)) {
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    }

    int count = l->filelen / sizeof(dvertex_t);
    loadmodel->vertexes_owner.resize(count);
    loadmodel->vertexes = loadmodel->vertexes_owner.data();
    loadmodel->numvertexes = count;

    dvertex_t* in = reinterpret_cast<dvertex_t*>(mod_base + l->fileofs);
    mvertex_t* out = loadmodel->vertexes;

    for (int i = 0; i < count; i++, in++, out++) {
        out->position[0] = LittleFloat(in->point[0]);
        out->position[1] = LittleFloat(in->point[1]);
        out->position[2] = LittleFloat(in->point[2]);
    }
}

/*
================
Mod_LoadSubmodels
================
*/
void Mod_LoadSubmodels(lump_t* l)
{
    if (l->filelen % sizeof(dmodel_t)) {
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    }

    int count = l->filelen / sizeof(dmodel_t);
    loadmodel->submodels_owner.resize(count);
    loadmodel->submodels = loadmodel->submodels_owner.data();
    loadmodel->numsubmodels = count;

    dmodel_t* in = reinterpret_cast<dmodel_t*>(mod_base + l->fileofs);
    dmodel_t* out = loadmodel->submodels;

    for (int i = 0; i < count; i++, in++, out++) {
        for (int j = 0; j < 3; j++) { // spread the mins / maxs by a pixel
            out->mins[j] = LittleFloat(in->mins[j]) - 1;
            out->maxs[j] = LittleFloat(in->maxs[j]) + 1;
            out->origin[j] = LittleFloat(in->origin[j]);
        }
        for (int j = 0; j < MAX_MAP_HULLS; j++) {
            out->headnode[j] = LittleLong(in->headnode[j]);
        }
        out->visleafs = LittleLong(in->visleafs);
        out->firstface = LittleLong(in->firstface);
        out->numfaces = LittleLong(in->numfaces);
    }
}

/*
================
Mod_LoadEdges
================
*/
void Mod_LoadEdges(lump_t* l)
{
    if (l->filelen % sizeof(dedge_t)) {
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    }

    int count = l->filelen / sizeof(dedge_t);
    loadmodel->edges_owner.resize(count + 1);
    loadmodel->edges = loadmodel->edges_owner.data();
    loadmodel->numedges = count;

    dedge_t* in = reinterpret_cast<dedge_t*>(mod_base + l->fileofs);
    medge_t* out = loadmodel->edges;

    for (int i = 0; i < count; i++, in++, out++) {
        out->v[0] = static_cast<unsigned short>(LittleShort(in->v[0]));
        out->v[1] = static_cast<unsigned short>(LittleShort(in->v[1]));
    }
}

/*
================
Mod_LoadTexinfo
================
*/
void Mod_LoadTexinfo(lump_t* l)
{
    if (l->filelen % sizeof(texinfo_t)) {
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    }

    int count = l->filelen / sizeof(texinfo_t);
    loadmodel->texinfo_owner.resize(count);
    loadmodel->texinfo = loadmodel->texinfo_owner.data();
    loadmodel->numtexinfo = count;

    texinfo_t* in = reinterpret_cast<texinfo_t*>(mod_base + l->fileofs);
    mtexinfo_t* out = loadmodel->texinfo;

    for (int i = 0; i < count; i++, in++, out++) {
        for (int j = 0; j < 4; j++) {
            out->vecs[0][j] = LittleFloat(in->vecs[0][j]);
            out->vecs[1][j] = LittleFloat(in->vecs[1][j]);
        }

        float len1 = Length(out->vecs[0]);
        float len2 = Length(out->vecs[1]);
        len1 = (len1 + len2) / 2;
        if (len1 < 0.32) {
            out->mipadjust = 4;
        } else if (len1 < 0.49) {
            out->mipadjust = 3;
        } else if (len1 < 0.99) {
            out->mipadjust = 2;
        } else {
            out->mipadjust = 1;
        }

        int miptex = LittleLong(in->miptex);
        out->flags = LittleLong(in->flags);

        if (!loadmodel->textures) {
            out->texture = r_notexture_mip; // checkerboard texture
            out->flags = 0;
        } else {
            if (miptex >= loadmodel->numtextures) {
                Sys_Error("miptex >= loadmodel->numtextures");
            }

            out->texture = loadmodel->textures[miptex];
            if (!out->texture) {
                out->texture = r_notexture_mip; // texture not found
                out->flags = 0;
            }
        }
    }
}

/*
================
CalcSurfaceExtents

Fills in s->texturemins[] and s->extents[]
================
*/
void CalcSurfaceExtents(msurface_t* s)
{
    float mins[2] = {999999.0f, 999999.0f};
    float maxs[2] = {-99999.0f, -99999.0f};

    mtexinfo_t* tex = s->texinfo;

    for (int i = 0; i < s->numedges; i++) {
        int e = loadmodel->surfedges[s->firstedge + i];
        mvertex_t* v;
        if (e >= 0) {
            v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
        } else {
            v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];
        }

        for (int j = 0; j < 2; j++) {
            float val = v->position[0] * tex->vecs[j][0] + v->position[1] * tex->vecs[j][1] + v->position[2] * tex->vecs[j][2] + tex->vecs[j][3];
            if (val < mins[j]) {
                mins[j] = val;
            }
            if (val > maxs[j]) {
                maxs[j] = val;
            }
        }
    }

    for (int i = 0; i < 2; i++) {
        int bmins = static_cast<int>(std::floor(mins[i] / 16.0f));
        int bmaxs = static_cast<int>(std::ceil(maxs[i] / 16.0f));

        s->texturemins[i] = static_cast<short>(bmins * 16);
        s->extents[i] = static_cast<short>((bmaxs - bmins) * 16);
        if (!(tex->flags & TEX_SPECIAL) && s->extents[i] > 256) {
            Sys_Error("Bad surface extents");
        }
    }
}

/*
================
Mod_LoadFaces
================
*/
void Mod_LoadFaces(lump_t* l)
{
    if (l->filelen % sizeof(dface_t)) {
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    }

    int count = l->filelen / sizeof(dface_t);
    loadmodel->surfaces_owner.resize(count);
    loadmodel->surfaces = loadmodel->surfaces_owner.data();
    loadmodel->numsurfaces = count;

    dface_t* in = reinterpret_cast<dface_t*>(mod_base + l->fileofs);
    msurface_t* out = loadmodel->surfaces;

    for (int surfnum = 0; surfnum < count; surfnum++, in++, out++) {
        out->firstedge = LittleLong(in->firstedge);
        out->numedges = LittleShort(in->numedges);
        out->flags = 0;

        int planenum = LittleShort(in->planenum);
        int side = LittleShort(in->side);
        if (side) {
            out->flags |= SURF_PLANEBACK;
        }

        out->plane = loadmodel->planes + planenum;
        out->texinfo = loadmodel->texinfo + LittleShort(in->texinfo);

        CalcSurfaceExtents(out);

        // lighting info
        for (int i = 0; i < MAXLIGHTMAPS; i++) {
            out->styles[i] = in->styles[i];
        }
        
        int i = LittleLong(in->lightofs);
        if (i == -1) {
            out->samples = nullptr;
        } else {
            out->samples = loadmodel->lightdata + i;
        }

        // set the drawing flags flag
        if (Q_strncmp(out->texinfo->texture->name, "sky", 3) == 0) // sky
        {
            out->flags |= (SURF_DRAWSKY | SURF_DRAWTILED);
            continue;
        }

        if (Q_strncmp(out->texinfo->texture->name, "*", 1) == 0) // turbulent
        {
            out->flags |= (SURF_DRAWTURB | SURF_DRAWTILED);
            for (int j = 0; j < 2; j++) {
                out->extents[j] = 16384;
                out->texturemins[j] = -8192;
            }
            continue;
        }
    }
}

/*
=================
Mod_SetParent
=================
*/
void Mod_SetParent(mnode_t* node, mnode_t* parent)
{
    node->parent = parent;
    if (node->contents < 0) {
        return;
    }

    Mod_SetParent(node->children[0], node);
    Mod_SetParent(node->children[1], node);
}

/*
================
Mod_LoadNodes
================
*/
void Mod_LoadNodes(lump_t* l)
{
    if (l->filelen % sizeof(dnode_t)) {
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    }

    int count = l->filelen / sizeof(dnode_t);
    loadmodel->nodes_owner.resize(count);
    loadmodel->nodes = loadmodel->nodes_owner.data();
    loadmodel->numnodes = count;

    dnode_t* in = reinterpret_cast<dnode_t*>(mod_base + l->fileofs);
    mnode_t* out = loadmodel->nodes;

    for (int i = 0; i < count; i++, in++, out++) {
        for (int j = 0; j < 3; j++) {
            out->minmaxs[j] = LittleShort(in->mins[j]);
            out->minmaxs[3 + j] = LittleShort(in->maxs[j]);
        }

        int p = LittleLong(in->planenum);
        out->plane = loadmodel->planes + p;

        out->firstsurface = LittleShort(in->firstface);
        out->numsurfaces = LittleShort(in->numfaces);

        for (int j = 0; j < 2; j++) {
            p = LittleShort(in->children[j]);
            if (p >= 0) {
                out->children[j] = loadmodel->nodes + p;
            } else {
                out->children[j] = reinterpret_cast<mnode_t*>(loadmodel->leafs + (-1 - p));
            }
        }
    }

    Mod_SetParent(loadmodel->nodes, nullptr); // sets nodes and leafs
}

/*
================
Mod_LoadLeafs
================
*/
void Mod_LoadLeafs(lump_t* l)
{
    if (l->filelen % sizeof(dleaf_t)) {
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    }

    int count = l->filelen / sizeof(dleaf_t);
    loadmodel->leafs_owner.resize(count);
    loadmodel->leafs = loadmodel->leafs_owner.data();
    loadmodel->numleafs = count;

    dleaf_t* in = reinterpret_cast<dleaf_t*>(mod_base + l->fileofs);
    mleaf_t* out = loadmodel->leafs;

    for (int i = 0; i < count; i++, in++, out++) {
        for (int j = 0; j < 3; j++) {
            out->minmaxs[j] = LittleShort(in->mins[j]);
            out->minmaxs[3 + j] = LittleShort(in->maxs[j]);
        }

        int p = LittleLong(in->contents);
        out->contents = p;

        out->firstmarksurface = loadmodel->marksurfaces + LittleShort(in->firstmarksurface);
        out->nummarksurfaces = LittleShort(in->nummarksurfaces);

        p = LittleLong(in->visofs);
        if (p == -1) {
            out->compressed_vis = nullptr;
        } else {
            out->compressed_vis = loadmodel->visdata + p;
        }

        out->efrags = nullptr;

        for (int j = 0; j < 4; j++) {
            out->ambient_sound_level[j] = in->ambient_level[j];
        }
    }
}

/*
================
Mod_LoadClipnodes
================
*/
void Mod_LoadClipnodes(lump_t* l)
{
    if (l->filelen % sizeof(dclipnode_t)) {
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    }

    int count = l->filelen / sizeof(dclipnode_t);
    loadmodel->clipnodes_owner.resize(count);
    loadmodel->clipnodes = loadmodel->clipnodes_owner.data();
    loadmodel->numclipnodes = count;

    dclipnode_t* in = reinterpret_cast<dclipnode_t*>(mod_base + l->fileofs);
    dclipnode_t* out = loadmodel->clipnodes;

    hull_t* hull = &loadmodel->hulls[1];
    hull->clipnodes = out;
    hull->firstclipnode = 0;
    hull->lastclipnode = count - 1;
    hull->planes = loadmodel->planes;
    hull->clip_mins[0] = -16;
    hull->clip_mins[1] = -16;
    hull->clip_mins[2] = -24;
    hull->clip_maxs[0] = 16;
    hull->clip_maxs[1] = 16;
    hull->clip_maxs[2] = 32;

    hull = &loadmodel->hulls[2];
    hull->clipnodes = out;
    hull->firstclipnode = 0;
    hull->lastclipnode = count - 1;
    hull->planes = loadmodel->planes;
    hull->clip_mins[0] = -32;
    hull->clip_mins[1] = -32;
    hull->clip_mins[2] = -24;
    hull->clip_maxs[0] = 32;
    hull->clip_maxs[1] = 32;
    hull->clip_maxs[2] = 64;

    for (int i = 0; i < count; i++, out++, in++) {
        out->planenum = LittleLong(in->planenum);
        out->children[0] = LittleShort(in->children[0]);
        out->children[1] = LittleShort(in->children[1]);
    }
}

/*
================
Mod_MakeHull0

Deplicate the drawing hull structure as a clipping hull
================
*/
void Mod_MakeHull0(void)
{
    hull_t* hull = &loadmodel->hulls[0];

    mnode_t* in = loadmodel->nodes;
    int count = loadmodel->numnodes;
    
    loadmodel->hull0_clipnodes_owner.resize(count);
    dclipnode_t* out = loadmodel->hull0_clipnodes_owner.data();

    hull->clipnodes = out;
    hull->firstclipnode = 0;
    hull->lastclipnode = count - 1;
    hull->planes = loadmodel->planes;

    for (int i = 0; i < count; i++, out++, in++) {
        out->planenum = static_cast<int>(in->plane - loadmodel->planes);
        for (int j = 0; j < 2; j++) {
            mnode_t* child = in->children[j];
            if (child->contents < 0) {
                out->children[j] = static_cast<short>(child->contents);
            } else {
                out->children[j] = static_cast<short>(child - loadmodel->nodes);
            }
        }
    }
}

/*
================
Mod_LoadMarksurfaces
================
*/
void Mod_LoadMarksurfaces(lump_t* l)
{
    if (l->filelen % sizeof(short)) {
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    }

    int count = l->filelen / sizeof(short);
    loadmodel->marksurfaces_owner.resize(count);
    loadmodel->marksurfaces = loadmodel->marksurfaces_owner.data();
    loadmodel->nummarksurfaces = count;

    short* in = reinterpret_cast<short*>(mod_base + l->fileofs);
    msurface_t** out = loadmodel->marksurfaces;

    for (int i = 0; i < count; i++) {
        int j = LittleShort(in[i]);
        if (j >= loadmodel->numsurfaces) {
            Sys_Error("Mod_ParseMarksurfaces: bad surface number");
        }

        out[i] = loadmodel->surfaces + j;
    }
}

/*
================
Mod_LoadSurfedges
================
*/
void Mod_LoadSurfedges(lump_t* l)
{
    if (l->filelen % sizeof(int)) {
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    }

    int count = l->filelen / sizeof(int);
    loadmodel->surfedges_owner.resize(count);
    loadmodel->surfedges = loadmodel->surfedges_owner.data();
    loadmodel->numsurfedges = count;

    int* in = reinterpret_cast<int*>(mod_base + l->fileofs);
    int* out = loadmodel->surfedges;

    for (int i = 0; i < count; i++) {
        out[i] = LittleLong(in[i]);
    }
}

/*
================
Mod_LoadPlanes
================
*/
void Mod_LoadPlanes(lump_t* l)
{
    if (l->filelen % sizeof(dplane_t)) {
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    }

    int count = l->filelen / sizeof(dplane_t);
    loadmodel->planes_owner.resize(count);
    loadmodel->planes = loadmodel->planes_owner.data();
    loadmodel->numplanes = count;

    dplane_t* in = reinterpret_cast<dplane_t*>(mod_base + l->fileofs);
    mplane_t* out = loadmodel->planes;

    for (int i = 0; i < count; i++, in++, out++) {
        int bits = 0;
        for (int j = 0; j < 3; j++) {
            out->normal[j] = LittleFloat(in->normal[j]);
            if (out->normal[j] < 0) {
                bits |= 1 << j;
            }
        }

        out->dist = static_cast<float>(LittleFloat(in->dist));
        out->type = static_cast<byte>(LittleLong(in->type));
        out->signbits = static_cast<byte>(bits);
    }
}

/*
================
RadiusFromBounds
================
*/
float RadiusFromBounds(const Vector3& mins, const Vector3& maxs)
{
    Vector3 corner;

    corner.x = std::max(std::fabs(mins.x), std::fabs(maxs.x));
    corner.y = std::max(std::fabs(mins.y), std::fabs(maxs.y));
    corner.z = std::max(std::fabs(mins.z), std::fabs(maxs.z));

    return corner.length();
}

/*
================
Mod_LoadBrushModel
================
*/
void Mod_LoadBrushModel(model_t* mod, void* buffer)
{
    loadmodel->type = mod_brush;

    dheader_t* header = reinterpret_cast<dheader_t*>(buffer);

    int version = LittleLong(header->version);
    if (version != BSPVERSION) {
        Sys_Error(
            "Mod_LoadBrushModel: %s has wrong version number (%i should be %i)",
            mod->name, version, BSPVERSION);
    }

    // swap all the lumps
    mod_base = reinterpret_cast<byte*>(header);

    for (size_t i = 0; i < sizeof(dheader_t) / 4; i++) {
        reinterpret_cast<int*>(header)[i] = LittleLong(reinterpret_cast<int*>(header)[i]);
    }

    // load into heap
    Mod_LoadVertexes(&header->lumps[LUMP_VERTEXES]);
    Mod_LoadEdges(&header->lumps[LUMP_EDGES]);
    Mod_LoadSurfedges(&header->lumps[LUMP_SURFEDGES]);
    Mod_LoadTextures(&header->lumps[LUMP_TEXTURES]);
    Mod_LoadLighting(&header->lumps[LUMP_LIGHTING]);
    Mod_LoadPlanes(&header->lumps[LUMP_PLANES]);
    Mod_LoadTexinfo(&header->lumps[LUMP_TEXINFO]);
    Mod_LoadFaces(&header->lumps[LUMP_FACES]);
    Mod_LoadMarksurfaces(&header->lumps[LUMP_MARKSURFACES]);
    Mod_LoadVisibility(&header->lumps[LUMP_VISIBILITY]);
    Mod_LoadLeafs(&header->lumps[LUMP_LEAFS]);
    Mod_LoadNodes(&header->lumps[LUMP_NODES]);
    Mod_LoadClipnodes(&header->lumps[LUMP_CLIPNODES]);
    Mod_LoadEntities(&header->lumps[LUMP_ENTITIES]);
    Mod_LoadSubmodels(&header->lumps[LUMP_MODELS]);

    Mod_MakeHull0();

    mod->numframes = 2; // regular and alternate animation
    mod->flags = 0;

    //
    // set up the submodels (FIXME: this is confusing)
    //
    for (int i = 0; i < mod->numsubmodels; i++) {
        dmodel_t* bm = &mod->submodels[i];

        mod->hulls[0].firstclipnode = bm->headnode[0];
        for (int j = 1; j < MAX_MAP_HULLS; j++) {
            mod->hulls[j].firstclipnode = bm->headnode[j];
            mod->hulls[j].lastclipnode = mod->numclipnodes - 1;
        }

        mod->firstmodelsurface = bm->firstface;
        mod->nummodelsurfaces = bm->numfaces;

        VectorCopy(bm->maxs, mod->maxs);
        VectorCopy(bm->mins, mod->mins);
        mod->radius = RadiusFromBounds(mod->mins, mod->maxs);

        mod->numleafs = bm->visleafs;

        if (i < mod->numsubmodels - 1) { // duplicate the basic information
            char name[10];

            sprintf_s(name, sizeof(name), "*%i", i + 1);
            loadmodel = Mod_FindName(name);
            *loadmodel = *mod;
            strcpy_s(loadmodel->name, sizeof(loadmodel->name), name);
            mod = loadmodel;
        }
    }
}

/*
==============================================================================

ALIAS MODELS

==============================================================================
*/

/*
=================
Mod_LoadAliasFrame
=================
*/
void* Mod_LoadAliasFrame(void* pin,
    int* pframeindex,
    int numv,
    trivertx_t* pbboxmin,
    trivertx_t* pbboxmax,
    aliashdr_t* pheader,
    char* name)
{
    daliasframe_t* pdaliasframe = reinterpret_cast<daliasframe_t*>(pin);

    strcpy_s(name, 16, pdaliasframe->name);

    for (int i = 0; i < 3; i++) {
        // these are byte values, so we don't have to worry about
        // endianness
        pbboxmin->v[i] = pdaliasframe->bboxmin.v[i];
        pbboxmax->v[i] = pdaliasframe->bboxmax.v[i];
    }

    trivertx_t* pinframe = reinterpret_cast<trivertx_t*>(pdaliasframe + 1);
    trivertx_t* pframe = reinterpret_cast<trivertx_t*>(Hunk_Alloc(numv * sizeof(*pframe), loadname));

    *pframeindex = static_cast<int>(reinterpret_cast<byte*>(pframe) - reinterpret_cast<byte*>(pheader));

    for (int j = 0; j < numv; j++) {
        // these are all byte values, so no need to deal with endianness
        pframe[j].lightnormalindex = pinframe[j].lightnormalindex;

        for (int k = 0; k < 3; k++) {
            pframe[j].v[k] = pinframe[j].v[k];
        }
    }

    pinframe += numv;

    return reinterpret_cast<void*>(pinframe);
}

/*
=================
Mod_LoadAliasGroup
=================
*/
void* Mod_LoadAliasGroup(void* pin,
    int* pframeindex,
    int numv,
    trivertx_t* pbboxmin,
    trivertx_t* pbboxmax,
    aliashdr_t* pheader,
    char* name)
{
    daliasgroup_t* pingroup = reinterpret_cast<daliasgroup_t*>(pin);

    int numframes = LittleLong(pingroup->numframes);

    maliasgroup_t* paliasgroup = reinterpret_cast<maliasgroup_t*>(Hunk_Alloc(
        sizeof(maliasgroup_t) + (numframes - 1) * sizeof(paliasgroup->frames[0]),
        loadname));

    paliasgroup->numframes = numframes;

    for (int i = 0; i < 3; i++) {
        // these are byte values, so we don't have to worry about endianness
        pbboxmin->v[i] = pingroup->bboxmin.v[i];
        pbboxmax->v[i] = pingroup->bboxmax.v[i];
    }

    *pframeindex = static_cast<int>(reinterpret_cast<byte*>(paliasgroup) - reinterpret_cast<byte*>(pheader));

    daliasinterval_t* pin_intervals = reinterpret_cast<daliasinterval_t*>(pingroup + 1);

    float* poutintervals = reinterpret_cast<float*>(Hunk_Alloc(numframes * sizeof(float), loadname));

    paliasgroup->intervals = static_cast<int>(reinterpret_cast<byte*>(poutintervals) - reinterpret_cast<byte*>(pheader));

    for (int i = 0; i < numframes; i++) {
        *poutintervals = LittleFloat(pin_intervals->interval);
        if (*poutintervals <= 0.0) {
            Sys_Error("Mod_LoadAliasGroup: interval<=0");
        }

        poutintervals++;
        pin_intervals++;
    }

    void* ptemp = reinterpret_cast<void*>(pin_intervals);

    for (int i = 0; i < numframes; i++) {
        ptemp = Mod_LoadAliasFrame(ptemp, &paliasgroup->frames[i].frame, numv,
            &paliasgroup->frames[i].bboxmin,
            &paliasgroup->frames[i].bboxmax, pheader, name);
    }

    return ptemp;
}

/*
=================
Mod_LoadAliasSkin
=================
*/
void* Mod_LoadAliasSkin(void* pin,
    int* pskinindex,
    int skinsize,
    aliashdr_t* pheader)
{
    byte* pskin = reinterpret_cast<byte*>(Hunk_Alloc(skinsize * r_pixbytes, loadname));
    byte* pinskin = reinterpret_cast<byte*>(pin);
    *pskinindex = static_cast<int>(reinterpret_cast<byte*>(pskin) - reinterpret_cast<byte*>(pheader));

    if (r_pixbytes == 1) {
        Q_memcpy(pskin, pinskin, skinsize);
    } else if (r_pixbytes == 2) {
        unsigned short* pusskin = reinterpret_cast<unsigned short*>(pskin);

        for (int i = 0; i < skinsize; i++) {
            pusskin[i] = d_8to16table[pinskin[i]];
        }
    } else {
        Sys_Error("Mod_LoadAliasSkin: driver set invalid r_pixbytes: %d\n",
            r_pixbytes);
    }

    pinskin += skinsize;

    return reinterpret_cast<void*>(pinskin);
}

/*
=================
Mod_LoadAliasSkinGroup
=================
*/
void* Mod_LoadAliasSkinGroup(void* pin,
    int* pskinindex,
    int skinsize,
    aliashdr_t* pheader)
{
    daliasskingroup_t* pinskingroup = reinterpret_cast<daliasskingroup_t*>(pin);

    int numskins = LittleLong(pinskingroup->numskins);

    maliasskingroup_t* paliasskingroup = reinterpret_cast<maliasskingroup_t*>(Hunk_Alloc(
        sizeof(maliasskingroup_t) + (numskins - 1) * sizeof(paliasskingroup->skindescs[0]),
        loadname));

    paliasskingroup->numskins = numskins;

    *pskinindex = static_cast<int>(reinterpret_cast<byte*>(paliasskingroup) - reinterpret_cast<byte*>(pheader));

    daliasskininterval_t* pinskinintervals = reinterpret_cast<daliasskininterval_t*>(pinskingroup + 1);

    float* poutskinintervals = reinterpret_cast<float*>(Hunk_Alloc(numskins * sizeof(float), loadname));

    paliasskingroup->intervals = static_cast<int>(reinterpret_cast<byte*>(poutskinintervals) - reinterpret_cast<byte*>(pheader));

    for (int i = 0; i < numskins; i++) {
        *poutskinintervals = LittleFloat(pinskinintervals->interval);
        if (*poutskinintervals <= 0) {
            Sys_Error("Mod_LoadAliasSkinGroup: interval<=0");
        }

        poutskinintervals++;
        pinskinintervals++;
    }

    void* ptemp = reinterpret_cast<void*>(pinskinintervals);

    for (int i = 0; i < numskins; i++) {
        ptemp = Mod_LoadAliasSkin(ptemp, &paliasskingroup->skindescs[i].skin,
            skinsize, pheader);
    }

    return ptemp;
}

/*
=================
Mod_LoadAliasModel
=================
*/
void Mod_LoadAliasModel(model_t* mod, void* buffer)
{
    int start = Hunk_LowMark();

    mdl_t* pinmodel = reinterpret_cast<mdl_t*>(buffer);

    int version = LittleLong(pinmodel->version);
    if (version != ALIAS_VERSION) {
        Sys_Error("%s has wrong version number (%i should be %i)", mod->name,
            version, ALIAS_VERSION);
    }

    //
    // allocate space for a working header, plus all the data except the frames,
    // skin and group info
    //
    int size = sizeof(aliashdr_t) + (LittleLong(pinmodel->numframes) - 1) * sizeof(aliashdr_t::frames[0]) + sizeof(mdl_t) + LittleLong(pinmodel->numverts) * sizeof(stvert_t) + LittleLong(pinmodel->numtris) * sizeof(mtriangle_t);

    aliashdr_t* pheader = reinterpret_cast<aliashdr_t*>(Hunk_Alloc(size, loadname));
    mdl_t* pmodel = reinterpret_cast<mdl_t*>(reinterpret_cast<byte*>(&pheader[1]) + (LittleLong(pinmodel->numframes) - 1) * sizeof(pheader->frames[0]));

    mod->flags = LittleLong(pinmodel->flags);

    //
    // endian-adjust and copy the data, starting with the alias model header
    //
    pmodel->boundingradius = LittleFloat(pinmodel->boundingradius);
    pmodel->numskins = LittleLong(pinmodel->numskins);
    pmodel->skinwidth = LittleLong(pinmodel->skinwidth);
    pmodel->skinheight = LittleLong(pinmodel->skinheight);

    if (pmodel->skinheight > MAX_LBM_HEIGHT) {
        Sys_Error("model %s has a skin taller than %d", mod->name, MAX_LBM_HEIGHT);
    }

    pmodel->numverts = LittleLong(pinmodel->numverts);

    if (pmodel->numverts <= 0) {
        Sys_Error("model %s has no vertices", mod->name);
    }

    if (pmodel->numverts > MAXALIASVERTS) {
        Sys_Error("model %s has too many vertices", mod->name);
    }

    pmodel->numtris = LittleLong(pinmodel->numtris);

    if (pmodel->numtris <= 0) {
        Sys_Error("model %s has no triangles", mod->name);
    }

    pmodel->numframes = LittleLong(pinmodel->numframes);
    pmodel->size = static_cast<float>(LittleFloat(pinmodel->size) * ALIAS_BASE_SIZE_RATIO);
    mod->synctype = static_cast<synctype_t>(LittleLong(static_cast<int>(pinmodel->synctype)));
    mod->numframes = pmodel->numframes;

    for (int i = 0; i < 3; i++) {
        pmodel->scale[i] = LittleFloat(pinmodel->scale[i]);
        pmodel->scale_origin[i] = LittleFloat(pinmodel->scale_origin[i]);
        pmodel->eyeposition[i] = LittleFloat(pinmodel->eyeposition[i]);
    }

    int numskins = pmodel->numskins;
    int numframes = pmodel->numframes;

    if (pmodel->skinwidth & 0x03) {
        Sys_Error("Mod_LoadAliasModel: skinwidth not multiple of 4");
    }

    pheader->model = static_cast<int>(reinterpret_cast<byte*>(pmodel) - reinterpret_cast<byte*>(pheader));

    //
    // load the skins
    //
    int skinsize = pmodel->skinheight * pmodel->skinwidth;

    if (numskins < 1) {
        Sys_Error("Mod_LoadAliasModel: Invalid # of skins: %d\n", numskins);
    }

    daliasskintype_t* pskintype = reinterpret_cast<daliasskintype_t*>(&pinmodel[1]);

    maliasskindesc_t* pskindesc = reinterpret_cast<maliasskindesc_t*>(Hunk_Alloc(numskins * sizeof(maliasskindesc_t), loadname));

    pheader->skindesc = static_cast<int>(reinterpret_cast<byte*>(pskindesc) - reinterpret_cast<byte*>(pheader));

    for (int i = 0; i < numskins; i++) {
        aliasskintype_t skintype = static_cast<aliasskintype_t>(LittleLong(static_cast<int>(pskintype->type)));
        pskindesc[i].type = skintype;

        if (skintype == aliasskintype_t::ALIAS_SKIN_SINGLE) {
            pskintype = reinterpret_cast<daliasskintype_t*>(Mod_LoadAliasSkin(
                pskintype + 1, &pskindesc[i].skin, skinsize, pheader));
        } else {
            pskintype = reinterpret_cast<daliasskintype_t*>(Mod_LoadAliasSkinGroup(
                pskintype + 1, &pskindesc[i].skin, skinsize, pheader));
        }
    }

    //
    // set base s and t vertices
    //
    stvert_t* pstverts = reinterpret_cast<stvert_t*>(&pmodel[1]);
    stvert_t* pinstverts = reinterpret_cast<stvert_t*>(pskintype);

    pheader->stverts = static_cast<int>(reinterpret_cast<byte*>(pstverts) - reinterpret_cast<byte*>(pheader));

    for (int i = 0; i < pmodel->numverts; i++) {
        pstverts[i].onseam = LittleLong(pinstverts[i].onseam);
        // put s and t in 16.16 format
        pstverts[i].s = LittleLong(pinstverts[i].s) << 16;
        pstverts[i].t = LittleLong(pinstverts[i].t) << 16;
    }

    //
    // set up the triangles
    //
    mtriangle_t* ptri = reinterpret_cast<mtriangle_t*>(&pstverts[pmodel->numverts]);
    dtriangle_t* pintriangles = reinterpret_cast<dtriangle_t*>(&pinstverts[pmodel->numverts]);

    pheader->triangles = static_cast<int>(reinterpret_cast<byte*>(ptri) - reinterpret_cast<byte*>(pheader));

    for (int i = 0; i < pmodel->numtris; i++) {
        ptri[i].facesfront = LittleLong(pintriangles[i].facesfront);

        for (int j = 0; j < 3; j++) {
            ptri[i].vertindex[j] = LittleLong(pintriangles[i].vertindex[j]);
        }
    }

    //
    // load the frames
    //
    if (numframes < 1) {
        Sys_Error("Mod_LoadAliasModel: Invalid # of frames: %d\n", numframes);
    }

    daliasframetype_t* pframetype = reinterpret_cast<daliasframetype_t*>(&pintriangles[pmodel->numtris]);

    for (int i = 0; i < numframes; i++) {
        aliasframetype_t frametype = static_cast<aliasframetype_t>(LittleLong(static_cast<int>(pframetype->type)));
        pheader->frames[i].type = frametype;

        if (frametype == aliasframetype_t::ALIAS_SINGLE) {
            pframetype = reinterpret_cast<daliasframetype_t*>(Mod_LoadAliasFrame(
                pframetype + 1, &pheader->frames[i].frame, pmodel->numverts,
                &pheader->frames[i].bboxmin, &pheader->frames[i].bboxmax, pheader,
                pheader->frames[i].name));
        } else {
            pframetype = reinterpret_cast<daliasframetype_t*>(Mod_LoadAliasGroup(
                pframetype + 1, &pheader->frames[i].frame, pmodel->numverts,
                &pheader->frames[i].bboxmin, &pheader->frames[i].bboxmax, pheader,
                pheader->frames[i].name));
        }
    }

    mod->type = mod_alias;

    // FIXME: do this right
    mod->mins[0] = mod->mins[1] = mod->mins[2] = -16.0f;
    mod->maxs[0] = mod->maxs[1] = mod->maxs[2] = 16.0f;

    //
    // move the complete, relocatable alias model to the cache
    //
    int end = Hunk_LowMark();
    int total = end - start;

    Cache_Alloc(&mod->cache, total, loadname);
    if (!mod->cache.data) {
        return;
    }

    std::memcpy(mod->cache.data, pheader, total);

    Hunk_FreeToLowMark(start);
}

//=============================================================================

/*
=================
Mod_LoadSpriteFrame
=================
*/
void* Mod_LoadSpriteFrame(void* pin, mspriteframe_t** ppframe)
{
    dspriteframe_t* pinframe = reinterpret_cast<dspriteframe_t*>(pin);

    int width = LittleLong(pinframe->width);
    int height = LittleLong(pinframe->height);
    int size = width * height;

    int alloc_size = sizeof(mspriteframe_t) + size * r_pixbytes;
    loadmodel->sprite_allocations.emplace_back();
    auto& sprite_buf = loadmodel->sprite_allocations.back();
    sprite_buf.resize(alloc_size);

    std::memset(sprite_buf.data(), 0, alloc_size);
    mspriteframe_t* pspriteframe = reinterpret_cast<mspriteframe_t*>(sprite_buf.data());
    *ppframe = pspriteframe;

    pspriteframe->width = width;
    pspriteframe->height = height;
    
    int origin[2];
    origin[0] = LittleLong(pinframe->origin[0]);
    origin[1] = LittleLong(pinframe->origin[1]);

    pspriteframe->up = static_cast<float>(origin[1]);
    pspriteframe->down = static_cast<float>(origin[1] - height);
    pspriteframe->left = static_cast<float>(origin[0]);
    pspriteframe->right = static_cast<float>(width + origin[0]);

    if (r_pixbytes == 1) {
        std::memcpy(&pspriteframe->pixels[0], reinterpret_cast<byte*>(pinframe + 1), size);
    } else if (r_pixbytes == 2) {
        byte* ppixin = reinterpret_cast<byte*>(pinframe + 1);
        unsigned short* ppixout = reinterpret_cast<unsigned short*>(&pspriteframe->pixels[0]);

        for (int i = 0; i < size; i++) {
            ppixout[i] = d_8to16table[ppixin[i]];
        }
    } else {
        Sys_Error("Mod_LoadSpriteFrame: driver set invalid r_pixbytes: %d\n",
            r_pixbytes);
    }

    return reinterpret_cast<void*>(reinterpret_cast<byte*>(pinframe) + sizeof(dspriteframe_t) + size);
}

/*
=================
Mod_LoadSpriteGroup
=================
*/
void* Mod_LoadSpriteGroup(void* pin, mspriteframe_t** ppframe)
{
    dspritegroup_t* pingroup = reinterpret_cast<dspritegroup_t*>(pin);

    int numframes = LittleLong(pingroup->numframes);

    int group_size = sizeof(mspritegroup_t) + (numframes - 1) * sizeof(mspritegroup_t::frames[0]);
    loadmodel->sprite_allocations.emplace_back();
    auto& group_buf = loadmodel->sprite_allocations.back();
    group_buf.resize(group_size);
    std::memset(group_buf.data(), 0, group_size);

    mspritegroup_t* pspritegroup = reinterpret_cast<mspritegroup_t*>(group_buf.data());
    pspritegroup->numframes = numframes;

    *ppframe = reinterpret_cast<mspriteframe_t*>(pspritegroup);

    dspriteinterval_t* pin_intervals = reinterpret_cast<dspriteinterval_t*>(pingroup + 1);

    int intervals_size = numframes * sizeof(float);
    loadmodel->sprite_allocations.emplace_back();
    auto& intervals_buf = loadmodel->sprite_allocations.back();
    intervals_buf.resize(intervals_size);
    
    float* poutintervals = reinterpret_cast<float*>(intervals_buf.data());
    pspritegroup->intervals = poutintervals;

    for (int i = 0; i < numframes; i++) {
        *poutintervals = LittleFloat(pin_intervals->interval);
        if (*poutintervals <= 0.0) {
            Sys_Error("Mod_LoadSpriteGroup: interval<=0");
        }

        poutintervals++;
        pin_intervals++;
    }

    void* ptemp = reinterpret_cast<void*>(pin_intervals);

    for (int i = 0; i < numframes; i++) {
        ptemp = Mod_LoadSpriteFrame(ptemp, &pspritegroup->frames[i]);
    }

    return ptemp;
}

/*
=================
Mod_LoadSpriteModel
=================
*/
void Mod_LoadSpriteModel(model_t* mod, void* buffer)
{
    dsprite_t* pin = reinterpret_cast<dsprite_t*>(buffer);

    int version = LittleLong(pin->version);
    if (version != SPRITE_VERSION) {
        Sys_Error(
            "%s has wrong version number "
            "(%i should be %i)",
            mod->name, version, SPRITE_VERSION);
    }

    int numframes = LittleLong(pin->numframes);

    int size = sizeof(msprite_t) + (numframes - 1) * sizeof(mspriteframedesc_t);

    mod->sprite_allocations.clear();
    mod->sprite_allocations.emplace_back();
    auto& sprite_buf = mod->sprite_allocations.back();
    sprite_buf.resize(size);
    std::memset(sprite_buf.data(), 0, size);

    msprite_t* psprite = reinterpret_cast<msprite_t*>(sprite_buf.data());

    mod->cache.data = psprite;
    psprite->type = LittleLong(pin->type);
    psprite->maxwidth = LittleLong(pin->width);
    psprite->maxheight = LittleLong(pin->height);
    psprite->beamlength = LittleFloat(pin->beamlength);
    mod->synctype = static_cast<synctype_t>(LittleLong(static_cast<int>(pin->synctype)));
    psprite->numframes = numframes;

    mod->mins[0] = mod->mins[1] = static_cast<float>(-psprite->maxwidth) / 2.0f;
    mod->maxs[0] = mod->maxs[1] = static_cast<float>(psprite->maxwidth) / 2.0f;
    mod->mins[2] = static_cast<float>(-psprite->maxheight) / 2.0f;
    mod->maxs[2] = static_cast<float>(psprite->maxheight) / 2.0f;

    //
    // load the frames
    //
    if (numframes < 1) {
        Sys_Error("Mod_LoadSpriteModel: Invalid # of frames: %d\n", numframes);
    }

    mod->numframes = numframes;
    mod->flags = 0;

    dspriteframetype_t* pframetype = reinterpret_cast<dspriteframetype_t*>(pin + 1);

    for (int i = 0; i < numframes; i++) {
        spriteframetype_t frametype = static_cast<spriteframetype_t>(LittleLong(static_cast<int>(pframetype->type)));
        psprite->frames[i].type = frametype;

        if (frametype == spriteframetype_t::SPR_SINGLE) {
            pframetype = reinterpret_cast<dspriteframetype_t*>(Mod_LoadSpriteFrame(
                pframetype + 1, &psprite->frames[i].frameptr));
        } else {
            pframetype = reinterpret_cast<dspriteframetype_t*>(Mod_LoadSpriteGroup(
                pframetype + 1, &psprite->frames[i].frameptr));
        }
    }

    mod->type = mod_sprite;
}

//=============================================================================

/*
================
Mod_Print
================
*/
void Mod_Print(void)
{
    Con_Printf("Cached models:\n");
    for (int i = 0; i < mod_numknown; ++i) {
        model_t* mod = &mod_known[i];
        Con_Printf("%8p : %s", mod->cache.data, mod->name);
        if (mod->needload & NL_UNREFERENCED) {
            Con_Printf(" (!R)");
        }

        if (mod->needload & NL_NEEDS_LOADED) {
            Con_Printf(" (!P)");
        }

        Con_Printf("\n");
    }
}

} // namespace Model

// sbar.cpp -- status bar code

#include <EASTL/array.h>
#include <EASTL/sort.h>
#include <charconv>

using namespace Client;
using namespace Common;
using namespace Console;
using namespace Render;
using namespace Draw;
using namespace Host;
using namespace Input;
using namespace Keys;
using namespace Math;
using namespace Menu;
using namespace Model;
using namespace Net;
using namespace VM;
using namespace Sbar;
using namespace Screen;
using namespace Server;
using namespace Audio;
using namespace Vid;
using namespace View;
using namespace Wad;
using namespace Cvar;
using namespace Cmd;


int sb_lines; // scan lines to draw

namespace Sbar {

int sb_updates; // if >= vid.numpages, no update needed

inline constexpr int STAT_MINUS = 10; // num frame for '-' stats digit
eastl::array<eastl::array<qpic_t*, 11>, 2> sb_nums{};
qpic_t* sb_colon = nullptr;
qpic_t* sb_slash = nullptr;
qpic_t* sb_ibar = nullptr;
qpic_t* sb_sbar = nullptr;
qpic_t* sb_scorebar = nullptr;

eastl::array<eastl::array<qpic_t*, 8>, 7> sb_weapons{}; // 0 is active, 1 is owned, 2-5 are flashes
eastl::array<qpic_t*, 4> sb_ammo{};
eastl::array<qpic_t*, 4> sb_sigil{};
eastl::array<qpic_t*, 3> sb_armor{};
eastl::array<qpic_t*, 32> sb_items{};

eastl::array<eastl::array<qpic_t*, 2>, 7> sb_faces{}; // 0 is gibbed, 1 is dead, 2-6 are alive
// 0 is static, 1 is temporary animation
qpic_t* sb_face_invis = nullptr;
qpic_t* sb_face_quad = nullptr;
qpic_t* sb_face_invuln = nullptr;
qpic_t* sb_face_invis_invuln = nullptr;

bool sb_showscores = false;

eastl::array<qpic_t*, 2> rsb_invbar{};
eastl::array<qpic_t*, 5> rsb_weapons{};
eastl::array<qpic_t*, 2> rsb_items{};
eastl::array<qpic_t*, 3> rsb_ammo{};
qpic_t* rsb_teambord = nullptr; // PGM 01/19/97 - team color border

//MED 01/04/97 added two more weapons + 3 alternates for grenade launcher
eastl::array<eastl::array<qpic_t*, 5>, 7> hsb_weapons{}; // 0 is active, 1 is owned, 2-5 are flashes
//MED 01/04/97 added array to simplify weapon parsing
constexpr eastl::array<int, 4> hipweapons = { HIT_LASER_CANNON_BIT, HIT_MJOLNIR_BIT, 4,
    HIT_PROXIMITY_GUN_BIT };
//MED 01/04/97 added hipnotic items array
eastl::array<qpic_t*, 2> hsb_items{};

void Sbar_MiniDeathmatchOverlay();
void Sbar_DeathmatchOverlay();

/*
===============
Sbar_ShowScores

Tab key down
===============
*/
void Sbar_ShowScores()
{
    if (sb_showscores) {
        return;
    }

    sb_showscores = true;
    sb_updates = 0;
}

/*
===============
Sbar_DontShowScores

Tab key up
===============
*/
void Sbar_DontShowScores()
{
    sb_showscores = false;
    sb_updates = 0;
}

/*
===============
Sbar_Changed
===============
*/
void Sbar_Changed()
{
    sb_updates = 0; // update next frame
}

/*
===============
Sbar_Init
===============
*/
void Sbar_Init()
{
    for (int i = 0; i < 10; i++) {
        sb_nums[0][i] = Draw_PicFromWad(va("num_%i", i));
        sb_nums[1][i] = Draw_PicFromWad(va("anum_%i", i));
    }

    sb_nums[0][10] = Draw_PicFromWad("num_minus");
    sb_nums[1][10] = Draw_PicFromWad("anum_minus");

    sb_colon = Draw_PicFromWad("num_colon");
    sb_slash = Draw_PicFromWad("num_slash");

    sb_weapons[0][0] = Draw_PicFromWad("inv_shotgun");
    sb_weapons[0][1] = Draw_PicFromWad("inv_sshotgun");
    sb_weapons[0][2] = Draw_PicFromWad("inv_nailgun");
    sb_weapons[0][3] = Draw_PicFromWad("inv_snailgun");
    sb_weapons[0][4] = Draw_PicFromWad("inv_rlaunch");
    sb_weapons[0][5] = Draw_PicFromWad("inv_srlaunch");
    sb_weapons[0][6] = Draw_PicFromWad("inv_lightng");

    sb_weapons[1][0] = Draw_PicFromWad("inv2_shotgun");
    sb_weapons[1][1] = Draw_PicFromWad("inv2_sshotgun");
    sb_weapons[1][2] = Draw_PicFromWad("inv2_nailgun");
    sb_weapons[1][3] = Draw_PicFromWad("inv2_snailgun");
    sb_weapons[1][4] = Draw_PicFromWad("inv2_rlaunch");
    sb_weapons[1][5] = Draw_PicFromWad("inv2_srlaunch");
    sb_weapons[1][6] = Draw_PicFromWad("inv2_lightng");

    for (int i = 0; i < 5; i++) {
        sb_weapons[2 + i][0] = Draw_PicFromWad(va("inva%i_shotgun", i + 1));
        sb_weapons[2 + i][1] = Draw_PicFromWad(va("inva%i_sshotgun", i + 1));
        sb_weapons[2 + i][2] = Draw_PicFromWad(va("inva%i_nailgun", i + 1));
        sb_weapons[2 + i][3] = Draw_PicFromWad(va("inva%i_snailgun", i + 1));
        sb_weapons[2 + i][4] = Draw_PicFromWad(va("inva%i_rlaunch", i + 1));
        sb_weapons[2 + i][5] = Draw_PicFromWad(va("inva%i_srlaunch", i + 1));
        sb_weapons[2 + i][6] = Draw_PicFromWad(va("inva%i_lightng", i + 1));
    }

    sb_ammo[0] = Draw_PicFromWad("sb_shells");
    sb_ammo[1] = Draw_PicFromWad("sb_nails");
    sb_ammo[2] = Draw_PicFromWad("sb_rocket");
    sb_ammo[3] = Draw_PicFromWad("sb_cells");

    sb_armor[0] = Draw_PicFromWad("sb_armor1");
    sb_armor[1] = Draw_PicFromWad("sb_armor2");
    sb_armor[2] = Draw_PicFromWad("sb_armor3");

    sb_items[0] = Draw_PicFromWad("sb_key1");
    sb_items[1] = Draw_PicFromWad("sb_key2");
    sb_items[2] = Draw_PicFromWad("sb_invis");
    sb_items[3] = Draw_PicFromWad("sb_invuln");
    sb_items[4] = Draw_PicFromWad("sb_suit");
    sb_items[5] = Draw_PicFromWad("sb_quad");

    sb_sigil[0] = Draw_PicFromWad("sb_sigil1");
    sb_sigil[1] = Draw_PicFromWad("sb_sigil2");
    sb_sigil[2] = Draw_PicFromWad("sb_sigil3");
    sb_sigil[3] = Draw_PicFromWad("sb_sigil4");

    sb_faces[4][0] = Draw_PicFromWad("face1");
    sb_faces[4][1] = Draw_PicFromWad("face_p1");
    sb_faces[3][0] = Draw_PicFromWad("face2");
    sb_faces[3][1] = Draw_PicFromWad("face_p2");
    sb_faces[2][0] = Draw_PicFromWad("face3");
    sb_faces[2][1] = Draw_PicFromWad("face_p3");
    sb_faces[1][0] = Draw_PicFromWad("face4");
    sb_faces[1][1] = Draw_PicFromWad("face_p4");
    sb_faces[0][0] = Draw_PicFromWad("face5");
    sb_faces[0][1] = Draw_PicFromWad("face_p5");

    sb_face_invis = Draw_PicFromWad("face_invis");
    sb_face_invuln = Draw_PicFromWad("face_invul2");
    sb_face_invis_invuln = Draw_PicFromWad("face_inv2");
    sb_face_quad = Draw_PicFromWad("face_quad");

    Cmd::AddCommand("+showscores", Sbar_ShowScores);
    Cmd::AddCommand("-showscores", Sbar_DontShowScores);

    sb_sbar = Draw_PicFromWad("sbar");
    sb_ibar = Draw_PicFromWad("ibar");
    sb_scorebar = Draw_PicFromWad("scorebar");

    //MED 01/04/97 added new hipnotic weapons
    if (hipnotic) {
        hsb_weapons[0][0] = Draw_PicFromWad("inv_laser");
        hsb_weapons[0][1] = Draw_PicFromWad("inv_mjolnir");
        hsb_weapons[0][2] = Draw_PicFromWad("inv_gren_prox");
        hsb_weapons[0][3] = Draw_PicFromWad("inv_prox_gren");
        hsb_weapons[0][4] = Draw_PicFromWad("inv_prox");

        hsb_weapons[1][0] = Draw_PicFromWad("inv2_laser");
        hsb_weapons[1][1] = Draw_PicFromWad("inv2_mjolnir");
        hsb_weapons[1][2] = Draw_PicFromWad("inv2_gren_prox");
        hsb_weapons[1][3] = Draw_PicFromWad("inv2_prox_gren");
        hsb_weapons[1][4] = Draw_PicFromWad("inv2_prox");

        for (int i = 0; i < 5; i++) {
            hsb_weapons[2 + i][0] = Draw_PicFromWad(va("inva%i_laser", i + 1));
            hsb_weapons[2 + i][1] = Draw_PicFromWad(va("inva%i_mjolnir", i + 1));
            hsb_weapons[2 + i][2] = Draw_PicFromWad(va("inva%i_gren_prox", i + 1));
            hsb_weapons[2 + i][3] = Draw_PicFromWad(va("inva%i_prox_gren", i + 1));
            hsb_weapons[2 + i][4] = Draw_PicFromWad(va("inva%i_prox", i + 1));
        }

        hsb_items[0] = Draw_PicFromWad("sb_wsuit");
        hsb_items[1] = Draw_PicFromWad("sb_eshld");
    }

    if (rogue) {
        rsb_invbar[0] = Draw_PicFromWad("r_invbar1");
        rsb_invbar[1] = Draw_PicFromWad("r_invbar2");

        rsb_weapons[0] = Draw_PicFromWad("r_lava");
        rsb_weapons[1] = Draw_PicFromWad("r_superlava");
        rsb_weapons[2] = Draw_PicFromWad("r_gren");
        rsb_weapons[3] = Draw_PicFromWad("r_multirock");
        rsb_weapons[4] = Draw_PicFromWad("r_plasma");

        rsb_items[0] = Draw_PicFromWad("r_shield1");
        rsb_items[1] = Draw_PicFromWad("r_agrav1");

        // PGM 01/19/97 - team color border
        rsb_teambord = Draw_PicFromWad("r_teambord");
        // PGM 01/19/97 - team color border

        rsb_ammo[0] = Draw_PicFromWad("r_ammolava");
        rsb_ammo[1] = Draw_PicFromWad("r_ammomulti");
        rsb_ammo[2] = Draw_PicFromWad("r_ammoplasma");
    }
}

//=============================================================================

// drawing routines are relative to the status bar location

/*
=============
Sbar_DrawPic
=============
*/
void Sbar_DrawPic(int x, int y, qpic_t* pic)
{
    if (cl.gametype == GAME_DEATHMATCH) {
        Draw_Pic(x /* + ((vid.width - 320)>>1)*/, y + (vid.height - SBAR_HEIGHT),
            pic);
    } else {
        Draw_Pic(x + ((vid.width - 320) >> 1), y + (vid.height - SBAR_HEIGHT), pic);
    }
}

/*
=============
Sbar_DrawTransPic
=============
*/
void Sbar_DrawTransPic(int x, int y, qpic_t* pic)
{
    if (cl.gametype == GAME_DEATHMATCH) {
        Draw_TransPic(x /*+ ((vid.width - 320)>>1)*/,
            y + (vid.height - SBAR_HEIGHT), pic);
    } else {
        Draw_TransPic(x + ((vid.width - 320) >> 1), y + (vid.height - SBAR_HEIGHT),
            pic);
    }
}

/*
================
Sbar_DrawCharacter

Draws one solid graphics character
================
*/
void Sbar_DrawCharacter(int x, int y, int num)
{
    if (cl.gametype == GAME_DEATHMATCH) {
        Draw_Character(x /*+ ((vid.width - 320)>>1) */ + 4,
            y + vid.height - SBAR_HEIGHT, num);
    } else {
        Draw_Character(x + ((vid.width - 320) >> 1) + 4,
            y + vid.height - SBAR_HEIGHT, num);
    }
}

/*
================
Sbar_DrawString
================
*/
void Sbar_DrawString(int x, int y, eastl::string_view str)
{
    if (cl.gametype == GAME_DEATHMATCH) {
        Draw_String(x /*+ ((vid.width - 320)>>1)*/, y + vid.height - SBAR_HEIGHT,
            str);
    } else {
        Draw_String(x + ((vid.width - 320) >> 1), y + vid.height - SBAR_HEIGHT,
            str);
    }
}

/*
=============
Sbar_itoa
=============
*/
int Sbar_itoa(int num, char* buf)
{
    auto [ptr, ec] = std::to_chars(buf, buf + 12, num);
    *ptr = '\0';
    return static_cast<int>(ptr - buf);
}

/*
=============
Sbar_DrawNum
=============
*/
void Sbar_DrawNum(int x, int y, int num, int digits, int color)
{
    char str[12];
    char* ptr;
    int l, frame;

    l = Sbar_itoa(num, str);
    ptr = str;
    if (l > digits) {
        ptr += (l - digits);
    }

    if (l < digits) {
        x += (digits - l) * 24;
    }

    while (*ptr) {
        if (*ptr == '-') {
            frame = STAT_MINUS;
        } else {
            frame = *ptr - '0';
        }

        Sbar_DrawTransPic(x, y, sb_nums[color][frame]);
        x += 24;
        ptr++;
    }
}

//=============================================================================

eastl::array<int, MAX_SCOREBOARD> fragsort{};
int scoreboardlines = 0;

/*
===============
Sbar_SortFrags
===============
*/
void Sbar_SortFrags()
{
    // sort by frags
    scoreboardlines = 0;
    for (int i = 0; i < cl.maxclients; i++) {
        if (cl.scores[i].name[0]) {
            fragsort[scoreboardlines] = i;
            scoreboardlines++;
        }
    }

    eastl::sort(fragsort.begin(), fragsort.begin() + scoreboardlines, [](int a, int b) {
        return cl.scores[a].frags > cl.scores[b].frags;
    });
}

int Sbar_ColorForMap(int m)
{
    return m < 128 ? m + 8 : m + 8;
}

/*
===============
Sbar_SoloScoreboard
===============
*/
void Sbar_SoloScoreboard(void)
{
    char str[80];
    int minutes, seconds, tens, units;
    int l;

    sprintf_s(str, sizeof(str), "Monsters:%3i /%3i", cl.stats[STAT_MONSTERS],
        cl.stats[STAT_TOTALMONSTERS]);
    Sbar_DrawString(8, 4, str);

    sprintf_s(str, sizeof(str), "Secrets :%3i /%3i", cl.stats[STAT_SECRETS],
        cl.stats[STAT_TOTALSECRETS]);
    Sbar_DrawString(8, 12, str);

    // time
    minutes = static_cast<int>(cl.time / 60);
    seconds = static_cast<int>(cl.time - 60 * minutes);
    tens = seconds / 10;
    units = seconds - 10 * tens;
    sprintf_s(str, sizeof(str), "Time :%3i:%i%i", minutes, tens, units);
    Sbar_DrawString(184, 4, str);

    // draw level name
    l = static_cast<int>(strlen(cl.levelname));
    Sbar_DrawString(232 - l * 4, 12, cl.levelname);
}

/*
===============
Sbar_DrawScoreboard
===============
*/
void Sbar_DrawScoreboard()
{
    Sbar_SoloScoreboard();
    if (cl.gametype == GAME_DEATHMATCH) {
        Sbar_DeathmatchOverlay();
    }
}

//=============================================================================

/*
===============
Sbar_DrawInventory
===============
*/
void Sbar_DrawInventory()
{
    char num[6];
    float time;
    int flashon;

    if (rogue) {
        if (cl.stats[STAT_ACTIVEWEAPON] >= RIT_LAVA_NAILGUN) {
            Sbar_DrawPic(0, -24, rsb_invbar[0]);
        } else {
            Sbar_DrawPic(0, -24, rsb_invbar[1]);
        }
    } else {
        Sbar_DrawPic(0, -24, sb_ibar);
    }

    // weapons
    for (int i = 0; i < 7; i++) {
        if (cl.items & (IT_SHOTGUN << i)) {
            time = cl.item_gettime[i];
            flashon = static_cast<int>((cl.time - time) * 10);
            if (flashon >= 10) {
                if (cl.stats[STAT_ACTIVEWEAPON] == (IT_SHOTGUN << i)) {
                    flashon = 1;
                } else {
                    flashon = 0;
                }
            } else {
                flashon = (flashon % 5) + 2;
            }

            Sbar_DrawPic(i * 24, -16, sb_weapons[flashon][i]);

            if (flashon > 1) {
                sb_updates = 0; // force update to remove flash
            }
        }
    }

    // MED 01/04/97
    // hipnotic weapons
    if (hipnotic) {
        int grenadeflashing = 0;
        for (int i = 0; i < 4; i++) {
            if (cl.items & (1 << hipweapons[i])) {
                time = cl.item_gettime[hipweapons[i]];
                flashon = static_cast<int>((cl.time - time) * 10);
                if (flashon >= 10) {
                    if (cl.stats[STAT_ACTIVEWEAPON] == (1 << hipweapons[i])) {
                        flashon = 1;
                    } else {
                        flashon = 0;
                    }
                } else {
                    flashon = (flashon % 5) + 2;
                }

                // check grenade launcher
                if (i == 2) {
                    if (cl.items & HIT_PROXIMITY_GUN) {
                        if (flashon) {
                            grenadeflashing = 1;
                            Sbar_DrawPic(96, -16, hsb_weapons[flashon][2]);
                        }
                    }
                } else if (i == 3) {
                    if (cl.items & (IT_SHOTGUN << 4)) {
                        if (flashon && !grenadeflashing) {
                            Sbar_DrawPic(96, -16, hsb_weapons[flashon][3]);
                        } else if (!grenadeflashing) {
                            Sbar_DrawPic(96, -16, hsb_weapons[0][3]);
                        }
                    } else {
                        Sbar_DrawPic(96, -16, hsb_weapons[flashon][4]);
                    }
                } else {
                    Sbar_DrawPic(176 + (i * 24), -16, hsb_weapons[flashon][i]);
                }

                if (flashon > 1) {
                    sb_updates = 0; // force update to remove flash
                }
            }
        }
    }

    if (rogue) {
        // check for powered up weapon.
        if (cl.stats[STAT_ACTIVEWEAPON] >= RIT_LAVA_NAILGUN) {
            for (int i = 0; i < 5; i++) {
                if (cl.stats[STAT_ACTIVEWEAPON] == (RIT_LAVA_NAILGUN << i)) {
                    Sbar_DrawPic((i + 2) * 24, -16, rsb_weapons[i]);
                }
            }
        }
    }

    // ammo counts
    for (int i = 0; i < 4; i++) {
        sprintf_s(num, sizeof(num), "%3i", cl.stats[STAT_SHELLS + i]);
        if (num[0] != ' ') {
            Sbar_DrawCharacter((6 * i + 1) * 8 - 2, -24, 18 + num[0] - '0');
        }

        if (num[1] != ' ') {
            Sbar_DrawCharacter((6 * i + 2) * 8 - 2, -24, 18 + num[1] - '0');
        }

        if (num[2] != ' ') {
            Sbar_DrawCharacter((6 * i + 3) * 8 - 2, -24, 18 + num[2] - '0');
        }
    }

    flashon = 0;
    // items
    for (int i = 0; i < 6; i++) {
        if (cl.items & (1 << (17 + i))) {
            time = cl.item_gettime[17 + i];
            if (time && time > cl.time - 2 && flashon) { // flash frame
                sb_updates = 0;
            } else {
                //MED 01/04/97 changed keys
                if (!hipnotic || (i > 1)) {
                    Sbar_DrawPic(192 + i * 16, -16, sb_items[i]);
                }
            }

            if (time && time > cl.time - 2) {
                sb_updates = 0;
            }
        }
    }
    //MED 01/04/97 added hipnotic items
    // hipnotic items
    if (hipnotic) {
        for (int i = 0; i < 2; i++) {
            if (cl.items & (1 << (24 + i))) {
                time = cl.item_gettime[24 + i];
                if (time && time > cl.time - 2 && flashon) { // flash frame
                    sb_updates = 0;
                } else {
                    Sbar_DrawPic(288 + i * 16, -16, hsb_items[i]);
                }

                if (time && time > cl.time - 2) {
                    sb_updates = 0;
                }
            }
        }
    }

    if (rogue) {
        // new rogue items
        for (int i = 0; i < 2; i++) {
            if (cl.items & (1 << (29 + i))) {
                time = cl.item_gettime[29 + i];

                if (time && time > cl.time - 2 && flashon) { // flash frame
                    sb_updates = 0;
                } else {
                    Sbar_DrawPic(288 + i * 16, -16, rsb_items[i]);
                }

                if (time && time > cl.time - 2) {
                    sb_updates = 0;
                }
            }
        }
    } else {
        // sigils
        for (int i = 0; i < 4; i++) {
            if (cl.items & (1 << (28 + i))) {
                time = cl.item_gettime[28 + i];
                if (time && time > cl.time - 2 && flashon) { // flash frame
                    sb_updates = 0;
                } else {
                    Sbar_DrawPic(320 - 32 + i * 8, -16, sb_sigil[i]);
                }

                if (time && time > cl.time - 2) {
                    sb_updates = 0;
                }
            }
        }
    }
}

//=============================================================================

/*
===============
Sbar_DrawFrags
===============
*/
void Sbar_DrawFrags()
{
    Sbar_SortFrags();

    // draw the text
    int l = scoreboardlines <= 4 ? scoreboardlines : 4;

    int x = 23;
    int xofs;
    if (cl.gametype == GAME_DEATHMATCH) {
        xofs = 0;
    } else {
        xofs = (vid.width - 320) >> 1;
    }

    int y = vid.height - SBAR_HEIGHT - 23;

    for (int i = 0; i < l; i++) {
        int k = fragsort[i];
        scoreboard_t* s = &cl.scores[k];
        if (!s->name[0]) {
            continue;
        }

        // draw background
        int top = s->colors & 0xf0;
        int bottom = (s->colors & 15) << 4;
        top = Sbar_ColorForMap(top);
        bottom = Sbar_ColorForMap(bottom);

        Draw_Fill(xofs + x * 8 + 10, y, 28, 4, top);
        Draw_Fill(xofs + x * 8 + 10, y + 4, 28, 3, bottom);

        // draw number
        int f = s->frags;
        char num[12];
        sprintf_s(num, sizeof(num), "%3i", f);

        Sbar_DrawCharacter((x + 1) * 8, -24, num[0]);
        Sbar_DrawCharacter((x + 2) * 8, -24, num[1]);
        Sbar_DrawCharacter((x + 3) * 8, -24, num[2]);

        if (k == cl.viewentity - 1) {
            Sbar_DrawCharacter(x * 8 + 2, -24, 16);
            Sbar_DrawCharacter((x + 4) * 8 - 4, -24, 17);
        }

        x += 4;
    }
}

//=============================================================================

/*
===============
Sbar_DrawFace
===============
*/
void Sbar_DrawFace()
{
    int f, anim;

    // PGM 01/19/97 - team color drawing
    // PGM 03/02/97 - fixed so color swatch only appears in CTF modes
    if (rogue && (cl.maxclients != 1) && (teamplay.value > 3) && (teamplay.value < 7)) {
        scoreboard_t* s = &cl.scores[cl.viewentity - 1];
        // draw background
        int top = s->colors & 0xf0;
        int bottom = (s->colors & 15) << 4;
        top = Sbar_ColorForMap(top);
        bottom = Sbar_ColorForMap(bottom);

        int xofs;
        if (cl.gametype == GAME_DEATHMATCH) {
            xofs = 113;
        } else {
            xofs = ((vid.width - 320) >> 1) + 113;
        }

        Sbar_DrawPic(112, 0, rsb_teambord);
        Draw_Fill(xofs, vid.height - SBAR_HEIGHT + 3, 22, 9, top);
        Draw_Fill(xofs, vid.height - SBAR_HEIGHT + 12, 22, 9, bottom);

        // draw number
        int frag_val = s->frags;
        char num[12];
        sprintf_s(num, sizeof(num), "%3i", frag_val);

        if (top == 8) {
            if (num[0] != ' ') {
                Sbar_DrawCharacter(109, 3, 18 + num[0] - '0');
            }

            if (num[1] != ' ') {
                Sbar_DrawCharacter(116, 3, 18 + num[1] - '0');
            }

            if (num[2] != ' ') {
                Sbar_DrawCharacter(123, 3, 18 + num[2] - '0');
            }
        } else {
            Sbar_DrawCharacter(109, 3, num[0]);
            Sbar_DrawCharacter(116, 3, num[1]);
            Sbar_DrawCharacter(123, 3, num[2]);
        }

        return;
    }

    // PGM 01/19/97 - team color drawing

    if ((cl.items & (IT_INVISIBILITY | IT_INVULNERABILITY)) == (IT_INVISIBILITY | IT_INVULNERABILITY)) {
        Sbar_DrawPic(112, 0, sb_face_invis_invuln);

        return;
    }

    if (cl.items & IT_QUAD) {
        Sbar_DrawPic(112, 0, sb_face_quad);

        return;
    }

    if (cl.items & IT_INVISIBILITY) {
        Sbar_DrawPic(112, 0, sb_face_invis);

        return;
    }

    if (cl.items & IT_INVULNERABILITY) {
        Sbar_DrawPic(112, 0, sb_face_invuln);

        return;
    }

    if (cl.stats[STAT_HEALTH] >= 100) {
        f = 4;
    } else {
        f = cl.stats[STAT_HEALTH] / 20;
    }

    if (cl.time <= cl.faceanimtime) {
        anim = 1;
        sb_updates = 0; // make sure the anim gets drawn over
    } else {
        anim = 0;
    }

    Sbar_DrawPic(112, 0, sb_faces[f][anim]);
}

/*
===============
Sbar_Draw
===============
*/
void Sbar_Draw()
{
    if (Screen::GetScreenSystem().GetConCurrent() == vid.height) {
        return; // console is full screen
    }

    if (sb_updates >= vid.numpages) {
        return;
    }

    Screen::GetScreenSystem().SetCopyeverything(1);

    sb_updates++;

    if (sb_lines && vid.width > 320) {
        Draw_TileClear(0, vid.height - sb_lines, vid.width, sb_lines);
    }

    if (sb_lines > 24) {
        Sbar_DrawInventory();
        if (cl.maxclients != 1) {
            Sbar_DrawFrags();
        }
    }

    if (sb_showscores || cl.stats[STAT_HEALTH] <= 0) {
        Sbar_DrawPic(0, 0, sb_scorebar);
        Sbar_DrawScoreboard();
        sb_updates = 0;
    } else if (sb_lines) {
        Sbar_DrawPic(0, 0, sb_sbar);

        // keys (hipnotic only)
        //MED 01/04/97 moved keys here so they would not be overwritten
        if (hipnotic) {
            if (cl.items & IT_KEY1) {
                Sbar_DrawPic(209, 3, sb_items[0]);
            }

            if (cl.items & IT_KEY2) {
                Sbar_DrawPic(209, 12, sb_items[1]);
            }
        }

        // armor
        if (cl.items & IT_INVULNERABILITY) {
            Sbar_DrawNum(24, 0, 666, 3, 1);
            Sbar_DrawPic(0, 0, draw_disc);
        } else {
            if (rogue) {
                Sbar_DrawNum(24, 0, cl.stats[STAT_ARMOR], 3,
                    cl.stats[STAT_ARMOR] <= 25);
                if (cl.items & RIT_ARMOR3) {
                    Sbar_DrawPic(0, 0, sb_armor[2]);
                } else if (cl.items & RIT_ARMOR2) {
                    Sbar_DrawPic(0, 0, sb_armor[1]);
                } else if (cl.items & RIT_ARMOR1) {
                    Sbar_DrawPic(0, 0, sb_armor[0]);
                }
            } else {
                Sbar_DrawNum(24, 0, cl.stats[STAT_ARMOR], 3,
                    cl.stats[STAT_ARMOR] <= 25);
                if (cl.items & IT_ARMOR3) {
                    Sbar_DrawPic(0, 0, sb_armor[2]);
                } else if (cl.items & IT_ARMOR2) {
                    Sbar_DrawPic(0, 0, sb_armor[1]);
                } else if (cl.items & IT_ARMOR1) {
                    Sbar_DrawPic(0, 0, sb_armor[0]);
                }
            }
        }

        // face
        Sbar_DrawFace();

        // health
        Sbar_DrawNum(136, 0, cl.stats[STAT_HEALTH], 3, cl.stats[STAT_HEALTH] <= 25);

        // ammo icon
        if (rogue) {
            if (cl.items & RIT_SHELLS) {
                Sbar_DrawPic(224, 0, sb_ammo[0]);
            } else if (cl.items & RIT_NAILS) {
                Sbar_DrawPic(224, 0, sb_ammo[1]);
            } else if (cl.items & RIT_ROCKETS) {
                Sbar_DrawPic(224, 0, sb_ammo[2]);
            } else if (cl.items & RIT_CELLS) {
                Sbar_DrawPic(224, 0, sb_ammo[3]);
            } else if (cl.items & RIT_LAVA_NAILS) {
                Sbar_DrawPic(224, 0, rsb_ammo[0]);
            } else if (cl.items & RIT_PLASMA_AMMO) {
                Sbar_DrawPic(224, 0, rsb_ammo[1]);
            } else if (cl.items & RIT_MULTI_ROCKETS) {
                Sbar_DrawPic(224, 0, rsb_ammo[2]);
            }
        } else {
            if (cl.items & IT_SHELLS) {
                Sbar_DrawPic(224, 0, sb_ammo[0]);
            } else if (cl.items & IT_NAILS) {
                Sbar_DrawPic(224, 0, sb_ammo[1]);
            } else if (cl.items & IT_ROCKETS) {
                Sbar_DrawPic(224, 0, sb_ammo[2]);
            } else if (cl.items & IT_CELLS) {
                Sbar_DrawPic(224, 0, sb_ammo[3]);
            }
        }

        Sbar_DrawNum(248, 0, cl.stats[STAT_AMMO], 3, cl.stats[STAT_AMMO] <= 10);
    }

    if (vid.width > 320) {
        if (cl.gametype == GAME_DEATHMATCH) {
            Sbar_MiniDeathmatchOverlay();
        }
    }
}

//=============================================================================

/*
==================
Sbar_IntermissionNumber

==================
*/
void Sbar_IntermissionNumber(int x, int y, int num, int digits, int color)
{
    char str[12];
    int l = Sbar_itoa(num, str);
    char* ptr = str;
    if (l > digits) {
        ptr += (l - digits);
    }

    if (l < digits) {
        x += (digits - l) * 24;
    }

    while (*ptr) {
        int frame;
        if (*ptr == '-') {
            frame = STAT_MINUS;
        } else {
            frame = *ptr - '0';
        }

        Draw_TransPic(x, y, sb_nums[color][frame]);
        x += 24;
        ptr++;
    }
}

/*
==================
Sbar_DeathmatchOverlay

==================
*/
void Sbar_DeathmatchOverlay()
{
    Screen::GetScreenSystem().SetCopyeverything(1);
    Screen::GetScreenSystem().SetFullupdate(0);

    qpic_t* pic = Draw_CachePic("gfx/ranking.lmp");
    M_DrawPic((320 - pic->width) / 2, 8, pic);

    // scores
    Sbar_SortFrags();

    // draw the text
    int l = scoreboardlines;

    int x = 80 + ((vid.width - 320) >> 1);
    int y = 40;
    for (int i = 0; i < l; i++) {
        int k = fragsort[i];
        scoreboard_t* s = &cl.scores[k];
        if (!s->name[0]) {
            continue;
        }

        // draw background
        int top = s->colors & 0xf0;
        int bottom = (s->colors & 15) << 4;
        top = Sbar_ColorForMap(top);
        bottom = Sbar_ColorForMap(bottom);

        Draw_Fill(x, y, 40, 4, top);
        Draw_Fill(x, y + 4, 40, 4, bottom);

        // draw number
        int f = s->frags;
        char num[12];
        sprintf_s(num, sizeof(num), "%3i", f);

        Draw_Character(x + 8, y, num[0]);
        Draw_Character(x + 16, y, num[1]);
        Draw_Character(x + 24, y, num[2]);

        if (k == cl.viewentity - 1) {
            Draw_Character(x - 8, y, 12);
        }

        // draw name
        Draw_String(x + 64, y, s->name);

        y += 10;
    }
}

/*
==================
Sbar_MiniDeathmatchOverlay

==================
*/
void Sbar_MiniDeathmatchOverlay()
{
    if (vid.width < 512 || !sb_lines) {
        return;
    }

    Screen::GetScreenSystem().SetCopyeverything(1);
    Screen::GetScreenSystem().SetFullupdate(0);

    // scores
    Sbar_SortFrags();

    // draw the text
    int y = vid.height - sb_lines;
    int numlines = sb_lines / 8;
    if (numlines < 3) {
        return;
    }

    //find us
    int i = 0;
    for (; i < scoreboardlines; i++) {
        if (fragsort[i] == cl.viewentity - 1) {
            break;
        }
    }

    if (i == scoreboardlines) { // we're not there
        i = 0;
    } else { // figure out start
        i = i - numlines / 2;
    }

    if (i > scoreboardlines - numlines) {
        i = scoreboardlines - numlines;
    }

    if (i < 0) {
        i = 0;
    }

    int x = 324;
    for (/* */; i < scoreboardlines && y < static_cast<int>(vid.height) - 8; i++) {
        int k = fragsort[i];
        scoreboard_t* s = &cl.scores[k];
        if (!s->name[0]) {
            continue;
        }

        // draw background
        int top = s->colors & 0xf0;
        int bottom = (s->colors & 15) << 4;
        top = Sbar_ColorForMap(top);
        bottom = Sbar_ColorForMap(bottom);

        Draw_Fill(x, y + 1, 40, 3, top);
        Draw_Fill(x, y + 4, 40, 4, bottom);

        // draw number
        int f = s->frags;
        char num[12];
        sprintf_s(num, sizeof(num), "%3i", f);

        Draw_Character(x + 8, y, num[0]);
        Draw_Character(x + 16, y, num[1]);
        Draw_Character(x + 24, y, num[2]);

        if (k == cl.viewentity - 1) {
            Draw_Character(x, y, 16);
            Draw_Character(x + 32, y, 17);
        }

        // draw name
        Draw_String(x + 48, y, s->name);

        y += 8;
    }
}

/*
==================
Sbar_IntermissionOverlay

==================
*/
void Sbar_IntermissionOverlay()
{
    Screen::GetScreenSystem().SetCopyeverything(1);
    Screen::GetScreenSystem().SetFullupdate(0);

    if (cl.gametype == GAME_DEATHMATCH) {
        Sbar_DeathmatchOverlay();

        return;
    }

    qpic_t* pic = Draw_CachePic("gfx/complete.lmp");
    Draw_Pic(64, 24, pic);

    pic = Draw_CachePic("gfx/inter.lmp");
    Draw_TransPic(0, 56, pic);

    // time
    int dig = cl.completed_time / 60;
    Sbar_IntermissionNumber(160, 64, dig, 3, 0);
    int num = cl.completed_time - dig * 60;
    Draw_TransPic(234, 64, sb_colon);
    Draw_TransPic(246, 64, sb_nums[0][num / 10]);
    Draw_TransPic(266, 64, sb_nums[0][num % 10]);

    Sbar_IntermissionNumber(160, 104, cl.stats[STAT_SECRETS], 3, 0);
    Draw_TransPic(232, 104, sb_slash);
    Sbar_IntermissionNumber(240, 104, cl.stats[STAT_TOTALSECRETS], 3, 0);

    Sbar_IntermissionNumber(160, 144, cl.stats[STAT_MONSTERS], 3, 0);
    Draw_TransPic(232, 144, sb_slash);
    Sbar_IntermissionNumber(240, 144, cl.stats[STAT_TOTALMONSTERS], 3, 0);
}

/*
==================
Sbar_FinaleOverlay

==================
*/
void Sbar_FinaleOverlay()
{
    Screen::GetScreenSystem().SetCopyeverything(1);

    qpic_t* pic = Draw_CachePic("gfx/finale.lmp");
    Draw_TransPic((vid.width - pic->width) / 2, 16, pic);
}

} // namespace Sbar

// screen.cpp -- master for refresh, status bar, console, chat, notify, etc


#include <EASTL/fixed_string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <cmath>
#include <cstdio>

using namespace Client;
using namespace Common;
using namespace Console;
using namespace Render;
using namespace Draw;
using namespace Host;
using namespace Input;
using namespace Keys;
using namespace Math;
using namespace Menu;
using namespace Model;
using namespace Net;
using namespace VM;
using namespace Sbar;
using namespace Screen;
using namespace Server;
using namespace Audio;
using namespace Vid;
using namespace View;
using namespace Wad;
using namespace Cvar;
using namespace Cmd;

namespace Screen {

ScreenSystem& GetScreenSystem()
{
    static ScreenSystem instance;
    return instance;
}

/*
===============================================================================

CENTER PRINTING

===============================================================================
*/

void ScreenSystem::CenterPrint(eastl::string_view str)
{
    if (str.length() >= centerstring_.capacity()) {
        centerstring_.assign(str.data(), centerstring_.capacity() - 1);
    } else {
        centerstring_.assign(str.data(), str.length());
    }

    centertime_off_ = centertime_.value;
    centertime_start_ = static_cast<float>(cl.time);

    // count the number of lines for centering
    center_lines_ = 1;
    for (char ch : centerstring_) {
        if (ch == '\n') {
            center_lines_++;
        }
    }
}

void ScreenSystem::EraseCenterString()
{
    int y = 0;

    if (erase_center_++ > vid.numpages) {
        erase_lines_ = 0;
        return;
    }

    if (center_lines_ <= 4) {
        y = static_cast<int>(vid.height * 0.35f);
    } else {
        y = 48;
    }

    copytop_ = 1;
    Draw_TileClear(0, y, vid.width, 8 * erase_lines_);
}

void ScreenSystem::DrawCenterString()
{
    int remaining = 0;

    if (cl.intermission) {
        remaining = static_cast<int>(printspeed_.value * (cl.time - centertime_start_));
    } else {
        remaining = 9999;
    }

    erase_center_ = 0;
    const char* start = centerstring_.c_str();

    int y = (center_lines_ <= 4) ? static_cast<int>(vid.height * 0.35f) : 48;

    do {
        int l = 0;
        for (l = 0; l < 40; l++) {
            if (start[l] == '\n' || start[l] == '\0') {
                break;
            }
        }

        int x = (vid.width - l * 8) / 2;
        for (int j = 0; j < l; j++, x += 8) {
            Draw_Character(x, y, start[j]);
            if (!remaining--) {
                return;
            }
        }

        y += 8;

        while (*start && *start != '\n') {
            start++;
        }

        if (!*start) {
            break;
        }

        start++; // skip the \n
    } while (true);
}

void ScreenSystem::CheckDrawCenterString()
{
    copytop_ = 1;
    if (center_lines_ > erase_lines_) {
        erase_lines_ = center_lines_;
    }

    centertime_off_ -= static_cast<float>(host_frametime);

    if (centertime_off_ <= 0.0f && !cl.intermission) {
        return;
    }

    if (key_dest != key_game) {
        return;
    }

    DrawCenterString();
}

/*
===============================================================================

CALCULATE REFDEF

===============================================================================
*/

float ScreenSystem::CalcFov(float fov_x, float width, float height)
{
    if (fov_x < 1.0f || fov_x > 179.0f) {
        Sys_Error("Bad fov: %f", fov_x);
    }

    float x = width / static_cast<float>(std::tan(fov_x / 360.0f * M_PI));
    float a = static_cast<float>(std::atan(height / x) * 360.0f / M_PI);
    return a;
}

void ScreenSystem::CalcRefdef()
{
    vrect_t vrect{};

    fullupdate_ = 0; // force a background redraw
    vid.recalc_refdef = 0;

    // force the status bar to redraw
    Sbar_Changed();

    // bound viewsize
    if (viewsize_.value < 30.0f) {
        Cvar::Set("viewsize", "30");
    }
    if (viewsize_.value > 120.0f) {
        Cvar::Set("viewsize", "120");
    }

    // bound field of view
    if (fov_.value < 10.0f) {
        Cvar::Set("fov", "10");
    }
    if (fov_.value > 170.0f) {
        Cvar::Set("fov", "170");
    }

    r_refdef.fov_x = fov_.value;
    r_refdef.fov_y = CalcFov(r_refdef.fov_x, static_cast<float>(r_refdef.vrect.width), static_cast<float>(r_refdef.vrect.height));

    float size = cl.intermission ? 120.0f : viewsize_.value;

    if (size >= 120.0f) {
        sb_lines = 0; // no status bar at all
    } else if (size >= 110.0f) {
        sb_lines = 24; // no inventory
    } else {
        sb_lines = 24 + 16 + 8;
    }

    vrect.x = 0;
    vrect.y = 0;
    vrect.width = vid.width;
    vrect.height = vid.height;

    R_SetVrect(&vrect, &vrect_, sb_lines);

    if (con_current_ > static_cast<float>(vid.height)) {
        con_current_ = static_cast<float>(vid.height);
    }

    R_ViewChanged(&vrect, sb_lines, vid.aspect);
}

void ScreenSystem::SizeUp()
{
    SizeUp_f();
}

void ScreenSystem::SizeDown()
{
    SizeDown_f();
}

void ScreenSystem::SizeUp_f()
{
    auto& sys = GetScreenSystem();
    Cvar::SetValue("viewsize", sys.viewsize_.value + 10.0f);
    vid.recalc_refdef = 1;
}

void ScreenSystem::SizeDown_f()
{
    auto& sys = GetScreenSystem();
    Cvar::SetValue("viewsize", sys.viewsize_.value - 10.0f);
    vid.recalc_refdef = 1;
}

/*
===============================================================================

INITIALIZATION

===============================================================================
*/

void ScreenSystem::Init()
{
    Cvar::Register(&fov_);
    Cvar::Register(&viewsize_);
    Cvar::Register(&conspeed_);
    Cvar::Register(&showram_);
    Cvar::Register(&showturtle_);
    Cvar::Register(&showpause_);
    Cvar::Register(&centertime_);
    Cvar::Register(&printspeed_);

    Cmd::AddCommand("screenshot", ScreenShot_f);
    Cmd::AddCommand("sizeup", SizeUp_f);
    Cmd::AddCommand("sizedown", SizeDown_f);

    ram_pic_ = Draw_PicFromWad("ram");
    net_pic_ = Draw_PicFromWad("net");
    turtle_pic_ = Draw_PicFromWad("turtle");

    initialized_ = true;
}

void ScreenSystem::DrawRam()
{
    if (!showram_.value || !r_cache_thrash) {
        return;
    }

    Draw_Pic(vrect_.x + 32, vrect_.y, ram_pic_);
}

void ScreenSystem::DrawTurtle()
{
    static int count = 0;

    if (!showturtle_.value) {
        return;
    }

    if (host_frametime < 0.1) {
        count = 0;
        return;
    }

    count++;
    if (count < 3) {
        return;
    }

    Draw_Pic(vrect_.x, vrect_.y, turtle_pic_);
}

void ScreenSystem::DrawNet()
{
    if (realtime - cl.last_received_message < 0.3 || cls.demoplayback) {
        return;
    }

    Draw_Pic(vrect_.x + 64, vrect_.y, net_pic_);
}

void ScreenSystem::DrawPause()
{
    if (!showpause_.value || !cl.paused) {
        return;
    }

    qpic_t* pic = Draw_CachePic("gfx/pause.lmp");
    Draw_Pic((vid.width - pic->width) / 2, (vid.height - 48 - pic->height) / 2, pic);
}

void ScreenSystem::DrawLoading()
{
    if (!drawloading_) {
        return;
    }

    qpic_t* pic = Draw_CachePic("gfx/loading.lmp");
    Draw_Pic((vid.width - pic->width) / 2, (vid.height - 48 - pic->height) / 2, pic);
}

/*
===============================================================================

CONSOLE DRAWING SETUP

===============================================================================
*/

void ScreenSystem::SetUpToDrawConsole()
{
    GetConsoleSystem().CheckResize();

    if (drawloading_) {
        return;
    }

    GetConsoleSystem().SetForcedUp(!cl.worldmodel || cls.signon != SIGNONS);

    if (GetConsoleSystem().IsForcedUp()) {
        conlines_ = static_cast<float>(vid.height);
        con_current_ = conlines_;
    } else if (key_dest == key_console) {
        conlines_ = static_cast<float>(vid.height / 2);
    } else {
        conlines_ = 0.0f;
    }

    if (conlines_ < con_current_) {
        con_current_ -= static_cast<float>(conspeed_.value * host_frametime);
        if (conlines_ > con_current_) {
            con_current_ = conlines_;
        }
    } else if (conlines_ > con_current_) {
        con_current_ += static_cast<float>(conspeed_.value * host_frametime);
        if (conlines_ < con_current_) {
            con_current_ = conlines_;
        }
    }

    if (clearconsole_++ < vid.numpages) {
        copytop_ = 1;
        Draw_TileClear(0, static_cast<int>(con_current_), vid.width,
            vid.height - static_cast<int>(con_current_));
        Sbar_Changed();
    } else if (clearnotify_++ < vid.numpages) {
        copytop_ = 1;
        Draw_TileClear(0, 0, vid.width, GetConsoleSystem().GetNotifyLines());
    } else {
        GetConsoleSystem().SetNotifyLines(0);
    }
}

void ScreenSystem::DrawConsole()
{
    if (con_current_) {
        copyeverything_ = 1;
        GetConsoleSystem().DrawConsole(static_cast<int>(con_current_), true);
        clearconsole_ = 0;
    } else {
        if (key_dest == key_game || key_dest == key_message) {
            GetConsoleSystem().DrawNotify();
        }
    }
}

/*
===============================================================================

SCREENSHOTS

===============================================================================
*/

#pragma pack(push, 1)
struct pcx_header_t {
    uint8_t manufacturer = 0x0a;
    uint8_t version = 5;
    uint8_t encoding = 1;
    uint8_t bits_per_pixel = 8;
    uint16_t xmin = 0;
    uint16_t ymin = 0;
    uint16_t xmax = 0;
    uint16_t ymax = 0;
    uint16_t hres = 0;
    uint16_t vres = 0;
    uint8_t palette[48]{};
    uint8_t reserved = 0;
    uint8_t color_planes = 1;
    uint16_t bytes_per_line = 0;
    uint16_t palette_type = 2;
    uint8_t filler[58]{};
};
#pragma pack(pop)

static void WritePCXfile(const char* filename,
    const byte* data,
    int width,
    int height,
    int rowbytes,
    const byte* palette)
{
    eastl::vector<uint8_t> buffer;
    buffer.reserve(sizeof(pcx_header_t) + width * height * 2 + 1024);
    buffer.resize(sizeof(pcx_header_t));

    auto* pcx = reinterpret_cast<pcx_header_t*>(buffer.data());
    pcx->manufacturer = 0x0a;
    pcx->version = 5;
    pcx->encoding = 1;
    pcx->bits_per_pixel = 8;
    pcx->xmin = 0;
    pcx->ymin = 0;
    pcx->xmax = LittleShort(static_cast<short>(width - 1));
    pcx->ymax = LittleShort(static_cast<short>(height - 1));
    pcx->hres = LittleShort(static_cast<short>(width));
    pcx->vres = LittleShort(static_cast<short>(height));
    pcx->color_planes = 1;
    pcx->bytes_per_line = LittleShort(static_cast<short>(width));
    pcx->palette_type = LittleShort(2);

    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            byte val = *data++;
            if ((val & 0xc0) != 0xc0) {
                buffer.push_back(val);
            } else {
                buffer.push_back(0xc1);
                buffer.push_back(val);
            }
        }
        data += rowbytes - width;
    }

    buffer.push_back(0x0c);
    for (int i = 0; i < 768; i++) {
        buffer.push_back(*palette++);
    }

    COM_WriteFile(filename, buffer.data(), static_cast<int>(buffer.size()));
}

void ScreenSystem::ScreenShot_f()
{
    int i = 0;
    eastl::fixed_string<char, 80> pcxname = "quake00.pcx";
    char checkname[MAX_OSPATH];

    for (i = 0; i <= 99; i++) {
        pcxname[5] = static_cast<char>(i / 10 + '0');
        pcxname[6] = static_cast<char>(i % 10 + '0');
        sprintf_s(checkname, sizeof(checkname), "%s/%s", com_gamedir, pcxname.c_str());
        if (Sys_FileTime(checkname) == -1) {
            break;
        }
    }

    if (i == 100) {
        Con_Printf("SCR_ScreenShot_f: Couldn't create a PCX file\n");
        return;
    }

    D_EnableBackBufferAccess();

    WritePCXfile(pcxname.c_str(), vid.buffer, vid.width, vid.height, vid.rowbytes, host_basepal);

    D_DisableBackBufferAccess();

    Con_Printf("Wrote %s\n", pcxname.c_str());
}

/*
===============================================================================

LOADING PLAQUE & MODAL DIALOGS

===============================================================================
*/

void ScreenSystem::BeginLoadingPlaque()
{
    S_StopAllSounds(true);

    if (cls.state != ca_connected || cls.signon != SIGNONS) {
        return;
    }

    GetConsoleSystem().ClearNotify();
    centertime_off_ = 0.0f;
    con_current_ = 0.0f;

    drawloading_ = true;
    fullupdate_ = 0;
    Sbar_Changed();
    UpdateScreen();
    drawloading_ = false;

    disabled_for_loading_ = true;
    disabled_time_ = static_cast<float>(realtime);
    fullupdate_ = 0;
}

void ScreenSystem::EndLoadingPlaque()
{
    disabled_for_loading_ = false;
    fullupdate_ = 0;
    GetConsoleSystem().ClearNotify();
}

void ScreenSystem::DrawNotifyString()
{
    const char* start = notifystring_.data();
    if (!start) {
        return;
    }

    int y = static_cast<int>(vid.height * 0.35f);

    do {
        int l = 0;
        for (l = 0; l < 40; l++) {
            if (start[l] == '\n' || !start[l]) {
                break;
            }
        }

        int x = (vid.width - l * 8) / 2;
        for (int j = 0; j < l; j++, x += 8) {
            Draw_Character(x, y, start[j]);
        }

        y += 8;

        while (*start && *start != '\n') {
            start++;
        }

        if (!*start) {
            break;
        }

        start++; // skip the \n
    } while (true);
}

bool ScreenSystem::ModalMessage(eastl::string_view text)
{
    if (cls.state == ca_dedicated) {
        return true;
    }

    notifystring_ = text;

    fullupdate_ = 0;
    drawdialog_ = true;
    UpdateScreen();
    drawdialog_ = false;

    S_ClearBuffer();

    do {
        key_count = -1;
        Sys_SendKeyEvents();
    } while (key_lastpress != 'y' && key_lastpress != 'n' && key_lastpress != K_ESCAPE);

    fullupdate_ = 0;
    UpdateScreen();

    return key_lastpress == 'y';
}

/*
===============================================================================

MAIN SCREEN UPDATE LOOP

===============================================================================
*/

void ScreenSystem::UpdateScreen()
{
    static float oldscr_viewsize = 0.0f;
    static float oldlcd_x = 0.0f;
    vrect_t vrect{};

    if (skipupdate_ || block_drawing_) {
        return;
    }

    copytop_ = 0;
    copyeverything_ = 0;

    if (disabled_for_loading_) {
        if (realtime - disabled_time_ > 60) {
            disabled_for_loading_ = false;
            Con_Printf("load failed.\n");
        } else {
            return;
        }
    }

    if (cls.state == ca_dedicated || !initialized_ || !GetConsoleSystem().IsInitialized()) {
        return;
    }

    if (viewsize_.value != oldscr_viewsize) {
        oldscr_viewsize = viewsize_.value;
        vid.recalc_refdef = 1;
    }

    if (oldfov_ != fov_.value) {
        oldfov_ = fov_.value;
        vid.recalc_refdef = true;
    }

    if (oldlcd_x != lcd_x.value) {
        oldlcd_x = lcd_x.value;
        vid.recalc_refdef = true;
    }

    if (oldscreensize_ != viewsize_.value) {
        oldscreensize_ = viewsize_.value;
        vid.recalc_refdef = true;
    }

    if (vid.recalc_refdef) {
        CalcRefdef();
    }

    D_EnableBackBufferAccess();

    if (fullupdate_++ < vid.numpages) {
        copyeverything_ = 1;
        Draw_TileClear(0, 0, vid.width, vid.height);
        Sbar_Changed();
    }

    pconupdate_ = nullptr;

    SetUpToDrawConsole();
    EraseCenterString();

    D_DisableBackBufferAccess();

    VID_LockBuffer();
    V_RenderView();
    VID_UnlockBuffer();

    D_EnableBackBufferAccess();

    if (drawdialog_) {
        Sbar_Draw();
        Draw_FadeScreen();
        DrawNotifyString();
        copyeverything_ = true;
    } else if (drawloading_) {
        DrawLoading();
        Sbar_Draw();
    } else if (cl.intermission == 1 && key_dest == key_game) {
        Sbar_IntermissionOverlay();
    } else if (cl.intermission == 2 && key_dest == key_game) {
        Sbar_FinaleOverlay();
        CheckDrawCenterString();
    } else if (cl.intermission == 3 && key_dest == key_game) {
        CheckDrawCenterString();
    } else {
        DrawRam();
        DrawNet();
        DrawTurtle();
        DrawPause();
        CheckDrawCenterString();
        Sbar_Draw();
        DrawConsole();
        M_Draw();
    }

    D_DisableBackBufferAccess();

    if (pconupdate_) {
        D_UpdateRects(pconupdate_);
    }

    V_UpdatePalette();

    if (copyeverything_) {
        vrect.x = 0;
        vrect.y = 0;
        vrect.width = vid.width;
        vrect.height = vid.height;
        vrect.pnext = nullptr;
        VID_Update(&vrect);
    } else if (copytop_) {
        vrect.x = 0;
        vrect.y = 0;
        vrect.width = vid.width;
        vrect.height = vid.height - sb_lines;
        vrect.pnext = nullptr;
        VID_Update(&vrect);
    } else {
        vrect.x = vrect_.x;
        vrect.y = vrect_.y;
        vrect.width = vrect_.width;
        vrect.height = vrect_.height;
        vrect.pnext = nullptr;
        VID_Update(&vrect);
    }
}

} // namespace Screen

// view.cpp -- player eye positioning


using namespace Client;
using namespace Common;
using namespace Console;
using namespace Render;
using namespace Draw;
using namespace Host;
using namespace Input;
using namespace Keys;
using namespace Math;
using namespace Menu;
using namespace Model;
using namespace Net;
using namespace VM;
using namespace Sbar;
using namespace Screen;
using namespace Server;
using namespace Audio;
using namespace Vid;
using namespace View;
using namespace Wad;
using namespace Cvar;
using namespace Cmd;


namespace View {

/*

The view is allowed to move slightly from it's true position for bobbing,
but if it exceeds 8 pixels linear distance (spherical, not box), the list of
entities sent from the server may not include everything in the pvs, especially
when crossing a water boudnary.

*/

cvar_t lcd_x = { "lcd_x", "0" };
cvar_t lcd_yaw = { "lcd_yaw", "0" };

cvar_t scr_ofsx = { "scr_ofsx", "0", false };
cvar_t scr_ofsy = { "scr_ofsy", "0", false };
cvar_t scr_ofsz = { "scr_ofsz", "0", false };

cvar_t cl_rollspeed = { "cl_rollspeed", "200" };
cvar_t cl_rollangle = { "cl_rollangle", "2.0" };

cvar_t cl_bob = { "cl_bob", "0.02", false };
cvar_t cl_bobcycle = { "cl_bobcycle", "0.6", false };
cvar_t cl_bobup = { "cl_bobup", "0.5", false };

cvar_t v_kicktime = { "v_kicktime", "0.5", false };
cvar_t v_kickroll = { "v_kickroll", "0.6", false };
cvar_t v_kickpitch = { "v_kickpitch", "0.6", false };

cvar_t v_iyaw_cycle = { "v_iyaw_cycle", "2", false };
cvar_t v_iroll_cycle = { "v_iroll_cycle", "0.5", false };
cvar_t v_ipitch_cycle = { "v_ipitch_cycle", "1", false };
cvar_t v_iyaw_level = { "v_iyaw_level", "0.3", false };
cvar_t v_iroll_level = { "v_iroll_level", "0.1", false };
cvar_t v_ipitch_level = { "v_ipitch_level", "0.3", false };

cvar_t v_idlescale = { "v_idlescale", "0", false };

cvar_t crosshair = { "crosshair", "0", true };
cvar_t cl_crossx = { "cl_crossx", "0", false };
cvar_t cl_crossy = { "cl_crossy", "0", false };

cvar_t gl_cshiftpercent = { "gl_cshiftpercent", "100", false };

float v_dmg_time, v_dmg_roll, v_dmg_pitch;

extern int in_forward, in_forward2, in_back;

/*
===============
V_CalcRoll

Used by view and sv_user
===============
*/
Vector3 forward, right, up;

float V_CalcRoll(const Vector3& angles, const Vector3& velocity)
{
    float sign;
    float side;
    float value;

    AngleVectors(angles, forward, right, up);
    side = velocity.dot(right);
    sign = static_cast<float>(side < 0 ? -1 : 1);
    side = fabs(side);

    value = cl_rollangle.value;
    //	if (cl.inwater)
    //		value *= 6;

    if (side < cl_rollspeed.value) {
        side = side * value / cl_rollspeed.value;
    } else {
        side = value;
    }

    return side * sign;
}

/*
===============
V_CalcBob

===============
*/
float V_CalcBob(void)
{
    float bob;
    float cycle;

    cycle = static_cast<float>(cl.time - (int)(cl.time / cl_bobcycle.value) * cl_bobcycle.value);
    cycle /= cl_bobcycle.value;
    if (cycle < cl_bobup.value) {
        cycle = static_cast<float>(M_PI * cycle / cl_bobup.value);
    } else {
        cycle = static_cast<float>(M_PI + M_PI * (cycle - cl_bobup.value) / (1.0 - cl_bobup.value));
    }

    // bob is proportional to velocity in the xy plane
    // (don't count Z, or jumping messes it up)

    bob = sqrt(cl.velocity[0] * cl.velocity[0] + cl.velocity[1] * cl.velocity[1]) * cl_bob.value;
    //Con_Printf ("speed: %5.1f\n", Length(cl.velocity));
    bob = static_cast<float>(bob * 0.3 + bob * 0.7 * sin(cycle));
    if (bob > 4) {
        bob = 4;
    } else if (bob < -7) {
        bob = -7;
    }

    return bob;
}

//=============================================================================

cvar_t v_centermove = { "v_centermove", "0.15", false };
cvar_t v_centerspeed = { "v_centerspeed", "500" };

void V_StartPitchDrift(void)
{
#if 1
    if (cl.laststop == cl.time) {
        return; // something else is keeping it from drifting
    }

#endif
    if (cl.nodrift || !cl.pitchvel) {
        cl.pitchvel = v_centerspeed.value;
        cl.nodrift = false;
        cl.driftmove = 0;
    }
}

void V_StopPitchDrift(void)
{
    cl.laststop = cl.time;
    cl.nodrift = true;
    cl.pitchvel = 0;
}

/*
===============
V_DriftPitch

Moves the client pitch angle towards cl.idealpitch sent by the server.

If the user is adjusting pitch manually, either with lookup/lookdown,
mlook and mouse, or klook and keyboard, pitch drifting is constantly stopped.

Drifting is enabled when the center view key is hit, mlook is released and
lookspring is non 0, or when
===============
*/
void V_DriftPitch(void)
{
    float delta, move;

    if (noclip_anglehack || !cl.onground || cls.demoplayback) {
        cl.driftmove = 0;
        cl.pitchvel = 0;

        return;
    }

    // don't count small mouse motion
    if (cl.nodrift) {
        if (fabs(cl.cmd.forwardmove) < cl_forwardspeed.value) {
            cl.driftmove = 0;
        } else {
            cl.driftmove += static_cast<float>(host_frametime);
        }

        if (cl.driftmove > v_centermove.value) {
            V_StartPitchDrift();
        }

        return;
    }

    delta = cl.idealpitch - cl.viewangles[PITCH];

    if (!delta) {
        cl.pitchvel = 0;

        return;
    }

    move = static_cast<float>(host_frametime * cl.pitchvel);
    cl.pitchvel += static_cast<float>(host_frametime * v_centerspeed.value);

    //Con_Printf ("move: %f (%f)\n", move, host_frametime);

    if (delta > 0) {
        if (move > delta) {
            cl.pitchvel = 0;
            move = delta;
        }

        cl.viewangles[PITCH] += move;
    } else if (delta < 0) {
        if (move > -delta) {
            cl.pitchvel = 0;
            move = -delta;
        }

        cl.viewangles[PITCH] -= move;
    }
}

/*
==============================================================================

						PALETTE FLASHES

==============================================================================
*/

cshift_t cshift_empty = { { 130, 80, 50 }, 0 };
static cshift_t cshift_water = { { 130, 80, 50 }, 128 };
cshift_t cshift_slime = { { 0, 25, 5 }, 150 };
cshift_t cshift_lava = { { 255, 80, 0 }, 150 };

cvar_t v_gamma = { "gamma", "1", true };

eastl::array<byte, 256> gammatable{}; // palette is sent through this


void BuildGammaTable(float g)
{
    int i, inf;

    if (g == 1.0) {
        for (i = 0; i < 256; i++) {
            gammatable[i] = static_cast<byte>(i);
        }

        return;
    }

    for (i = 0; i < 256; i++) {
        inf = static_cast<int>(255 * pow((i + 0.5) / 255.5, g) + 0.5);
        if (inf < 0) {
            inf = 0;
        }

        if (inf > 255) {
            inf = 255;
        }

        gammatable[i] = static_cast<byte>(inf);
    }
}

/*
=================
V_CheckGamma
=================
*/
qboolean V_CheckGamma(void)
{
    static float oldgammavalue;

    if (v_gamma.value == oldgammavalue) {
        return false;
    }

    oldgammavalue = v_gamma.value;

    BuildGammaTable(v_gamma.value);
    vid.recalc_refdef = 1; // force a surface cache flush

    return true;
}

/*
===============
V_ParseDamage
===============
*/
void V_ParseDamage(void)
{
    int armor, blood;
    Vector3 from;
    Vector3 v_forward, v_right, v_up;
    entity_t* ent;
    float side;
    float count;

    armor = MSG_ReadByte();
    blood = MSG_ReadByte();
    from.x = MSG_ReadCoord();
    from.y = MSG_ReadCoord();
    from.z = MSG_ReadCoord();

    count = static_cast<float>(blood * 0.5 + armor * 0.5);
    if (count < 10) {
        count = 10;
    }

    cl.faceanimtime = static_cast<float>(cl.time + 0.2); // but sbar face into pain frame

    cl.cshifts[CSHIFT_DAMAGE].percent += static_cast<int>(3 * count);
    if (cl.cshifts[CSHIFT_DAMAGE].percent < 0) {
        cl.cshifts[CSHIFT_DAMAGE].percent = 0;
    }

    if (cl.cshifts[CSHIFT_DAMAGE].percent > 150) {
        cl.cshifts[CSHIFT_DAMAGE].percent = 150;
    }

    if (armor > blood) {
        cl.cshifts[CSHIFT_DAMAGE].destcolor[0] = 200;
        cl.cshifts[CSHIFT_DAMAGE].destcolor[1] = 100;
        cl.cshifts[CSHIFT_DAMAGE].destcolor[2] = 100;
    } else if (armor) {
        cl.cshifts[CSHIFT_DAMAGE].destcolor[0] = 220;
        cl.cshifts[CSHIFT_DAMAGE].destcolor[1] = 50;
        cl.cshifts[CSHIFT_DAMAGE].destcolor[2] = 50;
    } else {
        cl.cshifts[CSHIFT_DAMAGE].destcolor[0] = 255;
        cl.cshifts[CSHIFT_DAMAGE].destcolor[1] = 0;
        cl.cshifts[CSHIFT_DAMAGE].destcolor[2] = 0;
    }

    //
    // calculate view angle kicks
    //
    ent = &cl_entities[cl.viewentity];

    from = from - ent->origin;
    from.normalize();

    AngleVectors(ent->angles, v_forward, v_right, v_up);

    side = from.dot(v_right);
    v_dmg_roll = count * side * v_kickroll.value;

    side = from.dot(v_forward);
    v_dmg_pitch = count * side * v_kickpitch.value;

    v_dmg_time = v_kicktime.value;
}

/*
==================
V_cshift_f
==================
*/
void V_cshift_f(void)
{
    cshift_empty.destcolor[0] = Q_atoi(Cmd::Argv(1));
    cshift_empty.destcolor[1] = Q_atoi(Cmd::Argv(2));
    cshift_empty.destcolor[2] = Q_atoi(Cmd::Argv(3));
    cshift_empty.percent = Q_atoi(Cmd::Argv(4));
}

/*
==================
V_BonusFlash_f

When you run over an item, the server sends this command
==================
*/
void V_BonusFlash_f(void)
{
    cl.cshifts[CSHIFT_BONUS].destcolor[0] = 215;
    cl.cshifts[CSHIFT_BONUS].destcolor[1] = 186;
    cl.cshifts[CSHIFT_BONUS].destcolor[2] = 69;
    cl.cshifts[CSHIFT_BONUS].percent = 50;
}

/*
=============
V_SetContentsColor

Underwater, lava, etc each has a color shift
=============
*/
void V_SetContentsColor(int contents)
{
    switch (contents) {
    case CONTENTS_EMPTY:
    case CONTENTS_SOLID:
        cl.cshifts[CSHIFT_CONTENTS] = cshift_empty;
        break;
    case CONTENTS_LAVA:
        cl.cshifts[CSHIFT_CONTENTS] = cshift_lava;
        break;
    case CONTENTS_SLIME:
        cl.cshifts[CSHIFT_CONTENTS] = cshift_slime;
        break;
    default:
        cl.cshifts[CSHIFT_CONTENTS] = cshift_water;
    }
}

/*
=============
V_CalcPowerupCshift
=============
*/
void V_CalcPowerupCshift(void)
{
    if (cl.items & IT_QUAD) {
        cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 0;
        cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 0;
        cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 255;
        cl.cshifts[CSHIFT_POWERUP].percent = 30;
    } else if (cl.items & IT_SUIT) {
        cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 0;
        cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 255;
        cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 0;
        cl.cshifts[CSHIFT_POWERUP].percent = 20;
    } else if (cl.items & IT_INVISIBILITY) {
        cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 100;
        cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 100;
        cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 100;
        cl.cshifts[CSHIFT_POWERUP].percent = 100;
    } else if (cl.items & IT_INVULNERABILITY) {
        cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 255;
        cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 255;
        cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 0;
        cl.cshifts[CSHIFT_POWERUP].percent = 30;
    } else {
        cl.cshifts[CSHIFT_POWERUP].percent = 0;
    }
}

/*
=============
V_CalcBlend
=============
*/

/*
=============
V_UpdatePalette
=============
*/
void V_UpdatePalette(void)
{
    int i, j;
    qboolean new_shift;
    byte *basepal, *newpal;
    byte pal[768];
    int r, g, b;
    qboolean force;

    V_CalcPowerupCshift();

    new_shift = false;

    for (i = 0; i < NUM_CSHIFTS; i++) {
        if (cl.cshifts[i].percent != cl.prev_cshifts[i].percent) {
            new_shift = true;
            cl.prev_cshifts[i].percent = cl.cshifts[i].percent;
        }

        for (j = 0; j < 3; j++) {
            if (cl.cshifts[i].destcolor[j] != cl.prev_cshifts[i].destcolor[j]) {
                new_shift = true;
                cl.prev_cshifts[i].destcolor[j] = cl.cshifts[i].destcolor[j];
            }
        }
    }

    // drop the damage value
    cl.cshifts[CSHIFT_DAMAGE].percent -= static_cast<int>(host_frametime * 150);
    if (cl.cshifts[CSHIFT_DAMAGE].percent <= 0) {
        cl.cshifts[CSHIFT_DAMAGE].percent = 0;
    }

    // drop the bonus value
    cl.cshifts[CSHIFT_BONUS].percent -= static_cast<int>(host_frametime * 100);
    if (cl.cshifts[CSHIFT_BONUS].percent <= 0) {
        cl.cshifts[CSHIFT_BONUS].percent = 0;
    }

    force = V_CheckGamma();
    if (!new_shift && !force) {
        return;
    }

    basepal = host_basepal;
    newpal = pal;

    for (i = 0; i < 256; i++) {
        r = basepal[0];
        g = basepal[1];
        b = basepal[2];
        basepal += 3;

        for (j = 0; j < NUM_CSHIFTS; j++) {
            r += (cl.cshifts[j].percent * (cl.cshifts[j].destcolor[0] - r)) >> 8;
            g += (cl.cshifts[j].percent * (cl.cshifts[j].destcolor[1] - g)) >> 8;
            b += (cl.cshifts[j].percent * (cl.cshifts[j].destcolor[2] - b)) >> 8;
        }

        newpal[0] = gammatable[r];
        newpal[1] = gammatable[g];
        newpal[2] = gammatable[b];
        newpal += 3;
    }

    VID_ShiftPalette(pal);
}

/*
==============================================================================

						VIEW RENDERING

==============================================================================
*/

float angledelta(float a)
{
    a = anglemod(a);
    if (a > 180) {
        a -= 360;
    }

    return a;
}

/*
==================
CalcGunAngle
==================
*/
void CalcGunAngle(void)
{
    float yaw, pitch, move;
    static float oldyaw = 0;
    static float oldpitch = 0;

    yaw = r_refdef.viewangles[YAW];
    pitch = -r_refdef.viewangles[PITCH];

    yaw = static_cast<float>(angledelta(yaw - r_refdef.viewangles[YAW]) * 0.4);
    if (yaw > 10) {
        yaw = 10;
    }

    if (yaw < -10) {
        yaw = -10;
    }

    pitch = static_cast<float>(angledelta(-pitch - r_refdef.viewangles[PITCH]) * 0.4);
    if (pitch > 10) {
        pitch = 10;
    }

    if (pitch < -10) {
        pitch = -10;
    }

    move = static_cast<float>(host_frametime * 20);
    if (yaw > oldyaw) {
        if (oldyaw + move < yaw) {
            yaw = oldyaw + move;
        }
    } else {
        if (oldyaw - move > yaw) {
            yaw = oldyaw - move;
        }
    }

    if (pitch > oldpitch) {
        if (oldpitch + move < pitch) {
            pitch = oldpitch + move;
        }
    } else {
        if (oldpitch - move > pitch) {
            pitch = oldpitch - move;
        }
    }

    oldyaw = yaw;
    oldpitch = pitch;

    cl.viewent.angles[YAW] = r_refdef.viewangles[YAW] + yaw;
    cl.viewent.angles[PITCH] = -(r_refdef.viewangles[PITCH] + pitch);

    cl.viewent.angles[ROLL] -= static_cast<float>(v_idlescale.value * sin(cl.time * v_iroll_cycle.value) * v_iroll_level.value);
    cl.viewent.angles[PITCH] -= static_cast<float>(v_idlescale.value * sin(cl.time * v_ipitch_cycle.value) * v_ipitch_level.value);
    cl.viewent.angles[YAW] -= static_cast<float>(v_idlescale.value * sin(cl.time * v_iyaw_cycle.value) * v_iyaw_level.value);
}

/*
==============
V_BoundOffsets
==============
*/
void V_BoundOffsets(void)
{
    entity_t* ent;

    ent = &cl_entities[cl.viewentity];

    // absolutely bound refresh reletive to entity clipping hull
    // so the view can never be inside a solid wall

    if (r_refdef.vieworg[0] < ent->origin[0] - 14) {
        r_refdef.vieworg[0] = ent->origin[0] - 14;
    } else if (r_refdef.vieworg[0] > ent->origin[0] + 14) {
        r_refdef.vieworg[0] = ent->origin[0] + 14;
    }

    if (r_refdef.vieworg[1] < ent->origin[1] - 14) {
        r_refdef.vieworg[1] = ent->origin[1] - 14;
    } else if (r_refdef.vieworg[1] > ent->origin[1] + 14) {
        r_refdef.vieworg[1] = ent->origin[1] + 14;
    }

    if (r_refdef.vieworg[2] < ent->origin[2] - 22) {
        r_refdef.vieworg[2] = ent->origin[2] - 22;
    } else if (r_refdef.vieworg[2] > ent->origin[2] + 30) {
        r_refdef.vieworg[2] = ent->origin[2] + 30;
    }
}

/*
==============
V_AddIdle

Idle swaying
==============
*/
void V_AddIdle(void)
{
    r_refdef.viewangles[ROLL] += static_cast<float>(v_idlescale.value * sin(cl.time * v_iroll_cycle.value) * v_iroll_level.value);
    r_refdef.viewangles[PITCH] += static_cast<float>(v_idlescale.value * sin(cl.time * v_ipitch_cycle.value) * v_ipitch_level.value);
    r_refdef.viewangles[YAW] += static_cast<float>(v_idlescale.value * sin(cl.time * v_iyaw_cycle.value) * v_iyaw_level.value);
}

/*
==============
V_CalcViewRoll

Roll is induced by movement and damage
==============
*/
void V_CalcViewRoll(void)
{
    float side;

    side = V_CalcRoll(cl_entities[cl.viewentity].angles, cl.velocity);
    r_refdef.viewangles[ROLL] += side;

    if (v_dmg_time > 0) {
        r_refdef.viewangles[ROLL] += v_dmg_time / v_kicktime.value * v_dmg_roll;
        r_refdef.viewangles[PITCH] += v_dmg_time / v_kicktime.value * v_dmg_pitch;
        v_dmg_time -= static_cast<float>(host_frametime);
    }

    if (cl.stats[STAT_HEALTH] <= 0) {
        r_refdef.viewangles[ROLL] = 80; // dead view angle

        return;
    }
}

/*
==================
V_CalcIntermissionRefdef

==================
*/
void V_CalcIntermissionRefdef(void)
{
    entity_t *ent, *view;
    float old;

    // ent is the player model (visible when out of body)
    ent = &cl_entities[cl.viewentity];
    // view is the weapon model (only visible from inside body)
    view = &cl.viewent;

    VectorCopy(ent->origin, r_refdef.vieworg);
    VectorCopy(ent->angles, r_refdef.viewangles);
    view->model = NULL;

    // allways idle in intermission
    old = v_idlescale.value;
    v_idlescale.value = 1;
    V_AddIdle();
    v_idlescale.value = old;
}

/*
==================
V_CalcRefdef

==================
*/
void V_CalcRefdef(void)
{
    entity_t *ent, *view;
    Vector3 v_forward, v_right, v_up;
    Vector3 angles;
    float bob;
    static float oldz = 0;

    V_DriftPitch();

    // ent is the player model (visible when out of body)
    ent = &cl_entities[cl.viewentity];
    // view is the weapon model (only visible from inside body)
    view = &cl.viewent;

    // transform the view offset by the model's matrix to get the offset from
    // model origin for the view
    ent->angles[YAW] = cl.viewangles[YAW]; // the model should face
    // the view dir
    ent->angles[PITCH] = -cl.viewangles[PITCH]; // the model should face
    // the view dir

    bob = V_CalcBob();

    // refresh position
    r_refdef.vieworg = ent->origin;
    r_refdef.vieworg.z += cl.viewheight + bob;

    // never let it sit exactly on a node line, because a water plane can
    // dissapear when viewed with the eye exactly on it.
    // the server protocol only specifies to 1/16 pixel, so add 1/32 in each axis
    r_refdef.vieworg += Vector3(1.0f / 32.0f, 1.0f / 32.0f, 1.0f / 32.0f);

    r_refdef.viewangles = cl.viewangles;
    V_CalcViewRoll();
    V_AddIdle();

    // offsets
    angles[PITCH] = -ent->angles[PITCH]; // because entity pitches are
    //  actually backward
    angles[YAW] = ent->angles[YAW];
    angles[ROLL] = ent->angles[ROLL];

    AngleVectors(angles, v_forward, v_right, v_up);

    r_refdef.vieworg += v_forward * scr_ofsx.value + v_right * scr_ofsy.value + v_up * scr_ofsz.value;

    V_BoundOffsets();

    // set up gun position
    view->angles = cl.viewangles;

    CalcGunAngle();

    view->origin = ent->origin;
    view->origin.z += cl.viewheight;

    view->origin += forward * (bob * 0.4f);
    view->origin.z += bob;

    // fudge position around to keep amount of weapon visible
    // roughly equal with different FOV

    float viewsize_val = Screen::GetScreenSystem().GetViewsize().value;
    if (viewsize_val == 110) {
        view->origin[2] += 1;
    } else if (viewsize_val == 100) {
        view->origin[2] += 2;
    } else if (viewsize_val == 90) {
        view->origin[2] += 1;
    } else if (viewsize_val == 80) {
        view->origin[2] += 0.5;
    }

    view->model = cl.model_precache[cl.stats[STAT_WEAPON]];
    view->frame = cl.stats[STAT_WEAPONFRAME];
    view->colormap = vid.colormap;

    // set up the refresh position
    VectorAdd(r_refdef.viewangles, cl.punchangle, r_refdef.viewangles);

    // smooth out stair step ups
    if (cl.onground && ent->origin[2] - oldz > 0) {
        float steptime;

        steptime = static_cast<float>(cl.time - cl.oldtime);
        if (steptime < 0) {
            //FIXME		I_Error ("steptime < 0");
            steptime = 0;
        }

        oldz += steptime * 80;
        if (oldz > ent->origin[2]) {
            oldz = ent->origin[2];
        }

        if (ent->origin[2] - oldz > 12) {
            oldz = ent->origin[2] - 12;
        }

        r_refdef.vieworg[2] += oldz - ent->origin[2];
        view->origin[2] += oldz - ent->origin[2];
    } else {
        oldz = ent->origin[2];
    }

    if (chase_active.value) {
        Chase_Update();
    }
}

/*
==================
V_RenderView

The player's clipping box goes from (-16 -16 -24) to (16 16 32) from
the entity origin, so any view position inside that will be valid
==================
*/
void V_RenderView(void)
{
    if (GetConsoleSystem().IsForcedUp()) {
        return;
    }

    // don't allow cheats in multiplayer
    if (cl.maxclients > 1) {
        Cvar::Set("scr_ofsx", "0");
        Cvar::Set("scr_ofsy", "0");
        Cvar::Set("scr_ofsz", "0");
    }

    if (cl.intermission) { // intermission / finale rendering
        V_CalcIntermissionRefdef();
    } else {
        if (!cl.paused /* && (sv.maxclients > 1 || key_dest == key_game) */) {
            V_CalcRefdef();
        }
    }

    R_PushDlights();

    if (lcd_x.value) {
        //
        // render two interleaved views
        //
        int i;

        vid.rowbytes <<= 1;
        vid.aspect *= 0.5;

        r_refdef.viewangles[YAW] -= lcd_yaw.value;
        for (i = 0; i < 3; i++) {
            r_refdef.vieworg[i] -= right[i] * lcd_x.value;
        }
        R_RenderView();

        vid.buffer += vid.rowbytes >> 1;

        R_PushDlights();

        r_refdef.viewangles[YAW] += lcd_yaw.value * 2;
        for (i = 0; i < 3; i++) {
            r_refdef.vieworg[i] += 2 * right[i] * lcd_x.value;
        }
        R_RenderView();

        vid.buffer -= vid.rowbytes >> 1;

        r_refdef.vrect.height <<= 1;

        vid.rowbytes >>= 1;
        vid.aspect *= 2;
    } else {
        R_RenderView();
    }

    if (crosshair.value) {
        const auto& vrect = Screen::GetScreenSystem().GetVrect();
        Draw_Character(static_cast<int>(vrect.x + vrect.width / 2 + cl_crossx.value),
            static_cast<int>(vrect.y + vrect.height / 2 + cl_crossy.value), '+');
    }

}

//============================================================================

/*
=============
V_Init
=============
*/
void V_Init(void)
{
    Cmd::AddCommand("v_cshift", V_cshift_f);
    Cmd::AddCommand("bf", V_BonusFlash_f);
    Cmd::AddCommand("centerview", V_StartPitchDrift);

    Cvar::Register(&lcd_x);
    Cvar::Register(&lcd_yaw);

    Cvar::Register(&v_centermove);
    Cvar::Register(&v_centerspeed);

    Cvar::Register(&v_iyaw_cycle);
    Cvar::Register(&v_iroll_cycle);
    Cvar::Register(&v_ipitch_cycle);
    Cvar::Register(&v_iyaw_level);
    Cvar::Register(&v_iroll_level);
    Cvar::Register(&v_ipitch_level);

    Cvar::Register(&v_idlescale);
    Cvar::Register(&crosshair);
    Cvar::Register(&cl_crossx);
    Cvar::Register(&cl_crossy);
    Cvar::Register(&gl_cshiftpercent);

    Cvar::Register(&scr_ofsx);
    Cvar::Register(&scr_ofsy);
    Cvar::Register(&scr_ofsz);
    Cvar::Register(&cl_rollspeed);
    Cvar::Register(&cl_rollangle);
    Cvar::Register(&cl_bob);
    Cvar::Register(&cl_bobcycle);
    Cvar::Register(&cl_bobup);

    Cvar::Register(&v_kicktime);
    Cvar::Register(&v_kickroll);
    Cvar::Register(&v_kickpitch);

    BuildGammaTable(1.0); // no gamma yet
    Cvar::Register(&v_gamma);
}

} // namespace View

short* d_pzbuffer = nullptr;
