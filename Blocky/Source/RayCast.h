#pragma once

struct ray
{
    v3 Origin;
    v3 Direction;
};

struct raycast_result
{
    f32 Near;
    f32 Far;
    v3 Normal;
    bool Hit = true;

    operator bool() const { return Hit; }
};

// Slab method raycast
// Ignores negative values to ensure one direction
internal raycast_result RayCastIntersectsAABB(const ray& Ray, const aabb& Box)
{
    raycast_result Result = {};

    // TODO: How does this work?
    f32 Min = -INFINITY;
    f32 Max = INFINITY;
    v3 Normal(0.0f);
    for (u32 Axis = 0; Axis < 3; ++Axis)
    {
        f32 InvD = 1.0f / (Axis == 0 ? Ray.Direction.x : (Axis == 1 ? Ray.Direction.y : Ray.Direction.z));
        f32 T0 = ((Axis == 0 ? Box.Min.x : (Axis == 1 ? Box.Min.y : Box.Min.z)) - (Axis == 0 ? Ray.Origin.x : (Axis == 1 ? Ray.Origin.y : Ray.Origin.z))) * InvD;
        f32 T1 = ((Axis == 0 ? Box.Max.x : (Axis == 1 ? Box.Max.y : Box.Max.z)) - (Axis == 0 ? Ray.Origin.x : (Axis == 1 ? Ray.Origin.y : Ray.Origin.z))) * InvD;

        if (InvD < 0.0f)
        {
            // Swap
            f32 Temp = T0;
            T0 = T1;
            T1 = Temp;
        }

        if (T0 > Min)
        {
            Min = T0;
            Normal = { 0, 0, 0 };
            if (Axis == 0) Normal.x = (Ray.Direction.x > 0) ? -1.0f : 1.0f;
            if (Axis == 1) Normal.y = (Ray.Direction.y > 0) ? -1.0f : 1.0f;
            if (Axis == 2) Normal.z = (Ray.Direction.z > 0) ? -1.0f : 1.0f;
        }

        //Min = std::max(Min, t0);
        Max = bkm::Min(Max, T1);

        if (Max < Min)
        {
            Result.Hit = false;
            break;
        }
    }

    // Ignore hits behind the ray
    if (Min < 0)
    {
        Result.Hit = false;
    }

    Result.Near = Min;
    Result.Far = Max;
    Result.Normal = Normal;
    return Result;
}
