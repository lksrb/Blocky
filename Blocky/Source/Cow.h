#pragma once

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
    WalkToNextLocation,
    RotateRandomly
};

struct action
{
    action_type Type;
    f32 Duration;
};

internal const char* GetAliveEntityStateString(alive_entity_state State)
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

struct state_timer
{
    f32 Current;
    f32 Time;
};

template<typename F>
internal void StateTimer(state_timer* Timer, f32 TimeStep, F&& Function)
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

// Forward declarations
struct cow;
internal void CowChangeState(cow* Cow, alive_entity_state NewState);

// Internal storage
struct cow
{
    f32 Speed = 1.0f;
    v2 Direction = v2(0.0f);

    alive_entity_state State = alive_entity_state::None;
    action CurrentAction = {};
    f32 ActionTimer = 0.0f;

    state_timer DecideDirectionTimer; // Go to a direction each 'Time' seconds.
    state_timer IdleTimer; // Neposeda

    random_series RandomSeries;
};

internal void CowCreate(entity_registry* Registry, entity Entity, logic_component* Logic)
{
    auto Cow = new cow; // TODO: Arena allocator
    Logic->Storage = Cow;

    // Set initial state
    CowChangeState(Cow, alive_entity_state::Idle);

    Cow->CurrentAction = { action_type::None, 1.0f };

    // Initialize random series for random behaviour
    Cow->RandomSeries = RandomSeriesCreate();
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

    // Actions
    if (1)
    {
        switch (Cow->CurrentAction.Type)
        {
            case action_type::None:
            {
                break;
            }
            case action_type::RotateRandomly:
            {
                f32 TargetAngle = bkm::Atan2(Cow->Direction.x, Cow->Direction.y);
                f32 AngleDiff = bkm::DeltaAngle(Transform.Rotation.y, TargetAngle);

                if (bkm::Equals(AngleDiff, TimeStep * 5.0f))
                {
                    Transform.Rotation.y = TargetAngle;
                }
                else
                {
                    Transform.Rotation.y += bkm::Sign(AngleDiff) * TimeStep * 5.0f;
                }

                //Transform.Rotation.y = bkm::Lerp(Transform.Rotation.y, bkm::Atan2(Cow->Direction.x, Cow->Direction.y), TimeStep * 5.0f);
            }
        }

        if (Cow->ActionTimer > Cow->CurrentAction.Duration)
        {
            Cow->ActionTimer = 0.0f;

            // Next random action and a random time
            Cow->CurrentAction = { action_type::RotateRandomly, 1.0f };

            switch (Cow->CurrentAction.Type)
            {
                case action_type::RotateRandomly:
                {
                    // Choose a random direcion to face body to
                    Cow->Direction = RandomDirection(&Cow->RandomSeries);
                    break;
                }
                default:
                    break;
            }


        }
        else
        {
            Cow->ActionTimer += TimeStep;
        }

    }

    return;

    // State update
    if (0)
    {
        if (Cow->State == alive_entity_state::Idle)
        {
            Cow->Direction = v2(0.0f);

            StateTimer(&Cow->IdleTimer, TimeStep, [&]()
            {
                CowChangeState(Cow, alive_entity_state::Walking);
            });
        }
        else if (Cow->State == alive_entity_state::Walking)
        {
            StateTimer(&Cow->DecideDirectionTimer, TimeStep, [&]()
            {
                Cow->Direction = RandomDirection(&Cow->RandomSeries);
            });

            Cow->Speed = 1.0f;
            // This way we can actually set velocity while respecting the physics simulation
            AABBPhysics.Velocity.x = Cow->Speed * Cow->Direction.x;
            AABBPhysics.Velocity.z = Cow->Speed * Cow->Direction.y;
        }
        else if (Cow->State == alive_entity_state::Running)
        {
            Cow->Speed = 5.0f;
            // This way we can actually set velocity while respecting the physics simulation... Not really
            // We need to take to account that Cow maybe hit by something so this does not respect it.
            // Instead we need to change cow state to InAir so it does respect it
            AABBPhysics.Velocity.x = Cow->Speed * Cow->Direction.x;
            AABBPhysics.Velocity.z = Cow->Speed * Cow->Direction.y;
        }
        else if (Cow->State == alive_entity_state::InAir)
        {
            // Dont modify anything
        }
    }

    // Alter "animation" of the legs, faster cow gets, faster the legs animation
    if (bkm::NonZero(v2(AABBPhysics.Velocity.x, AABBPhysics.Velocity.z)))
    {
        Transform.Rotation.y = bkm::Lerp(Transform.Rotation.y, bkm::Atan2(AABBPhysics.Velocity.x, AABBPhysics.Velocity.z), TimeStep * 5.0f);
    }
}

internal void CowChangeState(cow* Cow, alive_entity_state NewState)
{
    Assert(NewState != alive_entity_state::None, "Cannot change state to \"None\"!");
    if (Cow->State == NewState)
        return;

    alive_entity_state OldState = Cow->State;
    Cow->State = NewState;

    Trace("Cow has changed its state! From '%s' to '%s'.", GetAliveEntityStateString(OldState), GetAliveEntityStateString(NewState));

    // Something that will be done only once
    if (NewState == alive_entity_state::Idle)
    {
        Cow->IdleTimer.Current = 0;
        Cow->IdleTimer.Time = 1.0f;
    }
    else if (NewState == alive_entity_state::Walking)
    {
        Cow->DecideDirectionTimer.Current = 0;
        Cow->DecideDirectionTimer.Time = 1.0f;
    }
}

