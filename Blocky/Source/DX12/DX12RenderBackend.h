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
    dx12_descriptor_handle SwapChainBufferRTVViews[FIF];
    DXGI_FORMAT SwapChainFormat;

    dx12_descriptor_heap_free_list_allocator RTVAllocator;
    dx12_descriptor_heap_free_list_allocator DSVAllocator;
    dx12_descriptor_heap_free_list_allocator SRVCBVUAV_Allocator;

    u32 CurrentBackBufferIndex;

    bool DepthTesting = true;
    bool VSync = true;

    // Fence
    ID3D12Fence* Fence;
    u64 FenceValue;
    u64 FrameFenceValues[FIF];
    HANDLE DirectFenceEvent;

    //HANDLE FrameLatencyEvent;

    // Describes resources used in the shader
    dx12_root_signature RootSignature;

    // Textures
    texture WhiteTexture;
    dx12_descriptor_handle WhiteTextureDescriptorHandle;

    // These will be rendered to and then copied via fullscreen pass to swapchain buffers
    struct
    {
        ID3D12Resource* DepthBuffers[FIF];
        dx12_descriptor_handle DepthBuffersViews[FIF];

        ID3D12Resource* RenderBuffers[FIF];
        dx12_descriptor_handle RenderBuffersRTVViews[FIF];

        DXGI_FORMAT Format;

        dx12_descriptor_handle RenderBuffersSRVViews[FIF];
    } MainPass;

    // Bloom pass needs to be added, fullscreen pass does not cut it
    struct
    {
        dx12_root_signature RootSignature;
        dx12_pipeline PipelineCompute;
    } BloomPass;

    // Quad
    struct
    {
        dx12_pipeline Pipeline;
        dx12_index_buffer IndexBuffer;
        dx12_vertex_buffer VertexBuffers[FIF];
        dx12_root_signature RootSignature;
    } Quad;

    // Basic Cuboid - Has the same texture for each face
    struct
    {
        dx12_pipeline Pipeline = {};
        dx12_vertex_buffer TransformVertexBuffers[FIF] = {};
        dx12_vertex_buffer PositionsVertexBuffer = {};
        dx12_root_signature RootSignature;
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
        dx12_root_signature RootSignature;
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
        dx12_descriptor_handle ShadowMapsDSVViews[FIF];
        dx12_descriptor_handle ShadowMapsSRVViews[FIF];
        dx12_pipeline Pipeline;
        dx12_root_signature RootSignature;
    } ShadowPass;
#endif

    // HUD stuff
    struct
    {
        dx12_vertex_buffer VertexBuffers[FIF];
        dx12_pipeline Pipeline;
    } HUD;

    // Fullscreen Pass
    struct
    {
        dx12_pipeline Pipeline;
        dx12_root_signature RootSignature;
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
