#pragma once

#define render_backend dx12_render_backend

#include "DX12.h"
#include "DX12Buffer.h"
#include "DX12Texture.h"
#include "DX12Pipeline.h"

#define ENABLE_SHADOW_PASS 1
#define SHADOW_MAP_SIZE 1024

struct dx12_render_backend
{
    ID3D12Device2* Device;
    IDXGIFactory4* Factory;

    ID3D12Debug3* DebugInterface; // 3 is maximum for Windows 10 build 19045

    // This is not defined in release mode
#if BK_DEBUG
    IDXGIDebug1* DxgiDebugInterface;
#endif

    struct
    {
        u32 RTV;
        u32 DSV;
        u32 CBV_SRV_UAV;
    } DescriptorSizes;

    ID3D12CommandAllocator* DirectCommandAllocators[FIF];
    ID3D12GraphicsCommandList2* DirectCommandList;
    ID3D12CommandQueue* DirectCommandQueue;

    IDXGISwapChain4* SwapChain;
    ID3D12Resource* SwapChainBackbuffers[FIF];
    D3D12_CPU_DESCRIPTOR_HANDLE SwapChainBufferRTVHandles[FIF];
    DXGI_FORMAT SwapChainFormat;


    ID3D12DescriptorHeap* RTVDescriptorHeap;

    u32 CurrentBackBufferIndex;

    bool DepthTesting = true;
    bool VSync = true;
    // Fence
    ID3D12Fence* Fence;
    u64 FenceValue;
    u64 FrameFenceValues[FIF];
    HANDLE DirectFenceEvent;

    // Describes resources used in the shader
    ID3D12RootSignature* RootSignature;

    // Textures
    ID3D12DescriptorHeap* TextureDescriptorHeap;
    texture WhiteTexture;

    // These will be rendered to and then copied via fullscreen pass to swapchain buffers
    struct
    {
        ID3D12Resource* DepthBuffers[FIF];
        ID3D12DescriptorHeap* DSVDescriptorHeap;
        D3D12_CPU_DESCRIPTOR_HANDLE DSVHandles[FIF];

        ID3D12Resource* RenderBuffers[FIF];
        ID3D12DescriptorHeap* RTVDescriptorHeap;
        D3D12_CPU_DESCRIPTOR_HANDLE RTVHandles[FIF];

        DXGI_FORMAT Format;

        ID3D12DescriptorHeap* SRVDescriptorHeap;
    } MainPass;

    // Quad
    struct
    {
        dx12_pipeline Pipeline;
        dx12_index_buffer IndexBuffer;
        dx12_vertex_buffer VertexBuffers[FIF];
        ID3D12RootSignature* RootSignature;
    } Quad;

    // Basic Cuboid - Has the same texture for each face
    struct
    {
        dx12_pipeline Pipeline = {};
        dx12_vertex_buffer TransformVertexBuffers[FIF] = {};
        dx12_vertex_buffer PositionsVertexBuffer = {};
        ID3D12RootSignature* RootSignature;
    } Cuboid;

    // Quaded Cuboid - Can have unique texture for each face
    struct
    {
        dx12_pipeline Pipeline = {};
        dx12_vertex_buffer VertexBuffers[FIF];
    } QuadedCuboid;

    // Skybox
    struct
    {
        dx12_pipeline Pipeline;
        dx12_vertex_buffer VertexBuffer;
        dx12_index_buffer IndexBuffer;
        ID3D12RootSignature* RootSignature;
    } Skybox;

    // Distant quads (sun, moon, stars, ...)
    struct
    {
        dx12_pipeline Pipeline;
        dx12_vertex_buffer VertexBuffers[FIF];
    } DistantQuad;

#if ENABLE_SHADOW_PASS
    // Shadows
    struct
    {
        ID3D12Resource* ShadowMaps[FIF];
        D3D12_CPU_DESCRIPTOR_HANDLE DSVHandles[FIF];
        //D3D12_CPU_DESCRIPTOR_HANDLE OfflineSRVHandles[FIF];
        dx12_pipeline Pipeline;
        ID3D12RootSignature* RootSignature;
        ID3D12DescriptorHeap* DSVDescriptorHeap;
        //ID3D12DescriptorHeap* OfflineDescriptorHeap;
    } ShadowPass;
#endif

    // HUD stuff
    struct
    {
        dx12_vertex_buffer VertexBuffers[FIF];
        dx12_pipeline Pipeline;
    } HUD;

    // Fullscreen pass
    struct
    {
        dx12_pipeline Pipeline;
        ID3D12RootSignature* RootSignature;
    } FullscreenPass;

    dx12_constant_buffer LightEnvironmentConstantBuffers[FIF];
};

internal dx12_render_backend* dx12_render_backend_create(arena* Arena, const win32_window& Window);
internal void dx12_render_backend_destroy(dx12_render_backend* Backend);

internal void dx12_render_backend_initialize_pipeline(arena* Arena, dx12_render_backend* Backend);
internal void d3d12_render_backend_resize_swapchain(dx12_render_backend* Backend, u32 RequestWidth, u32 RequestHeight);
internal void d3d12_render_backend_render(dx12_render_backend* Backend, const game_renderer* Renderer, ImDrawData* ImGuiDrawData, ID3D12DescriptorHeap* ImGuiDescriptorHeap);

internal u64 dx12_render_backend_signal(ID3D12CommandQueue* CommandQueue, ID3D12Fence* Fence, u64* FenceValue);
internal void dx12_render_backend_wait_for_fence_value(ID3D12Fence* Fence, u64 FenceValue, HANDLE FenceEvent, u32 Duration = UINT32_MAX);
internal void dx12_render_backend_flush(dx12_render_backend* Backend);
