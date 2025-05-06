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

internal constexpr u32 c_MaxCubePerBatch = 1 << 16;
internal constexpr u32 c_MaxQuadsPerBatch = 1 << 4;
internal constexpr u32 c_MaxQuadVertices = c_MaxQuadsPerBatch * 4;
internal constexpr u32 c_MaxQuadIndices = c_MaxQuadsPerBatch * 6;
internal constexpr u32 c_MaxTexturesPerDrawCall = 32; // TODO: Get this from the driver

struct texture_coords
{
    v2 Coords[4];

    explicit texture_coords()
    {
        Coords[0] = { 0.0f, 0.0f };
        Coords[1] = { 1.0f, 0.0f };
        Coords[2] = { 1.0f, 1.0f };
        Coords[3] = { 0.0f, 1.0f };
    }
};

struct texture_block_coords
{
    union
    {
        struct
        {

            texture_coords Front;
            texture_coords Back;
            texture_coords Left;
            texture_coords Right;
            texture_coords Top;
            texture_coords Bottom;
        };

        texture_coords TextureCoords[6];
    };

    explicit texture_block_coords()
    {
        for (u32 i = 0; i < 6; i++)
        {
            TextureCoords[i] = texture_coords();
        }
    }
};

struct quad_vertex
{
    v3 Position;
    v4 Color;
    v2 TexCoord;
    u32 TexIndex;
};

struct quaded_cuboid_vertex
{
    v4 Position;
    v3 Normal;
};

struct quaded_cuboid_vertex_gpu
{
    v3 Position;
    v3 Normal;
    v4 Color;
    v2 TexCoord;
    u32 TexIndex;
};

struct basic_cuboid_vertex
{
    v4 Position;
    v3 Normal;
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

struct fast_cuboid_transform_vertex_data
{
    union
    {
        m4 Transform;
        XMMATRIX XmmTransform;
    };

    texture_block_coords TextureCoords;

    v4 Color;
    u32 TextureIndex;
};

// Basically a push constant
// TODO: Look up standardized push constant minimum value
// I think its 256 bytes
struct quad_root_signature_constant_buffer
{
    m4 ViewProjection;
};

struct cuboid_root_signature_constant_buffer
{
    m4 ViewProjection;
    m4 InverseView;
};

struct point_light
{
    v3 Position;
    f32 Intensity;

    v3 Radiance;
    f32 Radius;

    f32 FallOff;
    v3 _Pad0;
};

struct directional_light
{
    v3 Direction;
    f32 Intensity;
    v3 Radiance;
    f32 _Pad0;
};

struct light_environment
{
    static constexpr u32 MaxPointLights = 64;
    static constexpr u32 MaxDirectionalLights = 4;

    directional_light DirectionalLight[MaxDirectionalLights];
    point_light PointLights[MaxPointLights];
    i32 PointLightCount = 0;
    i32 DirectionalLightCount = 0;

    inline void Clear() { DirectionalLightCount = PointLightCount = 0; };
    inline auto& EmplaceDirectionalLight() { Assert(DirectionalLightCount < MaxDirectionalLights, "Too many directional lights!"); return DirectionalLight[DirectionalLightCount++]; }
    inline auto& EmplacePointLight() { Assert(PointLightCount < MaxPointLights, "Too many point lights!"); return PointLights[PointLightCount++]; }
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

    ID3D12CommandAllocator* DirectCommandAllocators[FIF];
    ID3D12GraphicsCommandList2* DirectCommandList;
    ID3D12CommandQueue* DirectCommandQueue;

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
    D3D12_CPU_DESCRIPTOR_HANDLE TextureStack[c_MaxTexturesPerDrawCall];

    // Quad
    dx12_pipeline QuadPipeline;
    dx12_index_buffer QuadIndexBuffer;
    dx12_vertex_buffer QuadVertexBuffers[FIF];
    quad_vertex* QuadVertexDataBase;
    quad_vertex* QuadVertexDataPtr;
    u32 QuadIndexCount = 0;
    quad_root_signature_constant_buffer QuadRootSignatureBuffer;

    // Basic Cuboid - Has same texture for every quad
    u32 CuboidInstanceCount = 0;
    dx12_pipeline CuboidPipeline = {};
    dx12_vertex_buffer CuboidTransformVertexBuffers[FIF] = {};
    dx12_vertex_buffer CuboidPositionsVertexBuffer = {};
    cuboid_transform_vertex_data* CuboidInstanceData = nullptr;
    cuboid_root_signature_constant_buffer CuboidRootSignatureBuffer;

    // Quaded cuboid
    dx12_pipeline QuadedCuboidPipeline = {};
    dx12_vertex_buffer QuadedCuboidVertexBuffers[FIF];
    quaded_cuboid_vertex_gpu* QuadedCuboidVertexDataBase;
    quaded_cuboid_vertex_gpu* QuadedCuboidVertexDataPtr;
    u32 QuadedCuboidIndexCount = 0;

    // FAST Custom Cuboid
    dx12_pipeline FastCuboidPipeline = {};
    fast_cuboid_transform_vertex_data* FastCuboidInstanceData = nullptr;
    u32 FastCuboidInstanceCount = 0;
    dx12_vertex_buffer FastCuboidTransformVertexBuffers[FIF] = {};

    dx12_vertex_buffer FastCuboidVertexBufferPositions;

    // Light stuff
    light_environment LightEnvironment;
    dx12_constant_buffer LightEnvironmentConstantBuffer;
};

// Init / Destroy functions
internal game_renderer GameRendererCreate(const game_window& Window);
internal void GameRendererDestroy(game_renderer* Renderer);

internal void GameRendererInitD3D(game_renderer* Renderer, const game_window& Window);
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
internal void GameRendererSetViewProjectionCuboid(game_renderer* Renderer, const m4& ViewProjection, const m4& InverseView);

// Render commands
internal void GameRendererSubmitQuad(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v2& Scale, const v4& Color);
internal void GameRendererSubmitQuad(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v2& Scale, const texture& Texture, const v4& Color);
internal void GameRendererSubmitQuadCustom(game_renderer* Renderer, v3 VertexPositions[4], const texture& Texture, const v4& Color);
internal void GameRendererSubmitCuboid(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v3& Scale, const texture& Texture, const v4& Color);
internal void GameRendererSubmitCuboid(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v3& Scale, const v4& Color);
internal void GameRendererSubmitCuboidNoRotScale(game_renderer* Renderer, const v3& Translation, const v4& Color);
internal void GameRendererSubmitCuboidNoRotScale(game_renderer* Renderer, const v3& Translation, const texture& Texture, const v4& Color);

internal void GameRendererSubmitCustomCuboid(game_renderer* Renderer, const m4& Transform, const texture& Texture, texture_coords TextureCoords[6], const v4& Color);

internal void GameRendererSubmitCustomCuboid(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v3& Scale, const texture& Texture, texture_coords TextureCoords[6], const v4& Color);

// Each face has to have a normal vector, so unfortunately we cannot encode Cuboid as 8 vertices
internal constexpr v4 c_CuboidVerticesPositions[24] =
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

internal constexpr quaded_cuboid_vertex c_CuboidVerticesPositionsAndNormals[24] =
{
    // Front face (+Z)
    quaded_cuboid_vertex(v4{ -0.5f, -0.5f,  0.5f, 1.0f }, v3{ 0.0f, 0.0f, 1.0f }),
    quaded_cuboid_vertex(v4{  0.5f, -0.5f,  0.5f, 1.0f }, v3{ 0.0f, 0.0f, 1.0f }),
    quaded_cuboid_vertex(v4{  0.5f,  0.5f,  0.5f, 1.0f }, v3{ 0.0f, 0.0f, 1.0f }),
    quaded_cuboid_vertex(v4{ -0.5f,  0.5f,  0.5f, 1.0f }, v3{ 0.0f, 0.0f, 1.0f }),

    // Back face (-Z)                                                          
    quaded_cuboid_vertex(v4{  0.5f, -0.5f, -0.5f, 1.0f }, v3{ 0.0f, 0.0f, -1.0f}),
    quaded_cuboid_vertex(v4{ -0.5f, -0.5f, -0.5f, 1.0f }, v3{ 0.0f, 0.0f, -1.0f}),
    quaded_cuboid_vertex(v4{ -0.5f,  0.5f, -0.5f, 1.0f }, v3{ 0.0f, 0.0f, -1.0f}),
    quaded_cuboid_vertex(v4{  0.5f,  0.5f, -0.5f, 1.0f }, v3{ 0.0f, 0.0f, -1.0f}),

    // Left face (-X)                                                          
    quaded_cuboid_vertex(v4{ -0.5f, -0.5f, -0.5f, 1.0f }, v3{ -1.0f, 0.0f, 0.0f}),
    quaded_cuboid_vertex(v4{ -0.5f, -0.5f,  0.5f, 1.0f }, v3{ -1.0f, 0.0f, 0.0f}),
    quaded_cuboid_vertex(v4{ -0.5f,  0.5f,  0.5f, 1.0f }, v3{ -1.0f, 0.0f, 0.0f}),
    quaded_cuboid_vertex(v4{ -0.5f,  0.5f, -0.5f, 1.0f }, v3{ -1.0f, 0.0f, 0.0f}),

    // Right face (+X)                                                         
    quaded_cuboid_vertex(v4{  0.5f, -0.5f,  0.5f, 1.0f }, v3{ 1.0f, 0.0f, 0.0f }),
    quaded_cuboid_vertex(v4{  0.5f, -0.5f, -0.5f, 1.0f }, v3{ 1.0f, 0.0f, 0.0f }),
    quaded_cuboid_vertex(v4{  0.5f,  0.5f, -0.5f, 1.0f }, v3{ 1.0f, 0.0f, 0.0f }),
    quaded_cuboid_vertex(v4{  0.5f,  0.5f,  0.5f, 1.0f }, v3{ 1.0f, 0.0f, 0.0f }),

    // Top face (+Y)                                                           
    quaded_cuboid_vertex(v4{ -0.5f,  0.5f,  0.5f, 1.0f }, v3{ 0.0f, 1.0f, 0.0f }),
    quaded_cuboid_vertex(v4{  0.5f,  0.5f,  0.5f, 1.0f }, v3{ 0.0f, 1.0f, 0.0f }),
    quaded_cuboid_vertex(v4{  0.5f,  0.5f, -0.5f, 1.0f }, v3{ 0.0f, 1.0f, 0.0f }),
    quaded_cuboid_vertex(v4{ -0.5f,  0.5f, -0.5f, 1.0f }, v3{ 0.0f, 1.0f, 0.0f }),

    // Bottom face (-Y)                                                        
    quaded_cuboid_vertex(v4{ -0.5f, -0.5f, -0.5f, 1.0f }, v3{ 0.0f, -1.0f, 0.0f}),
    quaded_cuboid_vertex(v4{  0.5f, -0.5f, -0.5f, 1.0f }, v3{ 0.0f, -1.0f, 0.0f}),
    quaded_cuboid_vertex(v4{  0.5f, -0.5f,  0.5f, 1.0f }, v3{ 0.0f, -1.0f, 0.0f}),
    quaded_cuboid_vertex(v4{ -0.5f, -0.5f,  0.5f, 1.0f }, v3{ 0.0f, -1.0f, 0.0f})
};


internal constexpr basic_cuboid_vertex c_CuboidVertices[24] =
{
    // Front face (+Z)
    basic_cuboid_vertex(v4{ -0.5f, -0.5f,  0.5f, 1.0f }, v3{ 0.0f, 0.0f, 1.0f }, v2{ 0.0f, 0.0f }),
    basic_cuboid_vertex(v4{  0.5f, -0.5f,  0.5f, 1.0f }, v3{ 0.0f, 0.0f, 1.0f }, v2{ 1.0f, 0.0f }),
    basic_cuboid_vertex(v4{  0.5f,  0.5f,  0.5f, 1.0f }, v3{ 0.0f, 0.0f, 1.0f }, v2{ 1.0f, 1.0f }),
    basic_cuboid_vertex(v4{ -0.5f,  0.5f,  0.5f, 1.0f }, v3{ 0.0f, 0.0f, 1.0f }, v2{ 0.0f, 1.0f }),

    // Back face (-Z)                                                                    
    basic_cuboid_vertex(v4{  0.5f, -0.5f, -0.5f, 1.0f }, v3{ 0.0f, 0.0f, -1.0f}, v2{ 0.0f, 0.0f }),
    basic_cuboid_vertex(v4{ -0.5f, -0.5f, -0.5f, 1.0f }, v3{ 0.0f, 0.0f, -1.0f}, v2{ 1.0f, 0.0f }),
    basic_cuboid_vertex(v4{ -0.5f,  0.5f, -0.5f, 1.0f }, v3{ 0.0f, 0.0f, -1.0f}, v2{ 1.0f, 1.0f }),
    basic_cuboid_vertex(v4{  0.5f,  0.5f, -0.5f, 1.0f }, v3{ 0.0f, 0.0f, -1.0f}, v2{ 0.0f, 1.0f }),

    // Left face (-X)                                                                    
    basic_cuboid_vertex(v4{ -0.5f, -0.5f, -0.5f, 1.0f }, v3{ -1.0f, 0.0f, 0.0f}, v2{ 0.0f, 0.0f }),
    basic_cuboid_vertex(v4{ -0.5f, -0.5f,  0.5f, 1.0f }, v3{ -1.0f, 0.0f, 0.0f}, v2{ 1.0f, 0.0f }),
    basic_cuboid_vertex(v4{ -0.5f,  0.5f,  0.5f, 1.0f }, v3{ -1.0f, 0.0f, 0.0f}, v2{ 1.0f, 1.0f }),
    basic_cuboid_vertex(v4{ -0.5f,  0.5f, -0.5f, 1.0f }, v3{ -1.0f, 0.0f, 0.0f}, v2{ 0.0f, 1.0f }),

    // Right face (+X)                                                                   
    basic_cuboid_vertex(v4{  0.5f, -0.5f,  0.5f, 1.0f }, v3{ 1.0f, 0.0f, 0.0f }, v2{ 0.0f, 0.0f }),
    basic_cuboid_vertex(v4{  0.5f, -0.5f, -0.5f, 1.0f }, v3{ 1.0f, 0.0f, 0.0f }, v2{ 1.0f, 0.0f }),
    basic_cuboid_vertex(v4{  0.5f,  0.5f, -0.5f, 1.0f }, v3{ 1.0f, 0.0f, 0.0f }, v2{ 1.0f, 1.0f }),
    basic_cuboid_vertex(v4{  0.5f,  0.5f,  0.5f, 1.0f }, v3{ 1.0f, 0.0f, 0.0f }, v2{ 0.0f, 1.0f }),

    // Top face (+Y)                                                                     
    basic_cuboid_vertex(v4{ -0.5f,  0.5f,  0.5f, 1.0f }, v3{ 0.0f, 1.0f, 0.0f }, v2{ 0.0f, 0.0f }),
    basic_cuboid_vertex(v4{  0.5f,  0.5f,  0.5f, 1.0f }, v3{ 0.0f, 1.0f, 0.0f }, v2{ 1.0f, 0.0f }),
    basic_cuboid_vertex(v4{  0.5f,  0.5f, -0.5f, 1.0f }, v3{ 0.0f, 1.0f, 0.0f }, v2{ 1.0f, 1.0f }),
    basic_cuboid_vertex(v4{ -0.5f,  0.5f, -0.5f, 1.0f }, v3{ 0.0f, 1.0f, 0.0f }, v2{ 0.0f, 1.0f }),

    // Bottom face (-Y)                                                                  
    basic_cuboid_vertex(v4{ -0.5f, -0.5f, -0.5f, 1.0f }, v3{ 0.0f, -1.0f, 0.0f}, v2{ 0.0f, 0.0f }),
    basic_cuboid_vertex(v4{  0.5f, -0.5f, -0.5f, 1.0f }, v3{ 0.0f, -1.0f, 0.0f}, v2{ 1.0f, 0.0f }),
    basic_cuboid_vertex(v4{  0.5f, -0.5f,  0.5f, 1.0f }, v3{ 0.0f, -1.0f, 0.0f}, v2{ 1.0f, 1.0f }),
    basic_cuboid_vertex(v4{ -0.5f, -0.5f,  0.5f, 1.0f }, v3{ 0.0f, -1.0f, 0.0f}, v2{ 0.0f, 1.0f })
};

internal constexpr v4 c_QuadVertexPositions[4]
{
    { -0.5f, -0.5f, 0.0f, 1.0f },
    {  0.5f, -0.5f, 0.0f, 1.0f },
    {  0.5f,  0.5f, 0.0f, 1.0f },
    { -0.5f,  0.5f, 0.0f, 1.0f }
};
