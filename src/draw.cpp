// draw.cpp -- this is the only file outside the refresh that touches the
// vid buffer

#include "quakedef.hpp"
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <algorithm>
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


struct RectDesc {
    vrect_t rect;
    int width;
    int height;
    byte* ptexbytes;
    int rowbytes;
};

static RectDesc r_rectdesc;

namespace Draw {

byte* draw_chars; // 8*8 graphic characters
qpic_t* draw_disc;
qpic_t* draw_backtile;

//=============================================================================
/* Support Routines */

struct CachePic {
    eastl::string name;
    cache_user_t cache{};
};

static eastl::vector<eastl::unique_ptr<CachePic>> menu_cachepics;

/*
================
Draw_CachePic
================
*/
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
            if (!dat) {
                Sys_Error("Draw_CachePic: failed to load %s", path_str.c_str());
            }
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
    if (!dat) {
        Sys_Error("Draw_CachePic: failed to load %s", path_str.c_str());
    }
    SwapPic(dat);

    return dat;
}

/*
===============
Draw_Init
===============
*/
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

/*
================
Draw_Character

Draws one 8*8 graphics character with 0 being transparent.
It can be clipped to the top of the screen to allow the console to be
smoothly scrolled off.
================
*/
void Draw_Character(int x, int y, int num)
{
    num &= 255;

    if (y <= -8) {
        return; // totally off screen
    }

#ifdef PARANOID
    if (y > vid.height - 8 || x < 0 || x > vid.width - 8) {
        Sys_Error("Con_DrawCharacter: (%i, %i)", x, y);
    }

    if (num < 0 || num > 255) {
        Sys_Error("Con_DrawCharacter: char %i", num);
    }
#endif

    const int row = num >> 4;
    const int col = num & 15;
    const byte* source = draw_chars + (row << 10) + (col << 3);

    int drawline;
    if (y < 0) { // clipped
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
                if (source[i]) {
                    dest[i] = source[i];
                }
            }
            source += 128;
            dest += vid.conrowbytes;
        }
    } else {
        // FIXME: pre-expand to native format?
        auto* pusdest = (unsigned short*)((byte*)vid.conbuffer + y * vid.conrowbytes + (x << 1));

        while (drawline--) {
            for (int i = 0; i < 8; ++i) {
                if (source[i]) {
                    pusdest[i] = d_8to16table[source[i]];
                }
            }
            source += 128;
            pusdest += (vid.conrowbytes >> 1);
        }
    }
}

/*
================
Draw_String
================
*/
void Draw_String(int x, int y, eastl::string_view str)
{
    for (const char c : str) {
        Draw_Character(x, y, c);
        x += 8;
    }
}

/*
=============
Draw_Pic
=============
*/
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
        // FIXME: pretranslate at load time?
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

/*
=============
Draw_TransPic
=============
*/
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
                if (const byte tbyte = source[u]; tbyte != TRANSPARENT_COLOR) {
                    dest[u] = tbyte;
                }
            }

            dest += vid.rowbytes;
            source += pic->width;
        }
    } else {
        // FIXME: pretranslate at load time?
        auto* pusdest = (unsigned short*)vid.buffer + y * (vid.rowbytes >> 1) + x;

        for (int v = 0; v < pic->height; v++) {
            for (int u = 0; u < pic->width; u++) {
                if (const byte tbyte = source[u]; tbyte != TRANSPARENT_COLOR) {
                    pusdest[u] = d_8to16table[tbyte];
                }
            }

            pusdest += vid.rowbytes >> 1;
            source += pic->width;
        }
    }
}

/*
=============
Draw_TransPicTranslate
=============
*/
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
                if (const byte tbyte = source[u]; tbyte != TRANSPARENT_COLOR) {
                    dest[u] = translation[tbyte];
                }
            }

            dest += vid.rowbytes;
            source += pic->width;
        }
    } else {
        // FIXME: pretranslate at load time?
        auto* pusdest = (unsigned short*)vid.buffer + y * (vid.rowbytes >> 1) + x;

        for (int v = 0; v < pic->height; v++) {
            for (int u = 0; u < pic->width; u++) {
                if (const byte tbyte = source[u]; tbyte != TRANSPARENT_COLOR) {
                    pusdest[u] = d_8to16table[tbyte];
                }
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
            if (source[x]) {
                dest[x] = 0x60 + source[x];
            }
        }
        source += 128;
        dest += 320;
    }
}

/*
================
Draw_ConsoleBackground

================
*/
void Draw_ConsoleBackground(int lines)
{
    qpic_t* conback = Draw_CachePic("gfx/conback.lmp");

    // hack the version number directly into the pic
    byte* dest = conback->data + 320 - 43 + 320 * 186;
    char ver[100];
    std::snprintf(ver, sizeof(ver), "%4.2f", VERSION);

    const std::string_view ver_view(ver);
    for (size_t x = 0; x < ver_view.length(); x++) {
        Draw_CharToConback(ver_view[x], dest + (x << 3));
    }

    // draw the pic
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
                    dest[x] = src[f >> 16];
                    f += fstep;
                    dest[x + 1] = src[f >> 16];
                    f += fstep;
                    dest[x + 2] = src[f >> 16];
                    f += fstep;
                    dest[x + 3] = src[f >> 16];
                    f += fstep;
                }
            }
        }
    } else {
        auto* pusdest = (unsigned short*)vid.conbuffer;

        for (int y = 0; y < lines; y++, pusdest += (vid.conrowbytes >> 1)) {
            // FIXME: pre-expand to native format?
            // FIXME: does the endian switching go away in production?
            const int v = (vid.conheight - lines + y) * 200 / vid.conheight;
            const byte* src = conback->data + v * 320;
            int f = 0;
            const int fstep = 320 * 0x10000 / vid.conwidth;
            for (int x = 0; x < (int)vid.conwidth; x += 4) {
                pusdest[x] = d_8to16table[src[f >> 16]];
                f += fstep;
                pusdest[x + 1] = d_8to16table[src[f >> 16]];
                f += fstep;
                pusdest[x + 2] = d_8to16table[src[f >> 16]];
                f += fstep;
                pusdest[x + 3] = d_8to16table[src[f >> 16]];
                f += fstep;
            }
        }
    }
}

/*
==============
R_DrawRect8
==============
*/
void R_DrawRect8(const vrect_t* prect, int rowbytes, const byte* psrc, bool transparent)
{
    byte* pdest = vid.buffer + (prect->y * vid.rowbytes) + prect->x;

    const int srcdelta = rowbytes - prect->width;
    const int destdelta = vid.rowbytes - prect->width;

    if (transparent) {
        for (int i = 0; i < prect->height; i++) {
            for (int j = 0; j < prect->width; j++) {
                if (const byte t = *psrc; t != TRANSPARENT_COLOR) {
                    *pdest = t;
                }
                psrc++;
                pdest++;
            }

            psrc += srcdelta;
            pdest += destdelta;
        }
    } else {
        for (int i = 0; i < prect->height; i++) {
            std::memcpy(pdest, psrc, prect->width);
            psrc += rowbytes;
            pdest += vid.rowbytes;
        }
    }
}

/*
==============
R_DrawRect16
==============
*/
void R_DrawRect16(const vrect_t* prect, int rowbytes, const byte* psrc, bool transparent)
{
    // FIXME: would it be better to pre-expand native-format versions?
    auto* pdest = (unsigned short*)vid.buffer + (prect->y * (vid.rowbytes >> 1)) + prect->x;

    const int srcdelta = rowbytes - prect->width;
    const int destdelta = (vid.rowbytes >> 1) - prect->width;

    if (transparent) {
        for (int i = 0; i < prect->height; i++) {
            for (int j = 0; j < prect->width; j++) {
                if (const byte t = *psrc; t != TRANSPARENT_COLOR) {
                    *pdest = d_8to16table[t];
                }
                psrc++;
                pdest++;
            }

            psrc += srcdelta;
            pdest += destdelta;
        }
    } else {
        for (int i = 0; i < prect->height; i++) {
            for (int j = 0; j < prect->width; j++) {
                *pdest = d_8to16table[*psrc];
                psrc++;
                pdest++;
            }

            psrc += srcdelta;
            pdest += destdelta;
        }
    }
}

/*
=============
Draw_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
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

        if (tileoffsety != 0) {
            vr.height = r_rectdesc.height - tileoffsety;
        } else {
            vr.height = r_rectdesc.height;
        }

        if (vr.height > height) {
            vr.height = height;
        }

        int tileoffsetx = vr.x % r_rectdesc.width;

        while (width > 0) {
            if (tileoffsetx != 0) {
                vr.width = r_rectdesc.width - tileoffsetx;
            } else {
                vr.width = r_rectdesc.width;
            }

            if (vr.width > width) {
                vr.width = width;
            }

            const byte* psrc = r_rectdesc.ptexbytes + (tileoffsety * r_rectdesc.rowbytes) + tileoffsetx;

            if (r_pixbytes == 1) {
                R_DrawRect8(&vr, r_rectdesc.rowbytes, psrc, false);
            } else {
                R_DrawRect16(&vr, r_rectdesc.rowbytes, psrc, false);
            }

            vr.x += vr.width;
            width -= vr.width;
            tileoffsetx = 0; // only the left tile can be left-clipped
        }

        vr.y += vr.height;
        height -= vr.height;
        tileoffsety = 0; // only the top tile can be top-clipped
    }
}

/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
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

//=============================================================================

/*
================
Draw_FadeScreen

================
*/
void Draw_FadeScreen()
{
    VID_UnlockBuffer();
    S_ExtraUpdate();
    VID_LockBuffer();

    for (int y = 0; y < static_cast<int>(vid.height); y++) {
        byte* pbuf = vid.buffer + vid.rowbytes * y;
        const int t = (y & 1) << 1;

        for (int x = 0; x < static_cast<int>(vid.width); x++) {
            if ((x & 3) != t) {
                pbuf[x] = 0;
            }
        }
    }

    VID_UnlockBuffer();
    S_ExtraUpdate();
    VID_LockBuffer();
}

//=============================================================================

/*
================
Draw_BeginDisc

Draws the little blue disc in the corner of the screen.
Call before beginning any disc IO.
================
*/
void Draw_BeginDisc()
{
    D_BeginDirectRect(vid.width - 24, 0, draw_disc->data, 24, 24);
}

/*
================
Draw_EndDisc

Erases the disc icon.
Call after completing any disc IO
================
*/
void Draw_EndDisc()
{
    D_EndDirectRect(vid.width - 24, 0, 24, 24);
}

} // namespace Draw
