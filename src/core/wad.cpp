// wad.cpp -- WAD archive loader implementation
#include "quakedef.hpp"
#include "core/wad.hpp"
#include "core/filesystem.hpp"
#include "core/endian.hpp"
#include "core/string_utils.hpp"
#include <EASTL/array.h>
#include <cctype>

namespace Wad {

int wad_numlumps = 0;
lumpinfo_t* wad_lumps = nullptr;
byte* wad_base = nullptr;

void W_CleanupName(eastl::string_view in, eastl::span<char, 16> out) {
    size_t i = 0, len = eastl::min(in.length(), static_cast<size_t>(16));
    for (; i < len && in[i] != '\0'; ++i) {
        out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(in[i])));
    }
    for (; i < 16; ++i) {
        out[i] = '\0';
    }
}

void W_LoadWadFile(eastl::string_view filename) {
    eastl::string fname(filename.data(), filename.length());
    wad_base = static_cast<byte*>(Common::COM_LoadHunkFile(fname.c_str()));
    if (!wad_base) Common::Sys_Error("W_LoadWadFile: couldn't load %s", fname.c_str());
    auto* header = reinterpret_cast<wadinfo_t*>(wad_base);
    if (header->identification[0] != 'W' || header->identification[1] != 'A' ||
        header->identification[2] != 'D' || header->identification[3] != '2') {
        Common::Sys_Error("Wad file %s doesn't have WAD2 id\n", fname.c_str());
    }
    wad_numlumps = Common::LittleLong(header->numlumps);
    wad_lumps = reinterpret_cast<lumpinfo_t*>(wad_base + Common::LittleLong(header->infotableofs));
    lumpinfo_t* lump_p = wad_lumps;
    for (int i = 0; i < wad_numlumps; ++i, ++lump_p) {
        lump_p->filepos = Common::LittleLong(lump_p->filepos);
        lump_p->size = Common::LittleLong(lump_p->size);
        W_CleanupName(lump_p->name, eastl::span<char, 16>(lump_p->name, 16));
        if (lump_p->type == TYP_QPIC) SwapPic(reinterpret_cast<qpic_t*>(wad_base + lump_p->filepos));
    }
}

lumpinfo_t* W_GetLumpinfo(eastl::string_view name) {
    eastl::array<char, 16> clean{};
    W_CleanupName(name, clean);
    lumpinfo_t* lump_p = wad_lumps;
    for (int i = 0; i < wad_numlumps; ++i, ++lump_p) {
        if (eastl::string_view(clean.data()) == lump_p->name) return lump_p;
    }
    eastl::string name_str(name.data(), name.length());
    Common::Sys_Error("W_GetLumpinfo: %s not found", name_str.c_str());
}

void* W_GetLumpName(eastl::string_view name) {
    lumpinfo_t* lump = W_GetLumpinfo(name);
    return reinterpret_cast<void*>(wad_base + lump->filepos);
}

void SwapPic(qpic_t* pic) {
    if (pic != nullptr) {
        pic->width = Common::LittleLong(pic->width);
        pic->height = Common::LittleLong(pic->height);
    }
}

} // namespace Wad
