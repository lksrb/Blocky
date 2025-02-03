#pragma once

struct camera
{
    m4 Projection{ 1.0f };
    m4 View{ 1.0f };

    f32 OrthographicSize = 15.0f;
    f32 OrthographicNear = -100.0f, OrthographicFar = 100.0f;

    f32 AspectRatio = 0.0f;

    f32 PerspectiveFOV = bkm::PI_HALF;
    f32 PerspectiveNear = 0.01f, PerspectiveFar = 1000.0f;

    void RecalculateProjectionOrtho(u32 Width, u32 Height)
    {
        AspectRatio = static_cast<f32>(Width) / Height;
        f32 OrthoLeft = -0.5f * AspectRatio * OrthographicSize;
        f32 OrthoRight = 0.5f * AspectRatio * OrthographicSize;
        f32 OrthoBottom = -0.5f * OrthographicSize;
        f32 OrthoTop = 0.5f * OrthographicSize;
        Projection = bkm::Ortho(OrthoLeft, OrthoRight, OrthoBottom, OrthoTop, OrthographicNear, OrthographicFar);
    }

    void RecalculateProjectionPerspective(u32 width, u32 height)
    {
        AspectRatio = static_cast<f32>(width) / height;
        Projection = bkm::Perspective(PerspectiveFOV, AspectRatio, PerspectiveNear, PerspectiveFar);
    }

    m4 GetViewProjection() const { return Projection * View; }
};

static void GameUpdateAndRender(dx12_game_renderer* Renderer, game_input* Input, u32 ClientAreaWidth, u32 ClientAreaHeight)
{
    camera Camera;
    {
        v3 Translation{ 0.0f, 1.0f, 3.0f };
        v3 Rotation{ -0.2f };
        v3 Scale{ 1.0f };

        Camera.View = bkm::Translate(m4(1.0f), Translation)
            * bkm::ToM4(qtn(Rotation))
            * bkm::Scale(m4(1.0f), Scale);

        Camera.View = bkm::Inverse(Camera.View);

        Camera.RecalculateProjectionPerspective(ClientAreaWidth, ClientAreaHeight);

        if (Input->W)
        {
            Trace("lajfkals;jkfslkj");
        }
    }

    SetViewProjection(Renderer, Camera.GetViewProjection());
    SubmitCube(Renderer, v3(0), v3(0), v3(1.0f), v4(1.0f, 0.0f, 0.0f, 1.0f));
    SubmitCube(Renderer, v3(1, 0, 0), v3(0), v3(1.0f), v4(1.0f, 0.0f, 0.0f, 1.0f));
    SubmitCube(Renderer, v3(2, 0, 0), v3(0), v3(1.0f), v4(1.0f, 1.0f, 0.0f, 1.0f));
    SubmitCube(Renderer, v3(3, 0, 0), v3(0), v3(1.0f), v4(1.0f, 0.0f, 1.0f, 1.0f));
}
