#include "Blocky.h"

internal game GameCreate(game_renderer* Renderer)
{
    game Game = {};

    // Crosshair texture will be separate from block textures
    Game.CrosshairTexture = TextureCreate(Renderer->Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, "Resources/Textures/MC/Crosshair.png");

    // Load block textures
    Game.BlockTextures[(u32)block_type::Dirt] = TextureCreate(Renderer->Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, "Resources/Textures/MC/dirt_2.png");

    Game.CowTexture = TextureCreate(Renderer->Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, "Resources/Textures/MC/cow.png");

    GameGenerateWorld(&Game);

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
                    Block.Texture = Game->BlockTextures[(u32)block_type::Dirt];
                    Block.Position = v3(StartPos.x, StartPos.y, StartPos.z);
                    Block.Type = block_type::Dirt;
                    Block.Color = (CikCak & 1) ? v4(0.2f, 0.2f, 0.2f, 1.0f) : v4(1.0f);
                    Block.Placed = true;
                }
                else
                {
                    Block.Texture = Game->BlockTextures[(u32)block_type::Dirt];
                    Block.Position = v3(StartPos.x, StartPos.y, StartPos.z);
                    Block.Type = block_type::Air;
                }

                StartPos.x++;

                CikCak++;

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
    GamePlayerUpdate(Game, Input, TimeStep);

    // Render Cow
    GameCowUpdate(Game, Renderer, TimeStep);

    // Update camera
    {
        Game->Camera.View = bkm::Translate(m4(1.0f), Game->Player.Position + Game->CameraOffset)
            * bkm::ToM4(qtn(Game->Player.Rotation));

        Game->Camera.View = bkm::Inverse(Game->Camera.View);
        Game->Camera.RecalculateProjectionPerspective(ClientAreaWidth, ClientAreaHeight);
    }

    // Set view projection to render player's view
    GameRendererSetViewProjectionCuboid(Renderer, Game->Camera.GetViewProjection());

    // Render blocks
    if (1)
    {
        for (auto& Block : Game->Blocks)
        {
            if (!Block.Placed)
                continue;

            if (Block.Texture.Handle)
            {
                GameRendererSubmitCuboidNoRotScale(Renderer, Block.Position, Block.Texture, Block.Color);
            }
            else
            {
                GameRendererSubmitCuboidNoRotScale(Renderer, Block.Position, Block.Color);
            }
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

internal void GamePlayerUpdate(game* Game, const game_input* Input, f32 TimeStep)
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

    if (Game->Player.IsPhysicsObject)
    {
        const f32 G = -9.81f * 2;
        const f32 CheckRadius = 5.0f;
        const f32 JumpStrength = 10.0f;
        auto& Player = Game->Player;

        // Apply gravity and movement forces
        v3 NextVelocity = Player.Velocity;
        NextVelocity.y += G * TimeStep;  // Gravity
        NextVelocity.x = Direction.x * Speed;
        NextVelocity.z = Direction.z * Speed;

        v3 NextPos = Player.Position;
        aabb PlayerAABB;

        if (Player.Grounded && JumpKeyPressed)
        {
            NextVelocity.y = JumpStrength;
            Player.Grounded = false;
        }

        // Y - Gravity
        NextPos.y += NextVelocity.y * TimeStep;
        PlayerAABB = AABBFromV3(NextPos, v3(0.5f, 1.8f, 0.5f));

        for (auto& Block : Game->Blocks)
        {
            if (!Block.Placed)
                continue;

            aabb BlockAABB = AABBFromV3(Block.Position, v3(1.0f));

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
                break;
            }
        }

        // Move X-axis
        NextPos.x += NextVelocity.x * TimeStep;
        PlayerAABB = AABBFromV3(NextPos, v3(0.5f, 1.8f, 0.5f));

        for (auto& Block : Game->Blocks)
        {
            if (!Block.Placed)
                continue;

            aabb BlockAABB = AABBFromV3(Block.Position, v3(1.0f));

            if (AABBCheckCollision(PlayerAABB, BlockAABB))
            {
                // Collision on X-axis: stop only X movement
                NextVelocity.x = 0;
                NextPos.x = Player.Position.x;
                break;
            }
        }

        // Move Z-axis
        NextPos.z += NextVelocity.z * TimeStep;
        PlayerAABB = AABBFromV3(NextPos, v3(0.5f, 1.8f, 0.5f));

        for (auto& Block : Game->Blocks)
        {
            if (!Block.Placed)
                continue;

            aabb BlockAABB = AABBFromV3(Block.Position, v3(1.0f));

            if (AABBCheckCollision(PlayerAABB, BlockAABB))
            {
                // Collision on Z-axis: stop only Z movement
                NextVelocity.z = 0;
                NextPos.z = Player.Position.z;
                break;
            }
        }

        Player.Position = NextPos;
        Player.Velocity = NextVelocity;
    }
    else
    {
        Player.Position += Direction * Speed * TimeStep;
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
            Block.Placed = false;

            // Color adjacent blocks
            if (0)
            {
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
            }

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
                    Block->Placed = true;
                }
            }
        }
    }

    // Stepping on a block
    // Is kinda wonky
    i32 C = (i32)bkm::Floor(Player.Position.x + 0.5f);
    i32 R = (i32)bkm::Floor(Player.Position.z + 0.5f);
    i32 L = (i32)bkm::Floor(Player.Position.y + 0.5f);

    auto SteppedOnBlock = BlockGetSafe(Game, C, R, L - 1);
    if (SteppedOnBlock)
    {
        SteppedOnBlock->Color = v4(0, 0, 1, 1);
    }
}

internal void GameCowUpdate(game* Game, game_renderer* Renderer, f32 TimeStep)
{
    // Render a COW
    {
        local_persist f32 Time = 0;
        Time += TimeStep;

        local_persist transform CowTransform;
        local_persist bool ValuesSet = false;

        if (!ValuesSet)
        {
            ValuesSet = true;

            CowTransform.Translation = v3(3, 17, 2);
            CowTransform.Scale = v3(0.7f, 0.7f, 1.0f);
            //   CowTransform.Scale = v3(1.0f);
        }

        //CowTransform.Rotation.y = Time;

        //CowTransform.Scale = v3((0.7f + bkm::Sin(Time) * 0.7f), (0.7f + bkm::Sin(Time) * 0.7f), (1.0f + bkm::Sin(Time) * 1.0f));

        // Body
        {
            auto Front = GetTextureCoords(12, 10, 28, 13);
            auto Back = GetTextureCoords(12, 10, 40, 13);

            auto Left = GetTextureCoords(10, 18, 18, 31, 1);
            auto Right = GetTextureCoords(10, 18, 40, 31, 3);

            auto Top = GetTextureCoords(12, 18, 50, 31, 2);
            auto Bottom = GetTextureCoords(12, 18, 28, 31, 0);

            texture_coords TextureCoords[6];
            TextureCoords[0] = Front;
            TextureCoords[1] = Back;
            TextureCoords[2] = Left;
            TextureCoords[3] = Right;
            TextureCoords[4] = Top;
            TextureCoords[5] = Bottom;
            GameRendererSubmitCustomCuboid(Renderer, CowTransform.Matrix(), Game->CowTexture, TextureCoords, v4(1.0f));
        }

        // IDEA: Move the head based on its scale to preserve the same position of the head/body
        // f32 Y = CowTranslation.y - Scale.y * 0.5f - LegScale.y * 0.5f;
        // Head
        {
            local_persist transform CowHeadTransform;
            CowHeadTransform.Translation = v3(0, 0.4f, 0.7f);
            //CowHeadTransform.Rotation = v3(bkm::Sin(Time) * 0.5f, 0, 0);
            CowHeadTransform.Scale = v3(0.5f / 0.7f, 0.5f / 0.7f, 0.4f);

            auto Front = GetTextureCoords(8, 8, 6, 13);
            auto Back = GetTextureCoords(8, 8, 20, 13);
            auto Left = GetTextureCoords(6, 8, 0, 13);
            auto Right = GetTextureCoords(6, 8, 14, 13);
            auto Top = GetTextureCoords(8, 6, 6, 5);
            auto Bottom = GetTextureCoords(8, 6, 14, 5);

            texture_coords TextureCoords[6];
            TextureCoords[0] = Front;
            TextureCoords[1] = Back;
            TextureCoords[2] = Left;
            TextureCoords[3] = Right;
            TextureCoords[4] = Top;
            TextureCoords[5] = Bottom;

            GameRendererSubmitCustomCuboid(Renderer, CowTransform.Matrix() * CowHeadTransform.Matrix(), Game->CowTexture, TextureCoords, v4(1.0f));
        }

        // Legs
#if 1
        {
            f32 DeltaX = 0.30f;
            f32 DeltaZ = 0.35f;
            f32 ScaleX = 0.3f / 0.7f * 0.9f;
            f32 ScaleY = 1.0f;
            f32 ScaleZ = 0.3f * 0.9f;

            transform LegTransform[4];
            LegTransform[0].Translation = v3(0, -1.0f, 0);
            LegTransform[0].Scale = v3(ScaleX, ScaleY, ScaleZ);

            LegTransform[1].Translation = v3(DeltaX, -1.0f, DeltaZ);
            LegTransform[1].Scale = v3(ScaleX, ScaleY, ScaleZ);

            LegTransform[2].Translation = v3(-DeltaX, -1.0f, -DeltaZ);
            LegTransform[2].Scale = v3(ScaleX, ScaleY, ScaleZ);

            LegTransform[3].Translation = v3(-DeltaX, -1.0f, DeltaZ);
            LegTransform[3].Scale = v3(ScaleX, ScaleY, ScaleZ);

            //f32 Y = CowTranslation.y - Scale.y * 0.5f - LegScale.y * 0.5f;

          /*  v3 Offsets[4];
            Offsets[0] = v3(DeltaX, Y, DeltaZ);
            Offsets[1] = v3(-DeltaX, Y, DeltaZ);
            Offsets[2] = v3(-DeltaX, Y, -DeltaZ);
            Offsets[3] = v3(DeltaX, Y, -DeltaZ);*/

            transform KneeTransform[4] = {};
            KneeTransform[0].Translation = v3(DeltaX, 0.0f, -DeltaZ);
            KneeTransform[0].Rotation = v3(-bkm::Sin(Time * 1.3f) * 0.3f, 0, 0);
            KneeTransform[1].Rotation = v3(bkm::Sin(Time * 10.3f) * 0.3f, 0, 0);
            KneeTransform[2].Rotation = v3(bkm::Sin(Time * 10.3f) * 0.3f, 0, 0);
            KneeTransform[3].Rotation = v3(-bkm::Sin(Time * 10.3f) * 0.3f, 0, 0);

            // Front Left
            for (u32 i = 0; i < 1; i++)
            {
                auto Left = GetTextureCoords(4, 12, 0, 31);
                auto Front = GetTextureCoords(4, 12, 4, 31);

                auto Back = GetTextureCoords(4, 12, 12, 31);
                auto Right = GetTextureCoords(4, 12, 8, 31);

                auto Top = GetTextureCoords(4, 4, 4, 19);
                auto Bottom = GetTextureCoords(4, 4, 8, 19, 2);

                texture_coords TextureCoords[6];
                TextureCoords[0] = Front;
                TextureCoords[1] = Back;
                TextureCoords[2] = Left;
                TextureCoords[3] = Right;
                TextureCoords[4] = Top;
                TextureCoords[5] = Bottom;

                GameRendererSubmitCustomCuboid(Renderer, CowTransform.Matrix() * KneeTransform[i].Matrix() * LegTransform[i].Matrix(), Game->CowTexture, TextureCoords, v4(1.0f));
            }
        }
#endif
    }
}
