#include "Blocky.h"

#include <vector>

static game GameCreate(game_renderer* Renderer)
{
    game Game = {};

    Game.TestTexture = DX12TextureCreate(Renderer->Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, "Resources/Textures/LevelBackground.png");

    Game.ContainerTexture = DX12TextureCreate(Renderer->Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, "Resources/Textures/container.png");

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

    struct Block
    {
        v3 Translation;
    };

    static std::vector<v3> s_Blocks;

    // Raycast
    if (Input->MouseLeft)
    {
        v3 Dest = Translation + Forward * 1.0f + Right * 2.0f + Up * -1.0f;
        //GameRendererSubmitCube(Renderer, Dest, Rotation, v3(1.0f), Game->ContainerTexture, v4(1.0f, 1.0f, 1.0f, 1.0f));
    }

    if (Input->MouseLeftPressed)
    {
        v3 Dest = Translation + Forward * 3.0f;
        s_Blocks.push_back(Dest);
    }

    // Update camera
    Game->Camera.View = bkm::Translate(m4(1.0f), Translation)
        * bkm::ToM4(qtn(Rotation));

    Game->Camera.View = bkm::Inverse(Game->Camera.View);

    Game->Camera.RecalculateProjectionPerspective(ClientAreaWidth, ClientAreaHeight);

    // Render stuff
    for (auto& block : s_Blocks)
    {
        GameRendererSubmitCube(Renderer, block, v3(0.0f), v3(1.0f), Game->ContainerTexture, v4(1.0f, 1.0f, 1.0f, 1.0f));
    }

    GameRendererSetViewProjection(Renderer, Game->Camera.GetViewProjection());

    static f32 Y = 0.0f;
    static f32 Time = 0.0f;
    Time += TimeStep;

    Y = bkm::Sin(Time);
    GameRendererSubmitCube(Renderer, v3(0), v3(0), v3(1.0f), v4(0.0f, 1.0f, 0.0f, 1.0f));
    GameRendererSubmitCube(Renderer, v3(1, Y, 0), v3(0), v3(1.0f), Game->TestTexture, v4(1.0f, 0.0f, 0.0f, 1.0f));
    GameRendererSubmitCube(Renderer, v3(3, 0, 0), v3(0), v3(1.0f), v4(1.0f, 1.0f, 1.0f, 1.0f));
}
