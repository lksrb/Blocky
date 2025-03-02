#pragma once

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

struct ray
{
    v3 Origin;
    v3 Direction;
};

struct aabb
{
    v3 Min;
    v3 Max;
};

enum class block_type : u32
{
    Dirt = 0,

    INVALID
};
#define BLOCK_TYPE_COUNT (u32)block_type::INVALID

struct player
{
    v3 Position = v3(0.0f, 20.0f, 1.0f);
    v3 Rotation = v3(-bkm::PI_HALF, 0.0f, 0.0f);
    v3 Velocity = v3(0.0f);
    bool PhysicsObject = true;
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
static const i64 LayerCount = 2;

struct game
{
    camera Camera;

    player Player;

    std::vector<block> LogicBlocks;

    std::vector<block> Blocks;
    std::vector<block> Intersections;

    texture CrosshairTexture;

    texture BlockTextures[BLOCK_TYPE_COUNT];
};

internal game GameCreate(game_renderer* Renderer);
internal void GameUpdate(game* Game, game_renderer* Renderer, const game_input* Input, f32 TimeStep, u32 ClientAreaWidth, u32 ClientAreaHeight);
internal void GamePlayerUpdate(game* Game, const game_input* Input, f32 TimeStep);

internal aabb AABBFromV3(v3 Position, v3 Scale)
{
    aabb Result;
    Result.Min = Position - (Scale * 0.5f);
    Result.Max = Position + (Scale * 0.5f);
    return Result;
}

enum class CollisionSide : u32
{
    Top = 0,
    Bottom = 1 << 0,
    Left = 1 << 1,
    Right = 1 << 2,
    Front = 1 << 3,
    Back = 1 << 4,
};

struct collision_result
{
    bool Collided;
    CollisionSide Side;
};

internal collision_result CheckCollisionAABB(const aabb& a, const aabb& b, f32 TimeStep)
{
    collision_result Result;
    Result.Collided = false;

    v3 centerA = (a.Min + a.Max) * 0.5f;
    v3 centerB = (b.Min + b.Max) * 0.5f;
    v3 halfSizeA = (a.Max - a.Min) * 0.5f;
    v3 halfSizeB = (b.Max - b.Min) * 0.5f;

    v3 delta = centerB - centerA;
    v3 overlap = (halfSizeA + halfSizeB) - bkm::Abs(delta);

    f32 Margin = 0;

    if (overlap.x > Margin && overlap.y > Margin && overlap.z > Margin)
    {
        Result.Collided = true;
        if (overlap.x < overlap.y && overlap.x < overlap.z)
        {
            Result.Side = (delta.x > Margin) ? CollisionSide::Left : CollisionSide::Right;
        }
        else if (overlap.y < overlap.x && overlap.y < overlap.z)
        {
            Result.Side = (delta.y > Margin) ? CollisionSide::Top : CollisionSide::Bottom;
        }
        else
        {
            Result.Side = (delta.z > Margin) ? CollisionSide::Front : CollisionSide::Back;
        }
    }

    return Result;
}
