#pragma once

// Internal storage
struct cow
{
    f32 Speed = 1.0f;
    v3 Direction = v3(1.0f, 0.0f, 0.0f);

    random_series Random;
};

internal void CowCreate(entity_registry* Registry, entity Entity, logic_component* Logic)
{
    auto Cow = new cow; // TODO: Arena allocator
    Logic->Storage = Cow;

    // Initialize random series for random behaviour
    Cow->Random = RandomSeriesCreate();
}

internal void CowDestroy(entity_registry* Registry, entity Entity, logic_component* Logic)
{
    delete Logic->Storage; // Does not call destruction, I dont think we need it
}

internal void CowUpdate(game* Game, entity_registry* Registry, entity Entity, logic_component* Logic, f32 TimeStep)
{
    cow* Cow = static_cast<cow*>(Logic->Storage);
    auto& Transform = GetComponent<transform>(Registry, Entity);
    auto& AABBPhysics = GetComponent<aabb_physics>(Registry, Entity);

    // This way we can actually set velocity while respecting the physics simulation
    AABBPhysics.Velocity.x = Cow->Speed * Cow->Direction.x;
    AABBPhysics.Velocity.z = Cow->Speed * Cow->Direction.y;

    if (bkm::NonZero(v2(AABBPhysics.Velocity.x, AABBPhysics.Velocity.z)))
    {
        Transform.Rotation.y = bkm::Atan2(AABBPhysics.Velocity.x, AABBPhysics.Velocity.z);
    }
}
