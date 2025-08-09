#include "Win32_Game.h"

#include <DirectXMath.h>
using namespace DirectX;

// Header files
#include "Win32DX12ImGui.h"
#include "GameRenderer.h"
#include "DX12/DX12RenderBackend.h"
#include "DX12/DX12Texture.h"
#include "Game.h"

// Source files
#include "Game.cpp"
#include "DX12/DX12RenderBackend.cpp"
#include "Win32DX12ImGui.cpp"

internal u32 g_ClientWidth = 0;
internal u32 g_ClientHeight = 0;
internal bool g_DoResize = false;
internal bool g_IsRunning = false;
internal bool g_IsFocused = false;

LRESULT win32_procedure_handler(HWND WindowHandle, UINT Message, WPARAM WParam, LPARAM LParam)
{
    if (ImGui_ImplWin32_WndProcHandler(WindowHandle, Message, WParam, LParam))
        return true;

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
internal buffer win32_read_buffer(const char* Path)
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

internal void win32_process_events(game_input* Input, HWND WindowHandle)
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
        // ImGui gonna eat all events if its focused
        if (ImGui_ImplWin32_WndProcHandler(WindowHandle, Message.message, Message.wParam, Message.lParam))
        {
            continue;
        }

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
                        case 'W': { Input->set_key_state(key::W, IsDown); break; }
                        case 'S': { Input->set_key_state(key::S, IsDown); break; }
                        case 'A': { Input->set_key_state(key::A, IsDown); break; }
                        case 'D': { Input->set_key_state(key::D, IsDown); break; }
                        case 'Q': { Input->set_key_state(key::Q, IsDown); break; }
                        case 'E': { Input->set_key_state(key::E, IsDown); break; }
                        case 'G': { Input->set_key_state(key::G, IsDown); break; }
                        case 'T':
                        {
                            Input->set_key_state(key::T, IsDown);

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
                        case VK_CONTROL: { Input->set_key_state(key::Control, IsDown); break; }
                        case VK_SHIFT: { Input->set_key_state(key::Shift, IsDown); break; }
                        case VK_SPACE: { Input->set_key_state(key::Space, IsDown); break; }
                        case VK_BACK: { Input->set_key_state(key::BackSpace, IsDown); break; }
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
                Input->set_mouse_state(mouse::Left, true);
                break;
            }
            case WM_RBUTTONDOWN:
            case WM_RBUTTONDBLCLK:
            {
                Input->set_mouse_state(mouse::Right, true);
                break;
            }
            case WM_MBUTTONDOWN:
            case WM_MBUTTONDBLCLK:
            {
                Input->set_mouse_state(mouse::Middle, true);
                break;
            }
            case WM_LBUTTONUP:
            {
                Input->set_mouse_state(mouse::Left, false);
                break;
            }
            case WM_RBUTTONUP:
            {
                Input->set_mouse_state(mouse::Right, false);
                break;
            }
            case WM_MBUTTONUP:
            {
                Input->set_mouse_state(mouse::Middle, false);
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
            ::GetClientRect(WindowHandle, &Rect);

            // Store cursor position
            ::GetCursorPos(&Input->RestoreMousePosition);
            ::ScreenToClient(WindowHandle, &Input->RestoreMousePosition);

            // Center cursor position
            POINT CenterCursor = { (LONG)g_ClientWidth / 2, (LONG)g_ClientHeight / 2 };
            ::ClientToScreen(WindowHandle, &CenterCursor);
            ::SetCursorPos(CenterCursor.x, CenterCursor.y);
        }
        else // Unlock
        {
            Trace("Cursor unlocked.");

            // Set last cursor position from the stored one
            POINT LastCursorPos = Input->RestoreMousePosition;
            ::ClientToScreen(WindowHandle, &LastCursorPos);
            ::SetCursorPos(LastCursorPos.x, LastCursorPos.y);
            Input->LastMousePosition = { LastCursorPos.x, LastCursorPos.y };
        }
    }

    // Maintain locked cursor
    if (Input->IsCursorLocked && g_IsFocused)
    {
        // Center cursor position
        POINT CenterCursor = { static_cast<int>(g_ClientWidth / 2), static_cast<int>(g_ClientHeight / 2) };
        ::ClientToScreen(WindowHandle, &CenterCursor);

        // Center cursor
        if (Input->LastMousePosition.x != CenterCursor.y || Input->LastMousePosition.y != CenterCursor.y)
        {
            POINT Pos = { (LONG)g_ClientWidth / 2, (LONG)g_ClientHeight / 2 };
            Input->LastMousePosition = { Pos.x, Pos.y };
            ::ClientToScreen(WindowHandle, &Pos);
            ::SetCursorPos(Pos.x, Pos.y);
        }
    }
}

internal win32_context* win32_context_create(arena* Arena, HMODULE ModuleInstance)
{
    win32_context* Win32Context = arena_new(Arena, win32_context);

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
    WindowClass.lpfnWndProc = win32_procedure_handler;
    WindowClass.hInstance = ModuleInstance;
    WindowClass.lpszClassName = L"BlockyWindowClass";
    WindowClass.hCursor = LoadCursor(0, IDC_ARROW);
    WindowClass.hIcon = LoadIcon(WindowClass.hInstance, nullptr);
    WindowClass.hbrBackground = nullptr;
    RegisterClassEx(&WindowClass);

    // NOTE: Also this means that there will be no glitch when opening the game
    // NOTE: When using Vulkan, WS_EX_NOREDIRECTIONBITMAP does not work on Intel GPUs (even when they just pass data)
    DWORD ExStyle = WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP;

    HWND WindowHandle = CreateWindowExW(ExStyle, WindowClass.lpszClassName, L"Blocky", WS_OVERLAPPEDWINDOW,
                             WindowX, WindowY, DefaultWindowWidth, DefaultWindowHeight,
                             0, 0, WindowClass.hInstance, 0);
    RECT ClientRect;
    GetClientRect(WindowHandle, &ClientRect);

    // Register mouse device for raw input
    {
        RAWINPUTDEVICE RawInputDevice[1];

        // Register mouse
        RawInputDevice[0].usUsagePage = HID_USAGE_PAGE_GENERIC; // Generic desktop controls
        RawInputDevice[0].usUsage = HID_USAGE_GENERIC_MOUSE;
        RawInputDevice[0].dwFlags = RIDEV_INPUTSINK;            // Receive input even if not in focus
        RawInputDevice[0].hwndTarget = WindowHandle;
        BOOL success = RegisterRawInputDevices(RawInputDevice, CountOf(RawInputDevice), sizeof(RAWINPUTDEVICE));

        // TODO: Fallback to default input
        Assert(success, "Failed to register raw input device!");
    }

    Win32Context->Window.Handle = WindowHandle;
    Win32Context->Window.ClientAreaWidth = ClientRect.right - ClientRect.left;
    Win32Context->Window.ClientAreaHeight = ClientRect.bottom - ClientRect.top;

    Trace("Window Client Area: %u, %u", Win32Context->Window.ClientAreaWidth, Win32Context->Window.ClientAreaHeight);

    return Win32Context;
}

int main(int argc, char** argv)
{
    Trace("Hello, Blocky!");

    HMODULE ModuleInstance = GetModuleHandle(nullptr);

    // If the application is not DPI aware, Windows will automatically scale the pixels to a DPI scale value (150% for example)
    // So if the resolution is 3840×2160, the application window client area would be 2560×1440, so Windows scales that defaultly.
    // By setting this, Windows will no longer be able to scale pixels resulting in sharper image.
    // Note that DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 is for Windows build version > 1607,
    // so we need to add something if this failes
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    arena Arena = {};
    Arena.Capacity = align(100 * 1024 * 1024, SMALL_PAGE_SIZE);
    Arena.MemoryBase = (u8*)::VirtualAlloc(nullptr, Arena.Capacity, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    Arena.MemoryPointer = Arena.MemoryBase;

    // Creates and shows the window
    win32_context* Win32Context = win32_context_create(&Arena, ModuleInstance);

    // TODO: How to get rid of these globals?
    g_ClientWidth = Win32Context->Window.ClientAreaWidth;
    g_ClientHeight = Win32Context->Window.ClientAreaHeight;

    // Get current system time and use its value to initialize seed for our random
    FILETIME FileTime;
    GetSystemTimePreciseAsFileTime(&FileTime);
    random_set_seed(FileTime.dwLowDateTime);

    // Initialize dx12 backend
    dx12_render_backend* DX12Backend = dx12_render_backend_create(&Arena, Win32Context->Window);

    // Initialize renderer
    game_renderer* Renderer = game_renderer_create(&Arena);

    // Initialize imgui 
    win32_dx12_imgui_context* Win32Dx12ImGuiContext = win32_dx12_imgui_create(&Arena, Win32Context, DX12Backend);

    // Initialize game
    game* Game = game_create(&Arena, DX12Backend);

    // Show window after initialization
    ShowWindow(Win32Context->Window.Handle, SW_SHOW);

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
        // Process events
        win32_process_events(&Input, Win32Context->Window.Handle);

        // Only reliable way of knowing if we are minimized or not
        IsMinimized = IsIconic(Win32Context->Window.Handle);

        if (g_DoResize)
        {
            g_DoResize = false;

            d3d12_render_backend_resize_swapchain(DX12Backend, g_ClientWidth, g_ClientHeight);
        }

        // ImGui::Begin();
        if (!IsMinimized)
        {
            //scoped_timer timer("Game update");

            v2i ClientArea = { (i32)g_ClientWidth, (i32)g_ClientHeight };
            game_update(Game, Renderer,  &Input, TimeStep, ClientArea);

            win32_dx12_imgui_begin_frame(Win32Dx12ImGuiContext);
            game_debug_ui_update(Game, Renderer, &Input, TimeStep, ClientArea);
            win32_dx12_imgui_end_frame(Win32Dx12ImGuiContext);
        }
        // ImGui::End

        // Render stuff
        d3d12_render_backend_render(DX12Backend, Renderer, Win32Dx12ImGuiContext->CurrentDrawData, Win32Dx12ImGuiContext->SrvDescHeap);

        // Clear old state
        game_renderer_clear(Renderer);

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

            SetWindowTextA(Win32Context->Window.Handle, Title);
        }
        else
        {
            EverySecond += TimeStep;
        }

        // Clamp Timestep to atleast 60 fps to preserve physics simulation and update simulation aswell.
        TimeStep = bkm::Clamp(TimeStep, 0.0f, 0.01666666f);
    }

    dx12_render_backend_destroy(DX12Backend);

    return 0;
}
