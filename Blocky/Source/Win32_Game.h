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
//#define VmAllocArray(__type, __count) (__type*)::VirtualAlloc(nullptr, sizeof(__type) * __count, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)

// SIMD Stuff
#define ENABLE_SIMD 0

// Memory
#define SMALL_PAGE_SIZE 4096

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <hidusage.h>

#include "Common.h"

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

    void set_mouse_state(mouse Mouse, bool IsDown)
    {
        MouseDown[(u32)Mouse] = IsDown;
        MousePressed[(u32)Mouse] = IsDown;
    }

    v2i get_mouse_input() const { return IsCursorLocked ? VirtualMousePosition : LastMousePosition; };
    v2i get_raw_mouse_input() const { return IsCursorLocked ? RawVirtualMousePosition : RawLastMousePosition; };

    bool is_key_down(key Key) const { return KeyDown[(u32)Key]; }
    bool is_key_pressed(key Key) const { return KeyPressed[(u32)Key]; }

    bool is_mouse_down(mouse Mouse) const { return MouseDown[(u32)Mouse]; }
    bool is_mouse_pressed(mouse Mouse) const { return MousePressed[(u32)Mouse]; }
};
