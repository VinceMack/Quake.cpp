// wad.cpp -- WAD archive loading and lump management

#include "quakedef.hpp"

using namespace CDAudio;
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

int wad_numlumps;
lumpinfo_t* wad_lumps;
byte* wad_base;

void SwapPic(qpic_t* pic);

/*
==================
W_CleanupName

Lowercases name and pads with spaces and a terminating 0 to the length of
lumpinfo_t->name.
Used so lumpname lookups can proceed rapidly by comparing 4 chars at a time
Space padding is so names can be printed nicely in tables.
Can safely be performed in place.
==================
*/
void W_CleanupName(const char* in, char* out)
{
    int i;
    int c;

    for (i = 0; i < 16; i++) {
        c = in[i];
        if (!c) {
            break;
        }

        if (c >= 'A' && c <= 'Z') {
            c += ('a' - 'A');
        }

        out[i] = static_cast<char>(c);
    }

    for (; i < 16; i++) {
        out[i] = static_cast<char>(0);
    }
}

/*
====================
W_LoadWadFile
====================
*/
void W_LoadWadFile(const char* filename)
{
    lumpinfo_t* lump_p;
    wadinfo_t* header;
    unsigned i;
    int infotableofs;

    wad_base = COM_LoadHunkFile(filename);
    if (!wad_base) {
        Sys_Error("W_LoadWadFile: couldn't load %s", filename);
    }

    header = (wadinfo_t*)wad_base;

    if (header->identification[0] != 'W' || header->identification[1] != 'A' || header->identification[2] != 'D' || header->identification[3] != '2') {
        Sys_Error("Wad file %s doesn't have WAD2 id\n", filename);
    }

    wad_numlumps = LittleLong(header->numlumps);
    infotableofs = LittleLong(header->infotableofs);
    wad_lumps = (lumpinfo_t*)(wad_base + infotableofs);

    for (i = 0, lump_p = wad_lumps; i < (unsigned)wad_numlumps; i++, lump_p++) {
        lump_p->filepos = LittleLong(lump_p->filepos);
        lump_p->size = LittleLong(lump_p->size);
        W_CleanupName(lump_p->name, lump_p->name);
        if (lump_p->type == TYP_QPIC) {
            SwapPic((qpic_t*)(wad_base + lump_p->filepos));
        }
    }
}

/*
=============
W_GetLumpinfo
=============
*/
lumpinfo_t* W_GetLumpinfo(const char* name)
{
    int i;
    lumpinfo_t* lump_p;
    char clean[16];

    W_CleanupName(name, clean);

    for (lump_p = wad_lumps, i = 0; i < wad_numlumps; i++, lump_p++) {
        if (!strcmp(clean, lump_p->name)) {
            return lump_p;
        }
    }

    Sys_Error("W_GetLumpinfo: %s not found", name);
}

void* W_GetLumpName(const char* name)
{
    lumpinfo_t* lump;

    lump = W_GetLumpinfo(name);

    return (void*)(wad_base + lump->filepos);
}

/*
=============================================================================

automatic byte swapping

=============================================================================
*/

void SwapPic(qpic_t* pic)
{
    pic->width = LittleLong(pic->width);
    pic->height = LittleLong(pic->height);
}

} // namespace Wad
