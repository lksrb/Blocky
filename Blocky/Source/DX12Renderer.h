/*
 * Note that this renderer is game specific so it does not have to be completely api-agnostic,
 * For example instead of having general set push constants functions, we can have set view projection function etc.
 * We will see in the future if this is a good approach
 */
#pragma once

 // DirectX 12
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")

// D3D12 extension
#include <d3dx12.h>

#define DxAssert(x) Assert(SUCCEEDED(x), #x)
#define FIF 2

#if BK_DEBUG
#define DX12_ENABLE_DEBUG_LAYER 1
#else
#define DX12_ENABLE_DEBUG_LAYER 0
#endif

#if DX12_ENABLE_DEBUG_LAYER
#include <dxgidebug.h>
#include <d3d12sdklayers.h>

#pragma comment(lib, "dxguid.lib")
#endif

struct quad_vertex
{
    v3 Position;
    v4 Color;
    v2 TexCoord;
    u32 TexIndex;
};

static constexpr inline u32 c_MaxQuadsPerBatch = 1 << 16;
static constexpr inline u32 c_MaxQuadVertices = c_MaxQuadsPerBatch * 4;
static constexpr inline u32 c_MaxQuadIndices = c_MaxQuadsPerBatch * 6;
static constexpr inline u32 c_MaxTexturesPerDrawCall = 32; // TODO: Get this from the driver

// Each face has to have a normal vector, so unfortunately we cannot encode cube as 8 vertices
static constexpr inline v4 c_CubeVertexPositions[24] =
{
    // Front face (+Z)
    { -0.5f, -0.5f,  0.5f, 1.0f },
    {  0.5f, -0.5f,  0.5f, 1.0f },
    {  0.5f,  0.5f,  0.5f, 1.0f },
    { -0.5f,  0.5f,  0.5f, 1.0f },

    // Back face (-Z)
    {  0.5f, -0.5f, -0.5f, 1.0f },
    { -0.5f, -0.5f, -0.5f, 1.0f },
    { -0.5f,  0.5f, -0.5f, 1.0f },
    {  0.5f,  0.5f, -0.5f, 1.0f },

    // Left face (-X)
    { -0.5f, -0.5f, -0.5f, 1.0f },
    { -0.5f, -0.5f,  0.5f, 1.0f },
    { -0.5f,  0.5f,  0.5f, 1.0f },
    { -0.5f,  0.5f, -0.5f, 1.0f },

    // Right face (+X)
    {  0.5f, -0.5f,  0.5f, 1.0f },
    {  0.5f, -0.5f, -0.5f, 1.0f },
    {  0.5f,  0.5f, -0.5f, 1.0f },
    {  0.5f,  0.5f,  0.5f, 1.0f },

    // Top face (+Y)
    { -0.5f,  0.5f,  0.5f, 1.0f },
    {  0.5f,  0.5f,  0.5f, 1.0f },
    {  0.5f,  0.5f, -0.5f, 1.0f },
    { -0.5f,  0.5f, -0.5f, 1.0f },

    // Bottom face (-Y)
    { -0.5f, -0.5f, -0.5f, 1.0f },
    {  0.5f, -0.5f, -0.5f, 1.0f },
    {  0.5f, -0.5f,  0.5f, 1.0f },
    { -0.5f, -0.5f,  0.5f, 1.0f }
};

static constexpr inline v4 c_QuadVertexPositions[4]
{
    { -0.5f, -0.5f, 0.0f, 1.0f },
    {  0.5f, -0.5f, 0.0f, 1.0f },
    {  0.5f,  0.5f, 0.0f, 1.0f },
    { -0.5f,  0.5f, 0.0f, 1.0f }
};

#include "DX12Buffer.h"
#include "DX12Texture.h"

// Basically a push constant
// TODO: Standardized push constant minimum value
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
    dx12_root_signature_constant_buffer RootSignatureBuffer;

    D3D12_VIEWPORT Viewport;
    D3D12_RECT ScissorRect;

    // Textures
    ID3D12DescriptorHeap* SRVDescriptorHeap;
    texture WhiteTexture;
    u32 CurrentTextureStackIndex = 1;
    texture TextureStack[c_MaxTexturesPerDrawCall];

    // Quad
    ID3D12PipelineState* QuadPipelineState;
    dx12_vertex_buffer QuadVertexBuffer[FIF];
    dx12_buffer QuadIndexBuffer;
    quad_vertex* QuadVertexDataBase;
    quad_vertex* QuadVertexDataPtr;
    u32 QuadIndexCount = 0;
};

// Init / Destroy functions
static game_renderer GameRendererCreate(game_window Window);
static void GameRendererDestroy(game_renderer* Renderer);

static void GameRendererInitD3D(game_renderer* Renderer, game_window Window);
static void GameRendererInitD3DPipeline(game_renderer* Renderer);
static void GameRendererResizeSwapChain(game_renderer* Renderer, u32 RequestWidth, u32 RequestHeight);

// API-agnostic used in game code
static void GameRendererSetViewProjection(game_renderer* Renderer, m4 ViewProjection);
static void GameRendererSubmitQuad(game_renderer* Renderer, v3 Translation, v3 Rotation, v3 Scale, v4 Color);
static void GameRendererSubmitCube(game_renderer* Renderer, v3 Translation, v3 Rotation, v3 Scale, texture Texture, v4 Color);
static void GameRendererSubmitCube(game_renderer* Renderer, v3 Translation, v3 Rotation, v3 Scale, v4 Color);

// Helpers
static D3D12_RESOURCE_BARRIER GameRendererTransition(
   ID3D12Resource* pResource,
   D3D12_RESOURCE_STATES stateBefore,
   D3D12_RESOURCE_STATES stateAfter,
   UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
   D3D12_RESOURCE_BARRIER_FLAGS flags = D3D12_RESOURCE_BARRIER_FLAG_NONE);

static void GameRendererDumpInfoQueue(ID3D12InfoQueue* InfoQueue);
static u64 GameRendererSignal(ID3D12CommandQueue* CommandQueue, ID3D12Fence* Fence, u64* FenceValue);
static void GameRendererWaitForFenceValue(ID3D12Fence* Fence, u64 FenceValue, HANDLE FenceEvent, u32 Duration = UINT32_MAX);
static void GameRendererFlush(game_renderer* Renderer);

// Blocking API for submitting stuff to GPU
template<typename F>
static void GameRendererSubmitToQueueImmidiate(ID3D12Device* Device, ID3D12CommandAllocator* CommandAllocator, ID3D12GraphicsCommandList* CommandList, ID3D12CommandQueue* CommandQueue, F&& Func);


