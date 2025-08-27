#pragma once

template<typename T>
struct free_list_item
{
    T Type;
    i32 FreeList;
};

template<typename T>
struct freelist_array
{
    T* Data;
    u32 Size;
    u32 Capacity;
    i32 FreeList;
};

template<typename T>
internal freelist_array<T> freelist_array_create(arena* Arena, u32 Capacity)
{
    freelist_array<T> Array = {
        .Data = arena_new_array(Arena, free_list_item<T>, Capacity),
        .Size = 0,
        .Capacity = Capacity,
        .FreeList = -1
    };

    return Array;
}

template<typename T>
internal void freelist_array_destroy(freelist_array<T>* Array)
{

}

template<typename T>
internal T& freelist_array_emplace_back(freelist_array<T>* Array)
{

}
