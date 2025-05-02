#pragma once

struct vertex
{
    v3 Position;
    v3 Normal;
    v2 TexCoords;
};

struct submesh
{
    u32 BaseVertex = 0;
    u32 BaseIndex = 0;
    u32 VertexCount = 0;
    u32 IndexCount = 0;
    m4 Transform{ 1.0f };

    //std::string DebugName;
};

using block_coords = v2[24];

struct submesh_v2
{
    m4 Transform;
    block_coords TextureCoords;
};
struct loaded_texture
{
    u32 Width;
    u32 Height;
    u8* Data;
};

struct loaded_mesh
{
    std::vector<submesh> Submeshes;
    std::vector<vertex> Vertices;
    std::vector<u32> Indices;
    loaded_texture Texture;
};

//struct loaded_mesh
//{
//    std::vector<submesh_v2> Submeshes;
//};

static u32 s_AssimpImporterFlags =
aiProcess_Triangulate // Ensures that everything is a triangle
| aiProcess_GenNormals // Generate normals if needed
| aiProcess_OptimizeMeshes // Reduces the number of meshes if possible
| aiProcess_GlobalScale // For .fbx import
| aiProcess_JoinIdenticalVertices // Optimizes the mesh
| aiProcess_ValidateDataStructure;

static m4 Mat4FromAssimpMat4(const aiMatrix4x4& matrix)
{
    m4 Result;
    //the a,b,c,d in assimp is the row ; the 1,2,3,4 is the column
    Result[0][0] = matrix.a1; Result[1][0] = matrix.a2; Result[2][0] = matrix.a3; Result[3][0] = matrix.a4;
    Result[0][1] = matrix.b1; Result[1][1] = matrix.b2; Result[2][1] = matrix.b3; Result[3][1] = matrix.b4;
    Result[0][2] = matrix.c1; Result[1][2] = matrix.c2; Result[2][2] = matrix.c3; Result[3][2] = matrix.c4;
    Result[0][3] = matrix.d1; Result[1][3] = matrix.d2; Result[2][3] = matrix.d3; Result[3][3] = matrix.d4;
    return Result;
}

static void MeshFlattenHierarchy(std::vector<submesh>& Submeshes, const aiNode* Node, const m4& ParentTransform, u32 Level)
{
    m4 LocalTransform = Mat4FromAssimpMat4(Node->mTransformation);
    m4 Transform = ParentTransform * LocalTransform;

    for (u32 i = 0; i < Node->mNumMeshes; i++)
    {
        u32 MeshIndex = Node->mMeshes[i];
        auto& Submesh = Submeshes[MeshIndex];
        Submesh.Transform = Transform;
        //Submesh.DebugName = node->mName.C_Str();
    }

    for (u32 i = 0; i < Node->mNumChildren; i++)
        MeshFlattenHierarchy(Submeshes, Node->mChildren[i], Transform, Level + 1);
}

static void MeshProcess(loaded_mesh* LoadedMesh, const aiScene* Scene)
{
    u32 VertexCount = 0;
    u32 IndexCount = 0;

    LoadedMesh->Submeshes.reserve(Scene->mNumMeshes);
    for (size_t i = 0; i < Scene->mNumMeshes; i++)
    {
        aiMesh* Mesh = Scene->mMeshes[i];
        auto& Submesh = LoadedMesh->Submeshes.emplace_back();
        Submesh.BaseVertex = VertexCount;
        Submesh.BaseIndex = IndexCount;
        Submesh.VertexCount = Mesh->mNumVertices;
        Submesh.IndexCount = Mesh->mNumFaces * 3; // If the geometry is not made out of triangles we're gonna have a big problem
        VertexCount += Mesh->mNumVertices;
        IndexCount += Submesh.IndexCount;

        // Vertices
        for (u32 i = 0; i < Mesh->mNumVertices; i++)
        {
            vertex& Vertex = LoadedMesh->Vertices.emplace_back();
            Vertex.Position = { Mesh->mVertices[i].x, Mesh->mVertices[i].y, Mesh->mVertices[i].z };
            Vertex.Normal = { Mesh->mNormals[i].x, Mesh->mNormals[i].y, Mesh->mNormals[i].z };
            Vertex.TexCoords = v2(0.0f);
            // Does the mesh contain texture coords
            if (Mesh->HasTextureCoords(0))
            {
                Vertex.TexCoords = { Mesh->mTextureCoords[0][i].x, Mesh->mTextureCoords[0][i].y };
            }
        }

        // Indices
        for (u32 i = 0; i < Mesh->mNumFaces; i++)
        {
            aiFace face = Mesh->mFaces[i];

            for (u32 j = 0; j < face.mNumIndices; j++)
            {
                LoadedMesh->Indices.push_back(face.mIndices[j]);
            }
        }
    }

    MeshFlattenHierarchy(LoadedMesh->Submeshes, Scene->mRootNode, m4(1.0f), 0);
}

static void MeshWrite(std::ostringstream& OutStream, const char* Path)
{
    Info("[Mesh] Processing '%s'", Path);

    // Logging setup
    aiEnableVerboseLogging(AI_TRUE);
    aiLogStream LogStream = aiGetPredefinedLogStream(aiDefaultLogStream_STDOUT, NULL);
    aiAttachLogStream(&LogStream);

    const aiScene* Scene = aiImportFile(Path, s_AssimpImporterFlags);

    Assert(Scene && !(Scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) && Scene->mRootNode, "Failed to load the mesh!");

    loaded_mesh Mesh;
    MeshProcess(&Mesh, Scene);

    // Texture stuff
    {
        aiMaterial* Material = Scene->mMaterials[0];
        aiString TexPath;
        aiTextureMapping Mapping;
        u32 UVIndex;

        aiReturn Result = aiGetMaterialTexture(Material, aiTextureType_DIFFUSE, 0, &TexPath, &Mapping, &UVIndex, nullptr, nullptr, nullptr, nullptr);

        // Diffuse maps to base color, since we are not using PBR (yet??? no) this is sufficient
        Assert(Result == aiReturn_SUCCESS, "There is no material texture!");

#if 1
        // Is texture embedded into the file?
        if (TexPath.data[0] == '*')
        {
            // Its is embedded
            aiTexture* Texture = Scene->mTextures[0];
            auto Format = Texture->achFormatHint;

            u32 Channels = 0;
            if (Texture->CheckFormat("png") || Texture->CheckFormat("jpg"))
            {
                stbi_set_flip_vertically_on_load(1);

                // Allow only compressed stuff, if not, then we can load normally
                Assert(Texture->mHeight == 0, "Texture is not compressed!");

                int Width, Height, Channels;
                // Enforce 4 bytes per pixel, for sanity
                // aiTexel is 4 bytes actually

                auto Pixels = stbi_load_from_memory((stbi_uc*)Texture->pcData, Texture->mWidth * 4, &Width, &Height, &Channels, 4);
                Assert(Pixels, "stbi_load_from_memory failed.");
                Mesh.Texture = { (u32)Width, (u32)Height, Pixels };
            }
            else
            {
                Assert(false, "");
            }
        }
        else
        {
            // Load the texture from disk
            Assert(false, "");
        }

#endif
    }

    // Vertices
    {
        u32 Size = (u32)(Mesh.Vertices.size() * sizeof(vertex));
        OutStream.write(reinterpret_cast<char*>(&Size), sizeof(Size));
        OutStream.write(reinterpret_cast<char*>(Mesh.Vertices.data()), Size);
    }

    // Indices
    {
        u32 Size = (u32)(Mesh.Indices.size() * sizeof(u32));
        OutStream.write(reinterpret_cast<char*>(&Size), sizeof(Size));
        OutStream.write(reinterpret_cast<char*>(Mesh.Indices.data()), Size);
    }

    // Submeshes
    {
        u32 Size = (u32)(Mesh.Submeshes.size() * sizeof(submesh));
        OutStream.write(reinterpret_cast<char*>(&Size), sizeof(Size));
        OutStream.write(reinterpret_cast<char*>(Mesh.Submeshes.data()), Size);
    }

    // Write texture
    {
        OutStream.write(reinterpret_cast<char*>(&Mesh.Texture.Width), sizeof(Mesh.Texture.Width));
        OutStream.write(reinterpret_cast<char*>(&Mesh.Texture.Height), sizeof(Mesh.Texture.Width));
        OutStream.write(reinterpret_cast<char*>(Mesh.Texture.Data), Mesh.Texture.Width * Mesh.Texture.Height * 4);
    }
}
