// draw2d.cpp -- 2D raster drawing primitives, fonts, pics & caching
#include "quakedef.hpp"
#include "render/draw2d.hpp"
#include "sys_render.hpp"
#include "sys_audio.hpp"

#include <EASTL/vector.h>
#include <EASTL/memory.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <algorithm>
#include <cstring>
#include <cstdio>

using namespace Common;
using namespace Render;
using namespace Draw;
using namespace Host;
using namespace Vid;
using namespace Wad;
using namespace Audio;

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

static qpic_t* LoadCachePic(CachePic& pic)
{
    if (auto* data = static_cast<qpic_t*>(Cache_Check(&pic.cache))) return data;
    COM_LoadCacheFile(pic.name.c_str(), &pic.cache);
    auto* data = static_cast<qpic_t*>(pic.cache.data);
    if (!data) Sys_Error("Draw_CachePic: failed to load %s", pic.name.c_str());
    SwapPic(data);
    return data;
}

qpic_t* Draw_CachePic(eastl::string_view path)
{
    for (const auto& pic : menu_cachepics) {
        if (eastl::string_view(pic->name.data(), pic->name.length()) == path) {
            return LoadCachePic(*pic);
        }
    }
    auto new_pic = eastl::make_unique<CachePic>();
    new_pic->name.assign(path.data(), path.length());
    CachePic& pic = *new_pic;
    menu_cachepics.push_back(eastl::move(new_pic));
    return LoadCachePic(pic);
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

template<bool Trans, bool Translate>
static inline void Draw_Pic_Impl(int x, int y, qpic_t* pic, const byte* translation = nullptr)
{
    if (x < 0 || (unsigned)(x + pic->width) > vid.width || y < 0 || (unsigned)(y + pic->height) > vid.height) {
        Sys_Error("Draw_Pic: bad coordinates");
    }
    const byte* source = pic->data;
    if (r_pixbytes == 1) {
        byte* dest = vid.buffer + y * vid.rowbytes + x;
        for (int v = 0; v < pic->height; v++, dest += vid.rowbytes, source += pic->width) {
            if constexpr (!Trans) {
                std::memcpy(dest, source, pic->width);
            } else {
                for (int u = 0; u < pic->width; u++) {
                    if (const byte tbyte = source[u]; tbyte != TRANSPARENT_COLOR) {
                        dest[u] = Translate ? translation[tbyte] : tbyte;
                    }
                }
            }
        }
    } else {
        auto* pusdest = (unsigned short*)vid.buffer + y * (vid.rowbytes >> 1) + x;
        for (int v = 0; v < pic->height; v++, pusdest += vid.rowbytes >> 1, source += pic->width) {
            for (int u = 0; u < pic->width; u++) {
                const byte tbyte = source[u];
                if constexpr (Trans) {
                    if (tbyte == TRANSPARENT_COLOR) continue;
                }
                const byte final_byte = (Trans && Translate) ? translation[tbyte] : tbyte;
                pusdest[u] = d_8to16table[final_byte];
            }
        }
    }
}

void Draw_Pic(int x, int y, qpic_t* pic)
{
    Draw_Pic_Impl<false, false>(x, y, pic);
}

void Draw_TransPic(int x, int y, qpic_t* pic)
{
    Draw_Pic_Impl<true, false>(x, y, pic);
}

void Draw_TransPicTranslate(int x, int y, qpic_t* pic, const byte* translation)
{
    Draw_Pic_Impl<true, true>(x, y, pic, translation);
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

template<typename T, bool Transparent>
static inline void R_DrawRect_T(const vrect_t* prect, int rowbytes, const byte* psrc, const T* table = nullptr)
{
    auto* pdest = reinterpret_cast<T*>(vid.buffer) + (prect->y * (vid.rowbytes / sizeof(T))) + prect->x;
    const int srcdelta = rowbytes - prect->width;
    const int destdelta = (vid.rowbytes / sizeof(T)) - prect->width;
    for (int i = 0; i < prect->height; i++) {
        for (int j = 0; j < prect->width; j++) {
            if (const byte t = *psrc; !Transparent || t != TRANSPARENT_COLOR) {
                *pdest = table ? table[t] : static_cast<T>(t);
            }
            psrc++; pdest++;
        }
        psrc += srcdelta; pdest += destdelta;
    }
}

void R_DrawRect8(const vrect_t* prect, int rowbytes, const byte* psrc, bool transparent)
{
    if (transparent) {
        R_DrawRect_T<byte, true>(prect, rowbytes, psrc);
    } else {
        byte* pdest = vid.buffer + (prect->y * vid.rowbytes) + prect->x;
        for (int i = 0; i < prect->height; i++, psrc += rowbytes, pdest += vid.rowbytes) {
            std::memcpy(pdest, psrc, prect->width);
        }
    }
}

void R_DrawRect16(const vrect_t* prect, int rowbytes, const byte* psrc, bool transparent)
{
    if (transparent) {
        R_DrawRect_T<unsigned short, true>(prect, rowbytes, psrc, d_8to16table);
    } else {
        R_DrawRect_T<unsigned short, false>(prect, rowbytes, psrc, d_8to16table);
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
