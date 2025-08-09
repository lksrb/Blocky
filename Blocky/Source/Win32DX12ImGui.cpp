#include "Win32DX12ImGui.h"

// Source files
#include <backends/imgui_impl_win32.cpp>
#include <backends/imgui_impl_dx12.cpp>

internal win32_dx12_imgui_context* win32_dx12_imgui_create(arena* Arena, win32_context* Win32Context, dx12_render_backend* DX12Backend)
{
    win32_dx12_imgui_context* Context = arena_new(Arena, win32_dx12_imgui_context);

    // what is this?
    f32 MainScale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle& Style = ImGui::GetStyle();
    Style.ScaleAllSizes(MainScale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    Style.FontScaleDpi = MainScale;        // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 64;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        DxAssert(DX12Backend->Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&Context->SrvDescHeap)));
        Context->DescriptorHeapAllocator.Create(DX12Backend->Device, Context->SrvDescHeap);
    }

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(Win32Context->Window.Handle);

    ImGui_ImplDX12_InitInfo InitInfo = {};
    InitInfo.Device = DX12Backend->Device;
    InitInfo.CommandQueue = DX12Backend->DirectCommandQueue;
    InitInfo.NumFramesInFlight = FIF;
    InitInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    InitInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
    //Allocating SRV descriptors (for textures) is up to the application, so we provide callbacks. (current version of the backend will only allocate one descriptor, future versions will need to allocate more)
    InitInfo.UserData = Context;
    InitInfo.SrvDescriptorHeap = Context->SrvDescHeap;
    InitInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* InitInfo,
        D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle,
        D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle)
    {
        auto Context = ((win32_dx12_imgui_context*)InitInfo->UserData);
        return Context->DescriptorHeapAllocator.Alloc(out_cpu_handle, out_gpu_handle);
    };
    InitInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* InitInfo,
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) 
    { 
        auto Context = ((win32_dx12_imgui_context*)InitInfo->UserData);
        return Context->DescriptorHeapAllocator.Free(cpu_handle, gpu_handle);
    };
    ImGui_ImplDX12_Init(&InitInfo);

    return Context;
}

internal void win32_dx12_imgui_destroy(win32_dx12_imgui_context* Context)
{
    // I want to call this so that imgui remembers the position of debug windows
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

internal void win32_dx12_imgui_begin_frame(win32_dx12_imgui_context* Context)
{
    // Start the Dear ImGui frame
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

internal void win32_dx12_imgui_end_frame(win32_dx12_imgui_context* Context)
{
    // Rendering
    ImGui::Render();
    Context->CurrentDrawData = ImGui::GetDrawData();
}
