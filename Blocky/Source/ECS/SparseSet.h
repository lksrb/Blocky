#pragma once

/*
 * Sparse Set Implementation
 */

struct sparse_set
{
    ecs_entity_type* Sparse = nullptr;
    ecs_entity_type* Dense = nullptr;
    ecs_entity_type DenseCount = 0;
    ecs_entity_type Capacity = 0;
};

internal sparse_set sparse_set_create(ecs_entity_type Capacity)
{
    sparse_set SparseSet;

    SparseSet.Sparse = new ecs_entity_type[Capacity];
    SparseSet.Dense = new ecs_entity_type[Capacity];
    SparseSet.Capacity = Capacity;
    SparseSet.DenseCount = 0;

    memset(SparseSet.Sparse, ecs_to_entity_type(INVALID_ID), Capacity * sizeof(ecs_entity_type));
    memset(SparseSet.Dense, ecs_to_entity_type(INVALID_ID), Capacity * sizeof(ecs_entity_type));

    return SparseSet;
}

internal void sparse_set_destroy(sparse_set* SparseSet)
{
    delete[] SparseSet->Sparse;
    delete[] SparseSet->Dense;

    // Reset
    *SparseSet = sparse_set();
}

internal bool sparse_set_contains(sparse_set* Set, ecs_entity_type Entity)
{
    ecs_entity_type DenseIndex = Set->Sparse[ecs_to_entity_type(Entity)];

    // TODO: optimalization
    return DenseIndex < Set->DenseCount && Set->Dense[DenseIndex] == Entity;
}

internal ecs_entity_type sparse_set_add(sparse_set* Set, ecs_entity_type Entity)
{
    Assert(!sparse_set_contains(Set, Entity), "Entity already has this component!");

    Set->Dense[Set->DenseCount] = Entity;
    Set->Sparse[Entity] = Set->DenseCount;

    return Set->DenseCount++;
}

internal ecs_entity_type sparse_set_get(sparse_set* Set, ecs_entity_type Entity)
{
    Assert(sparse_set_contains(Set, Entity), "Entity already has this component!");
    return Set->Sparse[Entity];
}

struct rem
{
    ecs_entity_type Last, DenseIndex;
};

internal rem sparse_set_remove(sparse_set* Set, ecs_entity_type Entity)
{
    Assert(sparse_set_contains(Set, Entity), "Entity does not have this component!");
    Assert(Set->DenseCount > 0, "Sparse Set underflow!");

    Set->DenseCount--;

    // We need to ensure that dense array stays dense,
    // so we need the last item to fill in the hole that is made by removing stuff

    ecs_entity_type DenseIndex = Set->Sparse[Entity];
    ecs_entity_type& Last = Set->Dense[Set->DenseCount];

    Set->Dense[DenseIndex] = Last;
    Set->Sparse[Last] = DenseIndex;

    // Update dense transforms based on the dense index array
    //Set->Components[DenseIndex] = Set->Components[Last];

    //Last = -1;

    return { Last, DenseIndex };
}

internal void sparse_set_clear(sparse_set* Set)
{
    Set->DenseCount = 0; // WOW, incredibly cheap
}
