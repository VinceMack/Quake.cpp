// zone.cpp -- memory allocation (hunk and cache) management


#include "quakedef.hpp"

using namespace CDAudio;
using namespace Client;
using namespace Common;
using namespace Console;
using namespace Render;
using namespace Draw;
using namespace Host;
using namespace Input;
using namespace Keys;
using namespace Math;
using namespace Menu;
using namespace Model;
using namespace Net;
using namespace VM;
using namespace Sbar;
using namespace Screen;
using namespace Server;
using namespace Audio;
using namespace Vid;
using namespace View;
using namespace Wad;
using namespace Cvar;
using namespace Cmd;

#include <stdlib.h>

namespace Common {

#define DYNAMIC_SIZE 0x100000

#define ZONEID 0x1d4a11
#define MINFRAGMENT 64

typedef struct memblock_s {
    int size; // including the header and possibly tiny fragments
    int tag;  // a tag of 0 is a free block
    int id;   // should be ZONEID
    struct memblock_s *next, *prev;
    int pad; // pad to 64 bit boundary
} memblock_t;

typedef struct {
    int size;             // total bytes malloced, including header
    memblock_t blocklist; // start / end cap for linked list
    memblock_t* rover;
} memzone_t;

void Cache_FreeLow(int new_low_hunk);
void Cache_FreeHigh(int new_high_hunk);

/*
==============================================================================

						ZONE MEMORY ALLOCATION

There is never any space between memblocks, and there will never be two
contiguous free memblocks.

The rover can be left pointing at a non-empty block

The zone calls are pretty much only used for small strings and structures,
all big things are allocated on the hunk.
==============================================================================
*/

memzone_t* mainzone;

void Z_ClearZone(memzone_t* zone, int size);

/*
========================
Z_ClearZone
========================
*/
void Z_ClearZone(memzone_t* zone, int size)
{
    memblock_t* block;

    // set the entire zone to one free block

    zone->blocklist.next = zone->blocklist.prev = block = (memblock_t*)((byte*)zone + sizeof(memzone_t));
    zone->blocklist.tag = 1; // in use block
    zone->blocklist.id = 0;
    zone->blocklist.size = 0;
    zone->rover = block;

    block->prev = block->next = &zone->blocklist;
    block->tag = 0; // free block
    block->id = ZONEID;
    block->size = size - sizeof(memzone_t);
}

/*
========================
Z_Free
========================
*/
void Z_Free(void* ptr)
{
    memblock_t *block, *other;

    if (!ptr) {
        Sys_Error("Z_Free: NULL pointer");
    }

    block = (memblock_t*)((byte*)ptr - sizeof(memblock_t));
    if (block->id != ZONEID) {
        Sys_Error("Z_Free: freed a pointer without ZONEID");
    }

    if (block->tag == 0) {
        Sys_Error("Z_Free: freed a freed pointer");
    }

    block->tag = 0; // mark as free

    other = block->prev;
    if (!other->tag) { // merge with previous free block
        other->size += block->size;
        other->next = block->next;
        other->next->prev = other;
        if (block == mainzone->rover) {
            mainzone->rover = other;
        }

        block = other;
    }

    other = block->next;
    if (!other->tag) { // merge the next free block onto the end
        block->size += other->size;
        block->next = other->next;
        block->next->prev = block;
        if (other == mainzone->rover) {
            mainzone->rover = block;
        }
    }
}

/*
========================
Z_Malloc
========================
*/
void* Z_Malloc(int size)
{
    Z_CheckHeap(); // Debug check

    void* buffer = Z_TagMalloc(size, 1);
    if (!buffer) {
        Sys_Error("Z_Malloc: failed to allocate %d bytes", size);
    }

    Q_memset(buffer, 0, size);

    return buffer;
}

void* Z_Realloc(void* ptr, int new_size)
{
    // Handle null pointer like a standard malloc
    if (!ptr) {
        return Z_Malloc(new_size);
    }

    // Validate the existing block
    memblock_t* block = (memblock_t*)((byte*)ptr - sizeof(memblock_t));
    if (block->id != ZONEID) {
        Sys_Error("Z_Realloc: pointer missing ZONEID");
    }
    if (block->tag == 0) {
        Sys_Error("Z_Realloc: pointer already freed");
    }

    // Compute usable size of the old block
    int usable_old_size = block->size - sizeof(memblock_t) - 4;

    // OPTIMIZATION: If the old block is already big enough, just return it!
    if (usable_old_size >= new_size) {
        return ptr;
    }

    // Attempt to allocate the new block safely alongside the old one
    void* new_ptr = Z_TagMalloc(new_size, 1);

    if (!new_ptr) {
        // ZONE OOM FALLBACK: The Zone is too fragmented to hold both blocks at once.
        // We must free the old block first to merge memory space, but we need to 
        // save the data first so it isn't corrupted! We use standard system malloc for the backup.
        void* backup = malloc(usable_old_size);
        if (!backup) {
            Sys_Error("Z_Realloc: System out of memory during backup");
        }
        
        Q_memcpy(backup, ptr, usable_old_size); // Save data
        Z_Free(ptr);                            // Free old block to open up Zone space
        
        new_ptr = Z_TagMalloc(new_size, 1);     // Try allocating again!
        if (!new_ptr) {
            Sys_Error("Z_Realloc: failed to allocate %d bytes even after freeing old block", new_size);
        }
        
        Q_memcpy(new_ptr, backup, usable_old_size); // Restore data
        free(backup);                               // Free system memory
    } 
    else {
        // Standard safe path: the Zone had enough room for both blocks
        Q_memcpy(new_ptr, ptr, usable_old_size);
        Z_Free(ptr);
    }

    // Zero-fill the newly expanded space
    Q_memset((char*)new_ptr + usable_old_size, 0, new_size - usable_old_size);

    return new_ptr;
}

void* Z_TagMalloc(int size, int tag)
{
    int extra;
    memblock_t *start, *rover, *new_block, *base;

    if (!tag) {
        Sys_Error("Z_TagMalloc: tried to use a 0 tag");
    }

    //
    // scan through the block list looking for the first free block
    // of sufficient size
    //
    size += sizeof(memblock_t); // account for size of block header
    size += 4;                  // space for memory trash tester
    size = (size + 7) & ~7;     // align to 8-byte boundary

    base = rover = mainzone->rover;
    start = base->prev;

    do {
        if (rover == start) { // scaned all the way around the list
            return NULL;
        }

        if (rover->tag) {
            base = rover = rover->next;
        } else {
            rover = rover->next;
        }
    } while (base->tag || base->size < size);

    //
    // found a block big enough
    //
    extra = base->size - size;
    if (extra > MINFRAGMENT) { // there will be a free fragment after the allocated block
        new_block = (memblock_t*)((byte*)base + size);
        new_block->size = extra;
        new_block->tag = 0; // free block
        new_block->prev = base;
        new_block->id = ZONEID;
        new_block->next = base->next;
        new_block->next->prev = new_block;
        base->next = new_block;
        base->size = size;
    }

    base->tag = tag; // no longer a free block

    mainzone->rover = base->next; // next allocation will start looking here

    base->id = ZONEID;

    // marker for memory trash testing
    *(int*)((byte*)base + base->size - 4) = ZONEID;

    return (void*)((byte*)base + sizeof(memblock_t));
}

/*
========================
Z_CheckHeap
========================
*/
void Z_CheckHeap(void)
{
    memblock_t* block;

    for (block = mainzone->blocklist.next;; block = block->next) {
        if (block->next == &mainzone->blocklist) {
            break; // all blocks have been hit
        }

        if ((byte*)block + block->size != (byte*)block->next) {
            Sys_Error("Z_CheckHeap: block size does not touch the next block\n");
        }

        if (block->next->prev != block) {
            Sys_Error("Z_CheckHeap: next block doesn't have proper back link\n");
        }

        if (!block->tag && !block->next->tag) {
            Sys_Error("Z_CheckHeap: two consecutive free blocks\n");
        }
    }
}

//============================================================================

#define HUNK_SENTINAL 0x1df001ed

typedef struct {
    int sentinal;
    int size; // including sizeof(hunk_t), -1 = not allocated
    char name[8];
} hunk_t;

byte* hunk_base;
int hunk_size;

int hunk_low_used;
int hunk_high_used;

qboolean hunk_tempactive;
int hunk_tempmark;

void R_FreeTextures(void);

/*
==============
Hunk_Check

Run consistancy and sentinal trahing checks
==============
*/
void Hunk_Check(void)
{
    hunk_t* h;

    for (h = (hunk_t*)hunk_base; (byte*)h != hunk_base + hunk_low_used;) {
        if (h->sentinal != HUNK_SENTINAL) {
            Sys_Error("Hunk_Check: trahsed sentinal");
        }

        if (h->size < 16 || h->size + (byte*)h - hunk_base > hunk_size) {
            Sys_Error("Hunk_Check: bad size");
        }

        h = (hunk_t*)((byte*)h + h->size);
    }
}

/*
===================
Hunk_Alloc
===================
*/
void* Hunk_Alloc(int size, const char* name)
{
    hunk_t* h;

#ifdef PARANOID
    Hunk_Check();
#endif

    if (size < 0) {
        Sys_Error("Hunk_Alloc: bad size: %i", size);
    }

    size = sizeof(hunk_t) + ((size + 15) & ~15);

    if (hunk_size - hunk_low_used - hunk_high_used < size) {
        Sys_Error("Hunk_Alloc: failed on %i bytes", size);
    }

    h = (hunk_t*)(hunk_base + hunk_low_used);
    hunk_low_used += size;

    Cache_FreeLow(hunk_low_used);

    memset(h, 0, size);

    h->size = size;
    h->sentinal = HUNK_SENTINAL;
    Q_strncpy(h->name, name, 8);

    return (void*)(h + 1);
}

int Hunk_LowMark(void)
{
    return hunk_low_used;
}

void Hunk_FreeToLowMark(int mark)
{
    if (mark < 0 || mark > hunk_low_used) {
        Sys_Error("Hunk_FreeToLowMark: bad mark %i", mark);
    }

    memset(hunk_base + mark, 0, hunk_low_used - mark);
    hunk_low_used = mark;
}

int Hunk_HighMark(void)
{
    if (hunk_tempactive) {
        hunk_tempactive = false;
        Hunk_FreeToHighMark(hunk_tempmark);
    }

    return hunk_high_used;
}

void Hunk_FreeToHighMark(int mark)
{
    if (hunk_tempactive) {
        hunk_tempactive = false;
        Hunk_FreeToHighMark(hunk_tempmark);
    }

    if (mark < 0 || mark > hunk_high_used) {
        Sys_Error("Hunk_FreeToHighMark: bad mark %i", mark);
    }

    memset(hunk_base + hunk_size - hunk_high_used, 0, hunk_high_used - mark);
    hunk_high_used = mark;
}

/*
===================
Hunk_HighAllocName
===================
*/
void* Hunk_HighAllocName(int size, const char* name)
{
    hunk_t* h;

    if (size < 0) {
        Sys_Error("Hunk_HighAllocName: bad size: %i", size);
    }

    if (hunk_tempactive) {
        Hunk_FreeToHighMark(hunk_tempmark);
        hunk_tempactive = false;
    }

#ifdef PARANOID
    Hunk_Check();
#endif

    size = sizeof(hunk_t) + ((size + 15) & ~15);

    if (hunk_size - hunk_low_used - hunk_high_used < size) {
        Con_Printf("Hunk_HighAlloc: failed on %i bytes\n", size);

        return NULL;
    }

    hunk_high_used += size;
    Cache_FreeHigh(hunk_high_used);

    h = (hunk_t*)(hunk_base + hunk_size - hunk_high_used);

    memset(h, 0, size);
    h->size = size;
    h->sentinal = HUNK_SENTINAL;
    Q_strncpy(h->name, name, 8);

    return (void*)(h + 1);
}

/*
=================
Hunk_TempAlloc

Return space from the top of the hunk
=================
*/
void* Hunk_TempAlloc(int size)
{
    void* buf;

    size = (size + 15) & ~15;

    if (hunk_tempactive) {
        Hunk_FreeToHighMark(hunk_tempmark);
        hunk_tempactive = false;
    }

    hunk_tempmark = Hunk_HighMark();

    buf = Hunk_HighAllocName(size, "temp");

    hunk_tempactive = true;

    return buf;
}

/*
===============================================================================

CACHE MEMORY

===============================================================================
*/

typedef struct cache_system_s {
    int size; // including this header
    cache_user_t* user;
    char name[16];
    struct cache_system_s *prev, *next;
    struct cache_system_s *lru_prev, *lru_next; // for LRU flushing
} cache_system_t;

cache_system_t* Cache_TryAlloc(int size, qboolean nobottom);

cache_system_t cache_head;

/*
===========
Cache_Move
===========
*/
void Cache_Move(cache_system_t* c)
{
    cache_system_t* new_cs;

    // we are clearing up space at the bottom, so only allocate it late
    new_cs = Cache_TryAlloc(c->size, true);
    if (new_cs) {
        //		Con_Printf ("cache_move ok\n");

        Q_memcpy(new_cs + 1, c + 1, c->size - sizeof(cache_system_t));
        new_cs->user = c->user;
        Q_memcpy(new_cs->name, c->name, sizeof(new_cs->name));
        Cache_Free(c->user);
        new_cs->user->data = (void*)(new_cs + 1);
    } else {
        //		Con_Printf ("cache_move failed\n");

        Cache_Free(c->user); // tough luck...
    }
}

/*
============
Cache_FreeLow

Throw things out until the hunk can be expanded to the given point
============
*/
void Cache_FreeLow(int new_low_hunk)
{
    cache_system_t* c;

    while (1) {
        c = cache_head.next;
        if (c == &cache_head) {
            return; // nothing in cache at all
        }

        if ((byte*)c >= hunk_base + new_low_hunk) {
            return; // there is space to grow the hunk
        }

        Cache_Move(c); // reclaim the space
    }
}

/*
============
Cache_FreeHigh

Throw things out until the hunk can be expanded to the given point
============
*/
void Cache_FreeHigh(int new_high_hunk)
{
    cache_system_t *c, *prev;

    prev = NULL;
    while (1) {
        c = cache_head.prev;
        if (c == &cache_head) {
            return; // nothing in cache at all
        }

        if ((byte*)c + c->size <= hunk_base + hunk_size - new_high_hunk) {
            return; // there is space to grow the hunk
        }

        if (c == prev) {
            Cache_Free(c->user); // didn't move out of the way
        } else {
            Cache_Move(c); // try to move it
            prev = c;
        }
    }
}

void Cache_UnlinkLRU(cache_system_t* cs)
{
    if (!cs->lru_next || !cs->lru_prev) {
        Sys_Error("Cache_UnlinkLRU: NULL link");
    }

    cs->lru_next->lru_prev = cs->lru_prev;
    cs->lru_prev->lru_next = cs->lru_next;

    cs->lru_prev = cs->lru_next = NULL;
}

void Cache_MakeLRU(cache_system_t* cs)
{
    if (cs->lru_next || cs->lru_prev) {
        Sys_Error("Cache_MakeLRU: active link");
    }

    cache_head.lru_next->lru_prev = cs;
    cs->lru_next = cache_head.lru_next;
    cs->lru_prev = &cache_head;
    cache_head.lru_next = cs;
}

/*
============
Cache_TryAlloc

Looks for a free block of memory between the high and low hunk marks
Size should already include the header and padding
============
*/
cache_system_t* Cache_TryAlloc(int size, qboolean nobottom)
{
    cache_system_t *cs, *new_cs;

    // is the cache completely empty?

    if (!nobottom && cache_head.prev == &cache_head) {
        if (hunk_size - hunk_high_used - hunk_low_used < size) {
            Sys_Error("Cache_TryAlloc: %i is greater then free hunk", size);
        }

        new_cs = (cache_system_t*)(hunk_base + hunk_low_used);
        memset(new_cs, 0, sizeof(*new_cs));
        new_cs->size = size;

        cache_head.prev = cache_head.next = new_cs;
        new_cs->prev = new_cs->next = &cache_head;

        Cache_MakeLRU(new_cs);

        return new_cs;
    }

    // search from the bottom up for space

    new_cs = (cache_system_t*)(hunk_base + hunk_low_used);
    cs = cache_head.next;

    do {
        if (!nobottom || cs != cache_head.next) {
            if ((byte*)cs - (byte*)new_cs >= size) { // found space
                memset(new_cs, 0, sizeof(*new_cs));
                new_cs->size = size;

                new_cs->next = cs;
                new_cs->prev = cs->prev;
                cs->prev->next = new_cs;
                cs->prev = new_cs;

                Cache_MakeLRU(new_cs);

                return new_cs;
            }
        }

        // continue looking
        new_cs = (cache_system_t*)((byte*)cs + cs->size);
        
        // FIX: If the block we just checked was below the new Hunk boundary,
        // clamp new_cs back up to the Hunk boundary to prevent overlapping!
        if ((byte*)new_cs < hunk_base + hunk_low_used) {
            new_cs = (cache_system_t*)(hunk_base + hunk_low_used);
        }

        cs = cs->next;

    } while (cs != &cache_head);

    // try to allocate one at the very end
    if (hunk_base + hunk_size - hunk_high_used - (byte*)new_cs >= size) {
        memset(new_cs, 0, sizeof(*new_cs));
        new_cs->size = size;

        new_cs->next = &cache_head;
        new_cs->prev = cache_head.prev;
        cache_head.prev->next = new_cs;
        cache_head.prev = new_cs;

        Cache_MakeLRU(new_cs);

        return new_cs;
    }

    return NULL; // couldn't allocate
}

/*
============
Cache_Flush

Throw everything out, so new data will be demand cached
============
*/
void Cache_Flush(void)
{
    while (cache_head.next != &cache_head) {
        Cache_Free(cache_head.next->user); // reclaim the space
    }
}

/*
============
Cache_Report

============
*/
void Cache_Report(void)
{
    Con_DPrintf(
        "%4.1f megabyte data cache\n",
        (hunk_size - hunk_high_used - hunk_low_used) / (float)(1024 * 1024));
}

/*
============
Cache_Init

============
*/
void Cache_Init(void)
{
    cache_head.next = cache_head.prev = &cache_head;
    cache_head.lru_next = cache_head.lru_prev = &cache_head;

    Cmd::AddCommand("flush", Cache_Flush);
}

/*
==============
Cache_Free

Frees the memory and removes it from the LRU list
==============
*/
void Cache_Free(cache_user_t* c)
{
    cache_system_t* cs;

    if (!c->data) {
        Sys_Error("Cache_Free: not allocated");
    }

    cs = ((cache_system_t*)c->data) - 1;

    cs->prev->next = cs->next;
    cs->next->prev = cs->prev;
    cs->next = cs->prev = NULL;

    c->data = NULL;

    Cache_UnlinkLRU(cs);
}

/*
==============
Cache_Check
==============
*/
void* Cache_Check(cache_user_t* c)
{
    cache_system_t* cs;

    if (!c->data) {
        return NULL;
    }

    cs = ((cache_system_t*)c->data) - 1;

    // move to head of LRU
    Cache_UnlinkLRU(cs);
    Cache_MakeLRU(cs);

    return c->data;
}

/*
==============
Cache_Alloc
==============
*/
void* Cache_Alloc(cache_user_t* c, int size, const char* name)
{
    cache_system_t* cs;

    if (c->data) {
        Sys_Error("Cache_Alloc: allready allocated");
    }

    if (size <= 0) {
        Sys_Error("Cache_Alloc: size %i", size);
    }

    size = (size + sizeof(cache_system_t) + 15) & ~15;

    // find memory for it
    while (1) {
        cs = Cache_TryAlloc(size, false);
        if (cs) {
            strncpy_s(cs->name, sizeof(cs->name), name, sizeof(cs->name) - 1);
            c->data = (void*)(cs + 1);
            cs->user = c;
            break;
        }

        // free the least recently used cahedat
        if (cache_head.lru_prev == &cache_head) {
            Sys_Error("Cache_Alloc: out of memory");
        }

        // not enough memory at all
        Cache_Free(cache_head.lru_prev->user);
    }

    return Cache_Check(c);
}

//============================================================================

/*
========================
Memory_Init
========================
*/
void Memory_Init(void* buf, int size)
{
    int p;
    int zonesize = DYNAMIC_SIZE;

    hunk_base = (byte*)buf; // C++ Fix: Explicitly cast void* to byte*
    hunk_size = size;
    hunk_low_used = 0;
    hunk_high_used = 0;

    Cache_Init();
    p = COM_CheckParm("-zone");
    if (p) {
        if (p < com_argc - 1) {
            zonesize = Q_atoi(com_argv[p + 1]) * 1024;
        } else {
            Sys_Error("Memory_Init: you must specify a size in KB after -zone");
        }
    }

    // C++ Fix: Explicitly cast void* returned from Hunk_AllocName
    mainzone = (memzone_t*)Hunk_Alloc(zonesize, "zone");
    Z_ClearZone(mainzone, zonesize);
}

} // namespace Common
