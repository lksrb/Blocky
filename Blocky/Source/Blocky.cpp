#include "Blocky.h"

// Slab method raycast
// Ignores negative values to ensure one direction

struct aabb_raycast_result
{
    f32 Near;
    f32 Far;
    v3 Normal;
    bool Hit = true;

    operator bool() const { return Hit; }
};
internal aabb_raycast_result RayIntersectsAABB(const ray& Ray, const aabb& Box)
{
    aabb_raycast_result Result = {};

    f32 Min = -INFINITY;
    f32 Max = INFINITY;
    v3 Normal(0.0f);
    for (u32 Axis = 0; Axis < 3; ++Axis)
    {
        f32 InvD = 1.0f / (Axis == 0 ? Ray.Direction.x : (Axis == 1 ? Ray.Direction.y : Ray.Direction.z));
        f32 T0 = ((Axis == 0 ? Box.Min.x : (Axis == 1 ? Box.Min.y : Box.Min.z)) - (Axis == 0 ? Ray.Origin.x : (Axis == 1 ? Ray.Origin.y : Ray.Origin.z))) * InvD;
        f32 T1 = ((Axis == 0 ? Box.Max.x : (Axis == 1 ? Box.Max.y : Box.Max.z)) - (Axis == 0 ? Ray.Origin.x : (Axis == 1 ? Ray.Origin.y : Ray.Origin.z))) * InvD;

        if (InvD < 0.0f)
        {
            std::swap(T0, T1);
        }

        if (T0 > Min)
        {
            Min = T0;
            Normal = { 0, 0, 0 };
            if (Axis == 0) Normal.x = (Ray.Direction.x > 0) ? -1.0f : 1.0f;
            if (Axis == 1) Normal.y = (Ray.Direction.y > 0) ? -1.0f : 1.0f;
            if (Axis == 2) Normal.z = (Ray.Direction.z > 0) ? -1.0f : 1.0f;
        }

        //Min = std::max(Min, t0);
        Max = std::min(Max, T1);

        if (Max < Min)
        {
            Result.Hit = false;
            break;
        }
    }

    // Ignore hits behind the ray
    if (Min < 0)
    {
        Result.Hit = false;
    }

    Result.Near = Min;
    Result.Far = Max;
    Result.Normal = Normal;
    return Result;
}

internal bool FindFirstHit(const ray& Ray, const block* Blocks, u64 BlocksCount, v3* HitPoint, v3* HitNormal, block* HitBlock, u64* HitIndex)
{
    f32 ClosestT = INFINITY;
    bool FoundHit = false;

    for (u64 Index = 0; Index < BlocksCount; Index++)
    {
        auto& Block = Blocks[Index];

        if (!Block.Placed)
            continue;

        aabb Box;
        Box.Min = Block.Position - v3(0.5f);
        Box.Max = Block.Position + v3(0.5f);

        if (aabb_raycast_result RayCast = RayIntersectsAABB(Ray, Box))
        {
            // Ensure we take the closest intersection
            if (RayCast.Near < ClosestT)
            {
                ClosestT = RayCast.Near;
                FoundHit = true;
                *HitBlock = Block;
                *HitNormal = RayCast.Normal;
                *HitPoint = Ray.Origin + Ray.Direction * RayCast.Near;
                *HitIndex = Index;
            }
        }
    }

    return FoundHit;
}

internal bool FindFirstHit(const ray& Ray, const std::vector<block>& Blocks, v3* HitPoint, v3* HitNormal, block* HitBlock, u64* HitIndex)
{
    return FindFirstHit(Ray, Blocks.data(), Blocks.size(), HitPoint, HitNormal, HitBlock, HitIndex);
}

internal void BlockCreate(game* Game, block_type Type, v3 Position)
{
    auto& Block = Game->Blocks.emplace_back();
    //Block.Texture = Game->BlockTextures[(u32)Type];
    Block.Color = v4(1.0f);
    Block.Position = Position;
}

internal void BlockDestroy(game* Game, u64 Index)
{
    Game->Blocks.erase(Game->Blocks.begin() + Index);
}

internal game GameCreate(game_renderer* Renderer)
{
    game Game = {};

    Game.LogicBlocks.resize(RowCount * ColumnCount * LayerCount);

    // Crosshair texture will be separate from block textures
    Game.CrosshairTexture = TextureCreate(Renderer->Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, "Resources/Textures/MC/Crosshair.png");

    // Load block textures
    Game.BlockTextures[(u32)block_type::Dirt] = TextureCreate(Renderer->Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, "Resources/Textures/MC/dirt_2.png");

    // Place a block
    BlockCreate(&Game, block_type::Dirt, v3(0.0f, 0.0f, 0.0f));

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
                auto& Block = Game.LogicBlocks[(L * RowCount * ColumnCount) + (R * RowCount) + C];
                Block.Texture = Game.BlockTextures[(u32)block_type::Dirt];
                Block.Position = v3(StartPos.x, StartPos.y, StartPos.z);
                Block.Type = block_type::Dirt;
                Block.Color = (CikCak & 1) ? v4(0.2f, 0.2f, 0.2f, 1.0f) : v4(1.0f);
                Block.Placed = true;

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

    return Game;
}

internal void GameUpdate(game* Game, game_renderer* Renderer, const game_input* Input, f32 TimeStep, u32 ClientAreaWidth, u32 ClientAreaHeight)
{
    GamePlayerUpdate(Game, Input, TimeStep);

    // Physics
    {
        // TODO: Separate Static blocks from dynamic since we dont want to iterate over every block twice.
        // This is ultimately O(n^2)

        if (0)
        {
            for (auto& Block : Game->LogicBlocks)
            {
                for (auto& Block : Game->LogicBlocks)
                {

                }
            }
        }


    }

    // Color pink if all neighbours are present
    //if (false)
    //{
    //    for (i32 R = 0; R < RowCount; R++)
    //    {
    //        for (i32 C = 0; C < ColumnCount; C++)
    //        {
    //            auto& Block = Game->LogicBlocks[R * RowCount + C];

    //            bool HasAllNeightbours = Block.Back != INT_MAX && Block.Front != INT_MAX && Block.Left != INT_MAX && Block.Right != INT_MAX;

    //            Block.Color = HasAllNeightbours ? v4(1.0f, 0.0f, 1.0f, 1.0f) : v4(1.0f, 1.0f, 1.0f, 1.0f);

    //            if (HasAllNeightbours)
    //            {
    //                /* auto& LeftNeightbour = Game.LogicBlocks[Block.Left];
    //                 auto& RightNeightbour = Game.LogicBlocks[Block.Right];
    //                 auto& FrontNeightbour = Game.LogicBlocks[Block.Front];
    //                 auto& BackNeightbour = Game.LogicBlocks[Block.Back];*/

    //            }
    //        }
    //    }
    //}

    // Update camera
    {
        Game->Camera.View = bkm::Translate(m4(1.0f), Game->Player.Position + v3(0.0f, 1.0f, 0.0f))
            * bkm::ToM4(qtn(Game->Player.Rotation));

        Game->Camera.View = bkm::Inverse(Game->Camera.View);
        Game->Camera.RecalculateProjectionPerspective(ClientAreaWidth, ClientAreaHeight);
        //Game->Camera.RecalculateProjectionOrtho_V2(ClientAreaWidth, ClientAreaHeight);
    }

    // Render

    // Render blocks
    GameRendererSetViewProjection(Renderer, Game->Camera.GetViewProjection());

    if (0)
    {
        for (const auto& Block : Game->Blocks)
        {
            if (Block.Texture.Handle)
            {
                GameRendererSubmitBlock(Renderer, Block.Position, v3(0.0f), Block.Scale, Block.Texture, Block.Color);
            }
            else
            {
                GameRendererSubmitBlock(Renderer, Block.Position, v3(0.0f), Block.Scale, Block.Color);
            }
        }
    }

    if (1)
    {
        for (auto& Block : Game->LogicBlocks)
        {
            if (!Block.Placed)
                continue;

            if (Block.Texture.Handle)
            {
                GameRendererSubmitBlockNoRotScale(Renderer, Block.Position, Block.Texture, Block.Color);
            }
            else
            {
                GameRendererSubmitBlockNoRotScale(Renderer, Block.Position, Block.Color);
            }
        }
    }

    //Trace("%.3f", TimeStep * 1000.0f);

    if (Input->IsMouseDown(mouse::Middle))
    {
        local_persist v3 Pos = v3(0.0f, 20.0f, 0.0f);
        local_persist v3 Vel = v3(0.0f);
        f32 G = -9.81;

        Vel.y += G * TimeStep;

        Pos += Vel * TimeStep;

        if (Pos.y < 1)
        {
            Pos.y = 1;
            Vel.y = 0;
        }
        //GameRendererSubmitBlock(Renderer, v3(0.0f), v3(0.0f), v3(1.0f), Game->BlockTextures[u32(block_type::Dirt)], v4(1.0), draw_layer::Main);

    }


    //// Render intersections
    //// TODO: Delete
    //for (auto& Block : Game->Intersections)
    //{
    //    if (Block.Texture.Handle)
    //    {
    //        GameRendererSubmitBlock(Renderer, Block.Translation, v3(0.0f), Block.Scale, Block.Texture, Block.Color, draw_layer::Main);
    //    }
    //    else
    //    {
    //        GameRendererSubmitBlock(Renderer, Block.Translation, v3(0.0f), Block.Scale, Block.Color, draw_layer::Main);
    //    }
    //}

    //GameRendererSubmitBlock(Renderer, v3(1.0), v3(0.0f), v3(1.0f), v4(1.0), draw_layer::Main);

    //GameRendererSubmitBlock_V2(Renderer, v3(0.0), v3(0.0f), v3(1.0f), v4(1.0), draw_layer::Main);
    // They render on top of each other or just the first one is rendererd
    //GameRendererSubmitBlock_V2(Renderer, v3(1.0, 1.0f, 0.0f), v3(0.0f), v3(1.0f), v4(1.0f, 0.0f, 0.0f, 1.0f), draw_layer::Main);
    //GameRendererSubmitBlock_V2(Renderer, v3(0.0, 0.0f, 1.0f), v3(0.0f), v3(1.0f), v4(1.0), draw_layer::Main);
    //GameRendererSubmitBlock_V2(Renderer, v3(1.0, 0.0f, 1.0f), v3(0.0f), v3(1.0f), v4(1.0), draw_layer::Main);

    // HUD
    // HUD
    // HUD

    // Orthographic projection: 0, 0, ClientAreaWidth, ClientAreaHeight
    Game->Camera.RecalculateProjectionOrtho_V2(ClientAreaWidth, ClientAreaHeight);
    GameRendererSetViewProjectionLayer(Renderer, Game->Camera.Projection, draw_layer::HUD);
    f32 crosshairSize = 15.0f * 2.0f;
    f32 centerX = ClientAreaWidth / 2.0f - crosshairSize / 2.0f;
    f32 centerY = ClientAreaHeight / 2.0f - crosshairSize / 2.0f;

    v3 Positions[4];
    Positions[3] = { centerX, centerY, 0.0f };
    Positions[2] = { centerX + crosshairSize, centerY, 0.0f };
    Positions[1] = { centerX + crosshairSize, centerY + crosshairSize, 0.0f };
    Positions[0] = { centerX, centerY + crosshairSize, 0.0f };

    GameRendererSubmitQuadCustom(Renderer, Positions, Game->CrosshairTexture, v4(1.0f, 1.0f, 1.0f, 0.7f), draw_layer::HUD);
}

internal void GamePlayerUpdate(game* Game, const game_input* Input, f32 TimeStep)
{
    auto& Player = Game->Player;

    v3 Up = { 0.0f, 1.0, 0.0f };
    v3 Right = { 1.0f, 0.0, 0.0f };
    v3 Forward = { 0.0f, 0.0, -1.0f };
    f32 Speed = 10.0f;

    // Rotating
    local_persist bool TPressed = false;

    bool JustPressed = false;

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

        //// TODO: What happens on simultaneously press?
        //if (Input->IsKeyDown(key::Q) || Input->IsKeyDown(key::BackSpace) || Input->IsKeyDown(key::Space))
        //{
        //    Direction += v3(0.0f, 1.0f, 0.0f);;
        //}

        //if (Input->IsKeyDown(key::E) || Input->IsKeyDown(key::Control) || Input->IsKeyDown(key::Shift))
        //{
        //    Direction -= v3(0.0f, 1.0f, 0.0f);;
        //}

        /*if (Input->IsKeyDown(key::BackSpace))
        {
            Player.Velocity.y = 1.0f;
        }*/

        if (bkm::Length(Direction) > 0.0f)
            Direction = bkm::Normalize(Direction);

    }

    if (Game->Player.PhysicsObject)
    {
        auto& Player = Game->Player;

        f32 G = -9.81;

        v3 NextPlayerVelocity = Player.Velocity + v3(0.0f , G * TimeStep, 0.0f);
        v3 FinalVelocity = v3(Direction.x * Speed, NextPlayerVelocity.y, Direction.z * Speed);

        v3 NextPlayerPosition = Player.Position + FinalVelocity * TimeStep;

        for (auto& Block : Game->LogicBlocks)
        {
            if (!Block.Placed)
                continue;

            aabb BlockAABB = AABBFromV3(Block.Position, v3(1.0f));
            aabb PlayerAABB = AABBFromV3(NextPlayerPosition, v3(0.5f, 2.0f, 0.5f));

            collision_result Result = CheckCollisionAABB(PlayerAABB, BlockAABB, TimeStep);
            if (Result.Collided)
            {
                if (Result.Side == CollisionSide::Bottom)
                {
                    FinalVelocity.y = 0.0f;
                }

                if (Result.Side == CollisionSide::Left || Result.Side == CollisionSide::Right)
                {
                    FinalVelocity.x = 0.0f;
                }

                if (Result.Side == CollisionSide::Front || Result.Side == CollisionSide::Back)
                {
                    FinalVelocity.z = 0.0f;
                }
                break;
            }
        }

        Player.Velocity = FinalVelocity;
        Player.Position += FinalVelocity * TimeStep;
    }
    else
    {
        Player.Position += v3(Direction.x, 0.0f, Direction.z) * Speed * TimeStep;
    }

    //
    // ----------------------------------------------------------------------------------------------------------
    // 

    // Place a block
    if (Input->IsMousePressed(mouse::Right) && false)
    {
        ray Ray;
        Ray.Origin = Player.Position;
        Ray.Direction = Forward; // Forward is already normalized

        v3 HitPoint;
        v3 HitNormal;
        block HitBlock;
        u64 HitIndex; // Unused
        if (FindFirstHit(Ray, Game->Blocks, &HitPoint, &HitNormal, &HitBlock, &HitIndex))
        {
            BlockCreate(Game, block_type::Dirt, HitBlock.Position + HitNormal);
            Info("New block added");
        }
    }

    // Destroy a block
    if (Input->IsMousePressed(mouse::Left) && false)
    {
        ray Ray;
        Ray.Origin = Player.Position;
        Ray.Direction = Forward; // Forward is already normalized

        v3 HitPoint;
        v3 HitNormal;
        block HitBlock;
        u64 HitIndex;
        if (FindFirstHit(Ray, Game->Blocks, &HitPoint, &HitNormal, &HitBlock, &HitIndex))
        {
            BlockDestroy(Game, HitIndex);
            Info("Block destroyed.");
        }
    }

    // Create Logic blocks
    if (Input->IsMousePressed(mouse::Right))
    {
        v2i PlacePos = { 0,0 };
    }

    // Destroy logic block
    if (Input->IsMousePressed(mouse::Left))
    {
        ray Ray;
        Ray.Origin = Player.Position + v3(0.0f, 1.0f, 0.0f); // TODO: Camera position 
        Ray.Direction = Forward; // Forward is already normalized

        v3 HitPoint;
        v3 HitNormal;
        block HitBlock;
        u64 HitIndex;
        if (FindFirstHit(Ray, Game->LogicBlocks, &HitPoint, &HitNormal, &HitBlock, &HitIndex))
        {
            auto& Block = Game->LogicBlocks[HitIndex];
            Block.Placed = false;

            // Color adjacent blocks
            if (1)
            {
                if (Block.Left != INT_MAX)
                {
                    auto& Neighbour = Game->LogicBlocks[Block.Left];
                    Neighbour.Color = v4(1.0f, 0.0f, 0.0f, 1.0f);
                    Neighbour.Right = INT_MAX;
                }

                if (Block.Right != INT_MAX)
                {
                    auto& Neighbour = Game->LogicBlocks[Block.Right];
                    Neighbour.Color = v4(1.0f, 0.0f, 0.0f, 1.0f);
                    Neighbour.Left = INT_MAX;
                }

                if (Block.Front != INT_MAX)
                {
                    auto& Neighbour = Game->LogicBlocks[Block.Front];
                    Neighbour.Color = v4(1.0f, 0.0f, 0.0f, 1.0f);
                    Neighbour.Back = INT_MAX;
                }

                if (Block.Back != INT_MAX)
                {
                    auto& Neighbour = Game->LogicBlocks[Block.Back];
                    Neighbour.Color = v4(1.0f, 0.0f, 0.0f, 1.0f);
                    Neighbour.Front = INT_MAX;
                }

                if (Block.Up != INT_MAX)
                {
                    auto& Neighbour = Game->LogicBlocks[Block.Up];
                    Neighbour.Color = v4(1.0f, 0.0f, 0.0f, 1.0f);
                    Neighbour.Down = INT_MAX;
                }

                if (Block.Down != INT_MAX)
                {
                    auto& Neighbour = Game->LogicBlocks[Block.Down];
                    Neighbour.Color = v4(1.0f, 0.0f, 0.0f, 1.0f);
                    Neighbour.Up = INT_MAX;
                }
            }

            Info("Block destroyed.");
        }
    }
}
