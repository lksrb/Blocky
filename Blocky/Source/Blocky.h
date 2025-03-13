#pragma once

#include "AABB.h"
#include "RayCast.h"

#include <vector>

struct camera
{
    m4 Projection{ 1.0f };
    m4 View{ 1.0f };

    f32 OrthographicSize = 15.0f;
    f32 OrthographicNear = -100.0f, OrthographicFar = 100.0f;

    f32 AspectRatio = 0.0f;

    f32 PerspectiveFOV = bkm::PI_HALF;
    f32 PerspectiveNear = 0.1f, PerspectiveFar = 1000.0f;

    void RecalculateProjectionOrtho(u32 Width, u32 Height)
    {
        AspectRatio = static_cast<f32>(Width) / Height;
        f32 OrthoLeft = -0.5f * AspectRatio * OrthographicSize;
        f32 OrthoRight = 0.5f * AspectRatio * OrthographicSize;
        f32 OrthoBottom = -0.5f * OrthographicSize;
        f32 OrthoTop = 0.5f * OrthographicSize;
        Projection = bkm::Ortho(OrthoLeft, OrthoRight, OrthoBottom, OrthoTop, OrthographicNear, OrthographicFar);
    }

    void RecalculateProjectionOrtho_V2(u32 Width, u32 Height)
    {
        Projection = bkm::Ortho(0, (f32)Width, (f32)Height, 0, -1.0f, 1.0f);
    }

    void RecalculateProjectionPerspective(u32 Width, u32 Height)
    {
        AspectRatio = static_cast<f32>(Width) / Height;
        Projection = bkm::Perspective(PerspectiveFOV, AspectRatio, PerspectiveNear, PerspectiveFar);
    }

    m4 GetViewProjection() const { return Projection * View; }
};

enum class block_type : u32
{
    Dirt = 0,
    Air,

    INVALID
};
#define BLOCK_TYPE_COUNT (u32)block_type::INVALID

struct transform
{
    v3 Translation = v3(0.0f);
    v3 Rotation = v3(0.0f);
    v3 Scale = v3(1.0f);

    inline m4 Matrix()
    {
        return bkm::Translate(m4(1.0f), Translation)
            * bkm::ToM4(qtn(Rotation))
            * bkm::Scale(m4(1.0f), Scale);
    }
};

struct player
{
    v3 Position = v3(0.0f, 18, 1.0f);
    v3 Rotation = v3(-bkm::PI_HALF, 0.0f, 0.0f);
    v3 Velocity = v3(0.0f);
    bool IsPhysicsObject = false;
    bool Grounded = false;
};

struct cow
{
    transform Transform;
    bool IsPhysicsObject = true;
    bool Grounded = false;
    texture Texture;
    v3 Velocity = v3(0.0f);
};

struct block
{
    bool Placed = false;
    v3 Position = {};
    v3 Scale = v3(1.0f);
    v4 Color = v4(1.0f);
    texture Texture = {};
    i32 Left = INT_MAX, Right = INT_MAX, Front = INT_MAX, Back = INT_MAX, Up = INT_MAX, Down = INT_MAX; // Neighbours
    block_type Type = block_type::INVALID;
};

static const i64 RowCount = 16;
static const i64 ColumnCount = 16;
static const i64 LayerCount = 256;

struct game
{
    camera Camera;
    v3 CameraOffset = v3(0.0f, 0.8f, 0.0f);

    player Player;
    cow Cow;

    std::vector<block> Blocks;

    texture CrosshairTexture;
    texture BlockTextures[BLOCK_TYPE_COUNT]; 

    f32 Time;
};

internal game GameCreate(game_renderer* Renderer);
internal void GameUpdate(game* Game, game_renderer* Renderer, const game_input* Input, f32 TimeStep, u32 ClientAreaWidth, u32 ClientAreaHeight);
internal void GamePlayerUpdate(game* Game, const game_input* Input, f32 TimeStep);
internal void GameCowUpdate(game* Game, game_renderer* Renderer, f32 TimeStep);
internal void GameGenerateWorld(game* Game);

internal bool FindFirstHit(const ray& Ray, const block* Blocks, u64 BlocksCount, v3* HitPoint, v3* HitNormal, block* HitBlock, u64* HitIndex)
{
    f32 ClosestT = INFINITY;
    bool FoundHit = false;

    for (u64 Index = 0; Index < BlocksCount; Index++)
    {
        auto& Block = Blocks[Index];

        if (!Block.Placed)
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

internal bool FindFirstHit(const ray& Ray, const std::vector<block>& Blocks, v3* HitPoint, v3* HitNormal, block* HitBlock, u64* HitIndex)
{
    return FindFirstHit(Ray, Blocks.data(), Blocks.size(), HitPoint, HitNormal, HitBlock, HitIndex);
}

internal block* BlockGetSafe(game* Game, i32 C, i32 R, i32 L)
{
    if (C >= 0 && C < ColumnCount &&
       R >= 0 && R < RowCount &&
       L > 0 && L < LayerCount)
    {
        return &Game->Blocks[(L * RowCount * ColumnCount) + (R * RowCount) + C];
    }

    return nullptr;
}

internal texture_coords GetTextureCoords(i32 GridWidth, i32 GridHeight, i32 BottomLeftX, i32 BottomLeftY, i32 RotationCount = 0)
{
    texture_coords TextureCoords;

    // TODO: Make this more generic?
    f32 TextureWidth = 64.0f;
    f32 TextureHeight = 32.0f;

    f32 PerTexelWidth = 1 / TextureWidth;
    f32 PerTexelHeight = 1 / TextureHeight;

    // 8x8 Grid
    TextureCoords.Coords[0] = { 0.0f, 0.0f };
    TextureCoords.Coords[1] = { GridWidth * PerTexelWidth, 0.0f };
    TextureCoords.Coords[2] = { GridWidth * PerTexelWidth, GridHeight * PerTexelHeight };
    TextureCoords.Coords[3] = { 0.0f, GridHeight * PerTexelHeight };

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
    TextureCoords.Coords[0] += v2(PerTexelWidth * BottomLeftX, PerTexelHeight * (TextureHeight - BottomLeftY - 1));
    TextureCoords.Coords[1] += v2(PerTexelWidth * BottomLeftX, PerTexelHeight * (TextureHeight - BottomLeftY - 1));
    TextureCoords.Coords[2] += v2(PerTexelWidth * BottomLeftX, PerTexelHeight * (TextureHeight - BottomLeftY - 1));
    TextureCoords.Coords[3] += v2(PerTexelWidth * BottomLeftX, PerTexelHeight * (TextureHeight - BottomLeftY - 1));

    return TextureCoords;
};
