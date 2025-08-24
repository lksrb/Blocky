#include "Game.h"

#include "GameRenderer.cpp"

#include "Cow.h"

#include <cmath>

struct texture_rect
{
    u8 X, Y, Width, Height, RotationCount; // TODO: Can we can rid of rotation count?

    texture_rect() = default;

    texture_rect(u8 x, u8 y, u8 w, u8 h, u8 r)
        : X(x), Y(y), Width(w), Height(h), RotationCount(r)
    {}
};

// IDEA: We can keep the pixel coords on the texture and calculate them in runtime
struct texture_rects
{
    texture_rects() = default;

    texture_rects(texture_rect front, texture_rect back,
                  texture_rect left, texture_rect right,
                  texture_rect top, texture_rect bottom)
        : Front(front), Back(back),
        Left(left), Right(right),
        Top(top), Bottom(bottom)
    {}

    union
    {
        struct
        {
            texture_rect Front;
            texture_rect Back;
            texture_rect Left;
            texture_rect Right;
            texture_rect Top;
            texture_rect Bottom;
        };

        texture_rect Data[6];
    };
};

internal void AddModelPart(entity_model* Model, v3 LocalPosition, v3 Size, texture_rects Rects)
{
    auto& Part = Model->Parts[Model->PartsCount++];
    texture_block_coords TextureCoords;

    i32 i = 0;
    for (auto& Rect : Rects.Data)
    {
        TextureCoords.TextureCoords[i++] = get_texture_coords(Rect.X, Rect.Y, Rect.Width, Rect.Height, Rect.RotationCount);
    }

    Part.Coords = TextureCoords;
    Part.LocalPosition = LocalPosition * c_TexelSize;
    Part.Size = Size * c_TexelSize;
}

internal game* game_create(arena* Arena, render_backend* Backend)
{
    game* Game = arena_new(Arena, game);

    {
        Game->BlocksCount = RowCount * ColumnCount * LayerCount;
        Game->Blocks = arena_new_array(Arena, block, Game->BlocksCount);
        Game->GenSeries = random_series_create();
        game_generate_world(Game, Game->Blocks, Game->BlocksCount);
    }

    // Crosshair texture will be separate from block textures
    Game->CrosshairTexture = texture_create(Backend, "Resources/Textures/MC/Crosshair.png");
    Game->BlockTextures[(u32)block_type::Dirt] = texture_create(Backend, "Resources/Textures/MC/dirt_2.png");
    Game->BlockTextures[(u32)block_type::GlowStone] = texture_create(Backend, "Resources/Textures/glowstone.png");
    Game->BlockTextures[(u32)block_type::Stone] = texture_create(Backend, "Resources/Textures/stone.png");
    Game->BlockTextures[(u32)block_type::Bedrock] = texture_create(Backend, "Resources/Textures/bedrock.png");
    Game->BlockTextures[(u32)block_type::Grass] = texture_create(Backend, "Resources/Textures/grass_block_top.png");
    Game->CowTexture = texture_create(Backend, "Resources/Textures/MC/cow.png");
    Game->PointLightIconTexture = texture_create(Backend, "Resources/Textures/PointLight.png", true);

    //Game->SunTexture = TextureCreate(Renderer->Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, "Resources/Textures/MC/environemnt/sun.png");

    //Game->MoonTexture = TextureCreate(Renderer->Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, "Resources/Textures/MC/environemnt/moon.png");

    Game->Registry = ecs_entity_registry_create(100);

    entity_model CowModel = EntityModelCreate();

    // Body
    {
        texture_rects Rects = {
            { 12, 10, 28, 13, 0 },
            { 12, 10, 40, 13, 0 },
            { 10, 18, 18, 31, 1 },
            { 10, 18, 40, 31, 3 },
            { 12, 18, 50, 31, 2 },
            { 12, 18, 28, 31, 0 }
        };

        AddModelPart(&CowModel, v3(0.0f), v3(12.0f, 10.0f, 18.0f), Rects);
    }

    // Head
    {
        texture_rects Rects = {
            { 8, 8,  6, 13, 0 },
            { 8, 8, 20, 13, 0 },
            { 6, 8,  0, 13, 0 },
            { 6, 8, 14, 13, 0 },
            { 8, 6,  6,  5, 0 },
            { 8, 6, 14,  5, 0 }
        };
        AddModelPart(&CowModel, v3(0, (10 + 8) / 4 - 1, (18 + 6) / 2), v3(8, 8, 6), Rects);
    }

    // Legs
    {
        texture_rects Rects = {
            { 4, 12,  4, 31, 0 },
            { 4, 12, 12, 31, 0 },
            { 4, 12,  0, 31, 0 },
            { 4, 12,  8, 31, 0 },
            { 4,  4,  4, 19, 0 },
            { 4,  4,  8, 19, 2 }
        };

        v2 Offset;
        Offset.x = 4;
        Offset.y = 7;
        f32 y = -11;
        AddModelPart(&CowModel, v3(Offset.x, y, -Offset.y), v3(4, 12, 4), Rects);
        AddModelPart(&CowModel, v3(-Offset.x, y, Offset.y), v3(4, 12, 4), Rects);
        AddModelPart(&CowModel, v3(Offset.x, y, Offset.y), v3(4, 12, 4), Rects);
        AddModelPart(&CowModel, v3(-Offset.x, y, -Offset.y), v3(4, 12, 4), Rects);
    }

    // Create another set of cows
    for (i32 i = 0; i < 1; i++)
    {
        auto CowEntity = ecs_create_entity(&Game->Registry);

        auto& Transform = ecs_add_component<transform_component>(&Game->Registry, CowEntity);
        Transform.Translation = v3((f32)c_TexelSize / 2 + 10.0f, 17.0, c_TexelSize / 2 + 10.0f);
        Transform.Scale = v3(1.0f);
        Transform.Rotation = v3(0.0f, 0.0f, 0.0f);

        auto& Render = ecs_add_component<entity_render_component>(&Game->Registry, CowEntity);
        Render.Texture = Game->CowTexture;
        Render.Model = CowModel;

        auto& AABB = ecs_add_component<aabb_physics_component>(&Game->Registry, CowEntity);
        AABB.BoxSize = v3(0.8f, 1.8, 0.8f);
        AABB.Velocity = v3(0.0f);
        AABB.Grounded = false;

        auto& Logic = ecs_add_component<logic_component>(&Game->Registry, CowEntity);
        Logic.CreateFunction = cow_create;
        Logic.DestroyFunction = cow_destroy;
        Logic.UpdateFunction = cow_update;
    }

    // On Create event
    auto View = ecs_view_components<logic_component>(&Game->Registry);
    for (auto Entity : View)
    {
        auto& Logic = View.Get(Entity);
        Logic.CreateFunction(&Game->Registry, Entity, &Logic);
    }

    return Game;
}

internal void game_destroy(game* Game, render_backend* RenderBackend)
{
    for (u32 i = 1; i < BLOCK_TYPE_COUNT - 1; i++)
    {
        texture_destroy(RenderBackend, &Game->BlockTextures[i]);
    }

    texture_destroy(RenderBackend, &Game->CowTexture);
    texture_destroy(RenderBackend, &Game->CrosshairTexture);
    texture_destroy(RenderBackend, &Game->PointLightIconTexture);
    //texture_destroy(&Game->SunTexture);
}

internal void game_generate_world(game* Game, block* Blocks, u32 BlocksCount)
{
    perlin_noise_create(&Game->PerlinNoise, random_series_u32(&Game->GenSeries, 1));
    i32 MaxTerrainHeight = LayerCount / 2 + 4;

    // 1. Generate the terrain
    const v3 StartPos = { 0, 0, 0 };
    v3 CurrentPosition = StartPos;
    for (i32 Z = 0; Z < RowCount; Z++)
    {
        CurrentPosition.x = StartPos.x;
        for (i32 X = 0; X < ColumnCount; X++)
        {
            CurrentPosition.y = StartPos.y;

            // Step 1: Generate height using noise
            f32 NoiseScale = 0.05f;
            f32 HeightValue = perlin_noise_get(&Game->PerlinNoise, X * NoiseScale, Z * NoiseScale); // in [0, 1]
            //f32 HeightValue = 0;
            i32 TerrainHeight = (i32)(HeightValue * MaxTerrainHeight);

            for (i32 Y = 0; Y < LayerCount; Y++)
            {
                i32 Index = (Y * RowCount * ColumnCount) + (Z * ColumnCount) + X;
                auto& Block = Game->Blocks[Index];
                Block.Color = v4(1.0f);
                Block.Position = v3(CurrentPosition.x, CurrentPosition.y, CurrentPosition.z);

                if (Y > TerrainHeight)
                {
                    Block.Type = block_type::Air;
                }
                else
                {
                    if (Y <= 1)
                    {
                        Block.Type = block_type::Bedrock;
                    }
                    else if (Y <= 3)
                    {
                        Block.Type = block_type::Stone;
                    }
                    else if (Y <= 5)
                    {
                        Block.Type = block_type::Dirt;
                    }
                    else if (Y <= 6)
                    {
                        Block.Type = block_type::Grass;
                        Block.Color = v4(0.2f, 0.8f, 0.1f, 1.0f);
                    }
                    else
                    {
                        Block.Type = block_type::Air;
                    }
                }

                CurrentPosition.y++;
            }
            CurrentPosition.x++;
        }
        CurrentPosition.z++;
    }

    // 2. Put on some trees (they only grow on grass blocks)
   /* CurrentPosition = StartPos;
    for (i32 Z = 0; Z < RowCount; Z++)
    {
        CurrentPosition.x = StartPos.x;
        for (i32 X = 0; X < ColumnCount; X++)
        {
            CurrentPosition.y = StartPos.y;

            for (i32 Y = 0; Y < LayerCount; Y++)
            {
                i32 Index = (Y * RowCount * ColumnCount) + (Z * ColumnCount) + X;
                auto& Block = Game->Blocks[Index];

                if (Block.Type == block_type::Grass)
                {
                    i32 DenyOffset = 10;
                    if (X > DenyOffset && X < RowCount - DenyOffset && X > DenyOffset && Z > DenyOffset && Z < ColumnCount - DenyOffset)
                    {
                        auto BlockAbove = block_get_safe(Game, X, Z, Y + 1);

                        if (BlockAbove && Index % 10 == 0 && random_series_u32(&Rand, 0, 1000) >= 900)
                        {
                            BlockAbove->Type = block_type::Dirt;
                        }
                    }
                }
                CurrentPosition.y++;
            }

            CurrentPosition.x++;
        }
        CurrentPosition.z++;
    }*/
}

internal void game_update(game* G, game_renderer* Renderer, const game_input* Input, f32 TimeStep, v2i ClientArea)
{
    //debug_cycle_counter GameUpdateCounter("GameUpdateCounter");

    if (Input->is_key_pressed(key::H) || Input->is_mouse_pressed(mouse::Side1) || Input->is_mouse_pressed(mouse::Side0))
    {
        G->RenderDebugUI = !G->RenderDebugUI;
    }

    // Player update
    // NOTE: I dont think we need everything to be an entity, just most dynamic stuff
    // Player and blocks are perfect examples of where we possibly dont need that flexibility
    bool UpdatePlayer = true;
    if (UpdatePlayer)
    {
        game_player_update(G, Input, Renderer, TimeStep);
    }

    // Physics simulation gave us some state of the entity and now we can use it to react to in the update loop
    bool UpdateEntities = true;
    if (UpdateEntities)
    {
        game_update_entities(G, TimeStep);
    }

    bool SimulatePhysics = true;
    if (SimulatePhysics)
    {
        game_physics_simulation_update_entities(G, TimeStep);
    }

    // Count time
    G->TimeSinceStart += TimeStep;

    bool RenderEntities = true;
    if (RenderEntities)
    {
        game_render_entities(G, Renderer, TimeStep);
    }

    bool RenderBlocks = true;
    if (RenderBlocks)
    {
        //debug_cycle_counter GameUpdateCounter("Render Blocks");

        for (u32 i = 0; i < G->BlocksCount; i++)
        {
            auto& Block = G->Blocks[i];

            if (!Block.placed())
                continue;

            //v4 BlockColor = Block.Color;

            //if (Block.Type == block_type::GlowStone)
            //{
            //    Block.Emission = G->GlowStoneEmission;
            //    f32 Radius = 6.0f;
            //    f32 FallOff = 1.0f;
            //    v3 Radiance = G->GlowStoneColor;
            //    BlockColor = v4(Radiance, 1.0f);
            //    game_renderer_submit_point_light(Renderer, Block.Position, Radius, FallOff, Radiance, G->GlowStoneEmission * 3.0f);
            //}

            game_renderer_submit_cuboid(Renderer, Block.Position, &G->BlockTextures[(u32)Block.Type], Block.Color, Block.Emission);
        }
    }

    // Lights
    f32 GameTime = G->TimeSinceStart * 0.1f;
    GameTime = 0;

    // Render sun
    {
        v3 SunDirection;
        v3 SunPosition;
        f32 DirGameTime = GameTime + bkm::PI_HALF;
        f32 Distance = 10.0f;
        f32 Angle = bkm::PI_HALF - GameTime; // speed of day-night cycle (radians per second)
        // Rotate baseDir around X axis to simulate sun path
        f32 CosA = bkm::Cos(Angle);
        f32 SinA = bkm::Sin(Angle);
        v3 BaseDir = v3(0.0f, 1.0f, 0.0f);
        SunDirection = v3(
            BaseDir.x,
            BaseDir.y * CosA - BaseDir.z * SinA,
            BaseDir.y * SinA + BaseDir.z * CosA
        );

        SunDirection = bkm::Normalize(SunDirection);
        SunPosition = SunDirection * Distance;
        v3 SunRotation(bkm::PI_HALF + Angle, 0, 0);
        game_renderer_submit_distant_quad(Renderer, SunPosition, SunRotation, nullptr, v4(1));

        // TODO: On sunset and sunrise, tweak directional light color to "reflect" the sky color
        local_persist v3 FinalColor = v3(0.8f, 0.5f, 0.3);
        if (DirGameTime > bkm::PI_HALF)
        {
            FinalColor = bkm::Lerp(FinalColor, v3(0.8f, 0.5f, 0.3), TimeStep * 0.3f);
        }
        else
        {
            FinalColor = bkm::Lerp(FinalColor, v3(1.0f), TimeStep * 0.3f);
        }

        //TraceV3(FinalColor);

        //GameRendererSubmitDirectionalLight(Renderer, -SunDirection, 1.5f, v3(1.0f));
    }

    auto CenterBlock = block_get_safe(G, (i32)G->Center.x, (i32)G->Center.z, (i32)G->Center.y);
    if (CenterBlock)
    {
        CenterBlock->Color = v4(0.0f, 1.0, 1.0, 1.0f);
    }
    // Update camera and HUD
    {
        m4 LightSpaceMatrix;

        auto Size = G->Size;
        auto Near = G->Near;
        auto Far = G->Far;
        auto Eye = G->Eye;
        auto LightDirection = G->DirectonalLightDirection;

        // Building light space matrix
        {
            m4 LightProjection = bkm::OrthoRH_ZO(-Size, Size, -Size, Size, Near, Far);

            v3 forward = bkm::Normalize(G->Center - G->Eye);
            v3 right = bkm::Normalize(bkm::Cross((fabs(forward.y) > 0.99f) ? v3(0, 0, 1) : v3(0, 1, 0), forward));
            v3 safeUp = bkm::Cross(forward, right);

            //m4 LightView = bkm::LookAtLH(Eye, Center, v3(0.0f, 1.0f, 0.0f));
            m4 LightView = bkm::LookAtRH(Eye, Eye + forward, safeUp);
            //m4 LightView = bkm::LookAt(v3(-SunDirection), v3(0, 0, 0), v3(0.0f, 1.0f, 0.0f));

            LightSpaceMatrix = LightProjection * LightView; // Remove translation
        }

        v3 CameraPosition = G->Player.Position + G->CameraOffset;

        m4 InverseView = bkm::Translate(m4(1.0f), CameraPosition) * bkm::ToM4(qtn(G->Player.Rotation));
        G->Camera.View = bkm::Inverse(InverseView);
        G->Camera.recalculate_projection_perspective(ClientArea.x, ClientArea.y);

        m4 HUDProjection = bkm::OrthoRH_ZO(0, (f32)ClientArea.x, (f32)ClientArea.y, 0, -1, 1);
        game_renderer_set_render_data(Renderer, CameraPosition, G->Camera.View, G->Camera.Projection, InverseView, HUDProjection, GameTime, LightSpaceMatrix);

        game_renderer_submit_directional_light(Renderer, LightDirection, G->DirectionalLightPower, G->DirectionalLightColor);
    }

    // Render lights
    {
        // Point lights
        auto View = ecs_view_components<transform_component, point_light_component>(&G->Registry);
        for (auto Entity : View)
        {
            auto [Transform, PLC] = View.Get(Entity);
            game_renderer_submit_point_light(Renderer, Transform.Translation, PLC.Radius, PLC.FallOff, PLC.Radiance, PLC.Intensity);
        }
    }

    // Render Editor UI
    if (G->RenderEditorUI)
    {
        game_render_editor_ui(G, Renderer);
    }

    // Render HUD
    if (G->RenderHUD)
    {
        m4 Projection = bkm::OrthoRH_ZO(0, (f32)ClientArea.x, (f32)ClientArea.y, 0, 0.0f, 1.0f);

        // Build crosshair vertices
        f32 CrosshairSize = 15.0f * 2.0f;
        f32 CenterX = ClientArea.x / 2.0f - CrosshairSize / 2.0f;
        f32 CenterY = ClientArea.y / 2.0f - CrosshairSize / 2.0f;

        v3 Positions[4];
        Positions[3] = { CenterX, CenterY, 0.0f };
        Positions[2] = { CenterX + CrosshairSize, CenterY, 0.0f };
        Positions[1] = { CenterX + CrosshairSize, CenterY + CrosshairSize, 0.0f };
        Positions[0] = { CenterX, CenterY + CrosshairSize, 0.0f };

        game_renderer_submit_hud_quad(Renderer, Positions, &G->CrosshairTexture, texture_coords(), v4(1.0f, 1.0f, 1.0f, 0.7f));
    }
}

internal void game_render_editor_ui(game* G, game_renderer* Renderer)
{
    // For each point lights so we know that its there
    game_renderer_submit_billboard_quad(Renderer, G->Eye, v2(0.5), &G->PointLightIconTexture, v4(1.0f));

    auto View = ecs_view_components<transform_component, point_light_component>(&G->Registry);
    for (auto Entity : View)
    {
        auto [Transform, PLC] = View.Get(Entity);
        game_renderer_submit_billboard_quad(Renderer, Transform.Translation, v2(0.5), &G->PointLightIconTexture, v4(1.0f));
    }
}

internal void game_debug_ui_update(game* G, game_renderer* Renderer, const game_input* Input, f32 TimeStep, v2i ClientArea)
{
    if (!G->RenderDebugUI)
        return;

    ImGui::Begin("Settings");

    if (ImGui::CollapsingHeader("Global Settings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("Render HUD", &G->RenderHUD);
        ImGui::Checkbox("Bloom", &Renderer->EnableBloom);
        ImGui::Checkbox("Shadows", &Renderer->EnableShadows);
        ImGui::Separator();
        if (ImGui::Button("Regenerate the world"))
        {
            game_generate_world(G, G->Blocks, G->BlocksCount);
        }
    }

    if (ImGui::CollapsingHeader("Player Settings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        //i32 X = (i32)G->Player.Position.x;
        //i32 Y = (i32)G->Player.Position.y;
        //i32 Z = (i32)G->Player.Position.z;

        i32 X = (i32)bkm::Floor(G->Player.Position.x + 0.5f);
        i32 Y = (i32)bkm::Floor(G->Player.Position.y + 0.5f);
        i32 Z = (i32)bkm::Floor(G->Player.Position.z + 0.5f);
        ImGui::Text("Coords: X: %i Y: %i Z: %i", X, Y, Z);
        ImGui::Separator();
        ImGui::Text("Currently selected block: %s", g_BlockLabels[(u32)G->Player.CurrentlySelectedBlock]);
    }

    if (ImGui::CollapsingHeader("Shadow Maps Testing", ImGuiTreeNodeFlags_DefaultOpen))
    {
        UI::DrawVec3Control("Eye", &G->Eye);
        ImGui::DragFloat("Size", &G->Size, 1.0f, 0.0f, 100.0f);
        ImGui::DragFloat("Near", &G->Near, 0.1f, -100.0f, 100.0f);
        ImGui::DragFloat("Far", &G->Far, 0.5f, 0.0f, 100.0f);
    }

    if (ImGui::CollapsingHeader("Light Testing", ImGuiTreeNodeFlags_DefaultOpen))
    {
        //UI::DrawVec3Control("Point Light Pos", &G->PointLightPos);
        //ImGui::DragFloat("Point Light Intensity", &G->PointLightIntenity, 0.5f, 0.0f, 100.0f);
        ImGui::Separator();
        ImGui::ColorEdit3("Directiona Light Color", &G->DirectionalLightColor.x);
        ImGui::DragFloat("Directional Light Power", &G->DirectionalLightPower, 0.2f, 0.0f, 100.0f);

        ImGui::ColorEdit3("GlowStone Color", &G->GlowStoneColor.x);
        ImGui::DragFloat("GlowStone Emission", &G->GlowStoneEmission, 0.2f, 0.0f, 100.0f);

        ImGui::Separator();

        // Point lights
        auto View = ecs_view_components<transform_component, point_light_component>(&G->Registry);
        for (auto Entity : View)
        {
            auto [Transform, PLC] = View.Get(Entity);

            ImGui::Text("%.3f %.3f %.3f", Transform.Translation.x, Transform.Translation.y, Transform.Translation.z);
        }
    }

    ImGui::End();
}

internal void game_player_update(game* Game, const game_input* Input, game_renderer* Renderer, f32 TimeStep)
{
    auto& Player = Game->Player;

    v3 Up = { 0.0f, 1.0, 0.0f };
    v3 Right = { 1.0f, 0.0, 0.0f };
    v3 Forward = { 0.0f, 0.0, -1.0f };
    f32 Speed = 10.0f;

    // Very important
    local_persist bool GameFocused = false;

    bool JustPressed = false;
    bool JumpKeyPressed = false;

    if (Input->is_key_pressed(key::T))
    {
        GameFocused = !GameFocused;

        JustPressed = true;
    }

    if (GameFocused)
    {
        local_persist v2i OldMousePos;

        v2i MousePos = Input->get_raw_mouse_input();

        // Avoids teleporting with camera rotation due to large delta
        if (JustPressed)
            OldMousePos = MousePos;

        v2i MouseDelta = MousePos - OldMousePos;

        //Trace("%i %i", MouseDelta.x, MouseDelta.y);
        OldMousePos = MousePos;

        f32 MouseSensitivity = 0.35f;

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
    if (GameFocused)
    {
        if (Input->is_key_down(key::W))
        {
            Direction += v3(Forward.x, 0.0f, Forward.z);
        }

        if (Input->is_key_down(key::S))
        {
            Direction -= v3(Forward.x, 0.0f, Forward.z);
        }

        if (Input->is_key_down(key::A))
        {
            Direction -= Right;
        }

        if (Input->is_key_down(key::D))
        {
            Direction += Right;
        }

        if (Input->is_key_pressed(key::BackSpace) || Input->is_key_down(key::Space))
        {
            JumpKeyPressed = true;
        }

        if (Input->is_key_pressed(key::G))
        {
            Player.IsPhysicsObject = !Player.IsPhysicsObject;
        }

        if (!Player.IsPhysicsObject)
        {
            if (Input->is_key_down(key::Q) || Input->is_key_down(key::BackSpace) || Input->is_key_down(key::Space))
            {
                Direction += v3(0.0f, 1.0f, 0.0f);
            }
            else if (Input->is_key_down(key::E) || Input->is_key_down(key::Control) || Input->is_key_down(key::Shift))
            {
                Direction -= v3(0.0f, 1.0f, 0.0f);
            }
        }

        // Cycle through available blocks
        if (Input->MouseScrollDelta > 0)
        {
            auto NextBlockType = (block_type)((u32)Player.CurrentlySelectedBlock + 1);

            if (NextBlockType == block_type::INVALID)
                NextBlockType = (block_type)1; // Excluding Air block type

            Assert(NextBlockType != block_type::INVALID, "Bruh");
            Assert(NextBlockType != block_type::Air, "Bruh");

            Player.CurrentlySelectedBlock = NextBlockType;
        }
        else if (Input->MouseScrollDelta < 0)
        {
            auto NextBlockType = block_type::INVALID;
            if (Player.CurrentlySelectedBlock == (block_type)1)
            {
                NextBlockType = block_type(u32(block_type::INVALID) - 1);
            }
            else
            {
                NextBlockType = block_type((u32)Player.CurrentlySelectedBlock - 1);
            }

            Assert(NextBlockType != block_type::INVALID, "Bruh");
            Assert(NextBlockType != block_type::Air, "Bruh");
            Player.CurrentlySelectedBlock = NextBlockType;
        }

        if (bkm::Length(Direction) > 0.0f)
            Direction = bkm::Normalize(Direction);
    }

    if (Player.IsPhysicsObject)
    {
        const f32 G = -9.81f * 2;
        const f32 CheckRadius = 5.0f;
        const f32 JumpStrength = 10.0f;

        // Apply gravity and movement forces
        v3 NextVelocity = Player.Velocity;
        NextVelocity.y += G * TimeStep;  // Gravity
        NextVelocity.x = Direction.x * Speed;
        NextVelocity.z = Direction.z * Speed;

        v3 NextPos = Player.Position;

        if (Player.Grounded && JumpKeyPressed)
        {
            NextVelocity.y = JumpStrength;
            Player.Grounded = false;
        }

        NextPos.y += NextVelocity.y * TimeStep;

        aabb PlayerAABB = aabb_from_v3(NextPos, v3(0.5f, 1.8f, 0.5f));

        // Stepping on a block
        auto Pos = get_world_to_block_position(NextPos);

        for (i32 l = -1; l <= 1; l += 2)
        {
            for (i32 c = -1; c <= 1; c++)
            {
                for (i32 r = -1; r <= 1; r++)
                {
                    auto Block = block_get_safe(Game, Pos.C + c, Pos.R + r, Pos.L + l);

                    if (Block && Block->placed())
                    {
                        aabb BlockAABB = aabb_from_v3(Block->Position, v3(1.0f));
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
                        }
                    }
                }
            }
        }

        // XXX
        NextPos.x += NextVelocity.x * TimeStep;

        PlayerAABB = aabb_from_v3(NextPos, v3(0.5f, 1.8f, 0.5f));

        Pos = get_world_to_block_position(NextPos);

        for (i32 c = -1; c <= 1; c += 2)
        {
            for (i32 r = -1; r <= 1; r++)
            {
                for (i32 l = -1; l <= 1; l++)
                {
                    auto Block = block_get_safe(Game, Pos.C + c, Pos.R + r, Pos.L + l);

                    if (Block && Block->placed())
                    {
                        aabb BlockAABB = aabb_from_v3(Block->Position, v3(1.0f));
                        if (AABBCheckCollision(PlayerAABB, BlockAABB))
                        {
                            //Block->Color = v4(0, 0, 1, 1);
                            NextPos.x = Player.Position.x;
                            NextVelocity.x = 0.0f;
                            goto ExitLoopX;
                        }
                    }
                }
            }
        }
    ExitLoopX:

        // ZZZ
        NextPos.z += NextVelocity.z * TimeStep;

        PlayerAABB = aabb_from_v3(NextPos, v3(0.5f, 1.8f, 0.5f));

        Pos = get_world_to_block_position(NextPos);

        for (i32 r = -1; r <= 1; r += 2)
        {
            for (i32 c = -1; c <= 1; c++)
            {
                for (i32 l = -1; l <= 1; l++)
                {
                    auto Block = block_get_safe(Game, Pos.C + c, Pos.R + r, Pos.L + l);

                    if (Block && Block->placed())
                    {
                        aabb BlockAABB = aabb_from_v3(Block->Position, v3(1.0f));
                        if (AABBCheckCollision(PlayerAABB, BlockAABB))
                        {
                            //Block->Color = v4(0, 0, 1, 1);
                            NextPos.z = Player.Position.z;
                            NextVelocity.z = 0.0f;
                            goto ExitLoopZ;
                        }
                    }
                }
            }
        }

    ExitLoopZ:
        Player.Position = NextPos;
        Player.Velocity = NextVelocity;
    }
    else // No physics
    {
        Player.Position += Direction * Speed * TimeStep;

        // Update coords
        auto Pos = get_world_to_block_position(Player.Position);
#if 0
        for (i32 i = -1; i <= 1; i++)
        {
            for (i32 j = -1; j <= 1; j++)
            {
                auto Block = BlockGetSafe(Game, Pos.C + i, Pos.R + 1, Pos.L + j);

                if (Block)
                {
                    GameRendererSubmitCuboidNoRotScale(Renderer, Block->Position, Game->BlockTextures[u32(block_type::Dirt)], v4(1.0f, 0.0f, 0.0f, 1.0f));
                }
            }
        }
#endif
    }

    if (GameFocused)
    {
        // Destroy block
        if (Input->is_mouse_pressed(mouse::Left))
        {
            ray Ray;
            Ray.Origin = Player.Position + Game->CameraOffset; // TODO: Camera position
            Ray.Direction = Forward; // Forward is already normalized

            v3 HitPoint;
            v3 HitNormal;
            block HitBlock;
            u64 HitIndex;
            if (find_first_hit(Ray, Game->Blocks, Game->BlocksCount, &HitPoint, &HitNormal, &HitBlock, &HitIndex))
            {
                auto& Block = Game->Blocks[HitIndex];

                if (Block.Type == block_type::GlowStone)
                {
                    Trace("Destroy: %i", (i32)Block.PointLightEntity);
                    ecs_destroy_entity(&Game->Registry, Block.PointLightEntity);
                    Block.PointLightEntity = INVALID_ID;
                    Block.Emission = 0.0f;
                }

                Block.Type = block_type::Air;

                //entity Entity = ecs_create_entity(&Game->Registry);
                // Color adjacent blocks
#if 0
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
#endif

                //Info("Block destroyed.");
            }
        }

        // Create blocks
        if (Input->is_mouse_pressed(mouse::Right))
        {
            v2i PlacePos = { 0, 0 };

            ray Ray;
            Ray.Origin = Player.Position + Game->CameraOffset; // TODO: Camera position
            Ray.Direction = Forward; // Forward is already normalized

            v3 HitPoint;
            v3 HitNormal;
            block HitBlock;
            u64 HitIndex;

            if (find_first_hit(Ray, Game->Blocks, Game->BlocksCount, &HitPoint, &HitNormal, &HitBlock, &HitIndex))
            {
                v3 NewBlockPos = HitBlock.Position + HitNormal;

                aabb BlockAABB = aabb_from_v3(NewBlockPos, v3(1.0f));
                aabb PlayerAABB = aabb_from_v3(Player.Position, v3(0.5f, 1.8f, 0.5f));
                if (!AABBCheckCollision(PlayerAABB, BlockAABB))
                {
                    i32 C = (i32)NewBlockPos.x;
                    i32 R = (i32)NewBlockPos.z;
                    i32 L = (i32)NewBlockPos.y;
                    auto Block = block_get_safe(Game, C, R, L);

                    if (Block)
                    {
                        Block->Type = Player.CurrentlySelectedBlock;

                        // Custom properties of each block
                        if (Block->Type == block_type::Dirt)
                        {
                            Block->Color = v4(1.0f);
                            Block->Emission = 0;
                        }
                        else if (Block->Type == block_type::GlowStone)
                        {
                            entity Entity = ecs_create_entity(&Game->Registry);
                            Trace("Createing %i", (i32)Entity);

                            Block->PointLightEntity = Entity;
                            Block->Emission = Game->GlowStoneEmission;
                            Block->Color = v4(0.8f, 0.3f, 0.2f, 1.0f);

                            auto& PLC = ecs_add_component2<point_light_component>(&Game->Registry, Entity);
                            PLC.Radius = 6.0f;
                            PLC.FallOff = 1.0f;
                            PLC.Radiance = v3(Block->Color);
                            PLC.Intensity = Block->Emission * 3.0f;

                            auto& Transform = ecs_add_component<transform_component>(&Game->Registry, Entity);
                            Transform.Translation = Block->Position;
                            Trace("%.3f, %.3f, %.3f", Transform.Translation.x, Transform.Translation.y, Transform.Translation.z);

                            //auto& PLC2 = ecs_get_component<point_light_component>(&Game->Registry, Entity);

                            Trace("test");
                        }
                    }
                }
            }
        }
    }

    // Stepping on a block
    // Is kinda wonky
    i32 C = (i32)bkm::Floor(Player.Position.x + 0.5f);
    i32 R = (i32)bkm::Floor(Player.Position.z + 0.5f);
    i32 L = (i32)bkm::Floor(Player.Position.y + 0.5f);

    //if (SteppedOnBlock)
    //{
    //    SteppedOnBlock->Color = v4(0, 0, 1, 1);
    //}

    //auto InFrontBlock = BlockGetSafe(Game, C + 1, R, L);
    //if (InFrontBlock)
    //{
    //    InFrontBlock->Color = v4(0, 1, 1, 1);
    //}
}

internal void game_update_entities(game* Game, f32 TimeStep)
{
    // TODO: Of course fetching function pointer is a lot slower than calling an actual function on a struct
    // The difference is between 5-6x when I spawn 10000 entities with optimalizations, thats quite huge and is worth considering.
    // However I dont think its necessary right now when the COW just goes and jumps off a cliff. There are more important things.
    //debug_cycle_counter Counter("GameUpdateEntities");

    auto View = ecs_view_components<logic_component>(&Game->Registry);

    for (auto Entity : View)
    {
        auto& Logic = View.Get(Entity);
        Logic.UpdateFunction(Game, &Game->Registry, Entity, &Logic, TimeStep);
    }
}

internal void game_physics_simulation_update_entities(game* Game, f32 TimeStep)
{
    auto View = ecs_view_components<transform_component, aabb_physics_component>(&Game->Registry);

    for (auto Entity : View)
    {
        auto [Transform, AABBPhysics] = View.Get(Entity);

        const f32 G = -9.81f * 2;
        const f32 CheckRadius = 2.0f;

        // Apply gravity and movement forces
        v3 NextVelocity = AABBPhysics.Velocity;
        NextVelocity.y += G * TimeStep;  // Gravity

        v3 NextPos = Transform.Translation;

        AABBPhysics.Grounded = false;

        NextPos.y += NextVelocity.y * TimeStep;

        aabb EntityAABB = aabb_from_v3(NextPos, AABBPhysics.BoxSize);

        // Figuring out if somehit is it
        block_pos Pos = get_world_to_block_position(NextPos);

        for (i32 l = -1; l <= 1; l += 2)
        {
            for (i32 c = -1; c <= 1; c++)
            {
                for (i32 r = -1; r <= 1; r++)
                {
                    auto Block = block_get_safe(Game, Pos.C + c, Pos.R + r, Pos.L + l);

                    if (Block && Block->placed())
                    {
                        aabb BlockAABB = aabb_from_v3(Block->Position, v3(1.0f));
                        if (AABBCheckCollision(EntityAABB, BlockAABB))
                        {
                            // Hit something above? Stop jumping.
                            if (NextVelocity.y > 0)
                            {
                                NextVelocity.y = 0;
                            }
                            // Hit the ground? Reset velocity and allow jumping again.
                            else if (NextVelocity.y < 0)
                            {
                                AABBPhysics.Grounded = true;
                                NextVelocity.y = 0;
                            }
                            NextPos.y = Transform.Translation.y; // Revert movement
                            goto ExitLoopY;
                        }
                    }
                }
            }
        }

    ExitLoopY:

        // XXX
        NextPos.x += NextVelocity.x * TimeStep;

        EntityAABB = aabb_from_v3(NextPos, AABBPhysics.BoxSize);

        Pos = get_world_to_block_position(NextPos);

        for (i32 c = -1; c <= 1; c += 2)
        {
            for (i32 r = -1; r <= 1; r++)
            {
                for (i32 l = -1; l <= 1; l++)
                {
                    auto Block = block_get_safe(Game, Pos.C + c, Pos.R + r, Pos.L + l);

                    if (Block && Block->placed())
                    {
                        aabb BlockAABB = aabb_from_v3(Block->Position, v3(1.0f));
                        if (AABBCheckCollision(EntityAABB, BlockAABB))
                        {
                            Block->Color = v4(0, 0, 1, 1);
                            NextPos.x = Transform.Translation.x;
                            NextVelocity.x = 0.0f;
                            goto ExitLoopX;
                        }
                    }
                }
            }
        }
    ExitLoopX:

        // ZZZ
        NextPos.z += NextVelocity.z * TimeStep;

        EntityAABB = aabb_from_v3(NextPos, AABBPhysics.BoxSize);

        Pos = get_world_to_block_position(NextPos);

        for (i32 r = -1; r <= 1; r += 2)
        {
            for (i32 c = -1; c <= 1; c++)
            {
                for (i32 l = -1; l <= 1; l++)
                {
                    auto Block = block_get_safe(Game, Pos.C + c, Pos.R + r, Pos.L + l);

                    if (Block && Block->placed())
                    {
                        aabb BlockAABB = aabb_from_v3(Block->Position, v3(1.0f));
                        if (AABBCheckCollision(EntityAABB, BlockAABB))
                        {
                            Block->Color = v4(0, 0, 1, 1);
                            NextPos.z = Transform.Translation.z;
                            NextVelocity.z = 0.0f;
                            goto ExitLoopZ;
                        }
                    }
                }
            }
        }
    ExitLoopZ:
        Transform.Translation = NextPos;
        AABBPhysics.Velocity = NextVelocity;
    }
}

internal void game_render_entities(game* Game, game_renderer* Renderer, f32 TimeStep)
{
    //debug_cycle_counter GameRenderEntities("Render Entities");

    auto View = ecs_view_components<transform_component, entity_render_component>(&Game->Registry);
    for (auto Entity : View)
    {
        auto [Transform, Render] = View.Get(Entity);

        for (i32 i = 0; i < Render.Model.PartsCount; i++)
        {
            entity_part& Part = Render.Model.Parts[i];

            // TODO: SIMD STUFF, we can take advantage in matrix multiplication
            m4 Local = bkm::Translate(m4(1.0f), Part.LocalPosition) * bkm::Scale(m4(1.0f), Part.Size);
            m4 TransformMatrix = Transform.Matrix() * Local;

            game_renderer_submit_quaded_cuboid(Renderer, TransformMatrix, &Render.Texture, Part.Coords, Render.Color);
        }
    }
}
