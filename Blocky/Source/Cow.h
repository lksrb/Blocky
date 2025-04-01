#pragma once

// Internal storage
struct cow
{

};

internal void CowCreate(entity_registry* Registry, entity Entity, logic_component* Logic)
{
    Logic->Storage = new cow; // TODO: Arena allocator
}

internal void CowDestroy(entity_registry* Registry, entity Entity, logic_component* Logic)
{
    delete Logic->Storage; // Does not call destruction, I dont think we need it
}

internal void CowUpdate(entity_registry* Registry, entity Entity, logic_component* Logic, f32 TimeStep)
{
    auto& Transform = GetComponent<transform>(Registry, Entity);
    auto& AABBPhysics = GetComponent<aabb_physics>(Registry, Entity);

    f32 Speed = 1.0f;
    v3 Direction(1.0f, 0.0f, 0.0f);

    // This way we can actually set velocity while respecting the physics simulation
    AABBPhysics.Velocity.x = Speed * Direction.x;
    AABBPhysics.Velocity.z = Speed * Direction.y;

    if (bkm::NonZero(v2(AABBPhysics.Velocity.x, AABBPhysics.Velocity.z)))
    {
        Transform.Rotation.y = bkm::Atan2(AABBPhysics.Velocity.x, AABBPhysics.Velocity.z);
    }
}
