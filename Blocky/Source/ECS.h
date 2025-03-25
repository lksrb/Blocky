#pragma once

/*
 * My own implementation of an ECS system.
 * It uses sparse sets which ensure that particular component instances are tighly packed together
 * at the cost of memory, we have to keep two arrays, one for the dense entities and one for sparse entities.
 * We also have freelist mechanism where on top of a sparse set, which just stores numbers, is an array of entities.
 * With this we can even check if a valid by comparing its index with its value. If that is true, the entity is valid, otherwise false.
 *
 * Sparse Sets
 * For each component there needs to be a unique sparse set implementation with that component.
 * This is perfect for template magic but that would sacrifice a lot of build time for nothing (thanks C++!) so we need to find some macro magic instead.
 * Or just compare if the template magic is negligable or not compared to macro.
 */

#define MAX_ENTITIES 16

#define INVALID_ID ecs_entity((ecs_entity_type)-1)

using ecs_entity_type = u32;
enum class ecs_entity : ecs_entity_type {};

#define to_entity(__EntityType) static_cast<ecs_entity>(__EntityType) 
#define to_entity_type(__Entity) static_cast<ecs_entity_type>(__Entity)

/*
 * Sparse Set Implementation
 */

template<typename Component>
struct sparse_set
{
    ecs_entity_type Sparse[MAX_ENTITIES];
    ecs_entity_type Dense[MAX_ENTITIES];
    Component Components[MAX_ENTITIES]; // Matches dense array
    ecs_entity_type DenseCount = 0;
};

template<typename Component>
internal auto SparseSetCreate(ecs_entity_type Capacity)
{
    auto SparseSet = sparse_set<Component>();

    // TODO: Proper tombstone/invalid ID?
    memset(SparseSet.Sparse, -1, sizeof(SparseSet.Sparse));
    memset(SparseSet.Dense, -1, sizeof(SparseSet.Dense));
    SparseSet.DenseCount = 0;

    return SparseSet;
}

template<typename Component>
internal void SparseSetDestroy(sparse_set<Component>* SparseSet)
{
    // TODO: Deallocation
}

template<typename Component>
internal bool SparseSetContains(sparse_set<Component>* Set, ecs_entity_type Entity)
{
    ecs_entity_type DenseIndex = Set->Sparse[to_entity_type(Entity)];

    // TODO: optimalization
    return DenseIndex < Set->DenseCount && Set->Dense[DenseIndex] == Entity;
}

template<typename Component>
internal Component& SparseSetAdd(sparse_set<Component>* Set, ecs_entity_type Entity)
{
    Assert(!SparseSetContains(Set, Entity), "Entity already has this component!");

    Set->Dense[Set->DenseCount] = Entity;
    Set->Sparse[Entity] = Set->DenseCount;

    // Get the actual transform
    return Set->Components[Set->DenseCount++];
}

template<typename Component>
internal Component& SparseSetGet(sparse_set<Component>* Set, ecs_entity_type Entity)
{
    Assert(SparseSetContains(Set, Entity), "Entity already has this component!");
    return Set->Components[Set->Sparse[Entity]];
}

template<typename Component>
internal void SparseSetRemove(sparse_set<Component>* Set, ecs_entity_type Entity)
{
    Assert(SparseSetContains(Set, Entity), "Entity does not have this component!");
    Assert(Set->DenseCount > 0, "Sparse Set underflow!");

    Set->DenseCount--;

    // We need to ensure that dense array stays dense,
    // so we need the last item to fill in the hole that is made by removing stuff

    ecs_entity_type DenseIndex = Set->Sparse[Entity];
    ecs_entity_type& Last = Set->Dense[Set->DenseCount];

    {
        // Update dense transforms based on the dense index array
        Set->Components[DenseIndex] = Set->Components[Last];

        // Reset
        // TODO: this is not necessary but good for debugging
        Set->Components[Last] = Component();
    }

    Set->Dense[DenseIndex] = Last;
    Set->Sparse[Last] = DenseIndex;

    //Last = -1;
}

template<typename Component>
internal void SparseSetClear(sparse_set<Component>* Set)
{
    Set->DenseCount = 0; // WOW, incredibly cheap
}

template<typename Component>
struct view
{
    Component* Components;
    ecs_entity_type Count;

    auto begin() { return Components; }
    auto end() { return Components + Count; }
};

struct entity_registry
{
    ecs_entity Entities[MAX_ENTITIES];
    u32 EntitiesCount = 0;
    sparse_set<transform> TransformSet;

    ecs_entity FreeList = INVALID_ID;
};

internal entity_registry ECS_EntityRegistryCreate(ecs_entity_type Capacity)
{
    entity_registry Registry;
    memset(Registry.Entities, (u32)INVALID_ID, sizeof(Registry.Entities));
    return Registry;
}

internal void ECS_EntityRegistryDestroy(entity_registry* Registry)
{

}

internal ecs_entity ECS_CreateEntity(entity_registry* Registry)
{
    Assert(Registry->EntitiesCount < MAX_ENTITIES, "Too many entities!");

    if (Registry->FreeList == INVALID_ID)
    {
        ecs_entity Entity = to_entity(Registry->EntitiesCount);
        Registry->Entities[to_entity_type(Entity)] = Entity;
        ++Registry->EntitiesCount;

        return Entity;
    }
    else
    {
        ecs_entity Entity = Registry->FreeList;
        Registry->FreeList = Registry->Entities[to_entity_type(Entity)];
        Registry->Entities[to_entity_type(Entity)] = Entity;
        return Entity;
    }
}

internal bool ECS_ValidEntity(entity_registry* Registry, ecs_entity Entity)
{
    return to_entity_type(Entity) < Registry->EntitiesCount && Registry->Entities[to_entity_type(Entity)] == Entity;
}

internal void ECS_DestroyEntity(entity_registry* Registry, ecs_entity Entity)
{
    Assert(ECS_ValidEntity(Registry, Entity), "Entity is not valid!");

    if (SparseSetContains(&Registry->TransformSet, to_entity_type(Entity)))
    {
        SparseSetRemove(&Registry->TransformSet, to_entity_type(Entity));
    }

    if (Registry->FreeList == INVALID_ID)
    {
        // Set free list entity to an invalid value to signalize that the its the last in the chain
        Registry->Entities[to_entity_type(Entity)] = INVALID_ID;

        // Set free list to entity index
        Registry->FreeList = Entity;
    }
    else
    {
        Registry->Entities[to_entity_type(Entity)] = Registry->FreeList;
        Registry->FreeList = Entity;
    }
}

internal transform& ECS_AddTransformComponent(entity_registry* Registry, ecs_entity Entity)
{
    Assert(!SparseSetContains(&Registry->TransformSet, to_entity_type(Entity)), "Entity already has this component!");
    return SparseSetAdd(&Registry->TransformSet, to_entity_type(Entity));
}

internal void ECS_RemoveTransformComponent(entity_registry* Registry, ecs_entity Entity)
{
    Assert(SparseSetContains(&Registry->TransformSet, to_entity_type(Entity)), "Entity does not have this component!");

    SparseSetRemove(&Registry->TransformSet, to_entity_type(Entity));
}

internal transform& ECS_GetTransformComponent(entity_registry* Registry, ecs_entity Entity)
{
    Assert(SparseSetContains(&Registry->TransformSet, to_entity_type(Entity)), "Entity does not have this component!");

    return SparseSetGet(&Registry->TransformSet, to_entity_type(Entity));
}

internal view<transform> ECS_ViewTransforms(entity_registry* Registry)
{
    return { Registry->TransformSet.Components, Registry->TransformSet.DenseCount };
}

internal void ECS_Test()
{
    entity_registry Registry;

    for (size_t i = 0; i < 1; i++)
    {
        for (i32 i = 0; i < 10; i++)
        {
            ecs_entity Entity = ECS_CreateEntity(&Registry);

            auto& T = ECS_AddTransformComponent(&Registry, Entity);
            T.Translation.x = (f32)i;
        }

        auto E = to_entity(0);

        bool Exists = ECS_ValidEntity(&Registry, E);
        ECS_DestroyEntity(&Registry, E);
        Exists = ECS_ValidEntity(&Registry, E);

        for (i32 i = 1; i < 5; i++)
        {
            ECS_DestroyEntity(&Registry, to_entity(i));
        }

        for (i32 i = 0; i < 10; i++)
        {
            ecs_entity Entity = ECS_CreateEntity(&Registry);

            auto& T = ECS_AddTransformComponent(&Registry, Entity);
            T.Translation.x = (f32)-i;
        }
    }

    auto View = ECS_ViewTransforms(&Registry);

    for (auto& Transform : View)
    {
        Transform.Translation.x += 1.0f;
        TraceV3(Transform.Translation);
    }


    //ExitProcess(0);
}
