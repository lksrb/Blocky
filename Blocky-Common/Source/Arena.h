#pragma once

struct arena
{
    u8* MemoryBase;
    u8* MemoryPointer;
    u64 Capacity;
    u64 Size;
};

inline u32 align(u32 n, u32 alignment)
{
    return (n + alignment - 1) & ~(alignment - 1);
}

#define arena_new(__arena, __type) ([](arena* Arena) { __type* __Variable = (__type*)arena_alloc(Arena, sizeof(__type)); new (__Variable) __type(); return __Variable; })(__arena)

#define arena_new_array(__arena, __type, __count) (__type*)arena_alloc(Arena, sizeof(__type) * __count)

internal inline void* arena_alloc(arena* Arena, u64 AllocationSize, u64 Alignment = 8)
{
    Assert(Arena->Size + AllocationSize < Arena->Capacity, "Not enough memory in the pool!");

    // Get the current pointer
    Arena->MemoryPointer = (u8*)((u64)Arena->MemoryPointer + (static_cast<u64>(Alignment) - 1) & ~(static_cast<u64>(Alignment) - 1));

    auto Pointer = Arena->MemoryPointer;
    Arena->MemoryPointer += AllocationSize;
    Arena->Size += AllocationSize;
    return Pointer;
}


// Temp allocator for large but temporary stuff

struct temp_allocator
{
    arena* Arena;
};

internal temp_allocator temp_allocator_create(arena* Arena)
{
    temp_allocator TempAllocator;
    TempAllocator.Arena = Arena;
    return TempAllocator;
}

internal void temp_allocator_destroy(temp_allocator* TempAllocator)
{

}
