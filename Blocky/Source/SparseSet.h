#pragma once

/*
 * Sparse Set Implementation
 */

#if 0

template<typename Component>
struct sparse_set
{
    ecs_entity_type* Sparse = nullptr;
    ecs_entity_type* Dense = nullptr;
    Component* Components = nullptr; // Matches dense array
    ecs_entity_type DenseCount = 0;
    ecs_entity_type Capacity = 0;
};

template<typename Component>
internal auto SparseSetCreate(ecs_entity_type Capacity)
{
    auto SparseSet = sparse_set<Component>();

    SparseSet.Components = new Component[Capacity];
    SparseSet.Sparse = new ecs_entity_type[Capacity];
    SparseSet.Dense = new ecs_entity_type[Capacity];
    SparseSet.Capacity = Capacity;
    SparseSet.DenseCount = 0;

    memset(SparseSet.Sparse, to_entity_type(INVALID_ID), Capacity * sizeof(ecs_entity_type));
    memset(SparseSet.Dense, to_entity_type(INVALID_ID), Capacity * sizeof(ecs_entity_type));

    return SparseSet;
}

template<typename Component>
internal void SparseSetDestroy(sparse_set<Component>* SparseSet)
{
    delete[] SparseSet->Components;
    delete[] SparseSet->Sparse;
    delete[] SparseSet->Dense;

    // Reset
    *SparseSet = sparse_set<Component>();
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

    // Component 
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

#endif

// -----------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------

struct sparse_set
{
    ecs_entity_type* Sparse = nullptr;
    ecs_entity_type* Dense = nullptr;
    ecs_entity_type DenseCount = 0;
    ecs_entity_type Capacity = 0;
};

internal auto SparseSetCreate(ecs_entity_type Capacity)
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

internal void SparseSetDestroy(sparse_set* SparseSet)
{
    delete[] SparseSet->Sparse;
    delete[] SparseSet->Dense;

    // Reset
    *SparseSet = sparse_set();
}

internal bool SparseSetContains(sparse_set* Set, ecs_entity_type Entity)
{
    ecs_entity_type DenseIndex = Set->Sparse[ecs_to_entity_type(Entity)];

    // TODO: optimalization
    return DenseIndex < Set->DenseCount && Set->Dense[DenseIndex] == Entity;
}

internal ecs_entity_type SparseSetAdd(sparse_set* Set, ecs_entity_type Entity)
{
    Assert(!SparseSetContains(Set, Entity), "Entity already has this component!");

    Set->Dense[Set->DenseCount] = Entity;
    Set->Sparse[Entity] = Set->DenseCount;

    // Get the actual transform
    return Set->DenseCount++;
}

internal ecs_entity_type SparseSetGet(sparse_set* Set, ecs_entity_type Entity)
{
    Assert(SparseSetContains(Set, Entity), "Entity already has this component!");
    return Set->Sparse[Entity];
}

struct rem
{
    ecs_entity_type Last, DenseIndex;
};

internal rem SparseSetRemove(sparse_set* Set, ecs_entity_type Entity)
{
    Assert(SparseSetContains(Set, Entity), "Entity does not have this component!");
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

internal void SparseSetClear(sparse_set* Set)
{
    Set->DenseCount = 0; // WOW, incredibly cheap
}
