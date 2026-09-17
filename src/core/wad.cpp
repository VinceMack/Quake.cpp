// wad.cpp -- WAD archive loader implementation
#include "core/wad.hpp"
#include "core/filesystem.hpp"
#include "core/endian.hpp"
#include "core/string_utils.hpp"
#include "core/math.hpp"
#include <array>
#include <vector>
#include <cctype>

namespace Wad {

int wad_numlumps = 0;
lumpinfo_t* wad_lumps = nullptr;
byte* wad_base = nullptr;
static std::vector<byte> wad_data;

void W_CleanupName(std::string_view in, std::span<char, 16> out)
{
    size_t i = 0, len = std::min(in.length(), static_cast<size_t>(16));
    for (; i < len && in[i] != '\0'; ++i) {
        out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(in[i])));
    }
    for (; i < 16; ++i) {
        out[i] = '\0';
    }
}

void W_LoadWadFile(std::string_view filename)
{
    std::string fname(filename.data(), filename.length());
    wad_data = Common::COM_LoadFile(fname.c_str());
    if (wad_data.empty()) Common::Sys_Error("W_LoadWadFile: couldn't load %s", fname.c_str());
    wad_base = wad_data.data();
    auto* header = reinterpret_cast<wadinfo_t*>(wad_base);
    if (header->identification[0] != 'W' || header->identification[1] != 'A' || header->identification[2] != 'D'
        || header->identification[3] != '2') {
        Common::Sys_Error("Wad file %s doesn't have WAD2 id\n", fname.c_str());
    }
    wad_numlumps = Common::LittleLong(header->numlumps);
    wad_lumps = reinterpret_cast<lumpinfo_t*>(wad_base + Common::LittleLong(header->infotableofs));
    lumpinfo_t* lump_p = wad_lumps;
    for (int i = 0; i < wad_numlumps; ++i, ++lump_p) {
        lump_p->filepos = Common::LittleLong(lump_p->filepos);
        lump_p->size = Common::LittleLong(lump_p->size);
        W_CleanupName(lump_p->name, std::span<char, 16>(lump_p->name, 16));
        if (lump_p->type == TYP_QPIC) SwapPic(reinterpret_cast<qpic_t*>(wad_base + lump_p->filepos));
    }
}

lumpinfo_t* W_GetLumpinfo(std::string_view name)
{
    std::array<char, 16> clean { };
    W_CleanupName(name, clean);
    lumpinfo_t* lump_p = wad_lumps;
    for (int i = 0; i < wad_numlumps; ++i, ++lump_p) {
        if (std::string_view(clean.data()) == lump_p->name) return lump_p;
    }
    std::string name_str(name.data(), name.length());
    Common::Sys_Error("W_GetLumpinfo: %s not found", name_str.c_str());
}

void* W_GetLumpName(std::string_view name)
{
    lumpinfo_t* lump = W_GetLumpinfo(name);
    return reinterpret_cast<void*>(wad_base + lump->filepos);
}

void SwapPic(qpic_t* pic)
{
    if (pic != nullptr) {
        pic->width = Common::LittleLong(pic->width);
        pic->height = Common::LittleLong(pic->height);
    }
}

} // namespace Wad
