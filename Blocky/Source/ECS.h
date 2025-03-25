#pragma once

// My own implementation of an ECS system

#define INVALID_ID ecs_entity((ecs_entity_type)-1)

using ecs_entity_type = u32;
enum class ecs_entity : ecs_entity_type {};

#define to_entity(__EntityType) static_cast<ecs_entity>(__EntityType) 
#define to_entity_type(__Entity) static_cast<ecs_entity_type>(__Entity)

struct transform_sparse_set
{
    ecs_entity_type Sparse[16];
    ecs_entity_type Dense[16];
    u32 DenseCount = 0; // TODO: Should this be entity type

    transform DenseTransforms[16]; // Matches dense array

    transform_sparse_set()
    {
        memset(Sparse, -1, sizeof(Sparse));
        memset(Dense, -1, sizeof(Dense));
        DenseCount = 0;
    }
};

bool TransformSparseSetContains(transform_sparse_set* Set, ecs_entity_type Entity)
{
    ecs_entity_type DenseIndex = Set->Sparse[to_entity_type(Entity)];

    // TODO: optimalization
    return DenseIndex < Set->DenseCount && Set->Dense[DenseIndex] == Entity;
}

transform& TransformSparseSetAdd(transform_sparse_set* Set, ecs_entity_type Entity)
{
    Assert(!TransformSparseSetContains(Set, Entity), "Entity already has this component!");

    Set->Dense[Set->DenseCount] = Entity;
    Set->Sparse[Entity] = Set->DenseCount;

    // Get the actual transform
    return Set->DenseTransforms[Set->DenseCount++];
}

transform& TransformSparseSetGet(transform_sparse_set* Set, ecs_entity_type Entity)
{
    return Set->DenseTransforms[Set->Sparse[Entity]];
}

void TransformSparseSetRemove(transform_sparse_set* Set, ecs_entity_type Entity)
{
    Assert(TransformSparseSetContains(Set, Entity), "Entity does not have this component!");

    Set->DenseCount--;

    // We need to ensure that dense array stays dense,
    // so we need the last item to fill in the hole that is made by removing stuff

    ecs_entity_type DenseIndex = Set->Sparse[Entity];
    ecs_entity_type& Last = Set->Dense[Set->DenseCount];

    {
        // Update dense transforms based on the dense index array
        Set->DenseTransforms[DenseIndex] = Set->DenseTransforms[Last];

        // Reset
        Set->DenseTransforms[Last] = transform();
    }

    Set->Dense[DenseIndex] = Last;
    Set->Sparse[Last] = DenseIndex;

    //Last = -1;
}

struct entity_registry
{
    ecs_entity Entities[16];
    u32 EntitiesCount = 0;
    transform_sparse_set TransformSet;

    ecs_entity FreeList = INVALID_ID;

    entity_registry()
    {
        memset(Entities, (u32)INVALID_ID, sizeof(Entities));
    }
};

ecs_entity ECS_CreateEntity(entity_registry* Registry)
{
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

void ECS_DestroyEntity(entity_registry* Registry, ecs_entity Entity)
{
    if (TransformSparseSetContains(&Registry->TransformSet, to_entity_type(Entity)))
    {
        TransformSparseSetRemove(&Registry->TransformSet, to_entity_type(Entity));
    }

    if (Registry->FreeList == INVALID_ID)
    {
        // Set free list entity to an invalid value to signalize that the its the last in the chain
        Registry->Entities[to_entity_type(Entity)] = INVALID_ID;

        // Set free list to entity index
        Registry->FreeList = Entity;

        //Registry->Entities[to_ecs_entity_type(Registry->FreeList)] = INVALID_ID;
    }
    else
    {
        Registry->Entities[to_entity_type(Entity)] = Registry->FreeList;
        Registry->FreeList = Entity;
    }
}

bool ECS_ValidEntity(entity_registry* Registry, ecs_entity Entity)
{
    return Registry->EntitiesCount < to_entity_type(Entity) && Registry->Entities[to_entity_type(Entity)] == Entity;
}

transform& ECS_AddTransformComponent(entity_registry* Registry, ecs_entity Entity)
{
    Assert(!TransformSparseSetContains(&Registry->TransformSet, to_entity_type(Entity)), "Entity already has this component!");
    return TransformSparseSetAdd(&Registry->TransformSet, to_entity_type(Entity));
}

void ECS_RemoveTransformComponent(entity_registry* Registry, ecs_entity Entity)
{
    Assert(TransformSparseSetContains(&Registry->TransformSet, to_entity_type(Entity)), "Entity does not have this component!");

    TransformSparseSetRemove(&Registry->TransformSet, to_entity_type(Entity));
}

transform& ECS_GetTransformComponent(entity_registry* Registry, ecs_entity Entity)
{
    Assert(TransformSparseSetContains(&Registry->TransformSet, to_entity_type(Entity)), "Entity does not have this component!");

    return TransformSparseSetGet(&Registry->TransformSet, to_entity_type(Entity));
}

void ECS_Test()
{
    entity_registry Registry;

    for (size_t i = 0; i < 10; i++)
    {

        for (i32 i = 0; i < 10; i++)
        {
            ecs_entity Entity = ECS_CreateEntity(&Registry);

            auto& T = ECS_AddTransformComponent(&Registry, Entity);
            T.Translation.x = i;
        }

        auto E = to_entity(0);

        bool Exists = ECS_ValidEntity(&Registry, E);
        ECS_DestroyEntity(&Registry, E);
        Exists = ECS_ValidEntity(&Registry, E);

        for (i32 i = 0; i < 5; i++)
        {
            ECS_DestroyEntity(&Registry, to_entity(i));
        }

        for (i32 i = 0; i < 10; i++)
        {
            ecs_entity Entity = ECS_CreateEntity(&Registry);

            auto& T = ECS_AddTransformComponent(&Registry, Entity);
            T.Translation.x = -i;
        }

        /*for (u32 i = 0; i < Registry.EntitiesCount; i++)
        {
            if(ECS_enti)

            ECS_DestroyEntity(&Registry, to_entity(i));
        }*/

    }


    for (size_t i = 0; i < Registry.TransformSet.DenseCount; i++)
    {
        auto& Transform = Registry.TransformSet.DenseTransforms[i];
        Transform.Translation.x += 1.0f;

        TraceV3(Transform.Translation);
    }

    ExitProcess(0);
}
