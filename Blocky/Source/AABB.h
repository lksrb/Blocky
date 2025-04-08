#pragma once

struct aabb
{
    v3 Min;
    v3 Max;
};

enum class aabb_collision_side : u32
{
    Top = 0,
    Bottom = 1 << 0,
    Left = 1 << 1,
    Right = 1 << 2,
    Front = 1 << 3,
    Back = 1 << 4,
};

struct aabb_collision_result
{
    bool Collided;
    aabb_collision_side Side;
};

internal aabb AABBFromV3(const v3& Position, const v3& Scale) noexcept
{
    aabb Result;
    v3 HalfScale = bkm::Abs(Scale) * 0.5f;
    Result.Min = Position - HalfScale;
    Result.Max = Position + HalfScale;
    return Result;
}

internal bool AABBCheckCollisionX(const aabb& Box0, const aabb& Box1, f32 Delta = bkm::EPSILON) noexcept
{
    return (Box0.Min.x <= Box1.Max.x + Delta && Box0.Max.x + Delta >= Box1.Min.x);
}

internal bool AABBCheckCollisionY(const aabb& Box0, const aabb& Box1, f32 Delta = bkm::EPSILON) noexcept
{
    return (Box0.Min.y <= Box1.Max.y + Delta && Box0.Max.y + Delta >= Box1.Min.y);
}

internal bool AABBCheckCollisionZ(const aabb& Box0, const aabb& Box1, f32 Delta = bkm::EPSILON) noexcept
{
    return (Box0.Min.z <= Box1.Max.z + Delta && Box0.Max.z + Delta >= Box1.Min.z);
}

internal bool AABBCheckCollision(const aabb& Box0, const aabb& Box1, f32 Delta = bkm::EPSILON) noexcept
{
    return AABBCheckCollisionX(Box0, Box1, Delta) && AABBCheckCollisionY(Box0, Box1, Delta) && AABBCheckCollisionZ(Box0, Box1, Delta);
}

// TODO: Figure out if this is useful
internal aabb_collision_result AABBCheckCollisionWithCollisionSide(const v3& Center0, const v3& Center1, const aabb& Box0, const aabb& Box1, f32 DeltaTime)
{
    aabb_collision_result Result;
    Result.Collided = false;

    v3 HalfSize0 = (Box0.Max - Box0.Min) * 0.5f;
    v3 halfSizeB = (Box1.Max - Box1.Min) * 0.5f;

    v3 Delta = Center1 - Center0;
    v3 Overlap = (HalfSize0 + halfSizeB) - bkm::Abs(Delta);

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
                Result.Side = (Delta.x > 0) ? aabb_collision_side::Left : aabb_collision_side::Right;
            }
            else if (Overlap.y <= DeltaTime)
            {
                Result.Side = (Delta.y > 0) ? aabb_collision_side::Top : aabb_collision_side::Bottom;
            }
            else if (Overlap.z <= DeltaTime)
            {
                Result.Side = (Delta.z > 0) ? aabb_collision_side::Front : aabb_collision_side::Back;
            }
        }
        else
        {
            // Normal collision handling (overlap is more than epsilon)
            if (Overlap.x < Overlap.y && Overlap.x < Overlap.z)
            {
                Result.Side = (Delta.x > DeltaTime) ? aabb_collision_side::Left : aabb_collision_side::Right;
            }
            else if (Overlap.y < Overlap.x && Overlap.y < Overlap.z)
            {
                Result.Side = (Delta.y > DeltaTime) ? aabb_collision_side::Top : aabb_collision_side::Bottom;
            }
            else
            {
                Result.Side = (Delta.z > DeltaTime) ? aabb_collision_side::Front : aabb_collision_side::Back;
            }
        }
    }

    return Result;
}
