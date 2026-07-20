#include <new>
#include <cstdlib>
#include <cstddef>
#include <cassert>

// Helper functions for manual aligned allocation
static void* aligned_alloc_helper(size_t size, size_t alignment) {
    size_t adjustedAlignment = (alignment > sizeof(void*)) ? alignment : sizeof(void*);
    // Allocate extra space for:
    // 1. The requested size
    // 2. The alignment padding
    // 3. Storing the original pointer (sizeof(void*))
    void* orig = std::malloc(size + adjustedAlignment + sizeof(void*));
    if (!orig) return nullptr;

    void* pPlusPointerSize = (void*)((uintptr_t)orig + sizeof(void*));
    void* pAligned = (void*)(((uintptr_t)pPlusPointerSize + adjustedAlignment - 1) & ~(adjustedAlignment - 1));
    ((void**)pAligned)[-1] = orig;
    return pAligned;
}

static void aligned_free_helper(void* ptr) {
    if (ptr) {
        void* orig = ((void**)ptr)[-1];
        std::free(orig);
    }
}

// 1. Standard Allocation Overloads
void* operator new[](size_t size) {
    void* ptr = aligned_alloc_helper(size, sizeof(void*));
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void* operator new[](size_t size, const char* /*pName*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/) {
    void* ptr = aligned_alloc_helper(size, sizeof(void*));
    assert(ptr != nullptr && "Out of memory");
    return ptr;
}

// 2. Aligned Allocation Overloads
void* operator new[](size_t size, size_t alignment, size_t /*alignmentOffset*/, const char* /*pName*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/) {
    void* ptr = aligned_alloc_helper(size, alignment);
    assert(ptr != nullptr && "Out of memory");
    return ptr;
}

void* operator new[](size_t size, std::align_val_t alignment) {
    void* ptr = aligned_alloc_helper(size, static_cast<size_t>(alignment));
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

// 3. Deallocation Overloads
void operator delete[](void* ptr) noexcept {
    aligned_free_helper(ptr);
}

void operator delete[](void* ptr, size_t /*size*/) noexcept {
    aligned_free_helper(ptr);
}

void operator delete[](void* ptr, std::align_val_t /*alignment*/) noexcept {
    aligned_free_helper(ptr);
}

void operator delete[](void* ptr, size_t /*size*/, std::align_val_t /*alignment*/) noexcept {
    aligned_free_helper(ptr);
}
