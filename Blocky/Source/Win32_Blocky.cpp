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

    u32 ClientAreaWidth;
    u32 ClientAreaHeight;
};

struct buffer
{
    void* Data;
    u64 Size;
};

#include "VulkanRenderer.h"
#include "DX12Renderer.h"

static u32 g_ClientWidth = 0;
static u32 g_ClientHeight = 0;
static bool g_DoResize = false;

LRESULT Win32ProcedureHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT Result = 0;

    switch (msg)
    {
        case WM_SIZE:
        {
            u32 width = GET_X_LPARAM(lParam);
            u32 height = GET_Y_LPARAM(lParam);

            // NOTE: Swapchain has a problem with width and height being the same after restoring the window and requires
            // resize as well
            if (width != 0 && height != 0)
            {
                if (g_ClientWidth != width || g_ClientHeight != height)
                    g_DoResize = true;

                g_ClientWidth = width;
                g_ClientHeight = height;

            }
            break;
        }
        case WM_SYSKEYUP:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_KEYDOWN:
        {
            //Log("Not raw");

            u64 vkCode = wParam;
            bool wasDown = (lParam & (1 << 30)) != 0;
            bool isDown = (lParam & (1 << 31)) == 0;

            // Don't allow repeats
            if (isDown != wasDown)
            {
                switch (vkCode)
                {
                    case VK_ESCAPE:
                    {
                        PostQuitMessage(0);
                        break;
                    }
                }
            }

            break;
        }
        case WM_DESTROY:
        case WM_CLOSE:
        {
            PostQuitMessage(0);
            break;
        }
        default:
        {
            Result = DefWindowProc(hWnd, msg, wParam, lParam);
            break;
        }
    }

    //Log("%i", msg);

    return Result;
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

struct camera
{
    m4 Projection{ 1.0f };
    m4 View{ 1.0f };

    f32 OrthographicSize = 15.0f;
    f32 OrthographicNear = -100.0f, OrthographicFar = 100.0f;

    f32 AspectRatio = 0.0f;

    f32 PerspectiveFOV = MyMath::PI_HALF;
    f32 PerspectiveNear = 0.01f, PerspectiveFar = 1000.0f;

    void RecalculateProjectionOrtho(u32 Width, u32 Height)
    {
        AspectRatio = static_cast<f32>(Width) / Height;
        f32 OrthoLeft = -0.5f * AspectRatio * OrthographicSize;
        f32 OrthoRight = 0.5f * AspectRatio * OrthographicSize;
        f32 OrthoBottom = -0.5f * OrthographicSize;
        f32 OrthoTop = 0.5f * OrthographicSize;
        Projection = MyMath::Ortho(OrthoLeft, OrthoRight, OrthoBottom, OrthoTop, OrthographicNear, OrthographicFar);
    }

    void RecalculateProjectionPerspective(u32 width, u32 height)
    {
        AspectRatio = static_cast<f32>(width) / height;
        Projection = MyMath::Perspective(PerspectiveFOV, AspectRatio, PerspectiveNear, PerspectiveFar);
    }

    m4 GetViewProjection() const { return Projection * View; }
};

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

    dx12_game_renderer Dx12Renderer = CreateDX12GameRenderer(Window);

    // Show window after initialization
    ShowWindow(Window.WindowHandle, SW_SHOW);

    LARGE_INTEGER LastCounter;
    QueryPerformanceCounter(&LastCounter);

    // NOTE: This value represent how many increments of performance counter is happening
    LARGE_INTEGER CounterFrequency;
    QueryPerformanceFrequency(&CounterFrequency);
    bool IsRunning = true;
    bool IsMinimized = false;
    f32 TimeStep = 0.0f;
    while (IsRunning)
    {
        // Process events
        MSG Message;
        while (PeekMessage(&Message, nullptr, 0, 0, PM_REMOVE))
        {
            if (Message.message == WM_QUIT)
            {
                IsRunning = false;
            }

            TranslateMessage(&Message);
            DispatchMessage(&Message);
        }

        IsMinimized = IsIconic(Window.WindowHandle);

        if (g_DoResize)
        {
            g_DoResize = false;
            DX12RendererResizeSwapChain(&Dx12Renderer, g_ClientWidth, g_ClientHeight);
        }

        if (!IsMinimized)
        {
            DX12RendererRender(&Dx12Renderer);
        }
    }

    DX12RendererFlush(Dx12Renderer.CommandQueue, Dx12Renderer.Fence, &Dx12Renderer.FenceValue, Dx12Renderer.FenceEvent);

#if 0
    vulkan_game_renderer VulkanRenderer = CreateVulkanGameRenderer(Window);

    // First resize
    {
        RecreateSwapChain(&VulkanRenderer, Window.ClientAreaWidth, Window.ClientAreaHeight);

        // Shows the window
        // Note that there are few transparent frames until we actually render something
        // Sort of nit-picking here
        ShowWindow(Window.WindowHandle, SW_SHOW);
    }

    camera Camera;
    v3 translation{ 0.0f, 1.0f, 3.0f };
    v3 rotation{ -0.2f };
    v3 scale{ 1.0f };
    Camera.RecalculateProjectionPerspective(g_ClientWidth, g_ClientHeight);

    // Timestep
    f32 TimeStep = 0.0f;

    LARGE_INTEGER LastCounter;
    QueryPerformanceCounter(&LastCounter);

    // NOTE: This value represent how many increments of performance counter is happening
    LARGE_INTEGER CounterFrequency;
    QueryPerformanceFrequency(&CounterFrequency);

    // Game loop
    bool IsRunning = true;
    bool IsMinimized = false;
    while (IsRunning)
    {
        // Process events
        MSG Message;
        while (PeekMessage(&Message, nullptr, 0, 0, PM_REMOVE))
        {
            if (Message.message == WM_QUIT)
            {
                IsRunning = false;
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
            Camera.View = MyMath::Translate(m4(1.0f), translation)
                * MyMath::ToM4(QTN(rotation))
                * MyMath::Scale(m4(1.0f), scale);

            Camera.View = MyMath::Inverse(Camera.View);
        }

        // Do not render when minimized
        if (!IsMinimized)
        {
            BeginRender(&VulkanRenderer, Camera.GetViewProjection());

            {
                v3 Pos = v3{ -2.0f, 0.0f, 0.0f };
                SubmitQuad(&VulkanRenderer, Pos, v3(0.0f), v3(1.0f), v4(1.0f, 0.0f, 0.0f, 1.0f));
                Pos.x += 1.0f;
                SubmitQuad(&VulkanRenderer, Pos, v3(0.0f), v3(1.0f), v4(0.0f, 1.0f, 0.0f, 1.0f));
                Pos.x += 1.0f;
                SubmitQuad(&VulkanRenderer, Pos, v3(0.0f), v3(1.0f), v4(0.0f, 0.0f, 1.0f, 1.0f));
                Pos.x += 1.0f;
                SubmitQuad(&VulkanRenderer, Pos, v3(0.0f), v3(1.0f), v4(0.0f, 1.0f, 1.0f, 1.0f));
                Pos.x += 1.0f;
                SubmitQuad(&VulkanRenderer, Pos, v3(0.0f), v3(1.0f), v4(1.0f, 0.0f, 1.0f, 1.0f));
                Pos.x += 1.0f;
            }

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
