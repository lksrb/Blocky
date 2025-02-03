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
#define Assert(__cond__, ...) do { if(!(__cond__)) { Err(__VA_ARGS__); Debugbreak(); } } while(0)
#else
#define Assert(...)
#endif

// Win32 assert
#define WAssert(__cond__, ...) Assert(SUCCEEDED(__cond__), __VA_ARGS__)

// Win32 utils
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))

// Vulkan assert
#define VkAssert(__vulkan_func) do { VkResult __result = (__vulkan_func); Assert(__result  == VK_SUCCESS, "%d", static_cast<u32>(__result)); } while(0)

// Log
#define BK_RESET_COLOR "\033[0m"
#define BK_GREEN_COLOR "\033[32m"
#define BK_YELLOW_COLOR "\033[33m"
#define BK_RED_COLOR "\033[31m"
#define BK_WHITE_RED_BG_COLOR "\033[41;37m"

#define Trace(...) do { \
    printf(__VA_ARGS__); \
    printf(BK_RESET_COLOR "\n"); \
} while(0)

#define Info(...) do { \
    printf(BK_GREEN_COLOR); \
    printf(__VA_ARGS__); \
    printf(BK_RESET_COLOR "\n"); \
} while(0)

#define Warn(...) do { \
    printf(BK_YELLOW_COLOR); \
    printf(__VA_ARGS__); \
    printf(BK_RESET_COLOR "\n"); \
} while(0)

#define Err(...) do { \
    printf(BK_WHITE_RED_BG_COLOR); \
    printf(__VA_ARGS__); \
    printf(BK_RESET_COLOR "\n"); \
} while(0)

#define ENABLE_BITWISE_OPERATORS(Enum, SizeOfEnum)                   \
constexpr Enum operator|(Enum inLHS, Enum inRHS)                     \
{                                                                    \
    return Enum(static_cast<SizeOfEnum>(inLHS) |                     \
                static_cast<SizeOfEnum>(inRHS));                     \
}                                                                    \
                                                                     \
constexpr Enum operator&(Enum inLHS, Enum inRHS)                     \
{                                                                    \
    return Enum(static_cast<SizeOfEnum>(inLHS) &                     \
                static_cast<SizeOfEnum>(inRHS));                     \
}                                                                    \
                                                                     \
constexpr Enum operator^(Enum inLHS, Enum inRHS)                     \
{                                                                    \
    return Enum(static_cast<SizeOfEnum>(inLHS) ^                     \
                static_cast<SizeOfEnum>(inRHS));                     \
}                                                                    \
                                                                     \
constexpr Enum operator~(Enum inLHS)                                 \
{                                                                    \
    return Enum(~static_cast<SizeOfEnum>(inLHS));                    \
}                                                                    \
                                                                     \
constexpr Enum& operator|=(Enum& ioLHS, Enum inRHS)                  \
{                                                                    \
    ioLHS = ioLHS | inRHS;                                           \
    return ioLHS;                                                    \
}                                                                    \
                                                                     \
constexpr Enum& operator&=(Enum& ioLHS, Enum inRHS)                  \
{                                                                    \
    ioLHS = ioLHS & inRHS;                                           \
    return ioLHS;                                                    \
}                                                                    \
                                                                     \
constexpr Enum& operator^=(Enum& ioLHS, Enum inRHS)                  \
{                                                                    \
    ioLHS = ioLHS ^ inRHS;                                           \
    return ioLHS;                                                    \
}                                                                    \

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <malloc.h>
#include <stdio.h>

#include <cstdint>

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

using b32 = u32;

// Math
#include "Math/BKM.h"

struct game_window
{
    HWND WindowHandle;
    HINSTANCE ModuleInstance;

    u32 ClientAreaWidth;
    u32 ClientAreaHeight;
};

struct game_input
{
    bool W, S, A, D;
};

struct buffer
{
    void* Data;
    u64 Size;
};

#include "VulkanRenderer.h"
#include "DX12Renderer.h"

// Game
#include "Blocky.h"

static u32 g_ClientWidth = 0;
static u32 g_ClientHeight = 0;
static bool g_DoResize = false;

static bool g_IsRunning = false;

LRESULT Win32ProcedureHandler(HWND WindowHandle, UINT Message, WPARAM WParam, LPARAM LParam)
{
    LRESULT Result = 0;

    switch (Message)
    {
        case WM_SIZE:
        {
            u32 Width = GET_X_LPARAM(LParam);
            u32 Height = GET_Y_LPARAM(LParam);

            // NOTE: Swapchain has a problem with width and height being the same after restoring the window and requires
            // resize as well
            if (Width != 0 && Height != 0)
            {
                if (g_ClientWidth != Width || g_ClientHeight != Height)
                {
                    g_DoResize = true;
                }

                g_ClientWidth = Width;
                g_ClientHeight = Height;

            }

            break;
        }

        case WM_SYSKEYUP:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_KEYDOWN:
        {
            Assert(false, "Dispached input to the procedure handler (Keyboard)!");

            break;
        }
        case WM_DESTROY:
        case WM_CLOSE:
        {
            g_IsRunning = false;
            break;
        }
        default:
        {
            Result = DefWindowProc(WindowHandle, Message, WParam, LParam);
            break;
        }
    }

    return Result;
}

static void Win32ProcessEvents(game_input* Input)
{
    MSG Message;

    // Win32 message queue
    while (PeekMessage(&Message, nullptr, 0, 0, PM_REMOVE))
    {
        switch (Message.message)
        {
            case WM_SYSKEYUP:
            case WM_SYSKEYDOWN:
            case WM_KEYUP:
            case WM_KEYDOWN:
            {
                u64 VkCode = Message.wParam;
                bool WasDown = (Message.lParam & (1 << 30)) != 0;
                bool IsDown = (Message.lParam & (1 << 31)) == 0;

                b32 AltKeyWasDown = (Message.lParam & (1 << 29));

                // Don't allow repeats
                if (IsDown != WasDown)
                {
                    switch (VkCode)
                    {
                        case 'W':
                        {
                            Input->W = IsDown;
                            break;
                        }
                        case 'S':
                        {
                            Input->S = IsDown;
                            break;
                        }
                        case 'A':
                        {
                            Input->A = IsDown;
                            break;
                        }
                        case 'D':
                        {
                            Input->D = IsDown;
                            break;
                        }
                        case VK_F4:
                        {
                            if (AltKeyWasDown)
                                g_IsRunning = false;
                            break;
                        }
                        case VK_ESCAPE:
                        {
                            g_IsRunning = false;
                            break;
                        }
                    }
                }

                break;
            }

            case WM_QUIT:
            {
                g_IsRunning = false;
                break;
            }

            default:
            {
                TranslateMessage(&Message);
                DispatchMessage(&Message);
                break;
            }
        }
    }
}

static game_window CreateGameWindow()
{
    game_window Window;
    const u32 DefaultWindowWidth = 1600;
    const u32 DefaultWindowHeight = 900;

    // Show window on primary window
    // TODO: User should choose on which monitor to display
    // TODO: Error handle
    MONITORINFO PrimaryMonitorInfo = { sizeof(MONITORINFO) };
    GetMonitorInfo(MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY), &PrimaryMonitorInfo);

    // TODO: Fullscreen

    // Center window
    u32 WindowX = PrimaryMonitorInfo.rcMonitor.right / 2 - DefaultWindowWidth / 2;
    u32 WindowY = PrimaryMonitorInfo.rcMonitor.bottom / 2 - DefaultWindowHeight / 2;

    // Create window
    WNDCLASSEX WindowClass = { sizeof(WindowClass) };
    WindowClass.style = CS_HREDRAW | CS_VREDRAW; // Always redraw when client area size changes (TODO: confirm)
    WindowClass.lpfnWndProc = Win32ProcedureHandler;
    WindowClass.hInstance = GetModuleHandle(nullptr);
    WindowClass.lpszClassName = L"BlockyWindowClass";
    WindowClass.hCursor = LoadCursor(0, IDC_ARROW);
    WindowClass.hIcon = LoadIcon(WindowClass.hInstance, nullptr);
    WindowClass.hbrBackground = nullptr;
    RegisterClassEx(&WindowClass);

    // NOTE: Also this means that there will be no glitch when opening the game
    // NOTE: When using Vulkan, WS_EX_NOREDIRECTIONBITMAP does not work on Intel GPUs (even when they just pass data)
    DWORD ExStyle = WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP;

    Window.WindowHandle = CreateWindowExW(ExStyle, WindowClass.lpszClassName, L"Blocky", WS_OVERLAPPEDWINDOW,
                             WindowX, WindowY, DefaultWindowWidth, DefaultWindowHeight,
                             0, 0, WindowClass.hInstance, 0);
    Window.ModuleInstance = WindowClass.hInstance;

    RECT ClientRect;
    GetClientRect(Window.WindowHandle, &ClientRect);

    Window.ClientAreaWidth = ClientRect.right - ClientRect.left;
    Window.ClientAreaHeight = ClientRect.bottom - ClientRect.top;

    Trace("Window Client Area: %u, %u", Window.ClientAreaWidth, Window.ClientAreaHeight);

    return Window;
}

int main(int argc, char** argv)
{
    Trace("Hello, Blocky!");

    // If the application is not DPI aware, Windows will automatically scale the pixels to a DPI scale value (150% for example)
    // So if the resolution would be 3840×2160, the application window client area would be 2560×1440, so Windows scales that defaultly.
    // By settings this, Windows will no longer be able to scale pixels resulting in sharper image.
    // Note that DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 is for Windows build version > 1607
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Creates and shows the window
    game_window Window = CreateGameWindow();
    g_ClientWidth = Window.ClientAreaWidth;
    g_ClientHeight = Window.ClientAreaHeight;

#if 1
    dx12_game_renderer Dx12Renderer = CreateDX12GameRenderer(Window);

    // Show window after initialization
    ShowWindow(Window.WindowHandle, SW_SHOW);

    LARGE_INTEGER LastCounter;
    QueryPerformanceCounter(&LastCounter);

    // NOTE: This value represent how many increments of performance counter is happening
    LARGE_INTEGER CounterFrequency;
    QueryPerformanceFrequency(&CounterFrequency);
    bool IsMinimized = false;
    f32 TimeStep = 0.0f;
    g_IsRunning = true;

    game_input Input = {};
    while (g_IsRunning)
    {
        // Process events
        Win32ProcessEvents(&Input);

        IsMinimized = IsIconic(Window.WindowHandle);

        if (g_DoResize)
        {
            g_DoResize = false;

            DX12RendererResizeSwapChain(&Dx12Renderer, g_ClientWidth, g_ClientHeight);
        }

        if (!IsMinimized)
        {
            GameUpdateAndRender(&Dx12Renderer, &Input, g_ClientWidth, g_ClientHeight);
        }

        DX12RendererRender(&Dx12Renderer, g_ClientWidth, g_ClientHeight);
        DX12RendererDumpInfoQueue(Dx12Renderer.DebugInfoQueue);
    }

    DX12GameRendererDestroy(&Dx12Renderer);

#else
    vulkan_game_renderer VulkanRenderer = CreateVulkanGameRenderer(Window);

    // First resize
    {
        RecreateSwapChain(&VulkanRenderer, Window.ClientAreaWidth, Window.ClientAreaHeight);

        // Shows the window
        // Note that there are few transparent frames until we actually render something
        // Sort of nit-picking here
        ShowWindow(Window.WindowHandle, SW_SHOW);
    }

    // Timestep
    f32 TimeStep = 0.0f;

    LARGE_INTEGER LastCounter;
    QueryPerformanceCounter(&LastCounter);

    // NOTE: This value represent how many increments of performance counter is happening
    LARGE_INTEGER CounterFrequency;
    QueryPerformanceFrequency(&CounterFrequency);

    // Game loop
    bool g_IsRunning = true;
    bool IsMinimized = false;
    while (g_IsRunning)
    {
        // Process events
        MSG Message;
        while (PeekMessage(&Message, nullptr, 0, 0, PM_REMOVE))
        {
            if (Message.message == WM_QUIT)
            {
                g_IsRunning = false;
            }

            TranslateMessage(&Message);
            DispatchMessage(&Message);
        }

        IsMinimized = IsIconic(Window.WindowHandle);

        if (g_DoResize)
        {
            g_DoResize = false;

            Camera.RecalculateProjectionPerspective(g_ClientWidth, g_ClientHeight);

            RecreateSwapChain(&VulkanRenderer, g_ClientWidth, g_ClientHeight);
        }

        // Logic
        {
            Camera.View = bkm::Translate(m4(1.0f), Translation)
                * bkm::ToM4(qtn(Rotation))
                * bkm::Scale(m4(1.0f), Scale);

            Camera.View = bkm::Inverse(Camera.View);
        }

        // Do not render when minimized
        if (!IsMinimized)
        {
            BeginRender(&VulkanRenderer, Camera.GetViewProjection());

            SubmitCube(&VulkanRenderer, v3(0), v3(0), v3(1.0f), v4(1.0f, 0.0f, 0.0f, 1.0f));
            SubmitCube(&VulkanRenderer, v3(1, 0, 0), v3(0), v3(1.0f), v4(1.0f, 0.0f, 0.0f, 1.0f));
            SubmitCube(&VulkanRenderer, v3(2, 0, 0), v3(0), v3(1.0f), v4(1.0f, 1.0f, 0.0f, 1.0f));
            SubmitCube(&VulkanRenderer, v3(3, 0, 0), v3(0), v3(1.0f), v4(1.0f, 0.0f, 1.0f, 1.0f));

            EndRender(&VulkanRenderer);
        }

        // Timestep
        LARGE_INTEGER EndCounter;
        QueryPerformanceCounter(&EndCounter);

        i64 CounterElapsed = EndCounter.QuadPart - LastCounter.QuadPart;
        TimeStep = (CounterElapsed / static_cast<f32>(CounterFrequency.QuadPart));
        LastCounter = EndCounter;
    }
#endif

    return 0;
}
