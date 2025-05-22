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

struct game_window
{
    HWND Handle;
    HINSTANCE ModuleInstance;

    u32 ClientAreaWidth;
    u32 ClientAreaHeight;
};

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

    void SetKeyState(key Key, bool IsDown)
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

// Headers
#include "DX12/DX12Renderer.h"
#include "Blocky.h"

// Source files
#include "Blocky.cpp"
#include "DX12/DX12Renderer.cpp"

internal u32 g_ClientWidth = 0;
internal u32 g_ClientHeight = 0;
internal bool g_DoResize = false;
internal bool g_IsRunning = false;
internal bool g_IsFocused = false;

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

// TODO: Move somewhere
internal buffer ReadBinary(const char* Path)
{
    buffer Result;

    HANDLE FileHandle = ::CreateFileA(Path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (FileHandle != INVALID_HANDLE_VALUE)
    {
        DWORD dwBytesRead = 0;
        DWORD dwBytesWritten = 0;
        LARGE_INTEGER size;
        if (::GetFileSizeEx(FileHandle, &size))
        {
            // NOTE: We allocate more but that does not matter for now
            u8* data = static_cast<u8*>(::VirtualAlloc(nullptr, size.QuadPart, MEM_COMMIT, PAGE_READWRITE));

            if (::ReadFile(FileHandle, data, static_cast<DWORD>(size.QuadPart), &dwBytesRead, nullptr))
            {
                Result.Data = data;
                Result.Size = static_cast<u32>(size.QuadPart);
            }
            else
            {
                ::VirtualFree(data, 0, MEM_RELEASE);
            }
        }

        ::CloseHandle(FileHandle);
    }

    return Result;
}

internal void Win32ProcessEvents(game_input* Input, const game_window& Window)
{
    bool DoSetCursorLock = false;
    bool DoSetShowCursor = false;

    MSG Message;

    // Reset pressed states to get the "click" behaviour
    memset(Input->KeyPressed, 0, sizeof(Input->KeyPressed));
    memset(Input->MousePressed, 0, sizeof(Input->MousePressed));

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
                        case 'W': { Input->SetKeyState(key::W, IsDown); break; }
                        case 'S': { Input->SetKeyState(key::S, IsDown); break; }
                        case 'A': { Input->SetKeyState(key::A, IsDown); break; }
                        case 'D': { Input->SetKeyState(key::D, IsDown); break; }
                        case 'Q': { Input->SetKeyState(key::Q, IsDown); break; }
                        case 'E': { Input->SetKeyState(key::E, IsDown); break; }
                        case 'G': { Input->SetKeyState(key::G, IsDown); break; }
                        case 'T':
                        {
                            Input->SetKeyState(key::T, IsDown);

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
                        case VK_CONTROL: { Input->SetKeyState(key::Control, IsDown); break; }
                        case VK_SHIFT: { Input->SetKeyState(key::Shift, IsDown); break; }
                        case VK_SPACE: { Input->SetKeyState(key::Space, IsDown); break; }
                        case VK_BACK: { Input->SetKeyState(key::BackSpace, IsDown); break; }
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
                 v2i MousePos = { GET_X_LPARAM(Message.lParam), GET_Y_LPARAM(Message.lParam) };
                 v2i MouseDelta = MousePos - Input->LastMousePosition;
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
                Input->SetMouseState(mouse::Left, true);
                break;
            }
            case WM_RBUTTONDOWN:
            case WM_RBUTTONDBLCLK:
            {
                Input->SetMouseState(mouse::Right, true);
                break;
            }
            case WM_MBUTTONDOWN:
            case WM_MBUTTONDBLCLK:
            {
                Input->SetMouseState(mouse::Middle, true);
                break;
            }
            case WM_LBUTTONUP:
            {
                Input->SetMouseState(mouse::Left, false);
                break;
            }
            case WM_RBUTTONUP:
            {
                Input->SetMouseState(mouse::Right, false);
                break;
            }
            case WM_MBUTTONUP:
            {
                Input->SetMouseState(mouse::Middle, false);
                break;
            }

            case WM_QUIT:
            {
                g_IsRunning = false;
                break;
            }
            case WM_INPUT:
            {
                // The official Microsoft examples are pretty terrible about this.
                // Size needs to be non-constant because GetRawInputData() can return the
                // size necessary for the RAWINPUT data, which is a weird feature.
                UINT RawInputStructSize = sizeof(RAWINPUT);
                static RAWINPUT RawInput[sizeof(RAWINPUT)];
                if (GetRawInputData((HRAWINPUT)Message.lParam, RID_INPUT, RawInput, &RawInputStructSize, sizeof(RAWINPUTHEADER)))
                {
                    if (RawInput->header.dwType == RIM_TYPEMOUSE)
                    {
                        RAWMOUSE RawMouse = RawInput->data.mouse;

                        if (RawMouse.usFlags != MOUSE_MOVE_RELATIVE)
                            break;

                        v2i Delta(RawMouse.lLastX, RawMouse.lLastY);
                        Input->RawVirtualMousePosition += Delta;
                        Input->RawLastMousePosition = Delta;

                        //// Clamping cursor positions so we dont wander off
                        //   RECT rect;
                        //GetClientRect(Window.Handle, &rect);
                        //if (io.LastCursorPos.x < 0) io.LastCursorPos.x = 0;
                        //if (io.LastCursorPos.y < 0) io.LastCursorPos.y = 0;
                        //if (io.LastCursorPos.x > static_cast<i32>(app.Width)) io.LastCursorPos.x = app.Width;
                        //if (io.LastCursorPos.y > static_cast<i32>(app.Height)) io.LastCursorPos.y = app.Height;

                        //if (io.VirtualCursorPos.x < 0) io.VirtualCursorPos.x = 0;
                        //if (io.VirtualCursorPos.y < 0) io.VirtualCursorPos.y = 0;
                        //if (io.VirtualCursorPos.x > static_cast<i32>(app.Width)) io.VirtualCursorPos.x = app.Width;
                        //if (io.VirtualCursorPos.y > static_cast<i32>(app.Height)) io.VirtualCursorPos.y = app.Height;
                    }
                }

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

    // Prevents continuous key event when Alt+Tab happen and a key was pressed
    if (!g_IsFocused)
    {
        memset(Input->KeyDown, 0, sizeof(Input->KeyDown));
        memset(Input->MouseDown, 0, sizeof(Input->MouseDown));
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
            ::GetClientRect(Window.Handle, &Rect);

            // Store cursor position
            ::GetCursorPos(&Input->RestoreMousePosition);
            ::ScreenToClient(Window.Handle, &Input->RestoreMousePosition);

            // Center cursor position
            POINT CenterCursor = { (LONG)g_ClientWidth / 2, (LONG)g_ClientHeight / 2 };
            ::ClientToScreen(Window.Handle, &CenterCursor);
            ::SetCursorPos(CenterCursor.x, CenterCursor.y);
        }
        else // Unlock
        {
            Trace("Cursor unlocked.");

            // Set last cursor position from the stored one
            POINT LastCursorPos = Input->RestoreMousePosition;
            ::ClientToScreen(Window.Handle, &LastCursorPos);
            ::SetCursorPos(LastCursorPos.x, LastCursorPos.y);
            Input->LastMousePosition = { LastCursorPos.x, LastCursorPos.y };
        }
    }

    // Maintain locked cursor
    if (Input->IsCursorLocked && g_IsFocused)
    {
        // Center cursor position
        POINT CenterCursor = { static_cast<int>(g_ClientWidth / 2), static_cast<int>(g_ClientHeight / 2) };
        ::ClientToScreen(Window.Handle, &CenterCursor);

        // Center cursor
        if (Input->LastMousePosition.x != CenterCursor.y || Input->LastMousePosition.y != CenterCursor.y)
        {
            POINT Pos = { (LONG)g_ClientWidth / 2, (LONG)g_ClientHeight / 2 };
            Input->LastMousePosition = { Pos.x, Pos.y };
            ::ClientToScreen(Window.Handle, &Pos);
            ::SetCursorPos(Pos.x, Pos.y);
        }
    }
}

internal game_window CreateGameWindow()
{
    game_window Window;

    // Show window on primary window
    // TODO: User should choose on which monitor to display
    // TODO: Error handle
    MONITORINFO PrimaryMonitorInfo = { sizeof(MONITORINFO) };
    GetMonitorInfo(MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY), &PrimaryMonitorInfo);

    u32 DefaultWindowWidth = u32((PrimaryMonitorInfo.rcMonitor.right - PrimaryMonitorInfo.rcMonitor.left) * 0.85f);
    u32 DefaultWindowHeight = u32((PrimaryMonitorInfo.rcMonitor.bottom - PrimaryMonitorInfo.rcMonitor.top) * 0.85f);

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

    Window.Handle = CreateWindowExW(ExStyle, WindowClass.lpszClassName, L"Blocky", WS_OVERLAPPEDWINDOW,
                             WindowX, WindowY, DefaultWindowWidth, DefaultWindowHeight,
                             0, 0, WindowClass.hInstance, 0);
    Window.ModuleInstance = WindowClass.hInstance;

    RECT ClientRect;
    GetClientRect(Window.Handle, &ClientRect);

    Window.ClientAreaWidth = ClientRect.right - ClientRect.left;
    Window.ClientAreaHeight = ClientRect.bottom - ClientRect.top;

    Trace("Window Client Area: %u, %u", Window.ClientAreaWidth, Window.ClientAreaHeight);

    // Register mouse device for raw input
    {
        RAWINPUTDEVICE rid[1];

        // Register mouse
        rid[0].usUsagePage = HID_USAGE_PAGE_GENERIC; // Generic desktop controls
        rid[0].usUsage = HID_USAGE_GENERIC_MOUSE;
        rid[0].dwFlags = RIDEV_INPUTSINK;            // Receive input even if not in focus
        rid[0].hwndTarget = Window.Handle;
        BOOL success = RegisterRawInputDevices(rid, CountOf(rid), sizeof(RAWINPUTDEVICE));
        // TODO: Fallback to default input
        Assert(success, "Failed to register raw input device!");
    }

    return Window;
}

int main(int argc, char** argv)
{
    Trace("Hello, Blocky!");

    // If the application is not DPI aware, Windows will automatically scale the pixels to a DPI scale value (150% for example)
    // So if the resolution is 3840×2160, the applicaton window client area would be 2560×1440, so Windows scales that defaultly.
    // By settings this, Windows will no longer be able to scale pixels resulting in sharper image.
    // Note that DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 is for Windows build version > 1607,
    // so we need to add something if this failes
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Creates and shows the window
    game_window Window = CreateGameWindow();
    g_ClientWidth = Window.ClientAreaWidth;
    g_ClientHeight = Window.ClientAreaHeight;

    // Get current system time and use its value to initialize seed for our random
    FILETIME FileTime;
    GetSystemTimePreciseAsFileTime(&FileTime);
    RandomSetSeed(FileTime.dwLowDateTime);

    // Initialize renderer
    game_renderer GameRenderer = GameRendererCreate(Window);

    // Initialize game
    game Game = GameCreate(&GameRenderer);

    // Show window after initialization
    ShowWindow(Window.Handle, SW_SHOW);

    // NOTE: This value represent how many increments of performance counter is happening
    LARGE_INTEGER CounterFrequency;
    QueryPerformanceFrequency(&CounterFrequency);

    LARGE_INTEGER LastCounter;
    QueryPerformanceCounter(&LastCounter);

    // How many cycles per some work done by CPU elapsed?
    // Not super accurate since the OS can insert some other instructions (scheduler)
    DWORD64 LastCycleCount = __rdtsc();

    bool IsMinimized = false;
    f32 TimeStep = 0.0f;
    g_IsRunning = true;
    game_input Input = {};
    while (g_IsRunning)
    {
        //ScopedTimer timer("Frame");

        // Process events
        Win32ProcessEvents(&Input, Window);

        IsMinimized = IsIconic(Window.Handle);

        if (g_DoResize)
        {
            g_DoResize = false;

            GameRendererResizeSwapChain(&GameRenderer, g_ClientWidth, g_ClientHeight);
        }

        if (!IsMinimized)
        {
            //ScopedTimer timer("Game update");
            GameUpdate(&Game, &GameRenderer, &Input, TimeStep, g_ClientWidth, g_ClientHeight);
        }

        // Render stuff
        {
            //ScopedTimer timer("Game render");

            GameRendererRender(&GameRenderer, g_ClientWidth, g_ClientHeight);
        }

        DWORD64 EndCycleCount = __rdtsc();

        // Timestep
        LARGE_INTEGER EndCounter;
        QueryPerformanceCounter(&EndCounter);

        LONGLONG CounterElapsed = EndCounter.QuadPart - LastCounter.QuadPart;
        TimeStep = CounterElapsed / (f32)CounterFrequency.QuadPart;
        LastCounter = EndCounter;

        LONGLONG FPS = CounterFrequency.QuadPart / CounterElapsed;

        DWORD64 CyclesElapsed = EndCycleCount - LastCycleCount;
        LastCycleCount = EndCycleCount;

        local_persist f32 EverySecond = 1.0f;

        // Display it on the title bar
        if (EverySecond >= 1.0f)
        {
            EverySecond = 0.0f;
            char Title[128];
            sprintf_s(Title, "Blocky | TimeStep: %.3f ms | FPS: %d | CycleCount: %d", TimeStep * 1000.0f, (i32)FPS, (i32)CyclesElapsed);

            SetWindowTextA(Window.Handle, Title);
        }
        else
        {
            EverySecond += TimeStep;
        }

        // Clamp Timestep to atleast 60 fps to preserve physics simulation and update simulation aswell.
        TimeStep = bkm::Clamp(TimeStep, 0.0f, 0.01666666f);
    }

    GameRendererDestroy(&GameRenderer);

    return 0;
}
