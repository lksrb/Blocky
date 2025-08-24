#pragma once

#include "Random.h"
#include "AABB.h"
#include "RayCast.h"
#include "ECS/ECS.h"
#include "PerlinNoise.h"

struct camera
{
    m4 Projection{ 1.0f };
    m4 View{ 1.0f };

    f32 OrthographicSize = 15.0f;
    f32 OrthographicNear = -100.0f, OrthographicFar = 100.0f;

    f32 AspectRatio = 0.0f;

    f32 PerspectiveFOV = bkm::PI_HALF;
    f32 PerspectiveNear = 0.1f, PerspectiveFar = 1000.0f;

    void recalculate_projection_ortho(u32 Width, u32 Height)
    {
        AspectRatio = static_cast<f32>(Width) / Height;
        f32 OrthoLeft = -0.5f * AspectRatio * OrthographicSize;
        f32 OrthoRight = 0.5f * AspectRatio * OrthographicSize;
        f32 OrthoBottom = -0.5f * OrthographicSize;
        f32 OrthoTop = 0.5f * OrthographicSize;
        Projection = bkm::OrthoRH_ZO(OrthoLeft, OrthoRight, OrthoBottom, OrthoTop, OrthographicNear, OrthographicFar);
    }

    void recalculate_projection_perspective(u32 Width, u32 Height)
    {
        AspectRatio = static_cast<f32>(Width) / Height;
        Projection = bkm::PerspectiveRH_ZO(PerspectiveFOV, AspectRatio, PerspectiveNear, PerspectiveFar);
    }

    m4 get_view_projection() const { return Projection * View; }
};

enum class block_type : u32
{
    Air = 0,

    Dirt,
    GlowStone,
    Stone,
    Bedrock,
    Grass,

    INVALID
};
#define BLOCK_TYPE_COUNT (u32)block_type::INVALID

const char* g_BlockLabels[] = {
    "Air",
    "Dirt",
    "GlowStone",
    "Stone",
    "Bedrock",
    "Grass",
    "INVALID"
};

//enum class entity_type : u32
//{
//    None = 0,
//    Player,
//    Cow,
//    // ...
//};

enum class entity_flags : u32
{
    None = 0,
    Valid = 1 << 0,
    InteractsWithBlocks = 1 << 1,
    Renderable = 1 << 2,
};
ENABLE_BITWISE_OPERATORS(entity_flags, u32);

struct player
{
    v3 Position = v3(-2.0f, 10.0f, -2.0f);
    //v3 Position = v3(2.5f, 0.0f, 0.0f);
    v3 Rotation = v3(-0.655001104, 4.02360773, 0);
    v3 Velocity = v3(0.0f);
    bool IsPhysicsObject = false;
    bool Grounded = false;

    block_type CurrentlySelectedBlock = block_type::Dirt;
};

// TODO: Possible cache optimalizations
struct block
{
    v3 Position = {}; // Position can be removed
    block_type Type = block_type::INVALID;
    v4 Color = v4(1.0f);

    f32 Emission = 0.0f;
    entity PointLightEntity = INVALID_ID; // For glowstone

    bool placed() const { return Type != block_type::Air; }
    //i32 Left = INT_MAX, Right = INT_MAX, Front = INT_MAX, Back = INT_MAX, Up = INT_MAX, Down = INT_MAX; // Neighbours
};

internal const i64 RowCount = 64;
internal const i64 ColumnCount = 64;
internal const i64 LayerCount = 16;
internal const i32 MaxAliveEntitiesCount = 16;
internal const f32 c_TexelSize = 1 / 16.0f; // Global scale for entity models. 1m block is exactly 16x16 texels.

// 16 * 16
//struct block_pos
//{
//    union
//    {
//        struct
//        {
//            u8 XandZ; // 4 bit for X and 4 bits for Z
//            u8 Y; // 0-255
//        };
//
//        u16 Pos;
//    };
//};

struct chunk
{
    v3 BasePosition;
    block Blocks[16 * 16 * 16];
};

struct game
{
    camera Camera;
    v3 CameraOffset = v3(0.0f, 0.8f, 0.0f);
    //v3 CameraOffset = v3(0.0f, 0.0f, 0.0f);

    player Player;

    block* Blocks;
    u32 BlocksCount = 0;

    texture CrosshairTexture;
    texture BlockTextures[BLOCK_TYPE_COUNT];

    texture CowTexture;

    texture PointLightIconTexture;

    texture SunTexture;
    //texture MoonTexture;

    f32 TimeSinceStart;

    perlin_noise PerlinNoise;

    entity_registry Registry;

    bool RenderEditorUI = true;
    bool RenderHUD = true;
    bool RenderDebugUI = true; // ImGui

    random_series GenSeries;

    // Shadows and lighting
    v3 Center = v3(ColumnCount / 2.0f, 0, RowCount / 2.0f);
    v3 Eye = Center + v3(0, 16, 0);
    f32 Size = 15;
    f32 Near = 1.0f;
    f32 Far = 15.5;

    v3 DirectonalLightDirection = bkm::Normalize(v3(0.0f, -1.0f, 0));
    v3 DirectionalLightColor = v3(0.578f, 0.488f, 0.503f);
    f32 DirectionalLightPower = 1.0f;

    v3 GlowStoneColor = v3(0.8f, 0.3f, 0.2f);
    f32 GlowStoneEmission = 2.0f;
};

internal game* game_create(arena* Arena, render_backend* Backend);
internal void game_destroy(game* G, render_backend* Backend);
internal void game_update(game* G, game_renderer* Renderer, const game_input* Input, f32 TimeStep, v2i ClientArea);
internal void game_debug_ui_update(game* G, game_renderer* Renderer, const game_input* Input, f32 TimeStep, v2i ClientArea);
internal void game_player_update(game* G, const game_input* Input, game_renderer* Renderer, f32 TimeStep);
internal void game_generate_world(game* Game, block* Blocks, u32 BlocksCount);

internal void game_update_entities(game* G, f32 TimeStep);
internal void game_render_entities(game* G, game_renderer* Renderer, f32 TimeStep);
internal void game_physics_simulation_update_entities(game* G, f32 TimeStep);

internal void game_render_editor_ui(game* G, game_renderer* Renderer);

internal bool find_first_hit(const ray& Ray, const block* Blocks, u64 BlocksCount, v3* HitPoint, v3* HitNormal, block* HitBlock, u64* HitIndex)
{
    f32 ClosestT = INFINITY;
    bool FoundHit = false;

    for (u64 Index = 0; Index < BlocksCount; Index++)
    {
        auto& Block = Blocks[Index];

        if (!Block.placed())
            continue;

        aabb Box;
        Box.Min = Block.Position - v3(0.5f);
        Box.Max = Block.Position + v3(0.5f);

        if (raycast_result RayCast = RayCastIntersectsAABB(Ray, Box))
        {
            // Ensure we take the closest intersection
            if (RayCast.Near < ClosestT)
            {
                ClosestT = RayCast.Near;
                FoundHit = true;
                *HitBlock = Block;
                *HitNormal = RayCast.Normal;
                *HitPoint = Ray.Origin + Ray.Direction * RayCast.Near;
                *HitIndex = Index;
            }
        }
    }

    return FoundHit;
}

internal block* block_get_safe(game* Game, i32 C, i32 R, i32 L)
{
    if (C >= 0 && C < ColumnCount &&
       R >= 0 && R < RowCount &&
       L >= 0 && L < LayerCount)
    {
        return &Game->Blocks[(L * RowCount * ColumnCount) + (R * RowCount) + C];
    }

    return nullptr;
}

internal texture_coords get_texture_coords(i32 GridWidth, i32 GridHeight, i32 BottomLeftX, i32 BottomLeftY, i32 RotationCount = 0)
{
    texture_coords TextureCoords;

    // TODO: Make this more generic?
    f32 TextureWidth = 64.0f;
    f32 TextureHeight = 32.0f;

    f32 TexelWidth = 1 / TextureWidth;
    f32 TexelHeight = 1 / TextureHeight;

    // 8x8 Grid
    TextureCoords.Coords[0] = { 0.0f, 0.0f };
    TextureCoords.Coords[1] = { GridWidth * TexelWidth, 0.0f };
    TextureCoords.Coords[2] = { GridWidth * TexelWidth, GridHeight * TexelHeight };
    TextureCoords.Coords[3] = { 0.0f, GridHeight * TexelHeight };

    // Rotate 90 degrees 'RotationCount'
    while (RotationCount-- > 0)
    {
        auto OriginalCoords = TextureCoords;

        TextureCoords.Coords[0] = OriginalCoords.Coords[1];  // New Bottom-left
        TextureCoords.Coords[1] = OriginalCoords.Coords[2];  // New Bottom-right
        TextureCoords.Coords[2] = OriginalCoords.Coords[3];  // New Top-right
        TextureCoords.Coords[3] = OriginalCoords.Coords[0];  // New Top-left
    }

    // Translation of the Grid
    TextureCoords.Coords[0] += v2(TexelWidth * BottomLeftX, TexelHeight * (TextureHeight - BottomLeftY - 1));
    TextureCoords.Coords[1] += v2(TexelWidth * BottomLeftX, TexelHeight * (TextureHeight - BottomLeftY - 1));
    TextureCoords.Coords[2] += v2(TexelWidth * BottomLeftX, TexelHeight * (TextureHeight - BottomLeftY - 1));
    TextureCoords.Coords[3] += v2(TexelWidth * BottomLeftX, TexelHeight * (TextureHeight - BottomLeftY - 1));

    return TextureCoords;
};

struct block_pos
{
    i32 C, R, L; // Column, Row, Layer // X, Z, Y
};

internal block_pos get_world_to_block_position(const v3& WorldPos)
{
    // Kinda wonky
    i32 C = (i32)bkm::Floor(WorldPos.x + 0.5f);
    i32 R = (i32)bkm::Floor(WorldPos.z + 0.5f);
    i32 L = (i32)bkm::Floor(WorldPos.y + 0.5f);

    return { C,R,L };
}

// TODO: Move somewhere
internal buffer win32_read_buffer(const char* Path);
