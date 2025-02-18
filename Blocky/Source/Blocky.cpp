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

internal bool FindFirstHit(const ray& Ray, const std::vector<block>& Blocks, v3* HitPoint, v3* HitNormal, block* HitBlock, u32* HitIndex)
{
    f32 ClosestT = INFINITY;
    bool FoundHit = false;

    u32 Index = 0;
    u32 BlockIndex = 0;
    for (const auto& Block : Blocks)
    {
        aabb Box;
        Box.Min = Block.Translation - v3(0.5f);
        Box.Max = Block.Translation + v3(0.5f);

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

        Index++;
    }

    return FoundHit;
}

internal game GameCreate(game_renderer* Renderer)
{
    game Game = {};

    /*
    Game.TestTexture = DX12TextureCreate(Renderer->Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, "Resources/Textures/LevelBackground.png");
    */

#if !USE_VULKAN_RENDERER
    Game.ContainerTexture = TextureCreate(Renderer->Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, "Resources/Textures/MC/dirt_2.png");

    Game.CrosshairTexture = TextureCreate(Renderer->Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, "Resources/Textures/MC/Crosshair.png");
#endif

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
        f32 Speed = 10.0f;
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

        // TODO: What happens on simultaneously press?
        if (Input->Q || Input->BackSpace || Input->Space)
        {
            Direction += v3(0.0f, 1.0f, 0.0f);;
        }

        if (Input->E || Input->Control || Input->Shift)
        {
            Direction -= v3(0.0f, 1.0f, 0.0f);;
        }

        if (bkm::Length(Direction) > 0.0f)
            Direction = bkm::Normalize(Direction);

        PlayerTranslation += Direction * Speed * TimeStep;
    }

    // Place a block
    if (Input->MouseRightPressed)
    {
        ray Ray;
        Ray.Origin = PlayerTranslation;
        Ray.Direction = Forward; // Forward is already normalized

        v3 HitPoint;
        v3 HitNormal;
        block HitBlock;
        u32 HitIndex; // Unused
        if (FindFirstHit(Ray, Game->Blocks, &HitPoint, &HitNormal, &HitBlock, &HitIndex))
        {
            block NewBlock;
            NewBlock.Translation = HitBlock.Translation + HitNormal;
            NewBlock.Texture = Game->ContainerTexture;
            Game->Blocks.push_back(NewBlock);
            Info("New block added");
        }
    }

    // Destroy a block
    if (Input->MouseLeftPressed)
    {
        ray Ray;
        Ray.Origin = PlayerTranslation;
        Ray.Direction = Forward; // Forward is already normalized

        v3 HitPoint;
        v3 HitNormal;
        block HitBlock;
        u32 HitIndex;
        if (FindFirstHit(Ray, Game->Blocks, &HitPoint, &HitNormal, &HitBlock, &HitIndex))
        {
            block NewBlock;
            NewBlock.Translation = HitBlock.Translation + HitNormal;
            NewBlock.Texture = Game->ContainerTexture;
            Game->Blocks.erase(Game->Blocks.begin() + HitIndex);
            Info("Block destroyed.");
        }
    }

    // Update camera
    {
        Game->Camera.View = bkm::Translate(m4(1.0f), PlayerTranslation)
            * bkm::ToM4(qtn(Rotation));

        Game->Camera.View = bkm::Inverse(Game->Camera.View);
        Game->Camera.RecalculateProjectionPerspective(ClientAreaWidth, ClientAreaHeight);
        //Game->Camera.RecalculateProjectionOrtho_V2(ClientAreaWidth, ClientAreaHeight);
    }

    // Render

    // Render blocks
    GameRendererSetViewProjection(Renderer, Game->Camera.GetViewProjection(), draw_layer::First);
    for (auto& Block : Game->Blocks)
    {
        if (Block.Texture.Handle)
        {
            GameRendererSubmitCube(Renderer, Block.Translation, v3(0.0f), Block.Scale, Block.Texture, Block.Color, draw_layer::First);
        }
        else
        {
            GameRendererSubmitCube(Renderer, Block.Translation, v3(0.0f), Block.Scale, Block.Color, draw_layer::First);
        }
    }

    // Render intersections
    // TODO: Delete
    for (auto& Block : Game->Intersections)
    {
        if (Block.Texture.Handle)
        {
            GameRendererSubmitCube(Renderer, Block.Translation, v3(0.0f), Block.Scale, Block.Texture, Block.Color, draw_layer::First);
        }
        else
        {
            GameRendererSubmitCube(Renderer, Block.Translation, v3(0.0f), Block.Scale, Block.Color, draw_layer::First);
        }
    }

    // HUD

    // Orthographic projection
    Game->Camera.RecalculateProjectionOrtho_V2(ClientAreaWidth, ClientAreaHeight);

    GameRendererSetViewProjection(Renderer, Game->Camera.Projection, draw_layer::Second);

    // RENDERING PIXEL PERFECT STUFF
    // RENDERING PIXEL PERFECT STUFF
    // RENDERING PIXEL PERFECT STUFF
    // RENDERING PIXEL PERFECT STUFF

    f32 crosshairSize = 15.0f * 2;
    f32 centerX = ClientAreaWidth / 2.0f - crosshairSize / 2.0f;
    f32 centerY = ClientAreaHeight / 2.0f - crosshairSize / 2.0f;

    f32 vertices[] = {
        centerX,          centerY,          0.0f, 0.0f, 0.0f,  // Top-left
        centerX + crosshairSize, centerY,          0.0f, 1.0f, 0.0f,  // Top-right
        centerX + crosshairSize, centerY + crosshairSize,  0.0f, 1.0f, 1.0f,  // Bottom-right
        centerX,          centerY + crosshairSize,  0.0f, 0.0f, 1.0f   // Bottom-left
    };

    v3 Positions[4];
    Positions[0] = { centerX, centerY, 0.0f };
    Positions[1] = { centerX + crosshairSize, centerY, 0.0f };
    Positions[2] = { centerX + crosshairSize, centerY + crosshairSize, 0.0f };
    Positions[3] = { centerX, centerY + crosshairSize, 0.0f };

    v2 Coords[4];
    Coords[0] = { 0.0f, 0.0f };
    Coords[1] = { 1.0f, 0.0f };
    Coords[2] = { 1.0f, 1.0f };
    Coords[3] = { 0.0f, 1.0f };

#if USE_VULKAN_RENDERER
    // Render crosshair
    {
       
        {
            for (u32 i = 0; i < CountOf(c_QuadVertexPositions); i++)
            {
                Renderer->QuadVertexDataPtr->Position = Positions[i];
                Renderer->QuadVertexDataPtr->Color = v4(1.0f, 1.0f, 1.0f, 0.6f);
                Renderer->QuadVertexDataPtr->TexCoord = Coords[i];
                Renderer->QuadVertexDataPtr->TextureIndex = 0;
                Renderer->QuadVertexDataPtr++;
            }

            Renderer->QuadIndexCount += 6;
        }

        //v3 Dest = PlayerTranslation;
        //GameRendererSubmitQuad(Renderer, v3(0.0f, 0.0f, 0.0f), v3(0.0f), v2(1.0f, 1.0f), Game->CrosshairTexture, v4(1.0f, 1.0f, 1.0f, 0.6f), draw_layer::Second);
    }

#else // DirectX 12 renderer

    // TODO: ID system, pointers are unreliable
    u32 TextureIndex = 0;
    for (u32 i = 1; i < Renderer->CurrentTextureStackIndex; i++)
    {
        if (Renderer->TextureStack[i].Handle == Game->CrosshairTexture.Handle)
        {
            TextureIndex = i;
            break;
        }
    }

    if (TextureIndex == 0)
    {
        Assert(Renderer->CurrentTextureStackIndex < c_MaxTexturesPerDrawCall, "Renderer->TextureStackIndex < c_MaxTexturesPerDrawCall");
        TextureIndex = Renderer->CurrentTextureStackIndex;
        Renderer->TextureStack[TextureIndex] = Game->CrosshairTexture;
        Renderer->CurrentTextureStackIndex++;
    }

    auto& DrawLayer = Renderer->QuadDrawLayers[(u32)draw_layer::Second];

    for (u32 i = 0; i < CountOf(c_QuadVertexPositions); i++)
    {
        DrawLayer.VertexDataPtr->Position = Positions[i];
        DrawLayer.VertexDataPtr->Color = v4(1.0f, 1.0f, 1.0f, 0.6f);
        DrawLayer.VertexDataPtr->TexCoord = Coords[i];
        DrawLayer.VertexDataPtr->TexIndex = TextureIndex;
        DrawLayer.VertexDataPtr++;
    }

    DrawLayer.IndexCount += 6;
#endif

}
