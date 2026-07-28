// wad.cpp -- WAD archive loading and lump management

#include "quakedef.hpp"
#include <EASTL/array.h>
#include <cctype>

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

namespace Wad {

int wad_numlumps = 0;
lumpinfo_t* wad_lumps = nullptr;
byte* wad_base = nullptr;

void SwapPic(qpic_t* pic);

/*
==================
W_CleanupName

Lowercases name and pads with spaces and a terminating 0 to the length of
lumpinfo_t->name.
Used so lumpname lookups can proceed rapidly by comparing 4 chars at a time.
Can safely be performed in place.
==================
*/
void W_CleanupName(eastl::string_view in, eastl::span<char, 16> out)
{
    size_t i = 0;
    const size_t len = eastl::min(in.length(), static_cast<size_t>(16));

    for (; i < len; ++i) {
        if (in[i] == '\0') {
            break;
        }
        out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(in[i])));
    }

    for (; i < 16; ++i) {
        out[i] = '\0';
    }
}

/*
====================
W_LoadWadFile
====================
*/
void W_LoadWadFile(eastl::string_view filename)
{
    eastl::string fname(filename.data(), filename.length());
    wad_base = static_cast<byte*>(COM_LoadHunkFile(fname.c_str()));
    if (!wad_base) {
        Sys_Error("W_LoadWadFile: couldn't load %s", fname.c_str());
    }

    auto* header = reinterpret_cast<wadinfo_t*>(wad_base);

    if (header->identification[0] != 'W' || header->identification[1] != 'A' ||
        header->identification[2] != 'D' || header->identification[3] != '2') {
        Sys_Error("Wad file %s doesn't have WAD2 id\n", fname.c_str());
    }

    wad_numlumps = LittleLong(header->numlumps);
    const int infotableofs = LittleLong(header->infotableofs);
    wad_lumps = reinterpret_cast<lumpinfo_t*>(wad_base + infotableofs);

    lumpinfo_t* lump_p = wad_lumps;
    for (int i = 0; i < wad_numlumps; ++i, ++lump_p) {
        lump_p->filepos = LittleLong(lump_p->filepos);
        lump_p->size = LittleLong(lump_p->size);
        W_CleanupName(lump_p->name, eastl::span<char, 16>(lump_p->name, 16));
        if (lump_p->type == TYP_QPIC) {
            SwapPic(reinterpret_cast<qpic_t*>(wad_base + lump_p->filepos));
        }
    }
}

/*
=============
W_GetLumpinfo
=============
*/
lumpinfo_t* W_GetLumpinfo(eastl::string_view name)
{
    eastl::array<char, 16> clean{};
    W_CleanupName(name, clean);

    lumpinfo_t* lump_p = wad_lumps;
    for (int i = 0; i < wad_numlumps; ++i, ++lump_p) {
        if (eastl::string_view(clean.data()) == lump_p->name) {
            return lump_p;
        }
    }

    eastl::string name_str(name.data(), name.length());
    Sys_Error("W_GetLumpinfo: %s not found", name_str.c_str());
}

void* W_GetLumpName(eastl::string_view name)
{
    lumpinfo_t* lump = W_GetLumpinfo(name);
    return reinterpret_cast<void*>(wad_base + lump->filepos);
}

/*
=============================================================================

automatic byte swapping

=============================================================================
*/

void SwapPic(qpic_t* pic)
{
    if (pic != nullptr) {
        pic->width = LittleLong(pic->width);
        pic->height = LittleLong(pic->height);
    }
}

} // namespace Wad

