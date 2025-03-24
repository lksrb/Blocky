#pragma once

// My own implementation of an ECS system

#define INVALID_ID ECS_entity((u32)-1)

//enum class entity : u32 {};

using ECS_entity = u32;

struct transform_sparse_set
{
    i32 Sparse[16];
    i32 Dense[16];
    i32 DenseCount = 0;

    transform DenseTransforms[16]; // Matches dense array

    transform_sparse_set()
    {
        memset(Sparse, -1, sizeof(Sparse));
        memset(Dense, -1, sizeof(Dense));
        DenseCount = 0;
    }
};

bool TransformSparseSetContains(transform_sparse_set* Set, i32 ID)
{
    i32 DenseIndex = Set->Sparse[ID];

    // TODO: optimalization
    return DenseIndex < Set->DenseCount && Set->Dense[DenseIndex] == ID;
}

void TransformSparseSetAdd(transform_sparse_set* Set, ECS_entity Entity, const transform& Transform)
{
    if (TransformSparseSetContains(Set, Entity))
        return;

    Set->Dense[Set->DenseCount] = Entity;
    Set->Sparse[Entity] = Set->DenseCount;

    // Set the actual transform
    Set->DenseTransforms[Set->DenseCount] = Transform;

    Set->DenseCount++;
}

i32 TransformSparseSetAdd(transform_sparse_set* Set, transform Transform)
{
    i32 Entity = Set->DenseCount;

    Set->Dense[Set->DenseCount] = Entity;
    Set->Sparse[Entity] = Set->DenseCount;

    // Set the actual transform
    Set->DenseTransforms[Set->DenseCount] = Transform;

    Set->DenseCount++;

    return Entity;
}

transform& TransformSparseSetGet(transform_sparse_set* Set, ECS_entity Entity)
{
    return Set->DenseTransforms[Set->Sparse[Entity]];
}

void TransformSparseSetRemove(transform_sparse_set* Set, i32 ID)
{
    if (!TransformSparseSetContains(Set, ID))
        return;

    Set->DenseCount--;

    // We need to ensure that dense array stays dense,
    // so we need the last item to fill in the hole that is made by removing stuff

    i32 DenseIndex = Set->Sparse[ID];
    i32& Last = Set->Dense[Set->DenseCount];

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
    ECS_entity Entities[16];
    u32 EntitiesCount = 0;
    transform_sparse_set TransformSet;

    ECS_entity FreeList = INVALID_ID;

    entity_registry()
    {
        memset(Entities, (u32)INVALID_ID, sizeof(Entities));
    }
};

ECS_entity ECS_CreateEntity(entity_registry* Registry)
{
    if (Registry->FreeList == INVALID_ID)
    {
        ECS_entity Entity = Registry->EntitiesCount;
        Registry->Entities[Entity] = Entity;
        ++Registry->EntitiesCount;

        return Entity;
    }
    else
    {
        ECS_entity Entity = Registry->FreeList;
        Registry->FreeList = Registry->Entities[Entity];
        Registry->Entities[Entity] = Entity;
        return Entity;
    }
}

void ECS_DestroyEntity(entity_registry* Registry, ECS_entity Entity)
{
    if (TransformSparseSetContains(&Registry->TransformSet, Entity))
    {
        TransformSparseSetRemove(&Registry->TransformSet, Entity);
    }

    if (Registry->FreeList == INVALID_ID)
    {
        Registry->Entities[Entity] = INVALID_ID;

        // Set free list to entity index
        Registry->FreeList = Entity;

        // Set free list entity to an invalid value to signalize that the its the last in the chain
        Registry->Entities[Registry->FreeList] = INVALID_ID;
    }
    else
    {
        Registry->Entities[Entity] = Registry->FreeList;
        Registry->FreeList = Entity;
    }
}

void ECS_AddTransformComponent(entity_registry* Registry, ECS_entity Entity, const transform& Transform)
{
    assert(!TransformSparseSetContains(&Registry->TransformSet, Entity));

    TransformSparseSetAdd(&Registry->TransformSet, Entity, Transform);
}

void ECS_RemoveTransformComponent(entity_registry* Registry, ECS_entity Entity)
{
    assert(TransformSparseSetContains(&Registry->TransformSet, Entity));

    TransformSparseSetRemove(&Registry->TransformSet, Entity);
}

transform& ECS_GetTransformComponent(entity_registry* Registry, ECS_entity Entity)
{
    assert(TransformSparseSetContains(&Registry->TransformSet, Entity));

    return TransformSparseSetGet(&Registry->TransformSet, Entity);
}
