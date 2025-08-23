#pragma once

// Components
struct transform_component
{
    v3 Translation = v3(0.0f);
    v3 Rotation = v3(0.0f);
    v3 Scale = v3(1.0f);

    inline m4 Matrix()
    {
        return bkm::Translate(m4(1.0f), Translation)
            * bkm::ToM4(qtn(Rotation))
            * bkm::Scale(m4(1.0f), Scale);
    }
};

struct aabb_physics_component
{
    v3 Velocity = v3(0.0f);
    v3 BoxSize; // Not exactly an AABB, just scale since the position of the aabb may vary
    bool Grounded = false;
};

struct point_light_component
{
    f32 Radius = 1.0f;
    f32 FallOff = 1.0f;
    v3 Radiance = v3(1.0f);
    f32 Intensity = 1.0f;
};

struct logic_component
{
    // TODO: Do we need pointer to the game structure in the create/destroy function?
    using create_function = void(*)(entity_registry* Registry, entity Entity, logic_component* Logic);
    using destroy_function = void(*)(entity_registry* Registry, entity Entity, logic_component* Logic);
    using update_function = void(*)(game* Game, entity_registry* Registry, entity Entity, logic_component* Logic, f32 TimeStep);

    // These cannot be null
    create_function CreateFunction; // Used for setup of an entity
    destroy_function DestroyFunction; // Called upon destruction of an entity
    update_function UpdateFunction; // Called each frame 

    void* Storage;
};

struct entity_part
{
    texture_block_coords Coords;
    v3 LocalPosition;
    v3 Size;
};

struct entity_model
{
    entity_part Parts[6]; // For now
    i32 PartsCount = 0;
};

internal entity_model EntityModelCreate()
{
    entity_model Model;
    return Model;
}

struct entity_render_component
{
    v4 Color = v4(1.0f);
    texture Texture;
    entity_model Model;
};
