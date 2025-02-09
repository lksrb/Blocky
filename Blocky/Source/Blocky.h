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

static void GameUpdateAndRender(game_renderer* Renderer, const game_input* Input, f32 TimeStep, u32 ClientAreaWidth, u32 ClientAreaHeight)
{
    camera Camera;
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

        // Raycast
        {
            v3 Dest = Translation + Forward * 5.0f;
            if (Input->MouseLeft)
            {
                GameRendererSubmitCube(Renderer, Dest, v3(0), v3(1.0f), v4(0.0f, 1.0f, 1.0f, 1.0f));
            }
        }

        Camera.View = bkm::Translate(m4(1.0f), Translation)
            * bkm::ToM4(qtn(Rotation));

        Camera.View = bkm::Inverse(Camera.View);

        Camera.RecalculateProjectionPerspective(ClientAreaWidth, ClientAreaHeight);
    }

    GameRendererSetViewProjection(Renderer, Camera.GetViewProjection());

    static f32 Y = 0.0f;
    static f32 Time = 0.0f;
    Time += TimeStep;

    Y = bkm::Sin(Time);

    GameRendererSubmitCube(Renderer, v3(0), v3(0), v3(1.0f), v4(0.0f, 1.0f, 0.0f, 1.0f));
    GameRendererSubmitCube(Renderer, v3(1, Y, 0), v3(0), v3(1.0f), v4(1.0f, 0.0f, 0.0f, 1.0f));
    GameRendererSubmitCube(Renderer, v3(2, 0, 0), v3(0), v3(1.0f), v4(1.0f, 1.0f, 0.0f, 1.0f));
    GameRendererSubmitCube(Renderer, v3(3, 0, 0), v3(0), v3(1.0f), v4(1.0f, 1.0f, 1.0f, 1.0f));
}
