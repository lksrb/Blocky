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
    v3 Position = v3(0.0f, 18, 1.0f);
    v3 Rotation = v3(-bkm::PI_HALF, 0.0f, 0.0f);
    v3 Velocity = v3(0.0f);
    bool IsPhysicsObject = true;
    bool Grounded = false;
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
static const i64 LayerCount = 16;

struct game
{
    camera Camera;
    v3 CameraOffset = v3(0.0f, 0.8f, 0.0f);

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
    v3 HalfScale = bkm::Abs(Scale) * 0.5f;
    Result.Min = Position - HalfScale;
    Result.Max = Position + HalfScale;
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

internal collision_result CheckCollisionAABB(const v3& Center0, const v3& Center1, const aabb& Box0, const aabb& Box1, f32 DeltaTime)
{
    collision_result Result;
    Result.Collided = false;

    v3 HalfSize0 = (Box0.Max - Box0.Min) * 0.5f;
    v3 halfSizeB = (Box1.Max - Box1.Min) * 0.5f;

    v3 Delta = Center1 - Center0;
    v3 Overlap = (HalfSize0 + halfSizeB) - bkm::Abs(Delta);

    DeltaTime = 0;
    // Edge handling: If overlap is very small, treat as a collision
    if (Overlap.x > DeltaTime && Overlap.y > DeltaTime && Overlap.z > DeltaTime)
    {
        Result.Collided = true;

        // Check if the AABBs are just touching (edge case detection)
        if (Overlap.x <= DeltaTime || Overlap.y <= DeltaTime || Overlap.z <= DeltaTime)
        {
            // Edge case detection (exactly touching or almost touching)
            if (Overlap.x <= DeltaTime)
            {
                Result.Side = (Delta.x > 0) ? CollisionSide::Left : CollisionSide::Right;
            }
            else if (Overlap.y <= DeltaTime)
            {
                Result.Side = (Delta.y > 0) ? CollisionSide::Top : CollisionSide::Bottom;
            }
            else if (Overlap.z <= DeltaTime)
            {
                Result.Side = (Delta.z > 0) ? CollisionSide::Front : CollisionSide::Back;
            }
        }
        else
        {
            // Normal collision handling (overlap is more than epsilon)
            if (Overlap.x < Overlap.y && Overlap.x < Overlap.z)
            {
                Result.Side = (Delta.x > DeltaTime) ? CollisionSide::Left : CollisionSide::Right;
            }
            else if (Overlap.y < Overlap.x && Overlap.y < Overlap.z)
            {
                Result.Side = (Delta.y > DeltaTime) ? CollisionSide::Top : CollisionSide::Bottom;
            }
            else
            {
                Result.Side = (Delta.z > DeltaTime) ? CollisionSide::Front : CollisionSide::Back;
            }
        }
    }

    return Result;
}

internal bool CheckCollisionAABBX(const aabb& Box0, const aabb& Box1)
{
    return (Box0.Min.x <= Box1.Max.x + bkm::EPSILON && Box0.Max.x + bkm::EPSILON >= Box1.Min.x);
}

internal bool CheckCollisionAABBY(const aabb& Box0, const aabb& Box1)
{
    return (Box0.Min.y <= Box1.Max.y + bkm::EPSILON && Box0.Max.y + bkm::EPSILON >= Box1.Min.y);
}

internal bool CheckCollisionAABBZ(const aabb& Box0, const aabb& Box1)
{
    return (Box0.Min.z <= Box1.Max.z + bkm::EPSILON && Box0.Max.z + bkm::EPSILON >= Box1.Min.z);
}

internal bool CheckCollisionAABB(const aabb& Box0, const aabb& Box1)
{
    return CheckCollisionAABBX(Box0, Box1) && CheckCollisionAABBY(Box0, Box1) && CheckCollisionAABBZ(Box0, Box1);
}
