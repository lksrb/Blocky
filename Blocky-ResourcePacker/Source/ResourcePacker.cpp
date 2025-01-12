
// Optimize shaders
#if defined(BK_DEBUG) || defined(BK_RELEASE)
#define OPTIMIZE_SHADER 0
#elif defined(BK_DIST)
#define OPTIMIZE_SHADER 1
#define GENERATE_HEADER 1
#endif

// Standard library
#include <cstdio>
#include <string>
#include <fstream>
#include <filesystem>
#include <vector>

using u8 = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;

#define Assert(__cond__, ...)
#define Log(x, ...)

// Shaderc and SPIR-V for cross-plaftform shader compilation (TODO: We should just invoke shaderc compiler's executable)
#include <shaderc/shaderc.hpp>
#include <spirv_cross/spirv_cross.hpp>

constexpr u32 c_ShaderStageCount = 2;

static const char* GLShaderStageCachedVulkanFileExtension(shaderc_shader_kind stage)
{
    switch (stage)
    {
        case shaderc_vertex_shader:    return ".cached_vulkan.vert";
        case shaderc_fragment_shader:  return ".cached_vulkan.frag";
        default:
            break;
    }

    Assert(false, "Unknown shader!");
    return "";
}

static const char* ShaderTypeToString(shaderc_shader_kind stage)
{
    switch (stage)
    {
        case shaderc_vertex_shader:   return "Vertex";
        case shaderc_fragment_shader: return "Fragment";
        default:
            break;
    }

    Assert(false, "");
    return "";
}

static shaderc_shader_kind ShaderTypeFromString(std::string_view type)
{
    if (type == "vertex")
        return shaderc_vertex_shader;
    if (type == "fragment" || type == "pixel")
        return shaderc_fragment_shader;

    Assert(false, "Unknown shader!");

    return (shaderc_shader_kind)(-1);
}

static std::filesystem::path GetShaderCachePath(u32 stage, std::string_view path)
{
    static std::filesystem::path s_CachedPath = "Resources/Cache/Shaders";

    return s_CachedPath / std::filesystem::path(path)
        .stem()
        .concat(GLShaderStageCachedVulkanFileExtension((shaderc_shader_kind)stage));
}

static std::string LoadSource(const char* path)
{
    std::string source;
    std::ifstream in(path, std::ios::in | std::ios::binary); // ifstream closes itself due to RAII
    if (in)
    {
        in.seekg(0, std::ios::end);
        u64 size = in.tellg();
        if (size != -1)
        {
            source.resize(size);
            in.seekg(0, std::ios::beg);
            in.read(&source[0], size);
        }
        else
        {
            Log("Could not read from file '%s'", path);
        }
    }
    else
    {
        Log("Could not open file '%s'", path);
    }

    return source;
}

static void ReadAndPreProcess(const char* path, std::string shaderSources[c_ShaderStageCount])
{
    std::string sourceCode = LoadSource(path);

    const char* typeToken = "#type";
    u64 typeTokenLength = strlen(typeToken);
    u64 pos = sourceCode.find(typeToken, 0); // Start of shader type declaration line
    while (pos != std::string::npos)
    {
        u64 eol = sourceCode.find_first_of("\r\n", pos); // End of shader type declaration line
        Assert(eol != std::string::npos, "Syntax error");
        u64 begin = pos + typeTokenLength + 1; // Start of shader type name (after "#type " keyword)
        std::string type = sourceCode.substr(begin, eol - begin);

        u64 nextLinePos = sourceCode.find_first_not_of("\r\n", eol); // Start of shader code after shader type declaration line
        Assert(nextLinePos != std::string::npos, "Syntax error");
        pos = sourceCode.find(typeToken, nextLinePos); // Start of next shader type declaration line

        shaderSources[ShaderTypeFromString(type)] = (pos == std::string::npos) ? sourceCode.substr(nextLinePos) : sourceCode.substr(nextLinePos, pos - nextLinePos);
    }
}

static std::vector<u32> CompileSingleShader(const char* path, const char* shaderSource, shaderc_shader_kind stage)
{
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
    options.SetGenerateDebugInfo();

    if constexpr (OPTIMIZE_SHADER)
        options.SetOptimizationLevel(shaderc_optimization_level_performance);

    shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(shaderSource, stage, path, options);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success)
    {
        Log("%s", result.GetErrorMessage().data());
        Assert(false, "");
    }

    return std::vector<u32>(result.cbegin(), result.cend());
}

struct ShaderCompilationResult
{
    std::vector<u32> compiled_vertex_shader;
    std::vector<u32> compiled_fragment_shader;
};

static ShaderCompilationResult CompileShader(const char* path, bool forceCompile)
{
    Assert(std::filesystem::exists(path), "Cannot find shader in specified path!");

    // Check if up to date
    bool compileShader[c_ShaderStageCount];
    memset(compileShader, forceCompile, sizeof(compileShader));
    std::vector<u32> compiledShaders[c_ShaderStageCount];
    const std::filesystem::path cachedPath = "Resources/Cache/Shaders";
    bool loadSource = forceCompile;

    // Cache folder does not exists, recusively create folders along the way
    if (!std::filesystem::exists(cachedPath))
    {
        std::filesystem::create_directories(cachedPath);

        for (u32 i = 0; i < c_ShaderStageCount; ++i)
        {
            compileShader[i] = true;
        }
    }

    // Check if up-to-date
    if (!loadSource)
    {
        for (u32 shader_index = 0; shader_index < c_ShaderStageCount; ++shader_index)
        {
            auto shaderCachePath = GetShaderCachePath(shader_index, path);
            compileShader[shader_index] = compileShader[shader_index] ? true : !std::filesystem::exists(shaderCachePath);

            // If there is a shader missing, load source
            if (compileShader[shader_index])
            {
                loadSource = true;
            }
        }
    }

    // Check if loading shader source is needed
    if (loadSource)
    {
        std::string shaderSources[c_ShaderStageCount];
        ReadAndPreProcess(path, shaderSources);

        for (u32 shader_index = 0; shader_index < c_ShaderStageCount; ++shader_index)
        {
            auto shaderCachePath = GetShaderCachePath(shader_index, path);
            auto& compiledShader = compiledShaders[shader_index];

            if (compileShader[shader_index])
            {
                Log("[%s]...", ShaderTypeToString((shaderc_shader_kind)shader_index));
                compiledShader = CompileSingleShader(path, shaderSources[shader_index].c_str(), (shaderc_shader_kind)shader_index);

                // Overwrite the contents of previous cached shader
                {
                    std::ofstream outStream(shaderCachePath, std::ios::binary | std::ios::trunc);
                    Assert(outStream, "Could not save compile shader to cache! Path: %s", shaderCachePath.string().data());
                    outStream.write(reinterpret_cast<char*>(compiledShader.data()), compiledShader.size() * sizeof(u32));
                    outStream.flush();
                    outStream.close();
                }
            }
            else // Read cached shader
            {
                std::ifstream stream(shaderCachePath, std::ios::in | std::ios::binary);
                Assert(stream, "Could not read from cached shader!");
                Log("[%s] Shader %s is up-to-date!", ShaderTypeToString((shaderc_shader_kind)shader_index), path);

                stream.seekg(0, std::ios::end);
                size_t size = stream.tellg();
                stream.seekg(0, std::ios::beg);

                compiledShader.resize(size / sizeof(u32));
                stream.read(reinterpret_cast<char*>(compiledShader.data()), size);
            }
        }
    }
    else // No source loading needed
    {
        for (u32 shader_index = 0; shader_index < c_ShaderStageCount; ++shader_index)
        {
            auto shaderCachePath = GetShaderCachePath(shader_index, path);
            auto& compiledShader = compiledShaders[shader_index];

            std::ifstream stream(shaderCachePath, std::ios::in | std::ios::binary);
            Assert(stream, "Could not read from cached shader!");
            Log("[%s] Shader %s is up-to-date!", ShaderTypeToString((shaderc_shader_kind)shader_index), path);

            stream.seekg(0, std::ios::end);
            size_t size = stream.tellg();
            stream.seekg(0, std::ios::beg);

            compiledShader.resize(size / sizeof(u32));
            stream.read(reinterpret_cast<char*>(compiledShader.data()), size);
        }
    }

    return ShaderCompilationResult{ compiledShaders[shaderc_vertex_shader], compiledShaders[shaderc_fragment_shader] };
}

struct buffer
{
    void* Data;
    u64 Size;
};

static buffer ReadBinary(const char* path)
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

static void WriteShader(std::ostringstream& OutStream, const char* Path, bool ForceCompile = false)
{
    ShaderCompilationResult result = CompileShader(Path, ForceCompile);

    // Write vertex shader first
    u64 size = result.compiled_vertex_shader.size() * sizeof(u32);
    OutStream.write(reinterpret_cast<char*>(&size), sizeof(u32));
    OutStream.write(reinterpret_cast<char*>(result.compiled_vertex_shader.data()), result.compiled_vertex_shader.size() * sizeof(u32));

    // Then write fragment shader
    size = result.compiled_fragment_shader.size() * sizeof(u32);
    OutStream.write(reinterpret_cast<char*>(&size), sizeof(u32));
    OutStream.write(reinterpret_cast<char*>(result.compiled_fragment_shader.data()), result.compiled_fragment_shader.size() * sizeof(u32));
}

int main()
{
    // Blocky Pack format
    std::ostringstream OutStream;

    // Write magic headers
    OutStream << "BLPF";

    // Quad shader 
    WriteShader(OutStream, "Resources/Quad.glsl");

    // Write it all at once
    {
        const char* PackedPath = "PackedResources.blp";

        std::ofstream FileStream(PackedPath, std::ios::binary | std::ios::trunc); // Always overwrite
        Assert(FileStream, "Could not create packed asset!");
        FileStream << OutStream.str();
        FileStream.close();
    }
}
