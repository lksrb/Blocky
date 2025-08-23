#include "GameRenderer.h"

internal game_renderer* game_renderer_create(arena* Arena)
{
    game_renderer* Renderer = arena_new(Arena, game_renderer);

    // Quad
    {
        auto& Quad = Renderer->Quad;
        Quad.VertexDataBase = arena_new_array(Arena, quad_vertex, c_MaxQuadVertices);
        Quad.VertexDataPtr = Renderer->Quad.VertexDataBase;
    }

    // Cuboid
    {
        auto& Cuboid = Renderer->Cuboid;
        Cuboid.InstanceData = arena_new_array(Arena, cuboid_transform_vertex_data, c_MaxCubePerBatch);
    }

    // Quaded cuboid
    {
        auto& QuadedCuboid = Renderer->QuadedCuboid;
        QuadedCuboid.VertexDataBase = arena_new_array(Arena, quaded_cuboid_vertex, c_MaxQuadedCuboidVertices);
        QuadedCuboid.VertexDataPtr = QuadedCuboid.VertexDataBase;
    }

    // HUD
    {
        auto& HUD = Renderer->HUD;
        HUD.VertexDataBase = arena_new_array(Arena, hud_quad_vertex, c_MaxHUDQuadVertices);;
        HUD.VertexDataPtr = HUD.VertexDataBase;
        // TODO: Maybe our own index buffer?
    }

    // Distant quads
    {
        auto& DistantQuad = Renderer->DistantQuad;
        DistantQuad.VertexDataBase = arena_new_array(Arena, distant_quad_vertex, c_MaxHUDQuadVertices);
        DistantQuad.VertexDataPtr = DistantQuad.VertexDataBase;
    }

    return Renderer;
}

internal void game_renderer_destroy(game_renderer* Renderer)
{
    // TBD
}

internal void game_renderer_clear(game_renderer* Renderer)
{
    // Reset light environment
    Renderer->LightEnvironment.clear();

    // Reset indices
    Renderer->Quad.IndexCount = 0;
    Renderer->Quad.VertexDataPtr = Renderer->Quad.VertexDataBase;

    // HUD
    Renderer->HUD.IndexCount = 0;
    Renderer->HUD.VertexDataPtr = Renderer->HUD.VertexDataBase;

    Renderer->QuadedCuboid.VertexDataPtr = Renderer->QuadedCuboid.VertexDataBase;
    Renderer->QuadedCuboid.IndexCount = 0;

    // Distant quads
    Renderer->DistantQuad.IndexCount = 0;
    Renderer->DistantQuad.VertexDataPtr = Renderer->DistantQuad.VertexDataBase;

    // Reset Cuboid indices
    Renderer->Cuboid.InstanceCount = 0;

    // Reset textures
    Renderer->CurrentTextureStackIndex = 1;

    for (u32 i = 1; i < Renderer->CurrentTextureStackIndex; i++)
    {
        Renderer->TextureStack[i] = {};
    }
}

internal void game_renderer_set_render_data(game_renderer* Renderer, const v3& CameraPosition, const m4& View, const m4& Projection, const m4& InverseView, const m4& HUDProjection, f32 Time, m4 LightSpaceMatrixTemp)
{
    auto& RenderData = Renderer->RenderData;
    RenderData.CuboidBuffer.View = View;
    RenderData.CuboidBuffer.ViewProjection = Projection * View;
    RenderData.Projection = Projection;
    RenderData.CameraPosition = CameraPosition;
    RenderData.HUDBuffer.Projection = HUDProjection;
    RenderData.SkyboxBuffer.InverseViewProjection = bkm::Inverse(RenderData.CuboidBuffer.ViewProjection);
    RenderData.SkyboxBuffer.Time = Time;

    // Distants quads
    RenderData.DistantObjectBuffer.ViewProjectionNoTranslation = Projection * m4(m3(View)); // Remove translation

    // Shadow pass
    Renderer->RenderData.CuboidBuffer.LightSpaceMatrix = LightSpaceMatrixTemp;
    RenderData.ShadowPassBuffer.LightSpaceMatrix = LightSpaceMatrixTemp;
}

internal void game_renderer_submit_quad(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v2& Scale, const v4& Color)
{
    Assert(Renderer->Quad.IndexCount < c_MaxQuadIndices, "Renderer->QuadIndexCount < c_MaxQuadIndices");

    m4 Transform = bkm::Translate(m4(1.0f), Translation)
        * bkm::ToM4(qtn(Rotation))
        * bkm::Scale(m4(1.0f), v3(Scale, 1.0f));

    v2 Coords[4];
    Coords[0] = { 0.0f, 0.0f };
    Coords[1] = { 1.0f, 0.0f };
    Coords[2] = { 1.0f, 1.0f };
    Coords[3] = { 0.0f, 1.0f };

    for (u32 i = 0; i < CountOf(c_QuadVertexPositions); i++)
    {
        Renderer->Quad.VertexDataPtr->Position = Transform * c_QuadVertexPositions[i];
        Renderer->Quad.VertexDataPtr->Color = Color;
        Renderer->Quad.VertexDataPtr->TexCoord = Coords[i];
        Renderer->Quad.VertexDataPtr->TexIndex = 0;
        Renderer->Quad.VertexDataPtr++;
    }

    Renderer->Quad.IndexCount += 6;
}

internal void game_renderer_submit_quad(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v2& Scale, const texture* Texture, const v4& Color)
{
    Assert(Renderer->Quad.IndexCount < c_MaxQuadIndices, "Renderer->QuadIndexCount < c_MaxQuadIndices");
    Assert(Texture != nullptr, "Texture is invalid!");

    // TODO: ID system, pointers are unreliable
    u32 TextureIndex = 0;
    for (u32 i = 1; i < Renderer->CurrentTextureStackIndex; i++)
    {
        if (Renderer->TextureStack[i] == Texture)
        {
            TextureIndex = i;
            break;
        }
    }

    if (TextureIndex == 0)
    {
        Assert(Renderer->CurrentTextureStackIndex < c_MaxTexturesPerDrawCall, "Renderer->TextureStackIndex < c_MaxTexturesPerDrawCall");
        TextureIndex = Renderer->CurrentTextureStackIndex;
        Renderer->TextureStack[TextureIndex] = Texture;
        Renderer->CurrentTextureStackIndex++;
    }

    m4 Transform = bkm::Translate(m4(1.0f), Translation)
        * bkm::ToM4(qtn(Rotation))
        * bkm::Scale(m4(1.0f), v3(Scale, 1.0f));

    v2 Coords[4];
    Coords[0] = { 0.0f, 0.0f };
    Coords[1] = { 1.0f, 0.0f };
    Coords[2] = { 1.0f, 1.0f };
    Coords[3] = { 0.0f, 1.0f };

    for (u32 i = 0; i < CountOf(c_QuadVertexPositions); i++)
    {
        Renderer->Quad.VertexDataPtr->Position = Transform * c_QuadVertexPositions[i];
        Renderer->Quad.VertexDataPtr->Color = Color;
        Renderer->Quad.VertexDataPtr->TexCoord = Coords[i];
        Renderer->Quad.VertexDataPtr->TexIndex = TextureIndex;
        Renderer->Quad.VertexDataPtr++;
    }

    Renderer->Quad.IndexCount += 6;
}

internal void game_renderer_submit_distant_quad(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const texture* Texture, const v4& Color)
{
    m4 Transform = bkm::Translate(m4(1.0f), Translation) * bkm::ToM4(qtn(Rotation));

    v2 Coords[4];
    Coords[0] = { 0.0f, 0.0f };
    Coords[1] = { 1.0f, 0.0f };
    Coords[2] = { 1.0f, 1.0f };
    Coords[3] = { 0.0f, 1.0f };

    for (u32 i = 0; i < CountOf(c_QuadVertexPositions); i++)
    {
        Renderer->DistantQuad.VertexDataPtr->Position = Transform * c_QuadVertexPositions[i];
        Renderer->DistantQuad.VertexDataPtr++;
    }

    Renderer->DistantQuad.IndexCount += 6;
}

internal void game_renderer_submit_billboard_quad(game_renderer* Renderer, const v3& Translation, const v2& Scale, const texture* Texture, const v4& Color)
{
    m4& CameraView = Renderer->RenderData.CuboidBuffer.View;

    v3 CamRightWS(CameraView[0][0], CameraView[1][0], CameraView[2][0]);
    v3 CamUpWS(CameraView[0][1], CameraView[1][1], CameraView[2][1]);

    v3 Positions[4];
    Positions[0] = Translation + CamRightWS * c_QuadVertexPositions[0].x * Scale.x + CamUpWS * c_QuadVertexPositions[0].y * Scale.y;
    Positions[1] = Translation + CamRightWS * c_QuadVertexPositions[1].x * Scale.x + CamUpWS * c_QuadVertexPositions[1].y * Scale.y;
    Positions[2] = Translation + CamRightWS * c_QuadVertexPositions[2].x * Scale.x + CamUpWS * c_QuadVertexPositions[2].y * Scale.y;
    Positions[3] = Translation + CamRightWS * c_QuadVertexPositions[3].x * Scale.x + CamUpWS * c_QuadVertexPositions[3].y * Scale.y;

    game_renderer_submit_quad_custom(Renderer, Positions, Texture, Color);
}

internal void game_renderer_submit_quad_custom(game_renderer* Renderer, v3 VertexPositions[4], const texture* Texture, const v4& Color)
{
    Assert(Renderer->Quad.IndexCount < c_MaxQuadIndices, "Renderer->QuadIndexCount < c_MaxQuadIndices");
    Assert(Texture != nullptr, "Texture handle is nullptr!");

    // TODO: ID system, pointers are unreliable
    u32 TextureIndex = 0;
    for (u32 i = 1; i < Renderer->CurrentTextureStackIndex; i++)
    {
        if (Renderer->TextureStack[i] == Texture)
        {
            TextureIndex = i;
            break;
        }
    }

    if (TextureIndex == 0)
    {
        Assert(Renderer->CurrentTextureStackIndex < c_MaxTexturesPerDrawCall, "Renderer->TextureStackIndex < c_MaxTexturesPerDrawCall");
        TextureIndex = Renderer->CurrentTextureStackIndex;
        Renderer->TextureStack[TextureIndex] = Texture;
        Renderer->CurrentTextureStackIndex++;
    }

    v2 Coords[4];
    Coords[0] = { 0.0f, 0.0f };
    Coords[1] = { 1.0f, 0.0f };
    Coords[2] = { 1.0f, 1.0f };
    Coords[3] = { 0.0f, 1.0f };

    for (u32 i = 0; i < 4; i++)
    {
        Renderer->Quad.VertexDataPtr->Position = v4(VertexPositions[i], 1.0f);
        Renderer->Quad.VertexDataPtr->Color = Color;
        Renderer->Quad.VertexDataPtr->TexCoord = Coords[i];
        Renderer->Quad.VertexDataPtr->TexIndex = TextureIndex;
        Renderer->Quad.VertexDataPtr++;
    }

    Renderer->Quad.IndexCount += 6;
}

internal void game_renderer_submit_cuboid(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v3& Scale, const texture* Texture, const v4& Color)
{
    Assert(Renderer->Cuboid.InstanceCount < c_MaxCubePerBatch, "Renderer->CuboidInstanceCount < c_MaxCuboidsPerBatch");

    u32 TextureIndex = 0;
    for (u32 i = 1; i < Renderer->CurrentTextureStackIndex; i++)
    {
        if (Renderer->TextureStack[i] == Texture)
        {
            TextureIndex = i;
            break;
        }
    }

    if (TextureIndex == 0)
    {
        Assert(Renderer->CurrentTextureStackIndex < c_MaxTexturesPerDrawCall, "Renderer->TextureStackIndex < c_MaxTexturesPerDrawCall");
        TextureIndex = Renderer->CurrentTextureStackIndex;
        Renderer->TextureStack[TextureIndex] = Texture;
        Renderer->CurrentTextureStackIndex++;
    }

    auto& Cuboid = Renderer->Cuboid.InstanceData[Renderer->Cuboid.InstanceCount];

#if ENABLE_SIMD
    XMMATRIX XmmScale = XMMatrixScalingFromVector(XMVectorSet(Scale.x, Scale.y, Scale.z, 0.0f));
    XMMATRIX XmmRot = XMMatrixRotationQuaternion(XMQuaternionRotationRollPitchYaw(Rotation.x, Rotation.y, Rotation.z));
    XMMATRIX XmmTrans = XMMatrixTranslationFromVector(XMVectorSet(Translation.x, Translation.y, Translation.z, 0.0f));
    XMMATRIX XmmTransform = XmmScale * XmmRot * XmmTrans;

    Cuboid.Color = Color;
    Cuboid.XmmTransform = XmmTransform;
    Cuboid.TextureIndex = TextureIndex;

    Renderer->Cuboid.InstanceCount++;
#else
    m4 Transform = bkm::Translate(m4(1.0f), Translation)
        * bkm::ToM4(qtn(Rotation))
        * bkm::Scale(m4(1.0f), Scale);

    Cuboid.Color = Color;
    Cuboid.Transform = Transform;
    Cuboid.TextureIndex = TextureIndex;
    Renderer->Cuboid.InstanceCount++;
#endif
}

internal void game_renderer_submit_cuboid(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v3& Scale, const v4& Color)
{
    Assert(Renderer->Cuboid.InstanceCount < c_MaxCubePerBatch, "Renderer->CuboidInstanceCount < c_MaxCuboidsPerBatch");

    auto& Cuboid = Renderer->Cuboid.InstanceData[Renderer->Cuboid.InstanceCount];

#if ENABLE_SIMD
    XMMATRIX XmmScale = XMMatrixScalingFromVector(XMVectorSet(Scale.x, Scale.y, Scale.z, 0.0f));
    XMMATRIX XmmRot = XMMatrixRotationQuaternion(XMQuaternionRotationRollPitchYaw(Rotation.x, Rotation.y, Rotation.z));
    XMMATRIX XmmTrans = XMMatrixTranslationFromVector(XMVectorSet(Translation.x, Translation.y, Translation.z, 0.0f));

    XMMATRIX XmmTransform = XmmScale * XmmRot * XmmTrans;

    Cuboid.Color = Color;
    Cuboid.XmmTransform = XmmTransform;
    Cuboid.TextureIndex = 0;
    Renderer->Cuboid.InstanceCount++;
#else
    m4 Transform = bkm::Translate(m4(1.0f), Translation)
        * bkm::ToM4(qtn(Rotation))
        * bkm::Scale(m4(1.0f), Scale);

    Cuboid.Color = Color;
    Cuboid.Transform = Transform;
    Cuboid.TextureIndex = 0;
    Renderer->Cuboid.InstanceCount++;
#endif
}

internal void game_renderer_submit_cuboid(game_renderer* Renderer, const v3& Translation, const texture* Texture, const v4& Color, f32 Emission)
{
    Assert(Renderer->Cuboid.InstanceCount < c_MaxCubePerBatch, "Renderer->CuboidInstanceCount < c_MaxCuboidsPerBatch");

    u32 TextureIndex = 0;

    if (Texture != nullptr)
    {
        for (u32 i = 1; i < Renderer->CurrentTextureStackIndex; i++)
        {
            if (Renderer->TextureStack[i] == Texture)
            {
                TextureIndex = i;
                break;
            }
        }

        if (TextureIndex == 0)
        {
            Assert(Renderer->CurrentTextureStackIndex < c_MaxTexturesPerDrawCall, "Renderer->TextureStackIndex < c_MaxTexturesPerDrawCall");
            TextureIndex = Renderer->CurrentTextureStackIndex;
            Renderer->TextureStack[TextureIndex] = Texture;
            Renderer->CurrentTextureStackIndex++;
        }
    }

    auto& Cuboid = Renderer->Cuboid.InstanceData[Renderer->Cuboid.InstanceCount];

    // TODO: We can do better by simply copying data and then calculate everything at one swoop
#if ENABLE_SIMD
    Cuboid.XmmTransform = XMMatrixTranslationFromVector(XMVectorSet(Translation.x, Translation.y, Translation.z, 0.0f));
#else
    Cuboid.Transform = bkm::Translate(m4(1.0f), Translation);
#endif

    Cuboid.Color = Color;
    Cuboid.TextureIndex = TextureIndex;
    Cuboid.Emission = Emission;
    Renderer->Cuboid.InstanceCount++;
}

internal void game_renderer_submit_cuboid(game_renderer* Renderer, const v3& Translation, const v4& Color)
{
    Assert(Renderer->Cuboid.InstanceCount < c_MaxCubePerBatch, "Renderer->CuboidInstanceCount < c_MaxCuboidsPerBatch");

    auto& Cuboid = Renderer->Cuboid.InstanceData[Renderer->Cuboid.InstanceCount];

#if ENABLE_SIMD
    Cuboid.XmmTransform = XMMatrixTranslation(Translation.x, Translation.y, Translation.z);
#else
    Cuboid.Transform = bkm::Translate(m4(1.0f), Translation);
#endif

    Cuboid.Color = Color;
    Cuboid.TextureIndex = 0;
    Renderer->Cuboid.InstanceCount++;
}

internal void game_renderer_submit_quaded_cuboid(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v3& Scale, const texture* Texture, const texture_block_coords& TextureCoords, const v4& Color)
{
    m4 Transform = bkm::Translate(m4(1.0f), Translation)
        * bkm::ToM4(qtn(Rotation))
        * bkm::Scale(m4(1.0f), Scale);

    game_renderer_submit_quaded_cuboid(Renderer, Transform, Texture, TextureCoords, Color);
}

internal void game_renderer_submit_quaded_cuboid(game_renderer* Renderer, const m4& Transform, const texture* Texture, const texture_block_coords& TextureCoords, const v4& Color)
{
    Assert(Renderer->QuadedCuboid.IndexCount < bkm::Max(c_MaxQuadIndices, c_MaxQuadedCuboidIndices), "Renderer->QuadedCuboidIndexCount < bkm::Max(c_MaxQuadIndices, c_MaxQuadedCuboidIndices)");
    Assert(Texture != nullptr, "Texture is invalid!");

    // TODO: ID system, pointers are unreliable
    u32 TextureIndex = 0;
    for (u32 i = 1; i < Renderer->CurrentTextureStackIndex; i++)
    {
        if (Renderer->TextureStack[i] == Texture)
        {
            TextureIndex = i;
            break;
        }
    }

    if (TextureIndex == 0)
    {
        Assert(Renderer->CurrentTextureStackIndex < c_MaxTexturesPerDrawCall, "Renderer->TextureStackIndex < c_MaxTexturesPerDrawCall");
        TextureIndex = Renderer->CurrentTextureStackIndex;
        Renderer->TextureStack[TextureIndex] = Texture;
        Renderer->CurrentTextureStackIndex++;
    }

    m3 InversedTransposedMatrix = m3(bkm::Transpose(bkm::Inverse(Transform)));

    u32 j = 0;
    for (u32 i = 0; i < CountOf(c_CuboidVertices); i++)
    {
        Renderer->QuadedCuboid.VertexDataPtr->Position = Transform * c_CuboidVertices[i].Position;
        Renderer->QuadedCuboid.VertexDataPtr->Normal = InversedTransposedMatrix * c_CuboidVertices[i].Normal;
        Renderer->QuadedCuboid.VertexDataPtr->Color = Color;
        Renderer->QuadedCuboid.VertexDataPtr->TexCoord = TextureCoords.TextureCoords[j].Coords[i % 4];
        Renderer->QuadedCuboid.VertexDataPtr->TexIndex = TextureIndex;
        Renderer->QuadedCuboid.VertexDataPtr++;

        // Each quad is 4 indexed vertices, so we 
        if (i != 0 && (i + 1) % 4 == 0)
        {
            j++;
        }
    }

    Renderer->QuadedCuboid.IndexCount += 36;
}

internal void game_renderer_submit_directional_light(game_renderer* Renderer, const v3& Direction, f32 Intensity, const v3& Radiance)
{
    directional_light& DirLight = Renderer->LightEnvironment.emplace_directional_light();
    DirLight.Direction = Direction;
    DirLight.Intensity = Intensity;
    DirLight.Radiance = Radiance;
}

internal void game_renderer_submit_point_light(game_renderer* Renderer, const v3& Position, f32 Radius, f32 FallOff, const v3& Radiance, f32 Intensity)
{
    point_light& Light = Renderer->LightEnvironment.emplace_point_light();
    Light.Position = Position;
    Light.Radius = Radius;
    Light.FallOff = FallOff;
    Light.Radiance = Radiance;
    Light.Intensity = Intensity;
}

// ===============================================================================================================
//                                             RENDERER API - HUD                                             
// ===============================================================================================================

internal void game_renderer_submit_hud_quad(game_renderer* Renderer, v3 VertexPositions[4], const texture* Texture, const texture_coords& Coords, const v4& Color)
{
    Assert(Renderer->HUD.IndexCount < c_MaxHUDQuadIndices, "Renderer->HUDQuadIndexCount < c_MaxHUDQuadIndices");
    Assert(Texture != nullptr, "Texture is invalid!");

    // TODO: ID system, pointers are unreliable
    u32 TextureIndex = 0;
    for (u32 i = 1; i < Renderer->CurrentTextureStackIndex; i++)
    {
        if (Renderer->TextureStack[i] == Texture)
        {
            TextureIndex = i;
            break;
        }
    }

    if (TextureIndex == 0)
    {
        Assert(Renderer->CurrentTextureStackIndex < c_MaxTexturesPerDrawCall, "Renderer->CurrentTextureStackIndex < c_MaxTexturesPerDrawCall");
        TextureIndex = Renderer->CurrentTextureStackIndex;
        Renderer->TextureStack[TextureIndex] = Texture;
        Renderer->CurrentTextureStackIndex++;
    }

    for (u32 i = 0; i < 4; i++)
    {
        Renderer->HUD.VertexDataPtr->Position = v4(VertexPositions[i], 1.0f);
        Renderer->HUD.VertexDataPtr->Color = Color;
        Renderer->HUD.VertexDataPtr->TexCoord = Coords[i];
        Renderer->HUD.VertexDataPtr->TexIndex = TextureIndex;
        Renderer->HUD.VertexDataPtr++;
    }

    Renderer->HUD.IndexCount += 6;
}
