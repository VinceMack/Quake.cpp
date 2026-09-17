// filesystem.hpp -- Virtual filesystem, PAK archive loading, search paths, and file streaming
#pragma once

#include "core/types.hpp"
#include <cstdio>
#include <vector>
#include <string>
#include <string_view>

namespace Common {

extern char com_token[1024];
extern bool com_eof;

const char* COM_Parse(const char* data);
inline char* COM_Parse(char* data)
{
    return const_cast<char*>(COM_Parse(static_cast<const char*>(data)));
}

extern int com_argc;
extern char** com_argv;

int COM_CheckParm(const char* parm);
void COM_Init(const char* basedir);
void COM_InitArgv(int argc, char** argv);

void COM_FileBase(const char* in, char* out);
void COM_DefaultExtension(char* path, const char* extension);
std::string_view COM_FileExtension(std::string_view in);

extern int com_filesize;
extern char com_gamedir[128];

void COM_WriteFile(const char* filename, void* data, int len);
int COM_FindFile(const char* filename, int* handle, FILE** file);

// Loads a whole file from the search path. The buffer carries one extra zero byte after
// the contents so text files can be read as C strings; com_filesize holds the true
// length. Returns an empty vector if the file does not exist.
[[nodiscard]] std::vector<byte> COM_LoadFile(const char* path);

inline int COM_OpenFile(const char* filename, int* hndl)
{
    return COM_FindFile(filename, hndl, nullptr);
}
inline int COM_FOpenFile(const char* filename, FILE** file)
{
    return COM_FindFile(filename, nullptr, file);
}
void COM_CloseFile(int h);

void COM_AddGameDirectory(const char* dir);
void COM_InitFilesystem(const char* basedir);

// Called around every file load so the UI can show its "disc" activity icon.
void COM_SetLoadIndicator(void (*begin)(), void (*end)());
void COM_Path_f(void);

extern bool standard_quake, rogue, hipnotic;
extern bool msg_suppress_1;
extern bool proghack;

void CRC_Init(std::uint16_t& crcvalue) noexcept;
void CRC_ProcessByte(std::uint16_t& crcvalue, byte data) noexcept;

} // namespace Common
