#pragma once

/*
 * My own implementation of an ECS system.
 * It uses sparse sets which ensure that particular component instances are tighly packed together
 * at the cost of memory, we have to keep two arrays, one for the dense entities and one for sparse entities.
 * We also have freelist mechanism where on top of a sparse set, which just stores numbers, is an array of entities.
 * With this we can even check if an entity is valid by looking at the index (entity value) and value at that index. If they match, its valid.
 *
 * Views
 * Now the next problem is that if we want to interate through multiple components in one loop, we need the entities' slots match within each component array.
 * This means that if a particular component is removed from component array A, all other components have to adjust t
 *
 * Sparse Sets
 * For each component there needs to be a unique sparse set implementation with that component.
 * This is perfect for template magic but that would sacrifice a lot of build time for nothing (thanks C++!) so we need to find some macro magic instead.
 * Or just compare if the template magic is negligable or not compared to macro.
 */

#define ECS_INVALID_ID entity((ecs_entity_type)-1)

using ecs_entity_type = u32;
enum class entity : ecs_entity_type {};

#define ecs_to_entity(__EntityType) static_cast<entity>(__EntityType) 
#define ecs_to_entity_type(__Entity) static_cast<ecs_entity_type>(__Entity)

#include "SparseSet.h"

#include <tuple>

// Forward declarations
struct entity_registry;
struct game;

template<typename T>
struct component_pool
{
    T* Data;
    ecs_entity_type Size;
    ecs_entity_type Capacity;

    sparse_set Set;

    void Create(ecs_entity_type Capacity)
    {
        Data = new T[Capacity];
        Set = sparse_set_create(Capacity, Capacity);
    }

    void Destroy()
    {
        sparse_set_destroy(&Set);
        delete[] Data;
    }
};

template<typename T>
struct ecs_view
{
    entity* DenseBegin;
    entity* DenseEnd;
    T* DenseComponentPool;

    auto begin() const { return DenseBegin; }
    auto end() const { return DenseEnd; }

    T& Get(entity Entity)
    {
        return DenseComponentPool[ecs_to_entity_type(Entity)];
    }
};

template<typename T0, typename T1>
struct double_view
{
    ecs_view<T0> View0;
    ecs_view<T1> View1;

    entity* DenseBegin;
    entity* DenseEnd;

    auto begin() { return DenseBegin; }
    auto end() { return DenseEnd; }

    auto Get(entity Entity)
    {
        auto& Component0 = View0.Get(Entity);
        auto& Component1 = View1.Get(Entity);

        return std::forward_as_tuple<T0&, T1&>(Component0, Component1);
    }
};

template<typename T0, typename T1, typename T2>
struct ecs_triple_view
{
    ecs_view<T0> View0;
    ecs_view<T1> View1;
    ecs_view<T2> View2;

    entity* DenseBegin;
    entity* DenseEnd;

    auto begin() { return DenseBegin; }
    auto end() { return DenseEnd; }

    auto Get(entity Entity)
    {
        auto& Component0 = View0.Get(Entity);
        auto& Component1 = View1.Get(Entity);
        auto& Component2 = View2.Get(Entity);

        return std::forward_as_tuple<T0&, T1&, T2&>(Component0, Component1, Component2);
    }
};

// Helper to get the index of a type in the tuple
template<typename T, typename Tuple>
struct type_index;

template<typename T, typename... Types>
struct type_index<T, std::tuple<T, Types...>>
{
    static constexpr std::size_t value = 0;
};

template<typename T, typename U, typename... Types>
struct type_index<T, std::tuple<U, Types...>>
{
    static constexpr std::size_t value = 1 + type_index<T, std::tuple<Types...>>::value;
};

#include "Components.h"

using components_pools = std::tuple<component_pool<transform_component>, component_pool<entity_render_component>, component_pool<aabb_physics_component>, component_pool<logic_component>, component_pool<point_light_component>>;

struct entity_registry
{
    entity* Entities = nullptr;
    u32 EntitiesCount = 0;
    u32 EntitiesCapacity = 0;

    components_pools ComponentPools;

    entity FreeList = ECS_INVALID_ID;
};

// ** Iterating over tuple elements **
template<typename TFunc, std::size_t... Index>
void ecs_for_each_helper(components_pools& Pools, TFunc&& Func, std::index_sequence<Index...>)
{
    (Func(std::get<Index>(Pools)), ...);  // Expands the function call over all tuple elements
}

// Wrapper function to apply a function to each pool
template<typename TFunc>
void for_each_pool(entity_registry* Registry, TFunc&& Func)
{
    ecs_for_each_helper(Registry->ComponentPools, std::forward<TFunc>(Func),
                  std::make_index_sequence<std::tuple_size<components_pools>::value>{});
}

// Get the pool for a specific type
template<typename T>
component_pool<T>& ecs_get_pool(entity_registry* Registry)
{
    constexpr std::size_t Index = type_index<component_pool<T>, components_pools>::value;
    return std::get<Index>(Registry->ComponentPools);
}

internal entity_registry ecs_entity_registry_create(ecs_entity_type Capacity)
{
    entity_registry Registry;
    Registry.Entities = new entity[Capacity];
    Registry.EntitiesCapacity = Capacity;
    memset(Registry.Entities, ecs_to_entity_type(ECS_INVALID_ID), Capacity * sizeof(ecs_entity_type));

    // Since we know all the components that we will use, we can just instantiate them here
    for_each_pool(&Registry, [Capacity](auto& Pool)
    {
        Pool.Create(Capacity);
    });

    return Registry;
}

internal void ecs_entity_registry_destroy(entity_registry* Registry)
{
    for_each_pool(Registry, [](auto& Pool)
    {
        Pool.Destroy();
    });

    delete[] Registry->Entities;

    // Reset
    *Registry = entity_registry();
}

internal entity ecs_create_entity(entity_registry* Registry)
{
    Assert(Registry->EntitiesCount < Registry->EntitiesCapacity, "Too many entities!");

    if (Registry->FreeList == ECS_INVALID_ID)
    {
        entity Entity = ecs_to_entity(Registry->EntitiesCount);
        Registry->Entities[ecs_to_entity_type(Entity)] = Entity;
        ++Registry->EntitiesCount;

        return Entity;
    }
    else
    {
        entity Entity = Registry->FreeList;
        Registry->FreeList = Registry->Entities[ecs_to_entity_type(Entity)];
        Registry->Entities[ecs_to_entity_type(Entity)] = Entity;
        ++Registry->EntitiesCount;
        return Entity;
    }
}

internal bool ecs_is_entity_valid(entity_registry* Registry, entity Entity)
{
    return ecs_to_entity_type(Entity) < Registry->EntitiesCount && Registry->Entities[ecs_to_entity_type(Entity)] == Entity;
}

internal void ecs_destroy_entity(entity_registry* Registry, entity Entity)
{
    Assert(ecs_is_entity_valid(Registry, Entity), "Entity is not valid!");

    for_each_pool(Registry, [Entity](auto& Pool)
    {
        if (sparse_set_contains(&Pool.Set, ecs_to_entity_type(Entity)))
        {
            rem Rem = sparse_set_remove(&Pool.Set, ecs_to_entity_type(Entity));

            // Update dense transforms based on the dense index array
            Pool.Data[Rem.DenseIndex] = Pool.Data[Rem.Last];
        }
    });

    if (Registry->FreeList == ECS_INVALID_ID)
    {
        // Set free list entity to an invalid value to signalize that the its the last in the chain
        Registry->Entities[ecs_to_entity_type(Entity)] = ECS_INVALID_ID;

        // Set free list to entity index
        Registry->FreeList = Entity;
    }
    else
    {
        Registry->Entities[ecs_to_entity_type(Entity)] = Registry->FreeList;
        Registry->FreeList = Entity;
    }

    --Registry->EntitiesCount;
}

template<typename T>
internal T& ecs_add_component(entity_registry* Registry, entity Entity)
{
    auto& Pool = ecs_get_pool<T>(Registry);

    Assert(!sparse_set_contains(&Pool.Set, ecs_to_entity_type(Entity)), "Entity already has this component!");

    auto Index = sparse_set_add(&Pool.Set, ecs_to_entity_type(Entity));
    
    return Pool.Data[Index];
}

template<typename T>
internal void ecs_remove_component(entity_registry* Registry, entity Entity)
{
    auto& Pool = ecs_get_pool<T>(Registry);

    Assert(sparse_set_contains(&Pool.Set, ecs_to_entity_type(Entity)), "Entity does not have this component!");

    sparse_set_remove(&Pool.Set, ecs_to_entity_type(Entity));
}

template<typename T>
internal T& ecs_get_component(entity_registry* Registry, entity Entity)
{
    auto& Pool = ecs_get_pool<T>(Registry);

    Assert(sparse_set_contains(&Pool.Set, ecs_to_entity_type(Entity)), "Entity does not have this component!");

    auto Index = sparse_set_get(&Pool.Set, ecs_to_entity_type(Entity));

    return Pool.Data[Index];
}

template<typename T>
internal ecs_view<T> ecs_view_components(entity_registry* Registry)
{
    auto& Pool = ecs_get_pool<T>(Registry);

    ecs_view<T> View;

    // Setup iterators
    View.DenseBegin = (entity*)Pool.Set.Dense;
    View.DenseEnd = (entity*)Pool.Set.Dense + Pool.Set.Size;

    // Also grab direct pointer to the pool
    View.DenseComponentPool = Pool.Data;

    return View;
}

template<typename T0, typename T1>
internal double_view<T0, T1> ecs_view_components(entity_registry* Registry)
{
    auto View0 = ecs_view_components<T0>(Registry);
    auto View1 = ecs_view_components<T1>(Registry);

    double_view<T0, T1> View;

    // Setup iterators
    auto View0Size = View0.end() - View0.begin();
    auto View1Size = View1.end() - View1.begin();
    if (View0Size < View1Size)
    {
        View.DenseBegin = View0.begin();
        View.DenseEnd = View0.end();
    }
    else
    {
        View.DenseBegin = View1.begin();
        View.DenseEnd = View1.end();
    }

    View.View0 = View0;
    View.View1 = View1;
    return View;
}

template<typename T0, typename T1, typename T2>
internal ecs_triple_view<T0, T1, T2> ecs_view_components(entity_registry* Registry)
{
    auto View0 = ecs_view_components<T0>(Registry);
    auto View1 = ecs_view_components<T1>(Registry);
    auto View2 = ecs_view_components<T2>(Registry);

    ecs_triple_view<T0, T1, T2> View;

    // Setup iterators
    entity* ViewBegins[] =
    {
        View0.begin(),
        View1.begin(),
        View2.begin(),
    };

    entity* ViewEnds[] =
    {
        View0.end(),
        View1.end(),
        View2.end(),
    };

    i64 Sizes[] = { View0.end() - View0.begin(), View1.end() - View1.begin(), View2.end() - View2.begin() };

    u32 Index = 0;
    i64 Candidate = 0;
    for (auto Size : Sizes)
    {
        if (Candidate < Size)
        {
            Candidate = Size;
            Index++;
        }
    }

    View.DenseBegin = ViewBegins[Index];
    View.DenseEnd = ViewEnds[Index];

    View.View0 = View0;
    View.View1 = View1;
    View.View2 = View2;
    return View;
}

internal void ECS_Test()
{
    entity_registry Registry = ecs_entity_registry_create(16);

    for (i32 i = 0; i < 10; i++)
    {
        entity Entity = ecs_create_entity(&Registry);

        auto& T = ecs_add_component<transform_component>(&Registry, Entity);
        T.Translation.x = (f32)i;

        auto& R = ecs_add_component<entity_render_component>(&Registry, Entity);
        R.Color = v4((f32)i, 1.0f, 1.0f, 1.0f);
    }

    for (i32 i = 0; i < 5; i++)
    {
        ecs_remove_component<transform_component>(&Registry, entity(i));
    }

    // View
    {
        auto View = ecs_view_components<transform_component>(&Registry);
        for (auto Entity : View)
        {
            auto& Transform = View.Get(Entity);

            TraceV3(Transform.Translation);
        }
    }

    {
        auto View = ecs_view_components<transform_component, entity_render_component>(&Registry);
        for (auto Entity : View)
        {
            auto [Transform, Renderable] = View.Get(Entity);

            Trace("Transform:");
            TraceV3(Transform.Translation);
            Trace("Color:");
            TraceV3(v3(Renderable.Color));
        }
    }

}
