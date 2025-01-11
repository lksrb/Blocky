/*
 * Win32 game layer
*/

// General defines
#define CountOf(arr) sizeof(arr) / sizeof(arr[0])
#define STRINGIFY(x) #x

// Asserts
#define ENABLE_ASSERTS 1

#ifdef BK_DEBUG
#define Debugbreak() __debugbreak()
#else
#define Debugbreak()
#endif

#if ENABLE_ASSERTS
#define Assert(__cond__, ...) do { if(!(__cond__)) { Log(__VA_ARGS__); Debugbreak(); } } while(0)
#else
#define Assert(...)
#endif

// Win32 assert
#define WAssert(__cond__, ...) Assert(SUCCEEDED(__cond__), __VA_ARGS__)

// Vulkan assert
#define VkAssert(__vulkan_func) do { VkResult __result = (__vulkan_func); Assert(__result  == VK_SUCCESS, "%d", static_cast<u32>(__result)); } while(0)

// Log
#define Log(X, ...) do { printf(X, __VA_ARGS__); printf("\n"); } while(0)

#include <stdio.h>
#include <cstdint>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

// Primitive types
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

using f32 = float;
using f64 = double;

struct game_window
{
    HWND WindowHandle;
    HINSTANCE ModuleInstance;
};

#include "Vulkan_Renderer.h"

LRESULT Win32ProcedureHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT Result = 0;

    switch (msg)
    {
        case WM_DESTROY:
        case WM_CLOSE:
        {
            ::PostQuitMessage(0);
            break;
        }
        default:
        {
            Result = ::DefWindowProc(hWnd, msg, wParam, lParam);
            break;
        }
    }

    return Result;
}

static game_window CreateGameWindow()
{
    game_window Result;

    // Create window
    WNDCLASS WindowClass = {};
    WindowClass.lpszClassName = L"BlockyWindowClass";
    WindowClass.hInstance = GetModuleHandle(NULL);
    WindowClass.lpfnWndProc = Win32ProcedureHandler;
    WindowClass.hCursor = nullptr;
    WindowClass.hbrBackground = nullptr;
    ::RegisterClass(&WindowClass);

    // NOTE(casey): Martins says WS_EX_NOREDIRECTIONBITMAP is necessary to make
    // DXGI_SWAP_EFFECT_FLIP_DISCARD "not glitch on window resizing", and since
    // I don't normally program DirectX and have no idea, we're just going to
    // leave it here :)
    DWORD ExStyle = WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP;

    Result.WindowHandle = CreateWindowExW(ExStyle, WindowClass.lpszClassName, L"Blocky", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                             0, 0, WindowClass.hInstance, 0);
    Result.ModuleInstance = WindowClass.hInstance;

    return Result;
}

int main(int argc, char** argv)
{
    Log("Hello, Blocky!");

    game_window Window = CreateGameWindow();

    game_renderer Renderer = CreateGameRenderer(Window);

    // Game loop
    bool IsRunning = true;
    while (IsRunning)
    {
        // Process events
        MSG Message;
        while (::PeekMessage(&Message, nullptr, 0, 0, PM_REMOVE))
        {
            if (Message.message == WM_QUIT)
            {
                IsRunning = false;
            }

            ::TranslateMessage(&Message);
            ::DispatchMessage(&Message);
        }

        BeginRender(&Renderer);

        EndRender(&Renderer);
    }

    return 0;
}
