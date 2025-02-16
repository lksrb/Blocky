#include "Blocky.h"

static bool RayIntersectsAABB(const ray& Ray, const aabb& Box, f32& Near, f32& Far)
{
    f32 Min = -FLT_MAX;
    f32 Max = FLT_MAX;
    for (u32 i = 0; i < 3; ++i)
    {
        f32 invD = 1.0f / (i == 0 ? Ray.Direction.x : (i == 1 ? Ray.Direction.y : Ray.Direction.z));
        f32 t0 = ((i == 0 ? Box.Min.x : (i == 1 ? Box.Min.y : Box.Min.z)) - (i == 0 ? Ray.Origin.x : (i == 1 ? Ray.Origin.y : Ray.Origin.z))) * invD;
        f32 t1 = ((i == 0 ? Box.Max.x : (i == 1 ? Box.Max.y : Box.Max.z)) - (i == 0 ? Ray.Origin.x : (i == 1 ? Ray.Origin.y : Ray.Origin.z))) * invD;

        if (invD < 0.0f) std::swap(t0, t1);

        Min = std::max(Min, t0);
        Max = std::min(Max, t1);

        if (Max < Min) return false;
    }

    Near = Min;
    Far = Max;
    return true;
}

static game GameCreate(game_renderer* Renderer)
{
    game Game = {};

    Game.TestTexture = DX12TextureCreate(Renderer->Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, "Resources/Textures/LevelBackground.png");

    Game.ContainerTexture = DX12TextureCreate(Renderer->Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, "Resources/Textures/container.png");

    Game.CrosshairTexture = DX12TextureCreate(Renderer->Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, "Resources/Textures/Crosshair.png");

    auto& Block = Game.Blocks.emplace_back();
    Block.Texture = Game.ContainerTexture;
    Block.Color = v4(1.0f);

    return Game;
}

static void GameUpdateAndRender(game* Game, game_renderer* Renderer, const game_input* Input, f32 TimeStep, u32 ClientAreaWidth, u32 ClientAreaHeight)
{
    static v3 Translation{ 0.0f, 1.0f, 3.0f };
    static v3 Rotation{ 0.0f };

    v3 Up = { 0.0f, 1.0, 0.0f };
    v3 Forward = { 0.0f, 0.0, -1.0f };
    v3 Right = { 1.0f, 0.0, 0.0f };

    // Rotating
    {
        static V2i OldMousePos;
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

        if (bkm::Length(Direction) > 0.0f)
            Direction = bkm::Normalize(Direction);

        Translation += Direction * Speed * TimeStep;
    }

    // Place a block
    if (Input->MouseLeftPressed && false)
    {
        // Raycast from LIDL
        // TODO: Proper raycast
        if (0)
        {
            for (auto& Block : Game->Blocks)
            {
                v3 Src = Translation;
                v3 Dest = Src + Forward * 3.0f;

                // Sphere collision
                if (bkm::Length(Dest - Block.Translation) < 0.5f)
                {
                    Block.Color = v4(0.6f, 0.6f, 0.6f, 1.0f);
                }
                else
                {
                    Block.Color = v4(1.0f);
                }
            }
        }
        else
        {

        }

        //v3 Dest = Translation + Forward * 1.0f + Right * 2.0f + Up * -1.0f;
        v3 Dest = Translation + Forward * 3.0f;

        auto& Block = Game->Blocks.emplace_back();
        Block.Translation = Dest;
        Block.Texture = Game->ContainerTexture;
        Block.Color = v4(1.0f);
    }

    if (Input->MouseLeftPressed)
    {
        // Slab method raycast
        {
            ray Ray;
            Ray.Origin = Translation;
            Ray.Direction = bkm::Normalize(Forward);
            for (auto& Block : Game->Blocks)
            {
                aabb Box;
                Box.Min = Block.Translation - v3(0.5f);
                Box.Max = Block.Translation + v3(0.5f);

                f32 Near, Far;
                if (RayIntersectsAABB(Ray, Box, Near, Far))
                {
                    if (Near < 3.0f)
                    {
                        v3 Intersection1 = Ray.Origin + Near * Ray.Direction;
                        v3 Intersection2 = Ray.Origin + Far * Ray.Direction;

                        auto& Block = Game->Intersections.emplace_back();
                        Block.Translation = Intersection1;
                        Block.Color = v4(1.0f);
                        Block.Scale = v3(0.1f);
                        //Block.Color = v4(0.6f, 0.6f, 0.6f, 1.0f);
                    }
                }
                else
                {
                    //Block.Color = v4(1.0f);
                }
            }
        }
    }

    // Update camera
    {
        Game->Camera.View = bkm::Translate(m4(1.0f), Translation)
            * bkm::ToM4(qtn(Rotation));

        Game->Camera.View = bkm::Inverse(Game->Camera.View);
        Game->Camera.RecalculateProjectionPerspective(ClientAreaWidth, ClientAreaHeight);
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
        v3 Dest = Translation + Forward * 3.0f;
        GameRendererSubmitQuad(Renderer, Dest, Rotation, v2(0.1f, 0.1f), v4(1.0f, 1.0f, 1.0f, 1.0f));
    }
}
