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
// From https://tavianator.com/2022/ray_box_boundary.html
internal raycast_result RayCastIntersectsAABB(const ray& Ray, const aabb& Box)
{
    raycast_result Result = {};

    f32 tMin = -INFINITY;  // Closest entry point along ray
    f32 tMax = INFINITY;  // Furthest exit point along ray
    v3 hitNormal = { 0, 0, 0 };

    // Loop over X, Y, Z axes
    for (u32 axis = 0; axis < 3; ++axis)
    {
        f32 rayOrigin = Ray.Origin[axis];
        f32 rayDir = Ray.Direction[axis];
        f32 boxMin = Box.Min[axis];
        f32 boxMax = Box.Max[axis];
        
        f32 invDir = 1.0f / rayDir;
        
        f32 t0 = (boxMin - rayOrigin) * invDir;
        f32 t1 = (boxMax - rayOrigin) * invDir;

        // Ensure t0 is the near intersection and t1 is the far
        if (invDir < 0.0f)
            std::swap(t0, t1);

        // Update near intersection
        if (t0 > tMin)
        {
            tMin = t0;

            // Update hit normal (points outward from box)
            hitNormal = { 0, 0, 0 };
            hitNormal[axis] = (rayDir > 0.0f) ? -1.0f : 1.0f;
        }

        // Update far intersection
        tMax = bkm::Min(tMax, t1);

        // Early exit: no overlap along this axis
        if (tMax < tMin)
        {
            Result.Hit = false;
            return Result;
        }
    }

    // Ignore intersections that are behind the ray origin
    if (tMin < 0.0f)
    {
        Result.Hit = false;
        return Result;
    }

    Result.Hit = true;
    Result.Near = tMin;
    Result.Far = tMax;
    Result.Normal = hitNormal;

    return Result;
}

