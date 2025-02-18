#pragma once

#include <vector>

struct camera
{
    m4 Projection{ 1.0f };
    m4 View{ 1.0f };

    f32 OrthographicSize = 15.0f;
    f32 OrthographicNear = -100.0f, OrthographicFar = 100.0f;

    f32 AspectRatio = 0.0f;

    f32 PerspectiveFOV = bkm::PI_HALF;
    f32 PerspectiveNear = 0.1f, PerspectiveFar = 1000.0f;

    void RecalculateProjectionOrtho(u32 Width, u32 Height)
    {
        AspectRatio = static_cast<f32>(Width) / Height;
        f32 OrthoLeft = -0.5f * AspectRatio * OrthographicSize;
        f32 OrthoRight = 0.5f * AspectRatio * OrthographicSize;
        f32 OrthoBottom = -0.5f * OrthographicSize;
        f32 OrthoTop = 0.5f * OrthographicSize;
        Projection = bkm::Ortho(OrthoLeft, OrthoRight, OrthoBottom, OrthoTop, OrthographicNear, OrthographicFar);
    }

    void RecalculateProjectionOrtho_V2(u32 Width, u32 Height)
    {
        Projection = bkm::Ortho(0, Width, Height, 0, -1.0f, 1.0f);
    }

    void RecalculateProjectionPerspective(u32 Width, u32 Height)
    {
        AspectRatio = static_cast<f32>(Width) / Height;
        Projection = bkm::Perspective(PerspectiveFOV, AspectRatio, PerspectiveNear, PerspectiveFar);
    }

    m4 GetViewProjection() const { return Projection * View; }
};

struct ray
{
    v3 Origin;
    v3 Direction;
};

struct aabb
{
    v3 Min;
    v3 Max;
};

struct block
{
    v3 Translation = {};
    v3 Scale = v3(1.0f);
    texture Texture = {};
    v4 Color = v4(1.0f);
};

struct game
{
    camera Camera;

    std::vector<block> Blocks;
    std::vector<block> Intersections;

    texture CrosshairTexture;
    texture TestTexture;
    texture ContainerTexture;
};

internal game GameCreate(game_renderer* Renderer);
internal void GameUpdateAndRender(game* Game, game_renderer* Renderer, const game_input* Input, f32 TimeStep, u32 ClientAreaWidth, u32 ClientAreaHeight);
