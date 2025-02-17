#include "Blocky.h"

// Slab method raycast
// Ignores negative values to ensure one direction
internal bool RayIntersectsAABB(const ray& Ray, const aabb& Box, f32& Near, f32& Far, v3& HitNormal)
{
    f32 Min = -INFINITY;
    f32 Max = INFINITY;
    v3 Normal(0.0f);
    for (u32 Axis = 0; Axis < 3; ++Axis)
    {
        f32 InvD = 1.0f / (Axis == 0 ? Ray.Direction.x : (Axis == 1 ? Ray.Direction.y : Ray.Direction.z));
        f32 T0 = ((Axis == 0 ? Box.Min.x : (Axis == 1 ? Box.Min.y : Box.Min.z)) - (Axis == 0 ? Ray.Origin.x : (Axis == 1 ? Ray.Origin.y : Ray.Origin.z))) * InvD;
        f32 T1 = ((Axis == 0 ? Box.Max.x : (Axis == 1 ? Box.Max.y : Box.Max.z)) - (Axis == 0 ? Ray.Origin.x : (Axis == 1 ? Ray.Origin.y : Ray.Origin.z))) * InvD;

        if (InvD < 0.0f)
            std::swap(T0, T1);

        if (T0 > Min)
        {
            Min = T0;
            Normal = { 0, 0, 0 };
            if (Axis == 0) Normal.x = (Ray.Direction.x > 0) ? -1 : 1;
            if (Axis == 1) Normal.y = (Ray.Direction.y > 0) ? -1 : 1;
            if (Axis == 2) Normal.z = (Ray.Direction.z > 0) ? -1 : 1;
        }

        //Min = std::max(Min, t0);
        Max = std::min(Max, T1);

        if (Max < Min) return false;
    }

    if (Min < 0) return false; // Ignore hits behind the ray

    Near = Min;
    Far = Max;
    HitNormal = Normal;
    return true;
}

internal bool FindFirstHit(const ray& Ray, const std::vector<block>& Blocks, v3& HitPoint, v3& HitNormal, block& HitBlock)
{
    f32 ClosestT = INFINITY;
    bool FoundHit = false;

    for (const auto& Block : Blocks)
    {
        f32 Near;
        f32 Far;
        v3 Normal;

        aabb Box;
        Box.Min = Block.Translation - v3(0.5f);
        Box.Max = Block.Translation + v3(0.5f);
        if (RayIntersectsAABB(Ray, Box, Near, Far, Normal))
        {
            // Ensure we take the closest intersection
            if (Near < ClosestT)
            {
                ClosestT = Near;
                HitBlock = Block;
                HitNormal = Normal;
                HitPoint = Ray.Origin + Ray.Direction * Near;
                FoundHit = true;
            }
        }
    }

    return FoundHit;
}

internal game GameCreate(game_renderer* Renderer)
{
    game Game = {};

    Game.TestTexture = DX12TextureCreate(Renderer->Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, "Resources/Textures/LevelBackground.png");

    Game.ContainerTexture = DX12TextureCreate(Renderer->Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, "Resources/Textures/container.png");

    Game.CrosshairTexture = DX12TextureCreate(Renderer->Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, "Resources/Textures/Crosshair.png");

    {
        auto& Block = Game.Blocks.emplace_back();
        Block.Texture = Game.ContainerTexture;
        Block.Color = v4(1.0f);
        Block.Translation = v3(0.0f, 0.0f, 0.0f);
    }

    return Game;
}

internal void GameUpdateAndRender(game* Game, game_renderer* Renderer, const game_input* Input, f32 TimeStep, u32 ClientAreaWidth, u32 ClientAreaHeight)
{
    local_persist v3 PlayerTranslation{ 0.0f, 1.0f, 3.0f };
    local_persist v3 Rotation{ 0.0f };

    v3 Up = { 0.0f, 1.0, 0.0f };
    v3 Forward = { 0.0f, 0.0, -1.0f };
    v3 Right = { 1.0f, 0.0, 0.0f };

    // Rotating
    {
        local_persist V2i OldMousePos;
        V2i MousePos = (Input->IsCursorLocked ? Input->VirtualMousePosition : Input->LastMousePosition);

        V2i MouseDelta = MousePos - OldMousePos;
        OldMousePos = MousePos;

        f32 MouseSensitivity = 1.0f;

        // Update rotation based on mouse input
        Rotation.y -= (f32)MouseDelta.x * MouseSensitivity * TimeStep; // Yaw
        Rotation.x -= (f32)MouseDelta.y * MouseSensitivity * TimeStep; // Pitch

        //Trace("%.3f %.3f", Rotation.y, Rotation.x);

        // Clamp pitch to avoid gimbal lock
        if (Rotation.x > bkm::Radians(89.0f))
            Rotation.x = bkm::Radians(89.0f);
        if (Rotation.x < bkm::Radians(-89.0f))
            Rotation.x = bkm::Radians(-89.0f);

        // Calculate the forward and right direction vectors
        Up = qtn(v3(Rotation.x, Rotation.y, 0.0f)) * v3(0.0f, 1.0f, 0.0f);
        Right = qtn(v3(Rotation.x, Rotation.y, 0.0f)) * v3(1.0f, 0.0f, 0.0f);
        Forward = qtn(v3(Rotation.x, Rotation.y, 0.0f)) * v3(0.0f, 0.0f, -1.0f);
    }

    //Trace("%d %d", Input->MouseDelta.x, Input->MouseDelta.y);

    v3 Direction = {};
    // Movement
    {
        f32 Speed = 5.0f;
        if (Input->W)
        {
            Direction += v3(Forward.x, 0.0f, Forward.z);
        }

        if (Input->S)
        {
            Direction -= v3(Forward.x, 0.0f, Forward.z);
        }

        if (Input->A)
        {
            Direction -= Right;
        }

        if (Input->D)
        {
            Direction += Right;
        }

        if (Input->Q)
        {
            Direction += Up;
        }

        if (Input->E)
        {
            Direction -= Up;
        }

        if (bkm::Length(Direction) > 0.0f)
            Direction = bkm::Normalize(Direction);

        PlayerTranslation += Direction * Speed * TimeStep;
    }

    if (Input->MouseLeftPressed)
    {
        ray Ray;
        Ray.Origin = PlayerTranslation;
        Ray.Direction = Forward; // Forward is already normalized
        bool PlaceBlock = false;

        v3 HitPoint;
        v3 HitNormal;
        block HitBlock;
        if (FindFirstHit(Ray, Game->Blocks, HitPoint, HitNormal, HitBlock))
        {
            block NewBlock;
            NewBlock.Translation = HitBlock.Translation + HitNormal;
            NewBlock.Texture = Game->ContainerTexture;
            Game->Blocks.push_back(NewBlock);
        }
    }

    // Update camera
    {
        Game->Camera.View = bkm::Translate(m4(1.0f), PlayerTranslation)
            * bkm::ToM4(qtn(Rotation));

        Game->Camera.View = bkm::Inverse(Game->Camera.View);
        Game->Camera.RecalculateProjectionPerspective(ClientAreaWidth, ClientAreaHeight);
        //Game->Camera.RecalculateProjectionOrtho(ClientAreaWidth, ClientAreaHeight);
    }

    // Render
    GameRendererSetViewProjection(Renderer, Game->Camera.GetViewProjection());
    for (auto& Block : Game->Blocks)
    {
        if (Block.Texture.Resource)
        {
            GameRendererSubmitCube(Renderer, Block.Translation, v3(0.0f), Block.Scale, Block.Texture, Block.Color);
        }
        else
        {
            GameRendererSubmitCube(Renderer, Block.Translation, v3(0.0f), Block.Scale, Block.Color);
        }
    }

    for (auto& Block : Game->Intersections)
    {
        if (Block.Texture.Resource)
        {
            GameRendererSubmitCube(Renderer, Block.Translation, v3(0.0f), Block.Scale, Block.Texture, Block.Color);
        }
        else
        {
            GameRendererSubmitCube(Renderer, Block.Translation, v3(0.0f), Block.Scale, Block.Color);
        }
    }

    // TODO: HUD
    // Render crosshair
    {
        v3 Dest = PlayerTranslation + Forward * 3.0f;
        GameRendererSubmitQuad(Renderer, Dest, Rotation, v2(0.1f, 0.1f), Game->CrosshairTexture, v4(0.0f, 0.0f, 0.0f, 0.4f), draw_layer::Second);
    }
}
