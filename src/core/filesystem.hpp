// filesystem.hpp -- Virtual filesystem, PAK archive loading, search paths, and file streaming
#pragma once

#include "core/types.hpp"
#include <cstdio>
#include <string>
#include <string_view>

namespace Common {

extern char com_token[1024];
extern bool com_eof;

const char* COM_Parse(const char* data);
inline char* COM_Parse(char* data) { return const_cast<char*>(COM_Parse(static_cast<const char*>(data))); }

extern int com_argc;
extern char** com_argv;

int COM_CheckParm(const char* parm);
void COM_Init();
void COM_InitArgv(int argc, char** argv);

void COM_FileBase(const char* in, char* out);
void COM_DefaultExtension(char* path, const char* extension);
std::string_view COM_FileExtension(std::string_view in);

extern int com_filesize;
extern char com_gamedir[128];

void COM_WriteFile(const char* filename, void* data, int len);
int COM_FindFile(const char* filename, int* handle, FILE** file);
byte* COM_LoadFile(const char* path, HunkType usehunk);

inline int COM_OpenFile(const char* filename, int* hndl) { return COM_FindFile(filename, hndl, nullptr); }
inline int COM_FOpenFile(const char* filename, FILE** file) { return COM_FindFile(filename, nullptr, file); }
void COM_CloseFile(int h);

byte* COM_LoadStackFile(const char* path, void* buffer, int bufsize);
inline byte* COM_LoadHunkFile(const char* path) { return COM_LoadFile(path, HunkType::Hunk); }
void COM_LoadCacheFile(const char* path, cache_user_s* cu);

void COM_AddGameDirectory(const char* dir);
void COM_InitFilesystem(void);
void COM_Path_f(void);

extern bool standard_quake, rogue, hipnotic;
extern bool msg_suppress_1;
extern bool proghack;

void CRC_Init(std::uint16_t& crcvalue) noexcept;
void CRC_ProcessByte(std::uint16_t& crcvalue, byte data) noexcept;

} // namespace Common
