#pragma once

/*
 * Win32 game layer
*/

// Win32 assert
#define WAssert(__cond__, ...) Assert(SUCCEEDED(__cond__), __VA_ARGS__)

// Win32 utils
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))

// Just to replace "new"s everywhere, they are slow as fuck
#define VmAllocArray(__type, __count) (__type*)::VirtualAlloc(nullptr, sizeof(__type) * __count, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)

// SIMD Stuff
#define ENABLE_SIMD 1

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <hidusage.h>

#include "Common.h"

// HAHA ITS GENIUS
#define debug_new(__type) ([]() { __type* __Variable = (__type*)VmAllocArray(__type, 1); new (__Variable) __type(); return __Variable; })()

struct win32_window
{
    HWND Handle;
    u32 ClientAreaWidth;
    u32 ClientAreaHeight;
};

struct win32_context
{
    win32_window Window;
    HINSTANCE ModuleInstance;
};

// THIS IS CROSS-PLATFORM
enum class key : u32
{
    W = 0, S, A, D, Q, E, T, G, Shift, Control, BackSpace, Space, COUNT
};

enum class mouse : u32
{
    Left = 0,
    Right,
    Middle,

    COUNT
};

struct game_input
{
    // TODO: Probably make it a single u32 or something

    bool MousePressed[(u32)mouse::COUNT];
    bool MouseDown[(u32)mouse::COUNT];

    bool KeyDown[(u32)key::COUNT];
    bool KeyPressed[(u32)key::COUNT];

    bool IsCursorLocked;
    bool IsCursorVisible = true;

    v2i LastMousePosition;
    v2i VirtualMousePosition;

    // Without mouse acceleration
    v2i RawLastMousePosition;
    v2i RawVirtualMousePosition;

    i32 MouseScrollDelta;

    // Temp
    POINT RestoreMousePosition;

    void set_key_state(key Key, bool IsDown)
    {
        KeyDown[(u32)Key] = IsDown;
        KeyPressed[(u32)Key] = IsDown;
    }

    void SetMouseState(mouse Mouse, bool IsDown)
    {
        MouseDown[(u32)Mouse] = IsDown;
        MousePressed[(u32)Mouse] = IsDown;
    }

    v2i GetMouseInput() const { return IsCursorLocked ? VirtualMousePosition : LastMousePosition; };
    v2i GetRawMouseInput() const { return IsCursorLocked ? RawVirtualMousePosition : RawLastMousePosition; };

    bool IsKeyDown(key Key) const { return KeyDown[(u32)Key]; }
    bool IsKeyPressed(key Key) const { return KeyPressed[(u32)Key]; }

    bool IsMouseDown(mouse Mouse) const { return MouseDown[(u32)Mouse]; }
    bool IsMousePressed(mouse Mouse) const { return MousePressed[(u32)Mouse]; }
};

struct arena
{
    u8* MemoryBase;
    u8* MemoryPointer;
    u64 Capacity;
    u64 Size;
};

#define SMALL_PAGE_SIZE 4096

inline u32 align(u32 n, u32 alignment)
{
    return (n + alignment - 1) & ~(alignment - 1);
}

#define arena_new(__arena, __type) ([](arena* Arena) { __type* __Variable = (__type*)arena_alloc(Arena, sizeof(__type)); new (__Variable) __type(); return __Variable; })(__arena)

internal void* arena_alloc(arena* Arena, u64 AllocationSize, u64 Alignment = 8)
{
    Assert(Arena->Size + AllocationSize < Arena->Capacity, "Not enough memory in the pool!");

    // Get the current pointer
    Arena->MemoryPointer = (u8*)((u64)Arena->MemoryPointer + (static_cast<u64>(Alignment) - 1) & ~(static_cast<u64>(Alignment) - 1));

    void* Pointer = Arena->MemoryPointer;
    Arena->MemoryPointer += AllocationSize;
    Arena->Size += AllocationSize;
    return Pointer;
}
