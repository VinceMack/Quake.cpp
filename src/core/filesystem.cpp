// filesystem.cpp -- Virtual filesystem, PAK archive manager, search paths, and file streaming
#include "core/filesystem.hpp"
#include "core/types.hpp"
#include "core/endian.hpp"
#include "core/string_utils.hpp"
#include "core/cvar.hpp"
#include "core/cmd.hpp"
#include "platform/system.hpp"
#include "render/draw2d.hpp"
#include "host/host.hpp"
#include "ui/console.hpp"
#include "quakedef.hpp"

#include <array>
#include <memory>
#include <utility>
#include <vector>
#include <SDL.h>

namespace Common {

//=============================================================================
// Endian function pointers & definitions
//=============================================================================

bool bigendien = false;
short (*BigShort)(short l) = nullptr;
short (*LittleShort)(short l) = nullptr;
int (*BigLong)(int l) = nullptr;
int (*LittleLong)(int l) = nullptr;
float (*BigFloat)(float l) = nullptr;
float (*LittleFloat)(float l) = nullptr;

//=============================================================================
// Command line and state variables
//=============================================================================

constexpr size_t NUM_SAFE_ARGVS = 3;
static char* largv[MAX_NUM_ARGVS + NUM_SAFE_ARGVS + 1];
static const char* argvdummy = " ";

static constexpr std::array<const char*, NUM_SAFE_ARGVS> safeargvs = { "-nolan", "-nosound", "-nomouse" };

bool proghack = false;
bool msg_suppress_1 = false;
bool com_eof = false;

char com_token[1024];
int com_argc = 0;
char** com_argv = nullptr;

constexpr size_t CMDLINE_LENGTH = 256;
char com_cmdline[CMDLINE_LENGTH];

bool standard_quake = true, rogue = false, hipnotic = false;

std::string_view COM_FileExtension(std::string_view in) {
    auto dot_pos = in.find('.');
    return (dot_pos == std::string_view::npos) ? "" : in.substr(dot_pos + 1, 7);
}

void COM_FileBase(const char* in, char* out) {
    std::string_view path(in);
    auto dot_pos = path.find_last_of('.');
    if (dot_pos == std::string_view::npos) { strcpy_s(out, 32, "?model?"); return; }
    auto filename = path.substr(0, dot_pos);
    auto last_slash = filename.find_last_of("/\\");
    std::string_view base = (last_slash == std::string_view::npos) ? filename : filename.substr(last_slash + 1);
    if (base.empty()) { strcpy_s(out, 32, "?model?"); }
    else {
        size_t len = std::min(base.size(), size_t{31});
        std::memcpy(out, base.data(), len); out[len] = '\0';
    }
}

void COM_DefaultExtension(char* path, const char* extension) {
    std::string_view p(path);
    auto last_slash = p.find_last_of("/\\");
    std::string_view fn = (last_slash == std::string_view::npos) ? p : p.substr(last_slash + 1);
    if (fn.find('.') == std::string_view::npos) strcat_s(path, 256, extension);
}

const char* COM_Parse(const char* data) {
    if (!data) return nullptr;
    int len = 0; com_token[0] = '\0';
    while (true) {
        while (*data && static_cast<unsigned char>(*data) <= ' ') data++;
        if (*data == '\0') return nullptr;
        if (data[0] == '/' && data[1] == '/') {
            while (*data && *data != '\n') data++;
            continue;
        }
        break;
    }
    char c = *data;
    if (c == '\"') {
        data++;
        while (true) {
            c = *data++;
            if (c == '\"' || c == '\0') { com_token[len] = '\0'; return data; }
            if (len < static_cast<int>(sizeof(com_token)) - 1) com_token[len++] = c;
        }
    }
    if (c == '{' || c == '}' || c == ')' || c == '(' || c == '\'' || c == ':') {
        com_token[0] = c; com_token[1] = '\0'; return data + 1;
    }
    while (true) {
        c = *data;
        if (c == '\0' || static_cast<unsigned char>(c) <= ' ' || c == '{' || c == '}' || c == ')' || c == '(' || c == '\'' || c == ':') break;
        if (len < static_cast<int>(sizeof(com_token)) - 1) com_token[len++] = c;
        data++;
    }
    com_token[len] = '\0';
    return data;
}

int COM_CheckParm(const char* parm) {
    for (int i = 1; i < com_argc; i++) {
        if (com_argv[i] && !Q_strcmp(parm, com_argv[i])) return i;
    }
    return 0;
}

void COM_InitArgv(int argc, char** argv) {
    int n = 0;
    for (int j = 0; j < MAX_NUM_ARGVS && j < argc; ++j) {
        for (char c : std::string_view(argv[j])) {
            if (n >= static_cast<int>(CMDLINE_LENGTH - 1)) break;
            com_cmdline[n++] = c;
        }
        if (n >= static_cast<int>(CMDLINE_LENGTH - 1)) break;
        com_cmdline[n++] = ' ';
    }
    com_cmdline[n] = '\0';
    bool safe = false;
    for (com_argc = 0; (com_argc < MAX_NUM_ARGVS) && (com_argc < argc); com_argc++) {
        largv[com_argc] = argv[com_argc];
        if (std::string_view(argv[com_argc]) == "-safe") safe = true;
    }
    if (safe) {
        for (size_t i = 0; i < NUM_SAFE_ARGVS; i++) largv[com_argc++] = const_cast<char*>(safeargvs[i]);
    }
    largv[com_argc] = const_cast<char*>(argvdummy); com_argv = largv;
    if (COM_CheckParm("-rogue")) { rogue = true; standard_quake = false; }
    if (COM_CheckParm("-hipnotic")) { hipnotic = true; standard_quake = false; }
}

void COM_Init() {
#ifdef SDL
    if (SDL_BYTEORDER == SDL_LIL_ENDIAN)
#else
    byte swaptest[2] = { 1, 0 };
    if (*reinterpret_cast<short*>(swaptest) == 1)
#endif
    {
        bigendien = false; BigShort = ShortSwap; LittleShort = ShortNoSwap;
        BigLong = LongSwap; LittleLong = LongNoSwap; BigFloat = FloatSwap; LittleFloat = FloatNoSwap;
    } else {
        bigendien = true; BigShort = ShortNoSwap; LittleShort = ShortSwap;
        BigLong = LongNoSwap; LittleLong = LongSwap; BigFloat = FloatNoSwap; LittleFloat = FloatSwap;
    }
    Cvar::Register(&registered); Cvar::Register(&cmdline);
    Cmd::AddCommand("path", COM_Path_f);
    COM_InitFilesystem();

    // QuakeC reads these; stock progs gate the later episodes on "registered".
    Cvar::Set("cmdline", com_cmdline);
    Cvar::Set("registered", "1");
}

int com_filesize = 0;

struct packfile_t { char name[MAX_QPATH]; int filepos, filelen; };
struct pack_t { char filename[MAX_OSPATH]; int handle; std::vector<packfile_t> files; };
struct dpackfile_t { char name[56]; int filepos, filelen; };
struct dpackheader_t { char id[4]; int dirofs; int dirlen; };

char com_gamedir[MAX_OSPATH];

struct SearchPath { std::string filename; std::unique_ptr<pack_t> pack; };
static std::vector<SearchPath> com_searchpaths;

void COM_Path_f(void) {
    Console::Con_Printf("Current search path:\n");
    for (const auto& s : com_searchpaths) {
        if (s.pack) Console::Con_Printf("%s (%i files)\n", s.pack->filename, static_cast<int>(s.pack->files.size()));
        else Console::Con_Printf("%s\n", s.filename.c_str());
    }
}

void COM_WriteFile(const char* filename, void* data, int len) {
    char name[MAX_OSPATH];
    sprintf_s(name, sizeof(name), "%s/%s", com_gamedir, filename);
    int handle = Sys_FileOpenWrite(name);
    if (handle == -1) { Sys_Printf("COM_WriteFile: failed on %s\n", name); return; }
    Sys_Printf("COM_WriteFile: %s\n", name);
    Sys_FileWrite(handle, data, len); Sys_FileClose(handle);
}

int COM_FindFile(const char* filename, int* handle, FILE** file) {
    char netpath[MAX_OSPATH];
    if (file && handle) Sys_Error("COM_FindFile: both handle and file set");
    if (!file && !handle) Sys_Error("COM_FindFile: neither handle or file set");

    auto it = com_searchpaths.begin();
    if (proghack && std::strcmp(filename, "progs.dat") == 0 && it != com_searchpaths.end()) ++it;

    for (; it != com_searchpaths.end(); ++it) {
        const auto& search = *it;
        if (search.pack) {
            pack_t* pak = search.pack.get();
            for (const packfile_t& entry : pak->files) {
                if (std::strcmp(entry.name, filename) == 0) {
                    Sys_Printf("PackFile: %s : %s\n", pak->filename, filename);
                    if (handle) { *handle = pak->handle; Sys_FileSeek(pak->handle, entry.filepos); }
                    else { fopen_s(file, pak->filename, "rb"); if (*file) fseek(*file, entry.filepos, SEEK_SET); }
                    com_filesize = entry.filelen; return com_filesize;
                }
            }
        } else {
            sprintf_s(netpath, sizeof(netpath), "%s/%s", search.filename.c_str(), filename);
            if (!Sys_FileExists(netpath)) continue;
            Sys_Printf("FindFile: %s\n", netpath);
            int i = 0; com_filesize = Sys_FileOpenRead(netpath, &i);
            if (handle) *handle = i;
            else { Sys_FileClose(i); fopen_s(file, netpath, "rb"); }
            return com_filesize;
        }
    }
    Sys_Printf("FindFile: can't find %s\n", filename);
    if (handle) *handle = -1; else *file = nullptr;
    com_filesize = -1; return -1;
}

void COM_CloseFile(int h) {
    for (const auto& s : com_searchpaths) {
        if (s.pack && s.pack->handle == h) return;
    }
    Sys_FileClose(h);
}

std::vector<byte> COM_LoadFile(const char* path) {
    int handle = 0;
    int len = COM_OpenFile(path, &handle);
    if (handle == -1) return {};
    std::vector<byte> buffer(static_cast<size_t>(len) + 1); // trailing zero for text files
    Draw::Draw_BeginDisc();
    Sys_FileRead(handle, buffer.data(), len);
    COM_CloseFile(handle);
    Draw::Draw_EndDisc();
    return buffer;
}

std::unique_ptr<pack_t> COM_LoadPackFile(const char* packfile) {
    int packhandle = 0;
    if (Sys_FileOpenRead(packfile, &packhandle) == -1) return nullptr;

    dpackheader_t header;
    Sys_FileRead(packhandle, &header, sizeof(header));
    if (header.id[0] != 'P' || header.id[1] != 'A' || header.id[2] != 'C' || header.id[3] != 'K') Sys_Error("%s is not a packfile", packfile);
    header.dirofs = LittleLong(header.dirofs);
    header.dirlen = LittleLong(header.dirlen);
    const int numpackfiles = header.dirlen / static_cast<int>(sizeof(dpackfile_t));

    std::vector<dpackfile_t> info(static_cast<size_t>(numpackfiles));
    Sys_FileSeek(packhandle, header.dirofs);
    Sys_FileRead(packhandle, info.data(), header.dirlen);

    auto pack = std::make_unique<pack_t>();
    strcpy_s(pack->filename, sizeof(pack->filename), packfile);
    pack->handle = packhandle;
    pack->files.resize(info.size());
    for (size_t i = 0; i < info.size(); i++) {
        strcpy_s(pack->files[i].name, sizeof(pack->files[i].name), info[i].name);
        pack->files[i].filepos = LittleLong(info[i].filepos);
        pack->files[i].filelen = LittleLong(info[i].filelen);
    }
    Console::Con_Printf("Added packfile %s (%i files)\n", packfile, numpackfiles);
    return pack;
}

void COM_AddGameDirectory(const char* dir) {
    char pakfile[MAX_OSPATH]; strcpy_s(com_gamedir, sizeof(com_gamedir), dir);
    SearchPath search;
    search.filename = dir;
    com_searchpaths.insert(com_searchpaths.begin(), std::move(search));
    for (int i = 0;; i++) {
        sprintf_s(pakfile, sizeof(pakfile), "%s/pak%i.pak", dir, i);
        std::unique_ptr<pack_t> pak = COM_LoadPackFile(pakfile);
        if (!pak) break;
        SearchPath sp;
        sp.pack = std::move(pak);
        com_searchpaths.insert(com_searchpaths.begin(), std::move(sp));
    }
}

void COM_InitFilesystem(void) {
    char basedir_buf[MAX_OSPATH];
    int i = COM_CheckParm("-basedir");
    if (i && i < com_argc - 1) strcpy_s(basedir_buf, sizeof(basedir_buf), com_argv[i + 1]);
    else strcpy_s(basedir_buf, sizeof(basedir_buf), Host::host_parms.basedir);
    int j = static_cast<int>(strlen(basedir_buf));
    if (j > 0 && ((basedir_buf[j - 1] == '\\') || (basedir_buf[j - 1] == '/'))) basedir_buf[j - 1] = 0;
    COM_AddGameDirectory(va("%s/" GAMENAME, basedir_buf));
    if (COM_CheckParm("-rogue")) COM_AddGameDirectory(va("%s/rogue", basedir_buf));
    if (COM_CheckParm("-hipnotic")) COM_AddGameDirectory(va("%s/hipnotic", basedir_buf));
    i = COM_CheckParm("-game");
    if (i && i < com_argc - 1) COM_AddGameDirectory(va("%s/%s", basedir_buf, com_argv[i + 1]));
    i = COM_CheckParm("-path");
    if (i) {
        com_searchpaths.clear();
        while (++i < com_argc) {
            if (!com_argv[i] || com_argv[i][0] == '+' || com_argv[i][0] == '-') break;
            SearchPath sp;
            if (COM_FileExtension(com_argv[i]) == "pak") {
                sp.pack = COM_LoadPackFile(com_argv[i]);
                if (!sp.pack) Sys_Error("Couldn't load packfile: %s", com_argv[i]);
            } else sp.filename = com_argv[i];
            com_searchpaths.insert(com_searchpaths.begin(), std::move(sp));
        }
    }
    if (COM_CheckParm("-proghack")) proghack = true;
}

//=============================================================================
// CRC Checksum Implementation
//=============================================================================

namespace {
constexpr auto make_crc_table() {
    std::array<std::uint16_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        std::uint16_t value = 0;
        std::uint16_t temp = static_cast<std::uint16_t>(i << 8);
        for (int j = 0; j < 8; ++j) {
            if ((value ^ temp) & 0x8000) value = static_cast<std::uint16_t>((value << 1) ^ 0x1021);
            else value = static_cast<std::uint16_t>(value << 1);
            temp = static_cast<std::uint16_t>(temp << 1);
        }
        table[i] = value;
    }
    return table;
}
constexpr auto crctable = make_crc_table();
constexpr std::uint16_t CRC_INIT_VALUE = 0xffff;
} // namespace

void CRC_Init(std::uint16_t& crcvalue) noexcept { crcvalue = CRC_INIT_VALUE; }
void CRC_ProcessByte(std::uint16_t& crcvalue, byte data) noexcept {
    crcvalue = static_cast<std::uint16_t>((crcvalue << 8) ^ crctable[(crcvalue >> 8) ^ data]);
}

} // namespace Common
