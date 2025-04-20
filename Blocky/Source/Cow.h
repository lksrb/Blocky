#pragma once

#include <vector>

struct mesh_vertex
{
    v3 Position;
    v3 Normal;
    v2 TextureCoords;
};

struct submesh
{
    u32 BaseVertex = 0;
    u32 BaseIndex = 0;
    u32 VertexCount = 0;
    u32 IndexCount = 0;
    m4 Transform{ 1.0f };
};

struct mesh
{
    std::vector<mesh_vertex> Vertices;
    std::vector<u32> Indices;
    std::vector<submesh> Submeshes;
};

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

    Cow->CurrentAction = { action_type::None, 3.0f, true };

    // Initialize random series for random behaviour
    Cow->RandomSeries = RandomSeriesCreate();
}

internal void CowDestroy(entity_registry* Registry, entity Entity, logic_component* Logic)
{
    delete Logic->Storage; // Does not call destruction, I dont think we need it
}

internal void CowUpdate(game* Game, entity_registry* Registry, entity Entity, logic_component* Logic, f32 TimeStep)
{
    using namespace bkm;

    cow* Cow = static_cast<cow*>(Logic->Storage);
    auto& Transform = GetComponent<transform_component>(Registry, Entity);
    auto& AABBPhysics = GetComponent<aabb_physics_component>(Registry, Entity);

    // Actions
    if (1)
    {
        switch (Cow->CurrentAction.Type)
        {
            case action_type::None:
            {
                block_pos Pos = GetWorldToBlockPos(Transform.Translation);

                auto GroundBlock = BlockGetSafe(Game, Pos.C, Pos.R, Pos.L - 1);

                if (GroundBlock)
                {
                    GroundBlock->Color = v4(1.0f, 0.0f, 0.0f, 1.0f);
                }

                break;
            }
            case action_type::RotateRandomly:
            {
                // TODO: Should this really be encoded into vec2? We could use singular f32 to represent the angle...
                f32 TargetAngle = Atan2(Cow->Direction.y, Cow->Direction.x);
                f32 AngleDiff = DeltaAngle(Transform.Rotation.y, TargetAngle);
                f32 RotationStep = TimeStep * 5.0f;

                if (Abs(AngleDiff) < RotationStep)
                {
                    Transform.Rotation.y = TargetAngle;
                    Cow->CurrentAction.Finished = true;

                }
                else
                {
                    Transform.Rotation.y += Sign(TargetAngle - Transform.Rotation.y) * RotationStep;
                }
            }

            case action_type::LookAtPlayer:
            {
                v3 PlayerDirDiff = Game->Player.Position - Transform.Translation;

                Transform.Rotation.y = Atan2(PlayerDirDiff.x, PlayerDirDiff.z);

                break;
            }
        }

        // After an action is finished, start counting
        if (Cow->CurrentAction.Finished)
        {
            if (Cow->ActionTimer > Cow->CurrentAction.Duration)
            {
                Cow->ActionTimer = 0.0f;

                //Cow->CurrentAction = { action_type::RotateRandomly, 1.0f, false };
                Cow->CurrentAction = { action_type::LookAtPlayer, 1.0f, false };

                // Once moment
                switch (Cow->CurrentAction.Type)
                {
                    case action_type::RotateRandomly:
                    {
                        // Choose a random direcion to face body to
                        Cow->Direction = RandomNormal(&Cow->RandomSeries);

                        Cow->CurrentAction.Duration = 1.0f + RandomFloat01(&Cow->RandomSeries);

                        Trace("%.3f, %.3f", Cow->Direction.x, Cow->Direction.y);

                        //Cow->Direction = v2(0.0f, -1.0);
                        break;
                    }
                    case action_type::LookAtPlayer:
                    {
                        
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

    }

    // !!!
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

