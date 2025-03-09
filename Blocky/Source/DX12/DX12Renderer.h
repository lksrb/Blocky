/*
 * Note that this renderer is game specific so it does not have to be completely api-agnostic,
 * For example instead of having general set push constants functions, we can have set view projection function etc.
 * We will see in the future if this is a good approach
 */
#pragma once

#include "DX12.h"
#include "DX12Buffer.h"
#include "DX12Texture.h"
#include "DX12Pipeline.h"

internal constexpr inline u32 c_MaxCubePerBatch = 1 << 16;
internal constexpr inline u32 c_MaxQuadsPerBatch = 1 << 16;
internal constexpr inline u32 c_MaxQuadVertices = c_MaxQuadsPerBatch * 4;
internal constexpr inline u32 c_MaxQuadIndices = c_MaxQuadsPerBatch * 6;
internal constexpr inline u32 c_MaxTexturesPerDrawCall = 32; // TODO: Get this from the driver

struct quad_vertex
{
    v3 Position;
    v4 Color;
    v2 TexCoord;
    u32 TexIndex;
};

struct cuboid_vertex
{
    v4 Position;
    v2 TextureCoord;
};

struct cuboid_transform_vertex_data
{
    union
    {
        m4 Transform;
        XMMATRIX XmmTransform;
    };

    v4 Color;
    u32 TextureIndex;
};

// Basically a push constant
// TODO: Look up standardized push constant minimum value
// I think its 256 bytes
struct dx12_root_signature_constant_buffer
{
    m4 ViewProjection;
};

// API-agnostic type definition
// There is a potential problem with navigating stuff if we would have multiple platforms
struct game_renderer
{
    ID3D12Device2* Device;
    IDXGIFactory4* Factory;

    ID3D12Debug3* DebugInterface; // 3 is maximum for Windows 10 build 19045

    // This is not defined in release mode
#if BK_DEBUG
    IDXGIDebug1* DxgiDebugInterface;
#endif

    ID3D12InfoQueue* DebugInfoQueue;

    ID3D12CommandAllocator* DirectCommandAllocators[FIF];
    ID3D12GraphicsCommandList2* DirectCommandList;
    ID3D12CommandQueue* DirectCommandQueue;

    ID3D12CommandAllocator* CopyCommandAllocators[FIF];
    ID3D12CommandQueue* CopyCommandQueue;
    ID3D12GraphicsCommandList2* CopyCommandList;

    IDXGISwapChain4* SwapChain;
    ID3D12Resource* BackBuffers[FIF];
    ID3D12DescriptorHeap* RTVDescriptorHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE RTVHandles[FIF];

    u32 CurrentBackBufferIndex;

    ID3D12Resource* DepthBuffers[FIF];
    ID3D12DescriptorHeap* DSVDescriptorHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE DSVHandles[FIF];

    bool DepthTesting = true;

    bool VSync = true;

    // Fence
    ID3D12Fence* Fence;
    u64 FenceValue;
    u64 FrameFenceValues[FIF];
    HANDLE DirectFenceEvent;

    // Describes resources used in the shader
    ID3D12RootSignature* RootSignature;

    D3D12_VIEWPORT Viewport;
    D3D12_RECT ScissorRect;

    // Textures
    ID3D12DescriptorHeap* SRVDescriptorHeap;
    texture WhiteTexture;
    u32 CurrentTextureStackIndex = 1;
    texture TextureStack[c_MaxTexturesPerDrawCall];

    // Quad
    dx12_pipeline QuadPipeline;
    dx12_index_buffer QuadIndexBuffer;
    dx12_vertex_buffer QuadVertexBuffers[FIF];
    quad_vertex* QuadVertexDataBase;
    quad_vertex* QuadVertexDataPtr;
    u32 QuadIndexCount = 0;
    dx12_root_signature_constant_buffer QuadRootSignatureBuffer;

    // Cuboid
    u32 CuboidInstanceCount = 0;
    dx12_pipeline CuboidPipeline = {};
    dx12_vertex_buffer CuboidTransformVertexBuffers[FIF] = {};
    dx12_vertex_buffer CuboidPositionsVertexBuffer = {};
    cuboid_transform_vertex_data* CuboidInstanceData = nullptr;
    dx12_root_signature_constant_buffer CuboidRootSignatureBuffer;
};

// Init / Destroy functions
internal game_renderer GameRendererCreate(game_window Window);
internal void GameRendererDestroy(game_renderer* Renderer);

internal void GameRendererInitD3D(game_renderer* Renderer, game_window Window);
internal void GameRendererInitD3DPipeline(game_renderer* Renderer);
internal void GameRendererResizeSwapChain(game_renderer* Renderer, u32 RequestWidth, u32 RequestHeight);
internal void GameRendererRender(game_renderer* Renderer, u32 Width, u32 Height);
internal u64 GameRendererSignal(ID3D12CommandQueue* CommandQueue, ID3D12Fence* Fence, u64* FenceValue);
internal void GameRendererWaitForFenceValue(ID3D12Fence* Fence, u64 FenceValue, HANDLE FenceEvent, u32 Duration = UINT32_MAX);
internal void GameRendererFlush(game_renderer* Renderer);

// ===============================================================================================================
//                                                   RENDERER API                                               
// ===============================================================================================================
internal void GameRendererSetViewProjectionQuad(game_renderer* Renderer, const m4& ViewProjection);
internal void GameRendererSetViewProjectionCuboid(game_renderer* Renderer, const m4& ViewProjection);

// Render commands
internal void GameRendererSubmitQuad(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v2& Scale, const v4& Color);
internal void GameRendererSubmitQuad(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v2& Scale, const texture& Texture, const v4& Color);
internal void GameRendererSubmitQuadCustom(game_renderer* Renderer, v3 VertexPositions[4], const texture& Texture, const v4& Color);
internal void GameRendererSubmitCuboid(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v3& Scale, const texture& Texture, const v4& Color);
internal void GameRendererSubmitCuboid(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v3& Scale, const v4& Color);
internal void GameRendererSubmitCuboidNoRotScale(game_renderer* Renderer, const v3& Translation, const v4& Color);
internal void GameRendererSubmitCuboidNoRotScale(game_renderer* Renderer, const v3& Translation, const texture& Texture, const v4& Color);
