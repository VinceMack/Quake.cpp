// wad.hpp -- WAD archive lump format and extraction
#pragma once

#include "core/types.hpp"
#include <cstdint>
#include <string_view>
#include <span>

enum class WadCompression : uint8_t { None = 0, LZSS = 1 };

enum class LumpType : uint8_t {
    None = 0, Label = 1, Lumpy = 64, Palette = 64, QTex = 65, QPic = 66, Sound = 67, MipTex = 68
};

constexpr int TYP_QPIC = 66;

#pragma pack(push, 1)
struct qpic_t {
    int width, height;
    byte data[4];
};

struct wadinfo_t {
    char identification[4];
    int numlumps;
    int infotableofs;
};

struct lumpinfo_t {
    int filepos;
    int disksize;
    int size;
    char type;
    char compression;
    char pad1, pad2;
    char name[16];
};
#pragma pack(pop)

namespace Wad {

extern int wad_numlumps;
extern lumpinfo_t* wad_lumps;
extern byte* wad_base;

void W_LoadWadFile(std::string_view filename);
void W_CleanupName(std::string_view in, std::span<char, 16> out);
[[nodiscard]] lumpinfo_t* W_GetLumpinfo(std::string_view name);
[[nodiscard]] void* W_GetLumpName(std::string_view name);

void SwapPic(qpic_t* pic);

} // namespace Wad
