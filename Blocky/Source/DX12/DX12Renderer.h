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

internal constexpr inline u32 c_MaxBlocksPerBatch = 1 << 16;
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

struct block_vertex
{
    v4 Position;
    v2 TextureCoord;
};

// Strong-typed uint
enum class draw_layer : u32
{
    HUD = 0,

    COUNT
};
#define DRAW_LAYER_COUNT (u32)draw_layer::COUNT

// Basically a push constant
// TODO: Standardized push constant minimum value
struct dx12_root_signature_constant_buffer
{
    m4 ViewProjection;
};

struct block_transform_vertex_data
{
    union
    {
        m4 Transform;
        XMMATRIX XmmTransform;
    };

    v4 Color;
    u32 TextureIndex;
};

struct quad_draw_layer_data
{
    dx12_vertex_buffer VertexBuffer[FIF];
    quad_vertex* VertexDataBase;
    quad_vertex* VertexDataPtr;
    u32 IndexCount = 0;
    dx12_root_signature_constant_buffer RootSignatureBuffer;
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
    quad_draw_layer_data QuadDrawLayers[DRAW_LAYER_COUNT] = {};

    // Block ONLY
    u32 BlockInstanceCount = 0;
    dx12_pipeline BlockPipeline = {};
    dx12_vertex_buffer BlockTransformVertexBuffers[FIF] = {};
    dx12_vertex_buffer BlockPositionsVertexBuffer = {};
    block_transform_vertex_data* BlockInstanceData = nullptr;
    dx12_root_signature_constant_buffer BlockRootSignatureBuffer;
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
internal void GameRendererSetViewProjectionLayer(game_renderer* Renderer, const m4& ViewProjection, draw_layer Layer);
internal void GameRendererSetViewProjection(game_renderer* Renderer, const m4& ViewProjection);

// Render commands
internal void GameRendererSubmitQuad(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v2& Scale, const v4& Color, draw_layer Layer);
internal void GameRendererSubmitQuad(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v2& Scale, const texture& Texture, const v4& Color, draw_layer Layer);
internal void GameRendererSubmitQuadCustom(game_renderer* Renderer, v3 VertexPositions[4], const texture& Texture, const v4& Color, draw_layer Layer);
internal void GameRendererSubmitBlock(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v3& Scale, const texture& Texture, const v4& Color);
internal void GameRendererSubmitBlock(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v3& Scale, const v4& Color);
internal void GameRendererSubmitBlockNoRotScale(game_renderer* Renderer, const v3& Translation, const v4& Color);
internal void GameRendererSubmitBlockNoRotScale(game_renderer* Renderer, const v3& Translation, const texture& Texture, const v4& Color);
