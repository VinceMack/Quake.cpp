// system.hpp -- Operating system abstraction layer (file I/O, timers, console, execution lifecycle)
#pragma once

#include "core/types.hpp"

namespace Common {

int Sys_FileOpenRead(const char* path, int* hndl);
int Sys_FileOpenWrite(const char* path);
void Sys_FileClose(int handle);
void Sys_FileSeek(int handle, int position);
int Sys_FileRead(int handle, void* dest, int count);
int Sys_FileWrite(int handle, const void* data, int count);
int Sys_FileTime(const char* path);
void Sys_mkdir(const char* path);

[[noreturn]] void Sys_Error(const char* error, ...);
void Sys_Printf(const char* fmt, ...);
void Sys_Quit(void);
double Sys_FloatTime(void);
char* Sys_ConsoleInput(void);
void Sys_SendKeyEvents(void);

void Sys_Init(void);
void Sys_LowFPPrecision(void);
void Sys_HighFPPrecision(void);
void Sys_SetFPCW(void);

} // namespace Common
