// wad.hpp -- WAD file format structures
#pragma once

#include "quakedef.hpp"
#include <EASTL/string_view.h>
#include <EASTL/span.h>

//===============
//   TYPES
//===============

enum class WadCompression : uint8_t {
    None = 0,
    LZSS = 1
};

enum class LumpType : uint8_t {
    None = 0,
    Label = 1,
    Lumpy = 64,
    Palette = 64,
    QTex = 65,
    QPic = 66,
    Sound = 67,
    MipTex = 68
};

constexpr int TYP_QPIC = 66;

#pragma pack(push, 1)
typedef struct {
    int width, height;
    byte data[4]; // variably sized
} qpic_t;

typedef struct {
    char identification[4]; // should be WAD2 or 2DAW
    int numlumps;
    int infotableofs;
} wadinfo_t;

typedef struct {
    int filepos;
    int disksize;
    int size; // uncompressed
    char type;
    char compression;
    char pad1, pad2;
    char name[16]; // must be null terminated
} lumpinfo_t;
#pragma pack(pop)

namespace Wad {

extern int wad_numlumps;
extern lumpinfo_t* wad_lumps;
extern byte* wad_base;

void W_LoadWadFile(eastl::string_view filename);
void W_CleanupName(eastl::string_view in, eastl::span<char, 16> out);
[[nodiscard]] lumpinfo_t* W_GetLumpinfo(eastl::string_view name);
[[nodiscard]] void* W_GetLumpName(eastl::string_view name);

void SwapPic(qpic_t* pic);

} // namespace Wad

