// memory.hpp -- Zone heap, Hunk level allocator, and Cache asset management
#pragma once

#include "core/types.hpp"

namespace Common {

void Memory_Init(void* buf, int size);
void Z_Free(void* ptr);
void* Z_Malloc(int size);
void* Z_Realloc(void* ptr, int new_size);
void* Z_TagMalloc(int size, int tag);
void Z_DumpHeap(void);
void Z_CheckHeap(void);
int Z_FreeMemory(void);

void* Hunk_Alloc(int size, const char* name = "unknown");
void* Hunk_HighAllocName(int size, const char* name);
int Hunk_LowMark(void);
void Hunk_FreeToLowMark(int mark);
int Hunk_HighMark(void);
void Hunk_FreeToHighMark(int mark);
void* Hunk_TempAlloc(int size);
void Hunk_Check(void);

void Cache_Flush(void);
void* Cache_Check(cache_user_t* c);
void Cache_Free(cache_user_t* c);
void* Cache_Alloc(cache_user_t* c, int size, const char* name);
void Cache_Report(void);

} // namespace Common
