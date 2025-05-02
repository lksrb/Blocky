#pragma once

#include <vector>

struct mesh_vertex
{
    v3 Position;
    v3 Normal;
    v2 TextureCoords;
};

struct submesh
{
    u32 BaseVertex = 0;
    u32 BaseIndex = 0;
    u32 VertexCount = 0;
    u32 IndexCount = 0;
    m4 Transform{ 1.0f };
};

struct mesh
{
    buffer Vertices; // mesh_vertex
    buffer Indices; // u32
    buffer Submeshes; // submesh
};

// Because everything is a cube, it does not make sense to have separate vertex buffer per model and waste draw calls
// Cow has 6 body parts, meaning 6 drawcalls, when multiple animals will be drawn, we got 6 x #animals because each will have an unique texture
struct submesh_v2
{
    m4 LocalTransform;
    block_coords TextureCoords;
};

struct mesh_v2
{
    std::vector<submesh_v2> Submeshes;
};
