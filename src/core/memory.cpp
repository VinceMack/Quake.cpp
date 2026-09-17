// memory.cpp -- Zone heap, Hunk allocator, and Cache asset system
#include "quakedef.hpp"
#include "core/memory.hpp"
#include "core/string_utils.hpp"
#include "core/cmd.hpp"
#include "core/filesystem.hpp"
#include "ui/console.hpp"

#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cstdarg>
#include <cstdio>

//=============================================================================
// Zone Memory Allocation
//=============================================================================

namespace Common {

constexpr int DYNAMIC_SIZE = 0x100000, ZONEID = 0x1d4a11, MINFRAGMENT = 64;

struct memblock_t { int size, tag, id; memblock_t *next, *prev; int pad; };
struct memzone_t { int size; memblock_t blocklist; memblock_t* rover; };

void Cache_FreeLow(int new_low_hunk);
void Cache_FreeHigh(int new_high_hunk);

memzone_t* mainzone = nullptr;

void Z_ClearZone(memzone_t* zone, int size) {
    auto* block = reinterpret_cast<memblock_t*>(reinterpret_cast<byte*>(zone) + sizeof(memzone_t));
    zone->blocklist.next = zone->blocklist.prev = block;
    zone->blocklist.tag = 1; zone->blocklist.id = 0; zone->blocklist.size = 0; zone->rover = block;
    block->prev = block->next = &zone->blocklist; block->tag = 0; block->id = ZONEID; block->size = size - sizeof(memzone_t);
}

void Z_Free(void* ptr) {
    if (!ptr) Sys_Error("Z_Free: NULL pointer");
    auto* block = reinterpret_cast<memblock_t*>(reinterpret_cast<byte*>(ptr) - sizeof(memblock_t));
    if (block->id != ZONEID) Sys_Error("Z_Free: freed a pointer without ZONEID");
    if (block->tag == 0) Sys_Error("Z_Free: freed a freed pointer");
    block->tag = 0;
    memblock_t* other = block->prev;
    if (!other->tag) {
        other->size += block->size; other->next = block->next; other->next->prev = other;
        if (block == mainzone->rover) mainzone->rover = other;
        block = other;
    }
    other = block->next;
    if (!other->tag) {
        block->size += other->size; block->next = other->next; block->next->prev = block;
        if (other == mainzone->rover) mainzone->rover = block;
    }
}

void* Z_Malloc(int size) {
    Z_CheckHeap();
    void* buffer = Z_TagMalloc(size, 1);
    if (!buffer) Sys_Error("Z_Malloc: failed to allocate %d bytes", size);
    Q_memset(buffer, 0, size);
    return buffer;
}

void* Z_Realloc(void* ptr, int new_size) {
    if (!ptr) return Z_Malloc(new_size);
    auto* block = reinterpret_cast<memblock_t*>(reinterpret_cast<byte*>(ptr) - sizeof(memblock_t));
    if (block->id != ZONEID) Sys_Error("Z_Realloc: pointer missing ZONEID");
    if (block->tag == 0) Sys_Error("Z_Realloc: pointer already freed");

    int usable_old_size = block->size - sizeof(memblock_t) - 4;
    if (usable_old_size >= new_size) return ptr;

    void* new_ptr = Z_TagMalloc(new_size, 1);
    if (!new_ptr) {
        void* backup = malloc(usable_old_size);
        if (!backup) Sys_Error("Z_Realloc: System out of memory during backup");
        Q_memcpy(backup, ptr, usable_old_size); Z_Free(ptr);
        new_ptr = Z_TagMalloc(new_size, 1);
        if (!new_ptr) Sys_Error("Z_Realloc: failed to allocate %d bytes even after freeing old block", new_size);
        Q_memcpy(new_ptr, backup, usable_old_size); free(backup);
    } else {
        Q_memcpy(new_ptr, ptr, usable_old_size); Z_Free(ptr);
    }
    Q_memset(reinterpret_cast<char*>(new_ptr) + usable_old_size, 0, new_size - usable_old_size);
    return new_ptr;
}

void* Z_TagMalloc(int size, int tag) {
    if (!tag) Sys_Error("Z_TagMalloc: tried to use a 0 tag");
    size = (size + sizeof(memblock_t) + 4 + 7) & ~7;
    memblock_t *base = mainzone->rover, *rover = base, *start = base->prev;
    do {
        if (rover == start) return nullptr;
        if (rover->tag) base = rover = rover->next;
        else rover = rover->next;
    } while (base->tag || base->size < size);

    int extra = base->size - size;
    if (extra > MINFRAGMENT) {
        auto* new_block = reinterpret_cast<memblock_t*>(reinterpret_cast<byte*>(base) + size);
        new_block->size = extra; new_block->tag = 0; new_block->prev = base; new_block->id = ZONEID;
        new_block->next = base->next; new_block->next->prev = new_block;
        base->next = new_block; base->size = size;
    }
    base->tag = tag; mainzone->rover = base->next; base->id = ZONEID;
    *reinterpret_cast<int*>(reinterpret_cast<byte*>(base) + base->size - 4) = ZONEID;
    return reinterpret_cast<void*>(reinterpret_cast<byte*>(base) + sizeof(memblock_t));
}

void Z_CheckHeap(void) {
    for (memblock_t* block = mainzone->blocklist.next; block->next != &mainzone->blocklist; block = block->next) {
        if (reinterpret_cast<byte*>(block) + block->size != reinterpret_cast<byte*>(block->next)) Sys_Error("Z_CheckHeap: block size does not touch the next block\n");
        if (block->next->prev != block) Sys_Error("Z_CheckHeap: next block doesn't have proper back link\n");
        if (!block->tag && !block->next->tag) Sys_Error("Z_CheckHeap: two consecutive free blocks\n");
    }
}

void Z_DumpHeap(void) {}
int Z_FreeMemory(void) { return 0; }

//=============================================================================
// Hunk Level Memory Allocation
//=============================================================================

constexpr int HUNK_SENTINAL = 0x1df001ed;
struct hunk_t { int sentinal, size; char name[8]; };

byte* hunk_base = nullptr;
int hunk_size = 0, hunk_low_used = 0, hunk_high_used = 0;
qboolean hunk_tempactive = false;
int hunk_tempmark = 0;

void Hunk_Check(void) {
    for (auto* h = reinterpret_cast<hunk_t*>(hunk_base); reinterpret_cast<byte*>(h) != hunk_base + hunk_low_used;) {
        if (h->sentinal != HUNK_SENTINAL) Sys_Error("Hunk_Check: trashed sentinal");
        if (h->size < 16 || h->size + reinterpret_cast<byte*>(h) - hunk_base > hunk_size) Sys_Error("Hunk_Check: bad size");
        h = reinterpret_cast<hunk_t*>(reinterpret_cast<byte*>(h) + h->size);
    }
}

void* Hunk_Alloc(int size, const char* name) {
    if (size < 0) Sys_Error("Hunk_Alloc: bad size: %i", size);
    size = sizeof(hunk_t) + ((size + 15) & ~15);
    if (hunk_size - hunk_low_used - hunk_high_used < size) Sys_Error("Hunk_Alloc: failed on %i bytes", size);
    auto* h = reinterpret_cast<hunk_t*>(hunk_base + hunk_low_used);
    hunk_low_used += size;
    Cache_FreeLow(hunk_low_used);
    std::memset(h, 0, size);
    h->size = size; h->sentinal = HUNK_SENTINAL; Q_strncpy(h->name, name, 8);
    return reinterpret_cast<void*>(h + 1);
}

int Hunk_LowMark(void) { return hunk_low_used; }

void Hunk_FreeToLowMark(int mark) {
    if (mark < 0 || mark > hunk_low_used) Sys_Error("Hunk_FreeToLowMark: bad mark %i", mark);
    std::memset(hunk_base + mark, 0, hunk_low_used - mark);
    hunk_low_used = mark;
}

int Hunk_HighMark(void) {
    if (hunk_tempactive) { hunk_tempactive = false; Hunk_FreeToHighMark(hunk_tempmark); }
    return hunk_high_used;
}

void Hunk_FreeToHighMark(int mark) {
    if (hunk_tempactive) { hunk_tempactive = false; Hunk_FreeToHighMark(hunk_tempmark); }
    if (mark < 0 || mark > hunk_high_used) Sys_Error("Hunk_FreeToHighMark: bad mark %i", mark);
    std::memset(hunk_base + hunk_size - hunk_high_used, 0, hunk_high_used - mark);
    hunk_high_used = mark;
}

void* Hunk_HighAllocName(int size, const char* name) {
    if (size < 0) Sys_Error("Hunk_HighAllocName: bad size: %i", size);
    if (hunk_tempactive) { Hunk_FreeToHighMark(hunk_tempmark); hunk_tempactive = false; }
    size = sizeof(hunk_t) + ((size + 15) & ~15);
    if (hunk_size - hunk_low_used - hunk_high_used < size) { Console::Con_Printf("Hunk_HighAlloc: failed on %i bytes\n", size); return nullptr; }
    hunk_high_used += size;
    Cache_FreeHigh(hunk_high_used);
    auto* h = reinterpret_cast<hunk_t*>(hunk_base + hunk_size - hunk_high_used);
    std::memset(h, 0, size); h->size = size; h->sentinal = HUNK_SENTINAL; Q_strncpy(h->name, name, 8);
    return reinterpret_cast<void*>(h + 1);
}

void* Hunk_TempAlloc(int size) {
    size = (size + 15) & ~15;
    if (hunk_tempactive) { Hunk_FreeToHighMark(hunk_tempmark); hunk_tempactive = false; }
    hunk_tempmark = Hunk_HighMark();
    void* buf = Hunk_HighAllocName(size, "temp");
    hunk_tempactive = true;
    return buf;
}

//=============================================================================
// Cache System
//=============================================================================

struct cache_system_t {
    int size; cache_user_t* user; char name[16];
    cache_system_t *prev, *next, *lru_prev, *lru_next;
};

cache_system_t* Cache_TryAlloc(int size, qboolean nobottom);
cache_system_t cache_head;

void Cache_Move(cache_system_t* c) {
    cache_system_t* new_cs = Cache_TryAlloc(c->size, true);
    if (new_cs) {
        Q_memcpy(new_cs + 1, c + 1, c->size - sizeof(cache_system_t));
        new_cs->user = c->user; Q_memcpy(new_cs->name, c->name, sizeof(new_cs->name));
        Cache_Free(c->user); new_cs->user->data = reinterpret_cast<void*>(new_cs + 1);
    } else Cache_Free(c->user);
}

void Cache_FreeLow(int new_low_hunk) {
    while (1) {
        cache_system_t* c = cache_head.next;
        if (c == &cache_head || reinterpret_cast<byte*>(c) >= hunk_base + new_low_hunk) return;
        Cache_Move(c);
    }
}

void Cache_FreeHigh(int new_high_hunk) {
    cache_system_t* prev = nullptr;
    while (1) {
        cache_system_t* c = cache_head.prev;
        if (c == &cache_head || reinterpret_cast<byte*>(c) + c->size <= hunk_base + hunk_size - new_high_hunk) return;
        if (c == prev) Cache_Free(c->user);
        else { Cache_Move(c); prev = c; }
    }
}

void Cache_UnlinkLRU(cache_system_t* cs) {
    if (!cs->lru_next || !cs->lru_prev) Sys_Error("Cache_UnlinkLRU: NULL link");
    cs->lru_next->lru_prev = cs->lru_prev; cs->lru_prev->lru_next = cs->lru_next;
    cs->lru_prev = cs->lru_next = nullptr;
}

void Cache_MakeLRU(cache_system_t* cs) {
    if (cs->lru_next || cs->lru_prev) Sys_Error("Cache_MakeLRU: active link");
    cache_head.lru_next->lru_prev = cs; cs->lru_next = cache_head.lru_next;
    cs->lru_prev = &cache_head; cache_head.lru_next = cs;
}

cache_system_t* Cache_TryAlloc(int size, qboolean nobottom) {
    if (!nobottom && cache_head.prev == &cache_head) {
        if (hunk_size - hunk_high_used - hunk_low_used < size) Sys_Error("Cache_TryAlloc: %i is greater then free hunk", size);
        auto* new_cs = reinterpret_cast<cache_system_t*>(hunk_base + hunk_low_used);
        std::memset(new_cs, 0, sizeof(*new_cs)); new_cs->size = size;
        cache_head.prev = cache_head.next = new_cs; new_cs->prev = new_cs->next = &cache_head;
        Cache_MakeLRU(new_cs); return new_cs;
    }
    auto* new_cs = reinterpret_cast<cache_system_t*>(hunk_base + hunk_low_used);
    cache_system_t* cs = cache_head.next;
    do {
        if (!nobottom || cs != cache_head.next) {
            if (reinterpret_cast<byte*>(cs) - reinterpret_cast<byte*>(new_cs) >= size) {
                std::memset(new_cs, 0, sizeof(*new_cs)); new_cs->size = size;
                new_cs->next = cs; new_cs->prev = cs->prev; cs->prev->next = new_cs; cs->prev = new_cs;
                Cache_MakeLRU(new_cs); return new_cs;
            }
        }
        new_cs = reinterpret_cast<cache_system_t*>(reinterpret_cast<byte*>(cs) + cs->size);
        if (reinterpret_cast<byte*>(new_cs) < hunk_base + hunk_low_used) new_cs = reinterpret_cast<cache_system_t*>(hunk_base + hunk_low_used);
        cs = cs->next;
    } while (cs != &cache_head);

    if (hunk_base + hunk_size - hunk_high_used - reinterpret_cast<byte*>(new_cs) >= size) {
        std::memset(new_cs, 0, sizeof(*new_cs)); new_cs->size = size;
        new_cs->next = &cache_head; new_cs->prev = cache_head.prev;
        cache_head.prev->next = new_cs; cache_head.prev = new_cs;
        Cache_MakeLRU(new_cs); return new_cs;
    }
    return nullptr;
}

void Cache_Flush(void) { while (cache_head.next != &cache_head) Cache_Free(cache_head.next->user); }
void Cache_Report(void) { Console::Con_DPrintf("%4.1f megabyte data cache\n", (hunk_size - hunk_high_used - hunk_low_used) / (float)(1024 * 1024)); }

void Cache_Init(void) {
    cache_head.next = cache_head.prev = &cache_head;
    cache_head.lru_next = cache_head.lru_prev = &cache_head;
    Cmd::AddCommand("flush", Cache_Flush);
}

void Cache_Free(cache_user_t* c) {
    if (!c->data) Sys_Error("Cache_Free: not allocated");
    auto* cs = reinterpret_cast<cache_system_t*>(c->data) - 1;
    cs->prev->next = cs->next; cs->next->prev = cs->prev;
    cs->next = cs->prev = nullptr; c->data = nullptr;
    Cache_UnlinkLRU(cs);
}

void* Cache_Check(cache_user_t* c) {
    if (!c->data) return nullptr;
    auto* cs = reinterpret_cast<cache_system_t*>(c->data) - 1;
    Cache_UnlinkLRU(cs); Cache_MakeLRU(cs);
    return c->data;
}

void* Cache_Alloc(cache_user_t* c, int size, const char* name) {
    if (c->data) Sys_Error("Cache_Alloc: allready allocated");
    if (size <= 0) Sys_Error("Cache_Alloc: size %i", size);
    size = (size + sizeof(cache_system_t) + 15) & ~15;
    while (1) {
        cache_system_t* cs = Cache_TryAlloc(size, false);
        if (cs) {
            strncpy_s(cs->name, sizeof(cs->name), name, sizeof(cs->name) - 1);
            c->data = reinterpret_cast<void*>(cs + 1); cs->user = c; break;
        }
        if (cache_head.lru_prev == &cache_head) Sys_Error("Cache_Alloc: out of memory");
        Cache_Free(cache_head.lru_prev->user);
    }
    return Cache_Check(c);
}

void Memory_Init(void* buf, int size) {
    hunk_base = static_cast<byte*>(buf); hunk_size = size; hunk_low_used = hunk_high_used = 0;
    Cache_Init();
    int zonesize = DYNAMIC_SIZE, p = COM_CheckParm("-zone");
    if (p) {
        if (p < com_argc - 1) zonesize = Q_atoi(com_argv[p + 1]) * 1024;
        else Sys_Error("Memory_Init: you must specify a size in KB after -zone");
    }
    mainzone = static_cast<memzone_t*>(Hunk_Alloc(zonesize, "zone"));
    Z_ClearZone(mainzone, zonesize);
}

} // namespace Common
