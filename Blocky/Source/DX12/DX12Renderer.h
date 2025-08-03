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

internal constexpr u32 c_MaxTexturesPerDrawCall = 32; // TODO: Get this from the driver

internal constexpr u32 c_MaxIndicesPerDrawCall = 1 << 4;

internal constexpr u32 c_MaxCubePerBatch = 1 << 18;

internal constexpr u32 c_MaxQuadsPerBatch = 1 << 4;
internal constexpr u32 c_MaxQuadVertices = c_MaxQuadsPerBatch * 4;
internal constexpr u32 c_MaxQuadIndices = c_MaxQuadsPerBatch * 6;

internal constexpr u32 c_MaxHUDQuadsPerBatch = 1 << 4;
internal constexpr u32 c_MaxHUDQuadVertices = c_MaxQuadsPerBatch * 4;
internal constexpr u32 c_MaxHUDQuadIndices = c_MaxQuadsPerBatch * 6;

internal constexpr u32 c_MaxQuadedCuboids = 1 << 8;
internal constexpr u32 c_MaxQuadedCuboidVertices = c_MaxQuadedCuboids * 4;
internal constexpr u32 c_MaxQuadedCuboidIndices = c_MaxQuadedCuboids * 6;

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

    v2& operator[](u32 Index) { return Coords[Index]; }
    const v2& operator[](u32 Index) const { return Coords[Index]; }
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
    v4 Position;
    v4 Color;
    v2 TexCoord;
    u32 TexIndex;
};

struct distant_quad_vertex
{
    v4 Position;
};

// Same as quad_vertex for now
struct hud_quad_vertex
{
    v4 Position;
    v4 Color;
    v2 TexCoord;
    u32 TexIndex;
};

struct quaded_cuboid_vertex
{
    v4 Position;
    v3 Normal;
    v4 Color;
    v2 TexCoord;
    u32 TexIndex;
};

struct cuboid_vertex
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

// Basically a push constant
// TODO: Look up standardized push constant minimum value
// I think its 256 bytes
// TODO: Combine these into a single concept?
struct hud_quad_root_signature_constant_buffer
{
    m4 Projection;
};

struct skybox_quad_root_signature_constant_buffer
{
    m4 InverseViewProjection;
    f32 Time;
};

struct cuboid_root_signature_constant_buffer
{
    m4 ViewProjection;
    m4 View;
    m4 LightSpaceMatrix;
};

struct shadow_pass_root_signature_constant_buffer
{
    m4 LightSpaceMatrix;
};

struct distant_quad_root_signature_constant_buffer
{
    m4 ViewProjectionNoTranslation;
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

struct render_data
{
    hud_quad_root_signature_constant_buffer HUDRootSignatureBuffer;
    cuboid_root_signature_constant_buffer CuboidRootSignatureBuffer;
    skybox_quad_root_signature_constant_buffer SkyboxRootSignatureBuffer;

    shadow_pass_root_signature_constant_buffer ShadowPassRootSignatureBuffer;

    distant_quad_root_signature_constant_buffer DistantObjectRootSignatureBuffer;

    m4 Projection;
    v3 CameraPosition;
};

internal constexpr v3 c_SkyboxVertices[8] =
{
    {-1.0f, -1.0f, -1.0f}, // 0: Left  Bottom Back
    { 1.0f, -1.0f, -1.0f}, // 1: Right Bottom Back
    { 1.0f,  1.0f, -1.0f}, // 2: Right Top    Back
    {-1.0f,  1.0f, -1.0f}, // 3: Left  Top    Back
    {-1.0f, -1.0f,  1.0f}, // 4: Left  Bottom Front
    { 1.0f, -1.0f,  1.0f}, // 5: Right Bottom Front
    { 1.0f,  1.0f,  1.0f}, // 6: Right Top    Front
    {-1.0f,  1.0f,  1.0f}  // 7: Left  Top    Front
};

internal constexpr u32 c_SkyboxIndices[36] =
{
    // Back face
    0, 1, 2,  2, 3, 0,
    // Front face
    4, 5, 6,  6, 7, 4,
    // Left face
    0, 4, 7,  7, 3, 0,
    // Right face
    1, 5, 6,  6, 2, 1,
    // Bottom face
    0, 1, 5,  5, 4, 0,
    // Top face
    3, 2, 6,  6, 7, 3
};

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

internal constexpr cuboid_vertex c_CuboidVertices[24] =
{
    // Front face (+Z)
    cuboid_vertex(v4{ -0.5f, -0.5f,  0.5f, 1.0f }, v3{ 0.0f, 0.0f, 1.0f }, v2{ 0.0f, 0.0f }),
    cuboid_vertex(v4{  0.5f, -0.5f,  0.5f, 1.0f }, v3{ 0.0f, 0.0f, 1.0f }, v2{ 1.0f, 0.0f }),
    cuboid_vertex(v4{  0.5f,  0.5f,  0.5f, 1.0f }, v3{ 0.0f, 0.0f, 1.0f }, v2{ 1.0f, 1.0f }),
    cuboid_vertex(v4{ -0.5f,  0.5f,  0.5f, 1.0f }, v3{ 0.0f, 0.0f, 1.0f }, v2{ 0.0f, 1.0f }),

    // Back face (-Z)                                                                    
    cuboid_vertex(v4{  0.5f, -0.5f, -0.5f, 1.0f }, v3{ 0.0f, 0.0f, -1.0f}, v2{ 0.0f, 0.0f }),
    cuboid_vertex(v4{ -0.5f, -0.5f, -0.5f, 1.0f }, v3{ 0.0f, 0.0f, -1.0f}, v2{ 1.0f, 0.0f }),
    cuboid_vertex(v4{ -0.5f,  0.5f, -0.5f, 1.0f }, v3{ 0.0f, 0.0f, -1.0f}, v2{ 1.0f, 1.0f }),
    cuboid_vertex(v4{  0.5f,  0.5f, -0.5f, 1.0f }, v3{ 0.0f, 0.0f, -1.0f}, v2{ 0.0f, 1.0f }),

    // Left face (-X)                                                                    
    cuboid_vertex(v4{ -0.5f, -0.5f, -0.5f, 1.0f }, v3{ -1.0f, 0.0f, 0.0f}, v2{ 0.0f, 0.0f }),
    cuboid_vertex(v4{ -0.5f, -0.5f,  0.5f, 1.0f }, v3{ -1.0f, 0.0f, 0.0f}, v2{ 1.0f, 0.0f }),
    cuboid_vertex(v4{ -0.5f,  0.5f,  0.5f, 1.0f }, v3{ -1.0f, 0.0f, 0.0f}, v2{ 1.0f, 1.0f }),
    cuboid_vertex(v4{ -0.5f,  0.5f, -0.5f, 1.0f }, v3{ -1.0f, 0.0f, 0.0f}, v2{ 0.0f, 1.0f }),

    // Right face (+X)                                                                   
    cuboid_vertex(v4{  0.5f, -0.5f,  0.5f, 1.0f }, v3{ 1.0f, 0.0f, 0.0f }, v2{ 0.0f, 0.0f }),
    cuboid_vertex(v4{  0.5f, -0.5f, -0.5f, 1.0f }, v3{ 1.0f, 0.0f, 0.0f }, v2{ 1.0f, 0.0f }),
    cuboid_vertex(v4{  0.5f,  0.5f, -0.5f, 1.0f }, v3{ 1.0f, 0.0f, 0.0f }, v2{ 1.0f, 1.0f }),
    cuboid_vertex(v4{  0.5f,  0.5f,  0.5f, 1.0f }, v3{ 1.0f, 0.0f, 0.0f }, v2{ 0.0f, 1.0f }),

    // Top face (+Y)                                                                     
    cuboid_vertex(v4{ -0.5f,  0.5f,  0.5f, 1.0f }, v3{ 0.0f, 1.0f, 0.0f }, v2{ 0.0f, 0.0f }),
    cuboid_vertex(v4{  0.5f,  0.5f,  0.5f, 1.0f }, v3{ 0.0f, 1.0f, 0.0f }, v2{ 1.0f, 0.0f }),
    cuboid_vertex(v4{  0.5f,  0.5f, -0.5f, 1.0f }, v3{ 0.0f, 1.0f, 0.0f }, v2{ 1.0f, 1.0f }),
    cuboid_vertex(v4{ -0.5f,  0.5f, -0.5f, 1.0f }, v3{ 0.0f, 1.0f, 0.0f }, v2{ 0.0f, 1.0f }),

    // Bottom face (-Y)                                                                  
    cuboid_vertex(v4{ -0.5f, -0.5f, -0.5f, 1.0f }, v3{ 0.0f, -1.0f, 0.0f}, v2{ 0.0f, 0.0f }),
    cuboid_vertex(v4{  0.5f, -0.5f, -0.5f, 1.0f }, v3{ 0.0f, -1.0f, 0.0f}, v2{ 1.0f, 0.0f }),
    cuboid_vertex(v4{  0.5f, -0.5f,  0.5f, 1.0f }, v3{ 0.0f, -1.0f, 0.0f}, v2{ 1.0f, 1.0f }),
    cuboid_vertex(v4{ -0.5f, -0.5f,  0.5f, 1.0f }, v3{ 0.0f, -1.0f, 0.0f}, v2{ 0.0f, 1.0f })
};

internal constexpr v4 c_QuadVertexPositions[4]
{
    { -0.5f, -0.5f, 0.0f, 1.0f },
    {  0.5f, -0.5f, 0.0f, 1.0f },
    {  0.5f,  0.5f, 0.0f, 1.0f },
    { -0.5f,  0.5f, 0.0f, 1.0f }
};

// API-agnostic type definition
// There is a potential problem with navigating stuff if we would have multiple platforms
struct d3d12_render_backend
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

    // Textures
    ID3D12DescriptorHeap* SRVDescriptorHeap;
    texture WhiteTexture;
    u32 CurrentTextureStackIndex = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE TextureStack[c_MaxTexturesPerDrawCall];

    // Quad
    struct  
    {
        dx12_pipeline Pipeline;
        dx12_index_buffer IndexBuffer;
        dx12_vertex_buffer VertexBuffers[FIF];
        quad_vertex* VertexDataBase;
        quad_vertex* VertexDataPtr;
        u32 IndexCount;
        ID3D12RootSignature* RootSignature;
    } Quad;

    // Basic Cuboid - Has the same texture for each face
    struct  
    {
        u32 InstanceCount = 0;
        dx12_pipeline Pipeline = {};
        dx12_vertex_buffer TransformVertexBuffers[FIF] = {};
        dx12_vertex_buffer PositionsVertexBuffer = {};
        cuboid_transform_vertex_data* InstanceData = nullptr;
        ID3D12RootSignature* RootSignature;
    } Cuboid;

    // Quaded Cuboid - Can have unique texture for each face
    struct
    {
        dx12_pipeline Pipeline = {};
        dx12_vertex_buffer VertexBuffers[FIF];
        quaded_cuboid_vertex* VertexDataBase;
        quaded_cuboid_vertex* VertexDataPtr;
        u32 IndexCount;
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
        distant_quad_vertex* VertexDataBase;
        distant_quad_vertex* VertexDataPtr;
        u32 IndexCount = 0;
    } DistantQuad;

    // Shadows
    struct
    {
        ID3D12Resource* ShadowMaps[FIF];
        D3D12_CPU_DESCRIPTOR_HANDLE DSVHandles[FIF];
        D3D12_CPU_DESCRIPTOR_HANDLE SRVHandles[FIF];
        dx12_pipeline Pipeline;
        ID3D12RootSignature* RootSignature;
        ID3D12DescriptorHeap* DSVDescriptorHeap;
        ID3D12DescriptorHeap* SRVDescriptorHeap;
    } ShadowPass;

    // HUD stuff
    struct  
    {
        hud_quad_vertex* VertexDataPtr;
        hud_quad_vertex* VertexDataBase;
        dx12_vertex_buffer VertexBuffers[FIF];
        u32 IndexCount = 0;
        dx12_pipeline Pipeline;
    } HUD;

    // All possible data the renderer needs
    render_data RenderData;

    // Light stuff
    light_environment LightEnvironment;
    dx12_constant_buffer LightEnvironmentConstantBuffers[FIF];
};

// ===============================================================================================================
//                                              RENDERER INTERNAL API                                               
// ===============================================================================================================

internal d3d12_render_backend* d3d12_render_backend_create(arena* Arena, const win32_window& Window);
internal void d3d12_render_backend_destroy(d3d12_render_backend* Renderer);

internal void GameRendererInitD3D(d3d12_render_backend* Renderer, const win32_window& Window);
internal void GameRendererInitD3DPipeline(d3d12_render_backend* Renderer);

internal void d3d12_render_backend_resize_swapchain(d3d12_render_backend* Renderer, u32 RequestWidth, u32 RequestHeight);
internal void d3d12_render_backend_render(d3d12_render_backend* Renderer);
internal u64 GameRendererSignal(ID3D12CommandQueue* CommandQueue, ID3D12Fence* Fence, u64* FenceValue);
internal void GameRendererWaitForFenceValue(ID3D12Fence* Fence, u64 FenceValue, HANDLE FenceEvent, u32 Duration = UINT32_MAX);
internal void GameRendererFlush(d3d12_render_backend* Renderer);
internal void GameRendererResetState(d3d12_render_backend* Renderer);

// ===============================================================================================================
//                                                   RENDERER API                                               
// ===============================================================================================================

// Set all possible needed state here
internal void GameRendererSetRenderData(d3d12_render_backend* Renderer, const v3& CameraPosition, const m4& View, const m4& Projection, const m4& InverseView, const m4& HUDProjection, f32 Time, m4 LightSpaceMatrixTemp);

// Quad render commands
internal void GameRendererSubmitQuad(d3d12_render_backend* Renderer, const v3& Translation, const v3& Rotation, const v2& Scale, const v4& Color);
internal void GameRendererSubmitQuad(d3d12_render_backend* Renderer, const v3& Translation, const v3& Rotation, const v2& Scale, const texture& Texture, const v4& Color);
internal void GameRendererSubmitQuadCustom(d3d12_render_backend* Renderer, v3 VertexPositions[4], const texture& Texture, const v4& Color);
internal void GameRendererSubmitBillboardQuad(d3d12_render_backend* Renderer, const v3& Translation, const v2& Scale, const texture& Texture, const v4& Color);
internal void GameRendererSubmitDistantQuad(d3d12_render_backend* Renderer, const v3& Translation, const v3& Rotation, const texture& Texture, const v4& Color);

// Cuboid first single unique texture - good for most of the blocks
internal void GameRendererSubmitCuboid(d3d12_render_backend* Renderer, const v3& Translation, const v4& Color);
internal void GameRendererSubmitCuboid(d3d12_render_backend* Renderer, const v3& Translation, const v3& Rotation, const v3& Scale, const v4& Color);
internal void GameRendererSubmitCuboid(d3d12_render_backend* Renderer, const v3& Translation, const v3& Rotation, const v3& Scale, const texture& Texture, const v4& Color);
internal void GameRendererSubmitCuboid(d3d12_render_backend* Renderer, const v3& Translation, const texture& Texture, const v4& Color);

// Quaded cuboids - slower than cuboids but each quad can have its own unique texture
internal void GameRendererSubmitQuadedCuboid(d3d12_render_backend* Renderer, const v3& Translation, const v3& Rotation, const v3& Scale, const texture& Texture, const texture_block_coords& TextureCoords, const v4& Color);
internal void GameRendererSubmitQuadedCuboid(d3d12_render_backend* Renderer, const m4& Transform, const texture& Texture, const texture_block_coords& TextureCoords, const v4& Color);

// Lights
internal void GameRendererSubmitDirectionalLight(d3d12_render_backend* Renderer, const v3& Direction, f32 Intensity, const v3& Radiance);
internal void GameRendererSubmitPointLight(d3d12_render_backend* Renderer, const v3& Position, f32 Radius, f32 FallOff, const v3& Radiance, f32 Intensity);

// ===============================================================================================================
//                                             RENDERER API - HUD                                             
// ===============================================================================================================
internal void GameRendererHUDSubmitQuad(d3d12_render_backend* Renderer, v3 VertexPositions[4], const texture& Texture, const texture_coords& Coords = texture_coords(), const v4& Color = v4(1.0f));
