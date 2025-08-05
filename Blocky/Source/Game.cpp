#include "Game.h"

#include "GameRenderer.cpp"

#include "Cow.h"

struct texture_rect
{
    u8 X, Y, Width, Height, RotationCount; // TODO: Can we can rid of rotation count?

    texture_rect() = default;

    texture_rect(u8 x, u8 y, u8 w, u8 h, u8 r)
        : X(x), Y(y), Width(w), Height(h), RotationCount(r)
    {}
};

// IDEA: We can keep the pixel coords on the texture and calculate them in runtime
struct texture_rects
{
    texture_rects() = default;

    texture_rects(texture_rect front, texture_rect back,
                  texture_rect left, texture_rect right,
                  texture_rect top, texture_rect bottom)
        : Front(front), Back(back),
        Left(left), Right(right),
        Top(top), Bottom(bottom)
    {}

    union
    {
        struct
        {
            texture_rect Front;
            texture_rect Back;
            texture_rect Left;
            texture_rect Right;
            texture_rect Top;
            texture_rect Bottom;
        };

        texture_rect Data[6];
    };
};

internal void AddModelPart(entity_model* Model, v3 LocalPosition, v3 Size, texture_rects Rects)
{
    auto& Part = Model->Parts[Model->PartsCount++];
    texture_block_coords TextureCoords;

    i32 i = 0;
    for (auto& Rect : Rects.Data)
    {
        TextureCoords.TextureCoords[i++] = get_texture_coords(Rect.X, Rect.Y, Rect.Width, Rect.Height, Rect.RotationCount);
    }

    Part.Coords = TextureCoords;
    Part.LocalPosition = LocalPosition * c_TexelSize;
    Part.Size = Size * c_TexelSize;
}

internal game* game_create(arena* Arena, render_backend* Backend)
{
    game* Game = arena_new(Arena, game);

    game_generate_world(Arena, Game);

    // Crosshair texture will be separate from block textures
    Game->CrosshairTexture = texture_create(Backend, "Resources/Textures/MC/Crosshair.png");
    Game->BlockTextures[(u32)block_type::Dirt] = texture_create(Backend, "Resources/Textures/MC/dirt_2.png");
    Game->CowTexture = texture_create(Backend, "Resources/Textures/MC/cow.png");
    Game->PointLightIconTexture = texture_create(Backend, "Resources/Textures/PointLight.png");

    //Game->SunTexture = TextureCreate(Renderer->Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, "Resources/Textures/MC/environemnt/sun.png");

    //Game->MoonTexture = TextureCreate(Renderer->Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, "Resources/Textures/MC/environemnt/moon.png");

    Game->Registry = EntityRegistryCreate(100);

    entity_model CowModel = EntityModelCreate();

    // Body
    {
        texture_rects Rects = {
            { 12, 10, 28, 13, 0 },
            { 12, 10, 40, 13, 0 },
            { 10, 18, 18, 31, 1 },
            { 10, 18, 40, 31, 3 },
            { 12, 18, 50, 31, 2 },
            { 12, 18, 28, 31, 0 }
        };

        AddModelPart(&CowModel, v3(0.0f), v3(12.0f, 10.0f, 18.0f), Rects);
    }

    // Head
    {
        texture_rects Rects = {
            { 8, 8,  6, 13, 0 },
            { 8, 8, 20, 13, 0 },
            { 6, 8,  0, 13, 0 },
            { 6, 8, 14, 13, 0 },
            { 8, 6,  6,  5, 0 },
            { 8, 6, 14,  5, 0 }
        };
        AddModelPart(&CowModel, v3(0, (10 + 8) / 4 - 1, (18 + 6) / 2), v3(8, 8, 6), Rects);
    }

    // Legs
    {
        texture_rects Rects = {
            { 4, 12,  4, 31, 0 },
            { 4, 12, 12, 31, 0 },
            { 4, 12,  0, 31, 0 },
            { 4, 12,  8, 31, 0 },
            { 4,  4,  4, 19, 0 },
            { 4,  4,  8, 19, 2 }
        };

        v2 Offset;
        Offset.x = 4;
        Offset.y = 7;
        f32 y = -11;
        AddModelPart(&CowModel, v3(Offset.x, y, -Offset.y), v3(4, 12, 4), Rects);
        AddModelPart(&CowModel, v3(-Offset.x, y, Offset.y), v3(4, 12, 4), Rects);
        AddModelPart(&CowModel, v3(Offset.x, y, Offset.y), v3(4, 12, 4), Rects);
        AddModelPart(&CowModel, v3(-Offset.x, y, -Offset.y), v3(4, 12, 4), Rects);
    }

    // Create another set of cows
    for (i32 i = 10; i < 11; i++)
    {
        auto CowEntity = CreateEntity(&Game->Registry);

        auto& Transform = AddComponent<transform_component>(&Game->Registry, CowEntity);
        Transform.Translation = v3((f32)c_TexelSize / 2 + 10.0f, 17.0, c_TexelSize / 2 + 10.0f);
        Transform.Scale = v3(1.0f);
        Transform.Rotation = v3(0.0f, 0.0f, 0.0f);

        auto& Render = AddComponent<entity_render_component>(&Game->Registry, CowEntity);
        Render.Texture = Game->CowTexture;
        Render.Model = CowModel;

        auto& AABB = AddComponent<aabb_physics_component>(&Game->Registry, CowEntity);
        AABB.BoxSize = v3(0.8f, 1.8, 0.8f);
        AABB.Velocity = v3(0.0f);
        AABB.Grounded = false;

        auto& Logic = AddComponent<logic_component>(&Game->Registry, CowEntity);
        Logic.CreateFunction = cow_create;
        Logic.DestroyFunction = cow_destroy;
        Logic.UpdateFunction = cow_update;
    }

    // On Create event
    auto View = ViewComponents<logic_component>(&Game->Registry);
    for (auto Entity : View)
    {
        auto& Logic = View.Get(Entity);
        Logic.CreateFunction(&Game->Registry, Entity, &Logic);
    }

    return Game;
}

internal void game_generate_world(arena* Arena, game* Game)
{
    Game->BlocksCount = RowCount * ColumnCount * LayerCount;
    Game->Blocks = arena_new_array(Arena, block, Game->BlocksCount);

    const v3 StartPos = { 0, 0, 0 };

    i32 CikCak = 0;
    i32 GlobalIndex = 0;

    v3 CurrentPosition = StartPos;

    for (i32 L = 0; L < LayerCount; L++)
    {
        CurrentPosition.z = StartPos.z;

        for (i32 R = 0; R < RowCount; R++)
        {
            CurrentPosition.x = StartPos.x;

            for (i32 C = 0; C < ColumnCount; C++)
            {
                // Establish a "Block"
                auto& Block = Game->Blocks[(L * RowCount * ColumnCount) + (R * RowCount) + C];
                //Block.Color = (CikCak & 1) ? v4(0.2f, 0.2f, 0.2f, 1.0f) : v4(1.0f);
                Block.Color = v4(1.0f);
                if (L < 1)
                {
                    Block.Position = v3(CurrentPosition.x, CurrentPosition.y, CurrentPosition.z);
                    Block.Type = block_type::Dirt;
                    Block.Color = v4(1.0f);
                }
                else
                {
                    Block.Position = v3(CurrentPosition.x, CurrentPosition.y, CurrentPosition.z);
                    Block.Type = block_type::Air;
                }

                CurrentPosition.x++;

                CikCak++;
#if 0
                // Left
                if (Block.Left == INT_MAX && C > 0 && C < ColumnCount)
                {
                    Block.Left = GlobalIndex - 1;
                }

                // Right
                if (Block.Right == INT_MAX && C < ColumnCount - 1)
                {
                    Block.Right = GlobalIndex + 1;
                }

                // Front
                if (Block.Front == INT_MAX && GlobalIndex - (RowCount + L * RowCount * ColumnCount) >= 0)
                {
                    Block.Front = GlobalIndex - RowCount;
                }

                // Back
                if (Block.Back == INT_MAX && GlobalIndex + RowCount < RowCount * ColumnCount * (L + 1))
                {
                    Block.Back = GlobalIndex + RowCount;
                }

                // Up
                if (GlobalIndex + ColumnCount * RowCount < RowCount * ColumnCount * LayerCount)
                {
                    Block.Up = GlobalIndex + ColumnCount * RowCount;
                }

                // Down
                if (GlobalIndex - ColumnCount * RowCount >= 0)
                {
                    Block.Down = GlobalIndex - ColumnCount * RowCount;
                }
#endif

                GlobalIndex++;
            }
            CikCak++;

            CurrentPosition.z++;
        }

        CikCak++;

        CurrentPosition.y++;
    }

    /*
    // Place a block
    i32 X = 0, Y = 3, Z = 0;
    if (auto Block = BlockGetSafe(Game, X, Y, Z))
    {
        Block->Type = block_type::Dirt;
        Block->Position = { (f32)X, (f32)Y, (f32)Z };
        Block->Color = v4(1.0f);
    }

    */
    return;
    // Disable Sin-like world for now
    random_series Series = random_series_create();

    for (i32 L = 0; L < LayerCount; L++)
    {
        for (i32 R = 0; R < RowCount; R++)
        {
            for (i32 C = 0; C < ColumnCount; C++)
            {
                i32 Index = (L * RowCount * ColumnCount) + (R * RowCount) + C;
                auto& Block = Game->Blocks[Index];

                if (!Block.placed())
                {
                    f32 A = 5.0f;
                    f32 F = 0.5f;
                    f32 Threshold = 2.0f; // max distance to sine curve for intersection

                    // Compute block center position in world space
                    v3 BlockPos((f32)C, (f32)L, (f32)R);

                    // Use block's x as parameter t along sine curve
                    f32 T0 = BlockPos.x;
                    f32 T1 = BlockPos.z;

                    // Calculate sine curve center at t
                    v3 CursePosition;
                    CursePosition.x = T0;
                    CursePosition.y = A * (bkm::Sin(F * T0) + bkm::Sin(F * T1));
                    CursePosition.z = BlockPos.z; // or fixed z if needed

                    // Calculate distance from block to sine curve point
                    f32 Distance = bkm::Length(BlockPos - CursePosition);

                    // If distance less than threshold, block intersects sine wave region
                    if (Distance < Threshold)
                    {
                        Block.Type = block_type::Dirt;
                        // This empty block intersects the sine curve area
                        // Do whatever you want here (e.g. mark it, fill it, etc)
                    }
                }
            }
        }
    }
}

internal void game_update(game* Game, game_renderer* Renderer, const game_input* Input, f32 TimeStep, u32 ClientAreaWidth, u32 ClientAreaHeight)
{
    //debug_cycle_counter GameUpdateCounter("GameUpdateCounter");

    // Player update
    // NOTE: I dont think we need everything to be an entity, just most dynamic stuff
    // Player and blocks are perfect examples of where we possibly dont need that flexibility
    game_player_update(Game, Input, Renderer, TimeStep);

    // Entity update
    // Physics simulation gave us some state of the entity and now we can use it to react to in the update loop
    if (1)
    {
        game_update_entities(Game, TimeStep);
    }

    // Physics simulation
    if (1)
    {
        game_physics_simulation_update_entities(Game, TimeStep);
    }

    // Count time
    Game->TimeSinceStart += TimeStep;

    // Render
    if (1)
    {
        game_render_entities(Game, Renderer, TimeStep);
    }

    // Render blocks
    if (1)
    {
        //debug_cycle_counter GameUpdateCounter("Render Blocks");

        for (u32 i = 0; i < Game->BlocksCount; i++)
        {
            auto& Block = Game->Blocks[i];

            if (!Block.placed())
                continue;

            game_renderer_submit_cuboid(Renderer, Block.Position, &Game->BlockTextures[(u32)Block.Type], Block.Color);
        }
    }

    // Lights
    v3 TestLightPos = v3(10, 5, 10);
    //GameRendererSubmitPointLight(Renderer, TestLightPos, 10.0, 1.0f, v3(1.0f), 2.0f);

    //GameRendererSubmitDirectionalLight(Renderer, )

    f32 GameTime = Game->TimeSinceStart * 0.1f;
    //Trace("%.3f", GameTime);

    // Render sun
    v3 SunDirection;
    v3 SunPosition;
    {
        GameTime = bkm::PI_HALF;
        f32 Distance = 10.0f;
        f32 Angle = bkm::PI_HALF - GameTime; // speed of day-night cycle (radians per second)
        // Rotate baseDir around X axis to simulate sun path
        f32 CosA = bkm::Cos(Angle);
        f32 SinA = bkm::Sin(Angle);
        v3 BaseDir = v3(0.0f, 1.0f, 0.0f);
        SunDirection = v3(
            BaseDir.x,
            BaseDir.y * CosA - BaseDir.z * SinA,
            BaseDir.y * SinA + BaseDir.z * CosA
        );

        SunDirection = bkm::Normalize(SunDirection);
        SunPosition = SunDirection * Distance;
        v3 SunRotation(bkm::PI_HALF + Angle, 0, 0);
        game_renderer_submit_distant_quad(Renderer, SunPosition, SunRotation, nullptr, v4(1));

        // TODO: On sunset and sunrise, tweak directional light color to "reflect" the sky color
        local_persist v3 FinalColor = v3(0.8f, 0.5f, 0.3);
        if (GameTime > bkm::PI_HALF)
        {
            FinalColor = bkm::Lerp(FinalColor, v3(0.8f, 0.5f, 0.3), TimeStep * 0.3f);
        }
        else
        {
            FinalColor = bkm::Lerp(FinalColor, v3(1.0f), TimeStep * 0.3f);
        }

        //TraceV3(FinalColor);

        //GameRendererSubmitDirectionalLight(Renderer, -SunDirection, 1.5f, v3(1.0f));
    }

    // Update camera and HUD
    {
        m4 LightSpaceMatrix;
        v3 LightDirection = bkm::Normalize(v3(0, -1.0f, 0));
        v3 LightPosition(2, -10, 0);

        {
            m4 LightProjection = bkm::OrthoLH(-25.0f, 25.0f, -25.0f, 25.0f, 0.1f, 40.0f);
            m4 LightView = bkm::LookAtLH(LightPosition, v3(ColumnCount / 2, 0, RowCount / 2), v3(0.0f, 1.0f, 0.0f));
            //m4 LightView = bkm::LookAt(v3(-SunDirection), v3(0, 0, 0), v3(0.0f, 1.0f, 0.0f));

            LightSpaceMatrix = LightProjection * m4(m3(LightView)); // Remove translation
        }

        v3 CameraPosition = Game->Player.Position + Game->CameraOffset;

        m4 InverseView = bkm::Translate(m4(1.0f), CameraPosition) * bkm::ToM4(qtn(Game->Player.Rotation));
        Game->Camera.View = bkm::Inverse(InverseView);
        Game->Camera.recalculate_projection_persperctive(ClientAreaWidth, ClientAreaHeight);

        m4 HUDProjection = bkm::OrthoLH(0, (f32)ClientAreaWidth, (f32)ClientAreaHeight, 0, -1, 1);
        game_renderer_set_render_data(Renderer, CameraPosition, Game->Camera.View, Game->Camera.Projection, InverseView, HUDProjection, Game->TimeSinceStart, LightSpaceMatrix);

        game_renderer_submit_directional_light(Renderer, LightDirection, 1.5f, v3(1.0f));
    }

    // Render Debug UI
    if (Game->RenderDebugUI)
    {
        // For each point lights so we know that its there
        //game_renderer_submit_billboard_quad(Renderer, TestLightPos, v2(0.5), &Game->PointLightIconTexture, v4(1.0f));
    }

    // Render HUD
    if (Game->RenderHUD)
    {
        m4 Projection = bkm::Ortho(0, (f32)ClientAreaWidth, (f32)ClientAreaHeight, 0, -1.0f, 1.0f);

        // Build crosshair vertices
        f32 CrosshairSize = 15.0f * 2.0f;
        f32 CenterX = ClientAreaWidth / 2.0f - CrosshairSize / 2.0f;
        f32 CenterY = ClientAreaHeight / 2.0f - CrosshairSize / 2.0f;

        v3 Positions[4];
        Positions[3] = { CenterX, CenterY, 0.0f };
        Positions[2] = { CenterX + CrosshairSize, CenterY, 0.0f };
        Positions[1] = { CenterX + CrosshairSize, CenterY + CrosshairSize, 0.0f };
        Positions[0] = { CenterX, CenterY + CrosshairSize, 0.0f };

        game_renderer_submit_hud_quad(Renderer, Positions, &Game->CrosshairTexture, texture_coords(), v4(1.0f, 1.0f, 1.0f, 0.7f));
    }
}

internal void game_player_update(game* Game, const game_input* Input, game_renderer* Renderer, f32 TimeStep)
{
    auto& Player = Game->Player;

    v3 Up = { 0.0f, 1.0, 0.0f };
    v3 Right = { 1.0f, 0.0, 0.0f };
    v3 Forward = { 0.0f, 0.0, -1.0f };
    f32 Speed = 4.0f;

    // Rotating
    local_persist bool TPressed = false;

    bool JustPressed = false;
    bool JumpKeyPressed = false;

    if (Input->is_key_pressed(key::T))
    {
        TPressed = !TPressed;

        JustPressed = true;
    }

    if (TPressed)
    {
        local_persist v2i OldMousePos;

        v2i MousePos = Input->get_raw_mouse_input();

        // Avoids teleporting with camera rotation due to large delta
        if (JustPressed)
            OldMousePos = MousePos;

        v2i MouseDelta = MousePos - OldMousePos;

        //Trace("%i %i", MouseDelta.x, MouseDelta.y);
        OldMousePos = MousePos;

        f32 MouseSensitivity = 0.35f;

        // Update rotation based on mouse input
        Player.Rotation.y -= (f32)MouseDelta.x * MouseSensitivity * TimeStep; // Yaw
        Player.Rotation.x -= (f32)MouseDelta.y * MouseSensitivity * TimeStep; // Pitch
    }

    // Clamp pitch to avoid gimbal lock
    if (Player.Rotation.x > bkm::Radians(89.0f))
        Player.Rotation.x = bkm::Radians(89.0f);
    if (Player.Rotation.x < bkm::Radians(-89.0f))
        Player.Rotation.x = bkm::Radians(-89.0f);

    // Calculate the forward and right direction vectors
    Up = qtn(v3(Player.Rotation.x, Player.Rotation.y, 0.0f)) * v3(0.0f, 1.0f, 0.0f);
    Right = qtn(v3(Player.Rotation.x, Player.Rotation.y, 0.0f)) * v3(1.0f, 0.0f, 0.0f);
    Forward = qtn(v3(Player.Rotation.x, Player.Rotation.y, 0.0f)) * v3(0.0f, 0.0f, -1.0f);

    //Trace("%d %d", Input->MouseDelta.x, Input->MouseDelta.y);

    // Movement
    v3 Direction = {};
    if (TPressed)
    {
        if (Input->is_key_down(key::W))
        {
            Direction += v3(Forward.x, 0.0f, Forward.z);
        }

        if (Input->is_key_down(key::S))
        {
            Direction -= v3(Forward.x, 0.0f, Forward.z);
        }

        if (Input->is_key_down(key::A))
        {
            Direction -= Right;
        }

        if (Input->is_key_down(key::D))
        {
            Direction += Right;
        }

        if (Input->is_key_pressed(key::BackSpace) || Input->is_key_down(key::Space))
        {
            JumpKeyPressed = true;
        }

        if (Input->is_key_pressed(key::G))
        {
            Player.IsPhysicsObject = !Player.IsPhysicsObject;
        }

        if (!Player.IsPhysicsObject)
        {
            if (Input->is_key_down(key::Q) || Input->is_key_down(key::BackSpace) || Input->is_key_down(key::Space))
            {
                Direction += v3(0.0f, 1.0f, 0.0f);
            }
            else if (Input->is_key_down(key::E) || Input->is_key_down(key::Control) || Input->is_key_down(key::Shift))
            {
                Direction -= v3(0.0f, 1.0f, 0.0f);
            }
        }

        if (bkm::Length(Direction) > 0.0f)
            Direction = bkm::Normalize(Direction);
    }

    if (Player.IsPhysicsObject)
    {
        const f32 G = -9.81f * 2;
        const f32 CheckRadius = 5.0f;
        const f32 JumpStrength = 10.0f;

        // Apply gravity and movement forces
        v3 NextVelocity = Player.Velocity;
        NextVelocity.y += G * TimeStep;  // Gravity
        NextVelocity.x = Direction.x * Speed;
        NextVelocity.z = Direction.z * Speed;

        v3 NextPos = Player.Position;

        if (Player.Grounded && JumpKeyPressed)
        {
            NextVelocity.y = JumpStrength;
            Player.Grounded = false;
        }

        NextPos.y += NextVelocity.y * TimeStep;

        aabb PlayerAABB = AABBFromV3(NextPos, v3(0.5f, 1.8f, 0.5f));

        // Stepping on a block
        auto Pos = get_world_to_block_position(NextPos);

        for (i32 l = -1; l <= 1; l += 2)
        {
            for (i32 c = -1; c <= 1; c++)
            {
                for (i32 r = -1; r <= 1; r++)
                {
                    auto Block = block_get_safe(Game, Pos.C + c, Pos.R + r, Pos.L + l);

                    if (Block && Block->placed())
                    {
                        aabb BlockAABB = AABBFromV3(Block->Position, v3(1.0f));
                        if (AABBCheckCollision(PlayerAABB, BlockAABB))
                        {
                            // Hit something above? Stop jumping.
                            if (NextVelocity.y > 0)
                            {
                                NextVelocity.y = 0;
                            }
                            // Hit the ground? Reset velocity and allow jumping again.
                            else if (NextVelocity.y < 0)
                            {
                                Player.Grounded = true;
                                NextVelocity.y = 0;
                            }
                            NextPos.y = Player.Position.y; // Revert movement
                        }
                    }
                }
            }
        }

        // XXX
        NextPos.x += NextVelocity.x * TimeStep;

        PlayerAABB = AABBFromV3(NextPos, v3(0.5f, 1.8f, 0.5f));

        Pos = get_world_to_block_position(NextPos);

        for (i32 c = -1; c <= 1; c += 2)
        {
            for (i32 r = -1; r <= 1; r++)
            {
                for (i32 l = -1; l <= 1; l++)
                {
                    auto Block = block_get_safe(Game, Pos.C + c, Pos.R + r, Pos.L + l);

                    if (Block && Block->placed())
                    {
                        aabb BlockAABB = AABBFromV3(Block->Position, v3(1.0f));
                        if (AABBCheckCollision(PlayerAABB, BlockAABB))
                        {
                            Block->Color = v4(0, 0, 1, 1);
                            NextPos.x = Player.Position.x;
                            NextVelocity.x = 0.0f;
                            goto ExitLoopX;
                        }
                    }
                }
            }
        }
    ExitLoopX:

        // ZZZ
        NextPos.z += NextVelocity.z * TimeStep;

        PlayerAABB = AABBFromV3(NextPos, v3(0.5f, 1.8f, 0.5f));

        Pos = get_world_to_block_position(NextPos);

        for (i32 r = -1; r <= 1; r += 2)
        {
            for (i32 c = -1; c <= 1; c++)
            {
                for (i32 l = -1; l <= 1; l++)
                {
                    auto Block = block_get_safe(Game, Pos.C + c, Pos.R + r, Pos.L + l);

                    if (Block && Block->placed())
                    {
                        aabb BlockAABB = AABBFromV3(Block->Position, v3(1.0f));
                        if (AABBCheckCollision(PlayerAABB, BlockAABB))
                        {
                            Block->Color = v4(0, 0, 1, 1);
                            NextPos.z = Player.Position.z;
                            NextVelocity.z = 0.0f;
                            goto ExitLoopZ;
                        }
                    }
                }
            }
        }

    ExitLoopZ:
        Player.Position = NextPos;
        Player.Velocity = NextVelocity;
    }
    else // No physics
    {
        Player.Position += Direction * Speed * TimeStep;

        // Update coords
        auto Pos = get_world_to_block_position(Player.Position);
#if 0
        for (i32 i = -1; i <= 1; i++)
        {
            for (i32 j = -1; j <= 1; j++)
            {
                auto Block = BlockGetSafe(Game, Pos.C + i, Pos.R + 1, Pos.L + j);

                if (Block)
                {
                    GameRendererSubmitCuboidNoRotScale(Renderer, Block->Position, Game->BlockTextures[u32(block_type::Dirt)], v4(1.0f, 0.0f, 0.0f, 1.0f));
                }
            }
        }
#endif
    }

    // Destroy block
    if (Input->is_mouse_pressed(mouse::Left))
    {
        ray Ray;
        Ray.Origin = Player.Position + Game->CameraOffset; // TODO: Camera position
        Ray.Direction = Forward; // Forward is already normalized

        v3 HitPoint;
        v3 HitNormal;
        block HitBlock;
        u64 HitIndex;
        if (find_first_hit(Ray, Game->Blocks, Game->BlocksCount, &HitPoint, &HitNormal, &HitBlock, &HitIndex))
        {
            auto& Block = Game->Blocks[HitIndex];
            Block.Type = block_type::Air;

            // Color adjacent blocks
#if 0
            if (Block.Left != INT_MAX)
            {
                auto& Neighbour = Game->Blocks[Block.Left];
                Neighbour.Color = v4(1.0f, 0.0f, 0.0f, 1.0f);
                Neighbour.Right = INT_MAX;
            }

            if (Block.Right != INT_MAX)
            {
                auto& Neighbour = Game->Blocks[Block.Right];
                Neighbour.Color = v4(1.0f, 0.0f, 0.0f, 1.0f);
                Neighbour.Left = INT_MAX;
            }

            if (Block.Front != INT_MAX)
            {
                auto& Neighbour = Game->Blocks[Block.Front];
                Neighbour.Color = v4(1.0f, 0.0f, 0.0f, 1.0f);
                Neighbour.Back = INT_MAX;
            }

            if (Block.Back != INT_MAX)
            {
                auto& Neighbour = Game->Blocks[Block.Back];
                Neighbour.Color = v4(1.0f, 0.0f, 0.0f, 1.0f);
                Neighbour.Front = INT_MAX;
            }

            if (Block.Up != INT_MAX)
            {
                auto& Neighbour = Game->Blocks[Block.Up];
                Neighbour.Color = v4(1.0f, 0.0f, 0.0f, 1.0f);
                Neighbour.Down = INT_MAX;
            }

            if (Block.Down != INT_MAX)
            {
                auto& Neighbour = Game->Blocks[Block.Down];
                Neighbour.Color = v4(1.0f, 0.0f, 0.0f, 1.0f);
                Neighbour.Up = INT_MAX;
            }
#endif

            Info("Block destroyed.");
        }
    }

    // Create blocks
    if (Input->is_mouse_pressed(mouse::Right))
    {
        v2i PlacePos = { 0,0 };

        ray Ray;
        Ray.Origin = Player.Position + Game->CameraOffset; // TODO: Camera position
        Ray.Direction = Forward; // Forward is already normalized

        v3 HitPoint;
        v3 HitNormal;
        block HitBlock;
        u64 HitIndex;

        if (find_first_hit(Ray, Game->Blocks, Game->BlocksCount, &HitPoint, &HitNormal, &HitBlock, &HitIndex))
        {
            v3 NewBlockPos = HitBlock.Position + HitNormal;

            aabb BlockAABB = AABBFromV3(NewBlockPos, v3(1.0f));
            aabb PlayerAABB = AABBFromV3(Player.Position, v3(0.5f, 1.8f, 0.5f));
            if (!AABBCheckCollision(PlayerAABB, BlockAABB))
            {
                i32 C = (i32)NewBlockPos.x;
                i32 R = (i32)NewBlockPos.z;
                i32 L = (i32)NewBlockPos.y;
                auto Block = block_get_safe(Game, C, R, L);

                if (Block)
                {
                    Block->Color = v4(1, 0, 1, 1);
                    Block->Type = block_type::Dirt;
                }
            }
        }
    }

    // Stepping on a block
    // Is kinda wonky
    i32 C = (i32)bkm::Floor(Player.Position.x + 0.5f);
    i32 R = (i32)bkm::Floor(Player.Position.z + 0.5f);
    i32 L = (i32)bkm::Floor(Player.Position.y + 0.5f);

    //if (SteppedOnBlock)
    //{
    //    SteppedOnBlock->Color = v4(0, 0, 1, 1);
    //}

    //auto InFrontBlock = BlockGetSafe(Game, C + 1, R, L);
    //if (InFrontBlock)
    //{
    //    InFrontBlock->Color = v4(0, 1, 1, 1);
    //}
}

internal void game_update_entities(game* Game, f32 TimeStep)
{
    // TODO: Of course fetching function pointer is a lot slower than calling an actual function on a struct
    // The difference is between 5-6x when I spawn 10000 entities with optimalizations, thats quite huge and is worth considering.
    // However I dont think its necessary right now when the COW just goes and jumps off a cliff. There are more important things.
    //debug_cycle_counter Counter("GameUpdateEntities");

    auto View = ViewComponents<logic_component>(&Game->Registry);

    for (auto Entity : View)
    {
        auto& Logic = View.Get(Entity);
        Logic.UpdateFunction(Game, &Game->Registry, Entity, &Logic, TimeStep);
    }
}

internal void game_physics_simulation_update_entities(game* Game, f32 TimeStep)
{
    auto View = ViewComponents<transform_component, aabb_physics_component>(&Game->Registry);

    for (auto Entity : View)
    {
        auto [Transform, AABBPhysics] = View.Get(Entity);

        const f32 G = -9.81f * 2;
        const f32 CheckRadius = 2.0f;

        // Apply gravity and movement forces
        v3 NextVelocity = AABBPhysics.Velocity;
        NextVelocity.y += G * TimeStep;  // Gravity

        v3 NextPos = Transform.Translation;

        AABBPhysics.Grounded = false;

        NextPos.y += NextVelocity.y * TimeStep;

        aabb EntityAABB = AABBFromV3(NextPos, AABBPhysics.BoxSize);

        // Figuring out if somehit is it
        block_pos Pos = get_world_to_block_position(NextPos);

        for (i32 l = -1; l <= 1; l += 2)
        {
            for (i32 c = -1; c <= 1; c++)
            {
                for (i32 r = -1; r <= 1; r++)
                {
                    auto Block = block_get_safe(Game, Pos.C + c, Pos.R + r, Pos.L + l);

                    if (Block && Block->placed())
                    {
                        aabb BlockAABB = AABBFromV3(Block->Position, v3(1.0f));
                        if (AABBCheckCollision(EntityAABB, BlockAABB))
                        {
                            // Hit something above? Stop jumping.
                            if (NextVelocity.y > 0)
                            {
                                NextVelocity.y = 0;
                            }
                            // Hit the ground? Reset velocity and allow jumping again.
                            else if (NextVelocity.y < 0)
                            {
                                AABBPhysics.Grounded = true;
                                NextVelocity.y = 0;
                            }
                            NextPos.y = Transform.Translation.y; // Revert movement
                            goto ExitLoopY;
                        }
                    }
                }
            }
        }

    ExitLoopY:

        // XXX
        NextPos.x += NextVelocity.x * TimeStep;

        EntityAABB = AABBFromV3(NextPos, AABBPhysics.BoxSize);

        Pos = get_world_to_block_position(NextPos);

        for (i32 c = -1; c <= 1; c += 2)
        {
            for (i32 r = -1; r <= 1; r++)
            {
                for (i32 l = -1; l <= 1; l++)
                {
                    auto Block = block_get_safe(Game, Pos.C + c, Pos.R + r, Pos.L + l);

                    if (Block && Block->placed())
                    {
                        aabb BlockAABB = AABBFromV3(Block->Position, v3(1.0f));
                        if (AABBCheckCollision(EntityAABB, BlockAABB))
                        {
                            Block->Color = v4(0, 0, 1, 1);
                            NextPos.x = Transform.Translation.x;
                            NextVelocity.x = 0.0f;
                            goto ExitLoopX;
                        }
                    }
                }
            }
        }
    ExitLoopX:

        // ZZZ
        NextPos.z += NextVelocity.z * TimeStep;

        EntityAABB = AABBFromV3(NextPos, AABBPhysics.BoxSize);

        Pos = get_world_to_block_position(NextPos);

        for (i32 r = -1; r <= 1; r += 2)
        {
            for (i32 c = -1; c <= 1; c++)
            {
                for (i32 l = -1; l <= 1; l++)
                {
                    auto Block = block_get_safe(Game, Pos.C + c, Pos.R + r, Pos.L + l);

                    if (Block && Block->placed())
                    {
                        aabb BlockAABB = AABBFromV3(Block->Position, v3(1.0f));
                        if (AABBCheckCollision(EntityAABB, BlockAABB))
                        {
                            Block->Color = v4(0, 0, 1, 1);
                            NextPos.z = Transform.Translation.z;
                            NextVelocity.z = 0.0f;
                            goto ExitLoopZ;
                        }
                    }
                }
            }
        }

    ExitLoopZ:
        Transform.Translation = NextPos;
        AABBPhysics.Velocity = NextVelocity;
    }
}

internal void game_render_entities(game* Game, game_renderer* Renderer, f32 TimeStep)
{
    //debug_cycle_counter GameRenderEntities("Render Entities");

    auto View = ViewComponents<transform_component, entity_render_component>(&Game->Registry);
    for (auto Entity : View)
    {
        auto [Transform, Render] = View.Get(Entity);

        for (i32 i = 0; i < Render.Model.PartsCount; i++)
        {
            entity_part& Part = Render.Model.Parts[i];

            // TODO: SIMD STUFF, we can take advantage in matrix multiplication
            m4 Local = bkm::Translate(m4(1.0f), Part.LocalPosition) * bkm::Scale(m4(1.0f), Part.Size);
            m4 TransformMatrix = Transform.Matrix() * Local;

            game_renderer_submit_quaded_cuboid(Renderer, TransformMatrix, &Render.Texture, Part.Coords, Render.Color);
        }
    }
}
