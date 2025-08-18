#pragma once

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
#if ENABLE_SIMD
        XMMATRIX XmmTransform;
#endif
    };

    v4 Color;
    u32 TextureIndex;
};

// Basically a push constant
// TODO: Look up standardized push constant minimum value
// I think its 256 bytes
// TODO: Combine these into a single concept?
struct hud_quad_buffer_data
{
    m4 Projection;
};

struct skybox_quad_buffer_data
{
    m4 InverseViewProjection;
    f32 Time;
};

struct cuboid_buffer_data
{
    m4 ViewProjection;
    m4 View;
    m4 LightSpaceMatrix;
};

struct bloom_pass_buffer_data
{
    enum class mode : i32
    {
        PreFilter = 0,
        DownSample = 1,
        FirstUpSample = 2,
        UpSample = 3,
    };

    v4 Params;
    f32 LOD;
    mode Mode;
};

struct shadow_pass_buffer_data
{
    m4 LightSpaceMatrix;
};

struct distant_quad_buffer_data
{
    m4 ViewProjectionNoTranslation;
};

struct render_data
{
    hud_quad_buffer_data HUDBuffer;
    cuboid_buffer_data CuboidBuffer;
    skybox_quad_buffer_data SkyboxBuffer;

    shadow_pass_buffer_data ShadowPassBuffer;

    distant_quad_buffer_data DistantObjectBuffer;

    m4 Projection;
    v3 CameraPosition;
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

    inline void clear() { DirectionalLightCount = PointLightCount = 0; };
    inline auto& emplace_directional_light() { Assert(DirectionalLightCount < MaxDirectionalLights, "Too many directional lights!"); return DirectionalLight[DirectionalLightCount++]; }
    inline auto& emplace_point_light() { Assert(PointLightCount < MaxPointLights, "Too many point lights!"); return PointLights[PointLightCount++]; }
};

struct texture;

struct game_renderer
{
    // All possible data the renderer needs
    render_data RenderData;

    // Light stuff
    light_environment LightEnvironment;

    // Textures
    u32 CurrentTextureStackIndex = 1;
    const texture* TextureStack[c_MaxTexturesPerDrawCall];

    // Quad
    struct
    {
        quad_vertex* VertexDataBase;
        quad_vertex* VertexDataPtr;
        u32 IndexCount;
    } Quad;

    // Basic Cuboid - Has the same texture for each face
    struct
    {
        u32 InstanceCount = 0;
        cuboid_transform_vertex_data* InstanceData = nullptr;
    } Cuboid;

    // Quaded Cuboid - Can have unique texture for each face
    struct
    {
        quaded_cuboid_vertex* VertexDataBase;
        quaded_cuboid_vertex* VertexDataPtr;
        u32 IndexCount;
    } QuadedCuboid;

    // Distant quads (sun, moon, stars, ...)
    struct
    {
        distant_quad_vertex* VertexDataBase;
        distant_quad_vertex* VertexDataPtr;
        u32 IndexCount = 0;
    } DistantQuad;

    // HUD stuff
    struct
    {
        hud_quad_vertex* VertexDataPtr;
        hud_quad_vertex* VertexDataBase;
        u32 IndexCount = 0;
    } HUD;

    // Render feature enabling
    bool EnableShadows = true;
    bool EnableBloom = true;
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

// ===============================================================================================================
//                                                   RENDERER API                                               
// ===============================================================================================================

internal game_renderer* game_renderer_create(arena* Arena);
internal void game_renderer_destroy(game_renderer* Renderer);

internal void game_renderer_clear(game_renderer* Renderer);

// Set all possible needed state here
internal void game_renderer_set_render_data(game_renderer* Renderer, const v3& CameraPosition, const m4& View, const m4& Projection, const m4& InverseView, const m4& HUDProjection, f32 Time, m4 LightSpaceMatrixTemp);

// Quad render commands
internal void game_renderer_submit_quad(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v2& Scale, const v4& Color);
internal void game_renderer_submit_quad(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v2& Scale, const texture* Texture, const v4& Color);
internal void game_renderer_submit_quad_custom(game_renderer* Renderer, v3 VertexPositions[4], const texture* Texture, const v4& Color);
internal void game_renderer_submit_billboard_quad(game_renderer* Renderer, const v3& Translation, const v2& Scale, const texture* Texture, const v4& Color);
internal void game_renderer_submit_distant_quad(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const texture* Texture, const v4& Color);

// Cuboid first single unique texture - good for most of the blocks
internal void game_renderer_submit_cuboid(game_renderer* Renderer, const v3& Translation, const v4& Color);
internal void game_renderer_submit_cuboid(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v3& Scale, const v4& Color);
internal void game_renderer_submit_cuboid(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v3& Scale, const texture* Texture, const v4& Color);
internal void game_renderer_submit_cuboid(game_renderer* Renderer, const v3& Translation, const texture* Texture, const v4& Color);

// Quaded cuboids - slower than cuboids but each quad can have its own unique texture
internal void game_renderer_submit_quaded_cuboid(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v3& Scale, const texture* Texture, const texture_block_coords& TextureCoords, const v4& Color);
internal void game_renderer_submit_quaded_cuboid(game_renderer* Renderer, const m4& Transform, const texture* Texture, const texture_block_coords& TextureCoords, const v4& Color);

// Lights
internal void game_renderer_submit_directional_light(game_renderer* Renderer, const v3& Direction, f32 Intensity, const v3& Radiance);
internal void game_renderer_submit_point_light(game_renderer* Renderer, const v3& Position, f32 Radius, f32 FallOff, const v3& Radiance, f32 Intensity);

// ===============================================================================================================
//                                             RENDERER API - HUD                                             
// ===============================================================================================================
internal void game_renderer_submit_hud_quad(game_renderer* Renderer, v3 VertexPositions[4], const texture* Texture, const texture_coords& Coords = texture_coords(), const v4& Color = v4(1.0f));
