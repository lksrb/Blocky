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

#define internal static
#define local_persist static

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

#define InfoV3(__V3) Info("(%.3f, %.3f, %.3f)", __V3.x, __V3.y, __V3.z)

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
    bool W, S, A, D, Q, E;
    bool MouseLeft, MouseMiddle, MouseRight;

    bool MouseLeftPressed, MouseMiddlePressed, MouseRightPressed;

    bool IsCursorLocked;
    bool IsCursorVisible = true;

    V2i LastMousePosition;
    V2i VirtualMousePosition;

    i32 MouseScrollDelta;

    // Temp
    POINT RestoreMousePosition;
};

struct buffer
{
    void* Data;
    u64 Size;
};

#include "DX12Renderer.h"
#include "Blocky.h"

#include "DX12Renderer.cpp"
#include "Blocky.cpp"

internal u32 g_ClientWidth = 0;
internal u32 g_ClientHeight = 0;
internal bool g_DoResize = false;
internal bool g_IsRunning = false;
internal bool g_IsFocused = false;
internal HWND g_WindowHandle;

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
        case WM_ACTIVATE:
        {
            g_IsFocused = LOWORD(WParam) != WA_INACTIVE;

            if (g_IsFocused)
            {
                Trace("Focused.");
            }
            else
            {
                Trace("Unfocused.");
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

internal void Win32ProcessEvents(game_input* Input)
{
    bool DoSetCursorLock = false;
    bool DoSetShowCursor = false;

    MSG Message;

    // Pressed states to get the "click" behaviour
    Input->MouseLeftPressed = false;
    Input->MouseMiddlePressed = false;
    Input->MouseRightPressed = false;

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
                        case 'W': { Input->W = IsDown; break; }
                        case 'S': { Input->S = IsDown; break; }
                        case 'A': { Input->A = IsDown; break; }
                        case 'D': { Input->D = IsDown; break; }
                        case 'Q': { Input->Q = IsDown; break; }
                        case 'E': { Input->E = IsDown; break; }
                        case 'T':
                        {
                            if (IsDown)
                            {
                                DoSetCursorLock = true;
                                DoSetShowCursor = true;

                                // Alternate between true and false
                                Input->IsCursorLocked = !Input->IsCursorLocked;
                                Input->IsCursorVisible = !Input->IsCursorVisible;
                            }
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
            case WM_MOUSEMOVE:
            {
                V2i MousePos = { GET_X_LPARAM(Message.lParam), GET_Y_LPARAM(Message.lParam) };
                V2i MouseDelta = MousePos - Input->LastMousePosition;
                Input->VirtualMousePosition += MouseDelta;
                Input->LastMousePosition = MousePos;

                // Is this really necessary?
                //// Clamping cursor positions so we dont wander off
                //if (Input->LastMousePosition.x < 0) Input->LastMousePosition.x = 0;
                //if (Input->LastMousePosition.y < 0) Input->LastMousePosition.y = 0;
                //if (Input->LastMousePosition.x > static_cast<i32>(g_ClientWidth)) Input->LastMousePosition.x = g_ClientWidth;
                //if (Input->LastMousePosition.y > static_cast<i32>(g_ClientHeight)) Input->LastMousePosition.y = g_ClientHeight;

                //if (Input->VirtualMousePosition.x < 0) Input->VirtualMousePosition.x = 0;
                //if (Input->VirtualMousePosition.y < 0) Input->VirtualMousePosition.y = 0;
                //if (Input->VirtualMousePosition.x > static_cast<i32>(g_ClientWidth)) Input->VirtualMousePosition.x = g_ClientWidth;
                //if (Input->VirtualMousePosition.y > static_cast<i32>(g_ClientHeight)) Input->VirtualMousePosition.y = g_ClientHeight;

                break;
            }
            case WM_MOUSEWHEEL:
            {
                // Gets reset every frame
                i16 Delta = GET_WHEEL_DELTA_WPARAM(Message.wParam);
                Input->MouseScrollDelta = (Delta >> 15) | 1;
                break;
            }
            case WM_LBUTTONDOWN:
            case WM_LBUTTONDBLCLK:
            {
                Input->MouseLeft = true;
                Input->MouseLeftPressed = true;
                break;
            }
            case WM_RBUTTONDOWN:
            case WM_RBUTTONDBLCLK:
            {
                Input->MouseRight = true;
                Input->MouseRightPressed = true;
                break;
            }
            case WM_MBUTTONDOWN:
            case WM_MBUTTONDBLCLK:
            {
                Input->MouseMiddle = true;
                Input->MouseMiddlePressed = true;
                break;
            }
            case WM_LBUTTONUP:
            {
                Input->MouseLeft = false;
                break;
            }
            case WM_RBUTTONUP:
            {
                Input->MouseRight = false;
                break;
            }
            case WM_MBUTTONUP:
            {
                Input->MouseMiddle = false;
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

    if (DoSetShowCursor)
    {
        DoSetShowCursor = false;

        ::ShowCursor(Input->IsCursorVisible);
    }

    // Deferred events to avoid bloated switch statement
    if (DoSetCursorLock)
    {
        DoSetCursorLock = false;

        // Lock
        if (Input->IsCursorLocked)
        {
            Trace("Cursor locked.");

            RECT Rect;
            ::GetClientRect(g_WindowHandle, &Rect);

            // Store cursor position
            //::GetCursorPos(&Input->RestoreMousePosition);
            //::ScreenToClient(g_WindowHandle, &Input->RestoreMousePosition);

            //// Center cursor position
            //POINT CenterCursor = { (LONG)g_ClientWidth / 2, (LONG)g_ClientHeight / 2 };
            //::ClientToScreen(g_WindowHandle, &CenterCursor);
            //::SetCursorPos(CenterCursor.x, CenterCursor.y);
        }
        else // Unlock
        {
            Trace("Cursor unlocked.");

            // Set last cursor position from the stored one
          /*  POINT LastCursorPos = Input->RestoreMousePosition;
            ::ClientToScreen(g_WindowHandle, &LastCursorPos);
            ::SetCursorPos(LastCursorPos.x, LastCursorPos.y);
            Input->LastMousePosition = { LastCursorPos.x, LastCursorPos.y };*/
        }
    }

    // Maintain locked cursor
    if (Input->IsCursorLocked && g_IsFocused)
    {
        // Center cursor position
        POINT CenterCursor = { static_cast<int>(g_ClientWidth / 2), static_cast<int>(g_ClientHeight / 2) };
        ::ClientToScreen(g_WindowHandle, &CenterCursor);

        // Center cursor
        if (Input->LastMousePosition.x != CenterCursor.y || Input->LastMousePosition.y != CenterCursor.y)
        {
            POINT Pos = { (LONG)g_ClientWidth / 2, (LONG)g_ClientHeight / 2 };
            Input->LastMousePosition = { Pos.x, Pos.y };
            ::ClientToScreen(g_WindowHandle, &Pos);
            ::SetCursorPos(Pos.x, Pos.y);
        }
    }
}

internal game_window CreateGameWindow()
{
    game_window Window;
    const u32 DefaultWindowWidth = u32(1600 * 1.3f);
    const u32 DefaultWindowHeight = u32(900 * 1.3f);

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
    WindowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC; // Always redraw when client area size changes (TODO: confirm)
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

    // TODO: Figure out how to do this
    g_WindowHandle = Window.WindowHandle;

    // Initialize renderer
    game_renderer GameRenderer = GameRendererCreate(Window);

    // Initialize game
    game Game = GameCreate(&GameRenderer);

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

            GameRendererResizeSwapChain(&GameRenderer, g_ClientWidth, g_ClientHeight);
        }

        if (!IsMinimized)
        {
            GameUpdateAndRender(&Game, &GameRenderer, &Input, TimeStep, g_ClientWidth, g_ClientHeight);
        }

        // Render stuff
        GameRendererRender(&GameRenderer, g_ClientWidth, g_ClientHeight);
        GameRendererDumpInfoQueue(GameRenderer.DebugInfoQueue);

        // Timestep
        LARGE_INTEGER EndCounter;
        QueryPerformanceCounter(&EndCounter);

        i64 CounterElapsed = EndCounter.QuadPart - LastCounter.QuadPart;
        TimeStep = (CounterElapsed / static_cast<f32>(CounterFrequency.QuadPart));
        LastCounter = EndCounter;
    }

    GameRendererDestroy(&GameRenderer);

    return 0;
}
