#include "Blocky.h"
#include "ECS.h"

internal game GameCreate(game_renderer* Renderer)
{
    game Game = {};

    // Crosshair texture will be separate from block textures
    Game.CrosshairTexture = TextureCreate(Renderer->Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, "Resources/Textures/MC/Crosshair.png");

    // Load block textures
    Game.BlockTextures[(u32)block_type::Dirt] = TextureCreate(Renderer->Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, "Resources/Textures/MC/dirt_2.png");

    GameGenerateWorld(&Game);

    Game.AliveEntities = new entity[MaxAliveEntitiesCount];

    texture CowTexture = TextureCreate(Renderer->Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, "Resources/Textures/MC/cow.png");;

    // Create player entity
    for (size_t i = 0; i < 100; i++)
    {
        auto CowEntity = EntityCreate(&Game);
        CowEntity->Type = entity_type::Cow;
        CowEntity->Transform.Translation = v3(i, 17, 2);
        CowEntity->Transform.Scale = v3(0.7f, 0.7f, 1.0f);
        CowEntity->Transform.Rotation = v3(0.0f, 0.0f, 0.0f);
        CowEntity->AABBPhysics.BoxSize = v3(0.8f, 1.8, 0.8f);
        CowEntity->SetFlags(entity_flags::Valid | entity_flags::InteractsWithBlocks);
        CowEntity->Render.Texture = CowTexture;
    }

    return Game;
}

internal void GameGenerateWorld(game* Game)
{
    Game->Blocks.resize(RowCount * ColumnCount * LayerCount);

    v3 StartPos = { 0, 0, 0, };

    i32 CikCak = 0;
    i32 GlobalIndex = 0;

    for (i32 L = 0; L < LayerCount; L++)
    {
        StartPos.z = 0;

        for (i32 R = 0; R < RowCount; R++)
        {
            StartPos.x = 0;

            for (i32 C = 0; C < ColumnCount; C++)
            {
                // Establish a "Block"
                auto& Block = Game->Blocks[(L * RowCount * ColumnCount) + (R * RowCount) + C];

                if (L < 16)
                {
                    Block.Position = v3(StartPos.x, StartPos.y, StartPos.z);
                    Block.Type = block_type::Dirt;
                    Block.Color = (CikCak & 1) ? v4(0.2f, 0.2f, 0.2f, 1.0f) : v4(1.0f);
                }
                else
                {
                    Block.Position = v3(StartPos.x, StartPos.y, StartPos.z);
                    Block.Type = block_type::Air;
                }

                StartPos.x++;

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

            StartPos.z++;
        }

        CikCak++;

        StartPos.y++;
    }

}

internal void GameUpdate(game* Game, game_renderer* Renderer, const game_input* Input, f32 TimeStep, u32 ClientAreaWidth, u32 ClientAreaHeight)
{
    //debug_cycle_counter GameUpdateCounter("GameUpdateCounter");

    // Live entities
    GamePlayerUpdate(Game, Input, Renderer, TimeStep);

    // Physics simulation gave us some state of the entity and now we can use it to react to in the update loop
    // Entity update
    {
        auto Entities = Game->AliveEntities;
        for (i32 i = 0; i < Game->AliveEntitiesCount; i++)
        {
            auto& Entity = Game->AliveEntities[i];

            if (!Entity.HasFlags(entity_flags::Valid))
                continue;

            if (Entity.Type == entity_type::Cow)
            {
                f32 Speed = 1.0f;
                v3 Direction(1.0f, 0.0f, 0.0f);

                // This way we can actually set velocity while respecting the physics simulation
                Entity.AABBPhysics.Velocity.x = Speed * Direction.x;
                Entity.AABBPhysics.Velocity.z = Speed * Direction.y;

                if (bkm::NonZero(v2(Entity.AABBPhysics.Velocity.x, Entity.AABBPhysics.Velocity.z)))
                {
                    Entity.Transform.Rotation.y = bkm::Atan2(Entity.AABBPhysics.Velocity.x, Entity.AABBPhysics.Velocity.z);
                }
            }
        }
    }

    // Physics simulation
    if (1)
    {
        //debug_cycle_counter CC("Fast physics simulation");
        //debug_scoped_timer Timer("Fast physics simulation");

        auto Entities = Game->AliveEntities;
        for (i32 i = 0; i < Game->AliveEntitiesCount; i++)
        {
            auto& Entity = Entities[i];

            // Skip all of those
            if (!Entity.HasFlags(entity_flags::Valid | entity_flags::InteractsWithBlocks))
                continue;

            const f32 G = -9.81f * 2;
            const f32 CheckRadius = 2.0f;

            // Apply gravity and movement forces
            v3 NextVelocity = Entity.AABBPhysics.Velocity;
            NextVelocity.y += G * TimeStep;  // Gravity

            v3 NextPos = Entity.Transform.Translation;

            Entity.AABBPhysics.Grounded = false;

            NextPos.y += NextVelocity.y * TimeStep;

            aabb EntityAABB = AABBFromV3(NextPos, Entity.AABBPhysics.BoxSize);

            // Stepping on a block
            // Is kinda wonky
            i32 C = (i32)bkm::Floor(NextPos.x + 0.5f);
            i32 R = (i32)bkm::Floor(NextPos.z + 0.5f);
            i32 L = (i32)bkm::Floor(NextPos.y + 0.5f);

            for (i32 l = -1; l <= 1; l += 2)
            {
                for (i32 c = -1; c <= 1; c++)
                {
                    for (i32 r = -1; r <= 1; r++)
                    {
                        auto Block = BlockGetSafe(Game, C + c, R + r, L + l);

                        if (Block && Block->Placed())
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
                                    Entity.AABBPhysics.Grounded = true;
                                    NextVelocity.y = 0;
                                }
                                NextPos.y = Entity.Transform.Translation.y; // Revert movement
                                goto ExitLoopY;
                            }
                        }
                    }
                }
            }

        ExitLoopY:

            // XXX
            NextPos.x += NextVelocity.x * TimeStep;

            EntityAABB = AABBFromV3(NextPos, Entity.AABBPhysics.BoxSize);

            C = (i32)bkm::Floor(NextPos.x + 0.5f);
            R = (i32)bkm::Floor(NextPos.z + 0.5f);
            L = (i32)bkm::Floor(NextPos.y + 0.5f);

            for (i32 c = -1; c <= 1; c += 2)
            {
                for (i32 r = -1; r <= 1; r++)
                {
                    for (i32 l = -1; l <= 1; l++)
                    {
                        auto Block = BlockGetSafe(Game, C + c, R + r, L + l);

                        if (Block && Block->Placed())
                        {
                            aabb BlockAABB = AABBFromV3(Block->Position, v3(1.0f));
                            if (AABBCheckCollision(EntityAABB, BlockAABB))
                            {
                                Block->Color = v4(0, 0, 1, 1);
                                NextPos.x = Entity.Transform.Translation.x;
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

            EntityAABB = AABBFromV3(NextPos, Entity.AABBPhysics.BoxSize);

            C = (i32)bkm::Floor(NextPos.x + 0.5f);
            R = (i32)bkm::Floor(NextPos.z + 0.5f);
            L = (i32)bkm::Floor(NextPos.y + 0.5f);

            for (i32 r = -1; r <= 1; r += 2)
            {
                for (i32 c = -1; c <= 1; c++)
                {
                    for (i32 l = -1; l <= 1; l++)
                    {
                        auto Block = BlockGetSafe(Game, C + c, R + r, L + l);

                        if (Block && Block->Placed())
                        {
                            aabb BlockAABB = AABBFromV3(Block->Position, v3(1.0f));
                            if (AABBCheckCollision(EntityAABB, BlockAABB))
                            {
                                Block->Color = v4(0, 0, 1, 1);
                                NextPos.z = Entity.Transform.Translation.z;
                                NextVelocity.z = 0.0f;
                                goto ExitLoopZ;
                            }
                        }
                    }
                }
            }

        ExitLoopZ:
            Entity.Transform.Translation = NextPos;
            Entity.AABBPhysics.Velocity = NextVelocity;
        }
    }

    // Render entities
    {
        debug_cycle_counter GameUpdateCounter("Render Entities");
        //scoped_timer Timer("Render entities");

        texture_coords BodyTextureCoords[6];
        {
            auto Front = GetTextureCoords(12, 10, 28, 13);
            auto Back = GetTextureCoords(12, 10, 40, 13);

            auto Left = GetTextureCoords(10, 18, 18, 31, 1);
            auto Right = GetTextureCoords(10, 18, 40, 31, 3);

            auto Top = GetTextureCoords(12, 18, 50, 31, 2);
            auto Bottom = GetTextureCoords(12, 18, 28, 31, 0);

            BodyTextureCoords[0] = Front;
            BodyTextureCoords[1] = Back;
            BodyTextureCoords[2] = Left;
            BodyTextureCoords[3] = Right;
            BodyTextureCoords[4] = Top;
            BodyTextureCoords[5] = Bottom;
        }

        texture_coords HeadTextureCoords[6];
        {
            auto Front = GetTextureCoords(8, 8, 6, 13);
            auto Back = GetTextureCoords(8, 8, 20, 13);
            auto Left = GetTextureCoords(6, 8, 0, 13);
            auto Right = GetTextureCoords(6, 8, 14, 13);
            auto Top = GetTextureCoords(8, 6, 6, 5);
            auto Bottom = GetTextureCoords(8, 6, 14, 5);

            HeadTextureCoords[0] = Front;
            HeadTextureCoords[1] = Back;
            HeadTextureCoords[2] = Left;
            HeadTextureCoords[3] = Right;
            HeadTextureCoords[4] = Top;
            HeadTextureCoords[5] = Bottom;
        }

        texture_coords LegTextureCoords[6];
        {
            auto Left = GetTextureCoords(4, 12, 0, 31);
            auto Right = GetTextureCoords(4, 12, 8, 31);

            auto Front = GetTextureCoords(4, 12, 4, 31);
            auto Back = GetTextureCoords(4, 12, 12, 31);

            auto Top = GetTextureCoords(4, 4, 4, 19);
            auto Bottom = GetTextureCoords(4, 4, 8, 19, 2);

            LegTextureCoords[0] = Front;
            LegTextureCoords[1] = Back;
            LegTextureCoords[2] = Left;
            LegTextureCoords[3] = Right;
            LegTextureCoords[4] = Top;
            LegTextureCoords[5] = Bottom;
        }

        {
            //GameRendererSubmitCuboidNoRotScale(Renderer, v3(0, 20, 0), Game->BlockTextures[(u32)block_type::Dirt], v4(1.0f));

            texture_coords TextureCoords[6];

            for (i32 i = 0; i < 6; i++)
            {
                TextureCoords[i] = texture_coords();
            }
            m4 Transform = bkm::Translate(m4(1.0f), v3(0, 20, 0));
            GameRendererSubmitCustomCuboid_FAST(Renderer, Transform, Game->BlockTextures[(u32)block_type::Dirt], TextureCoords, v4(1.0f, 0.0f, 0.0f, 1.0f));
        }

        auto Entities = Game->AliveEntities;
        for (i32 i = 0; i < Game->AliveEntitiesCount; i++)
        {
            auto& Entity = Entities[i];

            // Skip all of those
            if (!Entity.HasFlags(entity_flags::Valid | entity_flags::Renderable))
                continue;

            if (Entity.Type == entity_type::Cow)
            {
                // Body
                GameRendererSubmitCustomCuboid_FAST(Renderer, Entity.Transform.Matrix(), Entity.Render.Texture, BodyTextureCoords, v4(1.0f));

                if (0)
                {
                    // IDEA: Move the head based on its scale to preserve the same position of the head/body
                    // f32 Y = CowTranslation.y - Scale.y * 0.5f - LegScale.y * 0.5f;
                    // Head
                    {
                        local_persist transform CowHeadTransform;
                        CowHeadTransform.Translation = v3(0, 0.4f, 0.7f);
                        //CowHeadTransform.Rotation = v3(bkm::Sin(Time) * 0.5f, 0, 0);
                        CowHeadTransform.Scale = v3(0.5f / 0.7f, 0.5f / 0.7f, 0.4f);

                        GameRendererSubmitCustomCuboid_FAST(Renderer, Entity.Transform.Matrix() * CowHeadTransform.Matrix(), Entity.Render.Texture, HeadTextureCoords, v4(1.0f));
                    }

                    // Legs
                    if (1)
                    {
                        f32 DeltaX = 0.30f;
                        f32 DeltaZ = 0.35f;
                        f32 ScaleX = 0.3f / 0.7f * 0.9f;
                        f32 ScaleY = 1.0f;
                        f32 ScaleZ = 0.3f * 0.9f;

                        transform LegTransform[4];
                        LegTransform[0].Translation = v3(0, -1.0f, 0);
                        LegTransform[0].Scale = v3(ScaleX, ScaleY, ScaleZ);

                        LegTransform[1].Translation = v3(0, -1.0f, 0);
                        LegTransform[1].Scale = v3(ScaleX, ScaleY, ScaleZ);

                        LegTransform[2].Translation = v3(0, -1.0f, 0);
                        LegTransform[2].Scale = v3(ScaleX, ScaleY, ScaleZ);

                        LegTransform[3].Translation = v3(0, -1.0f, 0);
                        LegTransform[3].Scale = v3(ScaleX, ScaleY, ScaleZ);

                        //f32 Y = CowTranslation.y - Scale.y * 0.5f - LegScale.y * 0.5f;

                        /*  v3 Offsets[4];
                          Offsets[0] = v3(DeltaX, Y, DeltaZ);
                          Offsets[1] = v3(-DeltaX, Y, DeltaZ);
                          Offsets[2] = v3(-DeltaX, Y, -DeltaZ);
                          Offsets[3] = v3(DeltaX, Y, -DeltaZ);*/

                        f32 LegRotation = bkm::Sin(Game->Time * 10.3f * bkm::Length(Entity.AABBPhysics.Velocity)) * 0.3f;

                        transform KneeTransform[4] = {};
                        KneeTransform[0].Translation = v3(DeltaX, 0.0f, -DeltaZ);
                        KneeTransform[0].Rotation = v3(-LegRotation, 0, 0);

                        KneeTransform[1].Translation = v3(-DeltaX, 0.0f, -DeltaZ);
                        KneeTransform[1].Rotation = v3(LegRotation, 0, 0);

                        KneeTransform[2].Translation = v3(-DeltaX, 0.0f, DeltaZ);
                        KneeTransform[2].Rotation = v3(LegRotation, 0, 0);

                        KneeTransform[3].Translation = v3(DeltaX, 0.0f, DeltaZ);
                        KneeTransform[3].Rotation = v3(-LegRotation, 0, 0);

                        // Front Left
                        for (u32 i = 0; i < 4; i++)
                        {
                            GameRendererSubmitCustomCuboid_FAST(Renderer, Entity.Transform.Matrix() * KneeTransform[i].Matrix() * LegTransform[i].Matrix(), Entity.Render.Texture, LegTextureCoords, v4(1.0f));
                        }
                    }
                }


            }
        }
    }

    // Update camera
    {
        Game->Camera.View = bkm::Translate(m4(1.0f), Game->Player.Position + Game->CameraOffset)
            * bkm::ToM4(qtn(Game->Player.Rotation));

        Game->Camera.View = bkm::Inverse(Game->Camera.View);
        Game->Camera.RecalculateProjectionPerspective(ClientAreaWidth, ClientAreaHeight);
    }

    // Count time
    Game->Time += TimeStep;

    // Set view projection to render player's view
    GameRendererSetViewProjectionCuboid(Renderer, Game->Camera.GetViewProjection());

    // Render blocks
    if (1)
    {
        //debug_cycle_counter GameUpdateCounter("Render Blocks");

        for (auto& Block : Game->Blocks)
        {
            if (!Block.Placed())
                continue;

            GameRendererSubmitCuboidNoRotScale(Renderer, Block.Position, Game->BlockTextures[(u32)Block.Type], Block.Color);
        }
    }

    // Render HUD
    if (1)
    {
        // Orthographic projection: 0, 0, ClientAreaWidth, ClientAreaHeight
        Game->Camera.RecalculateProjectionOrtho_V2(ClientAreaWidth, ClientAreaHeight);
        GameRendererSetViewProjectionQuad(Renderer, Game->Camera.Projection);
        f32 crosshairSize = 15.0f * 2.0f;
        f32 centerX = ClientAreaWidth / 2.0f - crosshairSize / 2.0f;
        f32 centerY = ClientAreaHeight / 2.0f - crosshairSize / 2.0f;

        v3 Positions[4];
        Positions[3] = { centerX, centerY, 0.0f };
        Positions[2] = { centerX + crosshairSize, centerY, 0.0f };
        Positions[1] = { centerX + crosshairSize, centerY + crosshairSize, 0.0f };
        Positions[0] = { centerX, centerY + crosshairSize, 0.0f };

        GameRendererSubmitQuadCustom(Renderer, Positions, Game->CrosshairTexture, v4(1.0f, 1.0f, 1.0f, 0.7f));
    }
}

internal void GamePlayerUpdate(game* Game, const game_input* Input, game_renderer* Renderer, f32 TimeStep)
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

    if (Input->IsKeyPressed(key::T))
    {
        TPressed = !TPressed;

        JustPressed = true;
    }

    if (TPressed)
    {
        local_persist v2i OldMousePos;

        v2i MousePos = (Input->IsCursorLocked ? Input->VirtualMousePosition : Input->LastMousePosition);

        // Avoids teleporting with camera rotation due to large delta
        if (JustPressed)
            OldMousePos = MousePos;

        v2i MouseDelta = MousePos - OldMousePos;
        OldMousePos = MousePos;

        f32 MouseSensitivity = 1.0f;

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
        if (Input->IsKeyDown(key::W))
        {
            Direction += v3(Forward.x, 0.0f, Forward.z);
        }

        if (Input->IsKeyDown(key::S))
        {
            Direction -= v3(Forward.x, 0.0f, Forward.z);
        }

        if (Input->IsKeyDown(key::A))
        {
            Direction -= Right;
        }

        if (Input->IsKeyDown(key::D))
        {
            Direction += Right;
        }

        if (Input->IsKeyPressed(key::BackSpace) || Input->IsKeyDown(key::Space))
        {
            JumpKeyPressed = true;
        }

        if (Input->IsKeyPressed(key::G))
        {
            Player.IsPhysicsObject = !Player.IsPhysicsObject;
        }

        if (!Player.IsPhysicsObject)
        {
            if (Input->IsKeyDown(key::Q) || Input->IsKeyDown(key::BackSpace) || Input->IsKeyDown(key::Space))
            {
                Direction += v3(0.0f, 1.0f, 0.0f);
            }
            else if (Input->IsKeyDown(key::E) || Input->IsKeyDown(key::Control) || Input->IsKeyDown(key::Shift))
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
        // Is kinda wonky
        i32 C = (i32)bkm::Floor(NextPos.x + 0.5f);
        i32 R = (i32)bkm::Floor(NextPos.z + 0.5f);
        i32 L = (i32)bkm::Floor(NextPos.y + 0.5f);

        for (i32 l = -1; l <= 1; l += 2)
        {
            for (i32 c = -1; c <= 1; c++)
            {
                for (i32 r = -1; r <= 1; r++)
                {
                    auto Block = BlockGetSafe(Game, C + c, R + r, L + l);

                    if (Block && Block->Placed())
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

        C = (i32)bkm::Floor(NextPos.x + 0.5f);
        R = (i32)bkm::Floor(NextPos.z + 0.5f);
        L = (i32)bkm::Floor(NextPos.y + 0.5f);

        for (i32 c = -1; c <= 1; c += 2)
        {
            for (i32 r = -1; r <= 1; r++)
            {
                for (i32 l = -1; l <= 1; l++)
                {
                    auto Block = BlockGetSafe(Game, C + c, R + r, L + l);

                    if (Block && Block->Placed())
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

        C = (i32)bkm::Floor(NextPos.x + 0.5f);
        R = (i32)bkm::Floor(NextPos.z + 0.5f);
        L = (i32)bkm::Floor(NextPos.y + 0.5f);

        for (i32 r = -1; r <= 1; r += 2)
        {
            for (i32 c = -1; c <= 1; c++)
            {
                for (i32 l = -1; l <= 1; l++)
                {
                    auto Block = BlockGetSafe(Game, C + c, R + r, L + l);

                    if (Block && Block->Placed())
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
        i32 C = (i32)bkm::Floor(Player.Position.x + 0.5f);
        i32 R = (i32)bkm::Floor(Player.Position.z + 0.5f);
        i32 L = (i32)bkm::Floor(Player.Position.y + 0.5f);
#if 0
        for (i32 i = -1; i <= 1; i++)
        {
            for (i32 j = -1; j <= 1; j++)
            {
                auto Block = BlockGetSafe(Game, C + i, R + 1, L + j);

                if (Block)
                {
                    GameRendererSubmitCuboidNoRotScale(Renderer, Block->Position, Game->BlockTextures[u32(block_type::Dirt)], v4(1.0f, 0.0f, 0.0f, 1.0f));
                }
            }
        }
#endif
    }

    // Destroy block
    if (Input->IsMousePressed(mouse::Left))
    {
        ray Ray;
        Ray.Origin = Player.Position + Game->CameraOffset; // TODO: Camera position
        Ray.Direction = Forward; // Forward is already normalized

        v3 HitPoint;
        v3 HitNormal;
        block HitBlock;
        u64 HitIndex;
        if (FindFirstHit(Ray, Game->Blocks, &HitPoint, &HitNormal, &HitBlock, &HitIndex))
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
    if (Input->IsMousePressed(mouse::Right))
    {
        v2i PlacePos = { 0,0 };

        ray Ray;
        Ray.Origin = Player.Position + Game->CameraOffset; // TODO: Camera position
        Ray.Direction = Forward; // Forward is already normalized

        v3 HitPoint;
        v3 HitNormal;
        block HitBlock;
        u64 HitIndex;

        if (FindFirstHit(Ray, Game->Blocks, &HitPoint, &HitNormal, &HitBlock, &HitIndex))
        {
            v3 NewBlockPos = HitBlock.Position + HitNormal;

            aabb BlockAABB = AABBFromV3(NewBlockPos, v3(1.0f));
            aabb PlayerAABB = AABBFromV3(Player.Position, v3(0.5f, 1.8f, 0.5f));
            if (!AABBCheckCollision(PlayerAABB, BlockAABB))
            {
                i32 C = (i32)NewBlockPos.x;
                i32 R = (i32)NewBlockPos.z;
                i32 L = (i32)NewBlockPos.y;
                auto Block = BlockGetSafe(Game, C, R, L);

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

