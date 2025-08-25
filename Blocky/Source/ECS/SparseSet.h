#pragma once

/*
 * Sparse Set Implementation (minimal fixes)
 */

struct sparse_set
{
    ecs_entity_type* Sparse = nullptr;
    ecs_entity_type* Dense = nullptr;
    ecs_entity_type Size = 0;
    ecs_entity_type Capacity = 0;
    ecs_entity_type Universe = 0; // NEW: max possible entity + 1
};

internal sparse_set sparse_set_create(ecs_entity_type Universe, ecs_entity_type Capacity)
{
    sparse_set SparseSet;

    SparseSet.Universe = Universe;
    SparseSet.Capacity = Capacity;
    SparseSet.Size = 0;

    // Sparse indexed by Entity ID -> must be Universe size
    SparseSet.Sparse = new ecs_entity_type[Universe];
    SparseSet.Dense = new ecs_entity_type[Capacity];

    // Initialize Sparse to INVALID
    for (ecs_entity_type i = 0; i < Universe; i++)
    {
        SparseSet.Sparse[i] = ecs_to_entity_type(ECS_INVALID_ID);
    }

    // Dense init (optional)
    for (ecs_entity_type i = 0; i < Capacity; i++)
    {
        SparseSet.Dense[i] = ecs_to_entity_type(ECS_INVALID_ID);
    }

    return SparseSet;
}

internal void sparse_set_destroy(sparse_set* SparseSet)
{
    delete[] SparseSet->Sparse;
    delete[] SparseSet->Dense;

    *SparseSet = {};
}

internal bool sparse_set_contains(sparse_set* Set, ecs_entity_type Entity)
{
    ecs_entity_type DenseIndex = Set->Sparse[ecs_to_entity_type(Entity)];
    return DenseIndex < Set->Size && Set->Dense[DenseIndex] == Entity;
}

internal ecs_entity_type sparse_set_add(sparse_set* Set, ecs_entity_type Entity)
{
    Assert(!sparse_set_contains(Set, Entity), "Entity already has this component!");

    Set->Dense[Set->Size] = Entity;
    Set->Sparse[Entity] = Set->Size;

    return Set->Size++; // returns old size = index where inserted
}

internal ecs_entity_type sparse_set_get(sparse_set* Set, ecs_entity_type Entity)
{
    Assert(sparse_set_contains(Set, Entity), "Entity does not have this component!");
    return Set->Sparse[Entity];
}

struct rem
{
    ecs_entity_type Last, DenseIndex;
};

internal rem sparse_set_remove(sparse_set* Set, ecs_entity_type Entity)
{
    Assert(sparse_set_contains(Set, Entity), "Entity does not have this component!");
    Assert(Set->Size > 0, "Sparse Set underflow!");

    ecs_entity_type DenseIndex = Set->Sparse[Entity];
    ecs_entity_type LastValue = Set->Dense[Set->Size - 1];

    Set->Dense[DenseIndex] = LastValue;
    Set->Sparse[LastValue] = DenseIndex;

    Set->Size--;

    Set->Sparse[Entity] = ecs_to_entity_type(ECS_INVALID_ID);

    return { LastValue, DenseIndex };
}

internal void sparse_set_clear(sparse_set* Set)
{
    Set->Size = 0; // still O(1), safe because contains() checks size
}
