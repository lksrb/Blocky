#pragma once

struct game;

enum class alive_entity_state : u32
{
    None = 0,
    Idle,
    Walking,
    Running,
    InAir
};

enum class action_type : u32
{
    None = 0,
    LookAtPlayer,
    WalkToRandomLocation,
    RotateRandomly,
};

struct action
{
    action_type Type;
    f32 Duration;
    bool Finished = false; // Upon finishing, start the timer
};

struct state_timer
{
    f32 Current;
    f32 Time;
};

template<typename F>
internal void state_timer_update(state_timer* Timer, f32 TimeStep, F&& Function)
{
    // Simple timer
    if (Timer->Current > Timer->Time)
    {
        Timer->Current = 0.0f;

        Function();
    }
    else
    {
        Timer->Current += TimeStep;
    }
}

internal const char* get_alive_entity_state_string(alive_entity_state State)
{
    switch (State)
    {
        case alive_entity_state::None:    return "None";
        case alive_entity_state::Idle:    return "Idle";
        case alive_entity_state::Walking: return "Walking";
        case alive_entity_state::Running: return "Running";
        case alive_entity_state::InAir:   return "InAir";
    }

    Assert(false, "Unknown state!");
    return "UNKNOWN";
}

// Internal storage
struct cow_object
{
    f32 Speed = 1.0f;
    v2 Direction = v2(0.0f);

    alive_entity_state State = alive_entity_state::None;
    action CurrentAction = {};
    f32 ActionTimer = 0.0f;

    state_timer DecideDirectionTimer; // Go to a direction each 'Time' seconds.
    state_timer IdleTimer; // Neposeda

    random_series RandomSeries;

    transform_component Transform;
    aabb_physics_component AABBPhysics;
    entity_render_component Render;
};

internal void cow_create(cow_object* Cow);
internal void cow_destroy(cow_object* Cow);
internal void cow_update(cow_object* Cow, game* Game, f32 TimeStep);
internal void cow_change_state(cow_object* Cow, alive_entity_state NewState);
