#include "ResourcePacker.h"

static buffer platform_read_buffer(const char* path)
{
    buffer Buffer;

    std::ifstream stream(path, std::ios::in | std::ios::binary);
    Assert(stream, "Could not find the file!");

    stream.seekg(0, std::ios::end);
    size_t size = stream.tellg();
    stream.seekg(0, std::ios::beg);

    Buffer.Data = new u8[size];
    Buffer.Size = (u32)size;
    stream.read(reinterpret_cast<char*>(Buffer.Data), size);

    return Buffer;
}

int main()
{
    // Blocky Pack format
    std::ostringstream OutStream;

    // Write magic header
    OutStream << "BLPF";

    // Quad shader 
    //WriteShader(OutStream, "Resources/Quad.glsl");
    ModelWrite(OutStream, "Resources/Mesh/Cow.glb");

    //MeshWrite(OutStream, "Resources/Mesh/Cow.glb");

    // Our custom format for meshes:
    // Constains:
    //   - Tag
    //   - Vertices
    //   - Indices
    //   - Submeshes
    //   - Texture

    const char* PackedPath = "Resources/Cow.blmodel";
    // Write it all at once
    {
        std::ofstream FileStream(PackedPath, std::ios::binary | std::ios::trunc); // Always overwrite
        Assert(FileStream, "Could not create packed asset!");
        FileStream << OutStream.str();
        FileStream.close();
    }

    // !!!
    //return 0;
    {
        loaded_mesh Mesh;

        buffer Buffer = platform_read_buffer(PackedPath);
        u8* Pointer = (u8*)Buffer.Data;

        // Get tag
        char Tag[4];
        memcpy(Tag, Pointer, 4);
        Pointer += 4;

        // Get vertices
        u32 Size = 0;
        memcpy(&Size, Pointer, 4);
        Pointer += 4;
        Mesh.Vertices = std::vector<vertex>((vertex*)Pointer, (vertex*)(Pointer + Size));
        Pointer += Size;

        // Get Indices
        Size = 0;
        memcpy(&Size, Pointer, 4);
        Pointer += 4;
        Mesh.Indices = std::vector<u32>((u32*)Pointer, (u32*)(Pointer + Size));
        Pointer += Size;

        // Get Submeshes
        Size = 0;
        memcpy(&Size, Pointer, 4);
        Pointer += 4;
        Mesh.Submeshes = std::vector<submesh>((submesh*)Pointer, (submesh*)(Pointer + Size));
        Pointer += Size;

        // Get Texture
        u32 Width;
        u32 Height;
        memcpy(&Width, Pointer, 4);
        Pointer += 4;
        memcpy(&Height, Pointer, 4);
        Pointer += 4;
        Mesh.Texture = { Width, Height, Pointer };
        
        int k = 0;
    }
}
