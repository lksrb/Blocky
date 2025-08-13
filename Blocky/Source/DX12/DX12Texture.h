#pragma once

// TODO: Remove
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "stb_image.h"

enum class texture_format : u32
{
    None = 0,
    RGBA8_Unorm_SRGB
};

struct texture
{
    ID3D12Resource* Handle;
    u32 Width;
    u32 Height;
    DXGI_FORMAT Format;
    D3D12_CPU_DESCRIPTOR_HANDLE SRVDescriptor;
    u32 Mips = 1;
};

struct dx12_render_backend;

internal texture texture_create(render_backend* Backend, u32 Width, u32 Height, buffer Pixels, bool GenerateMips = false, texture_format Format = texture_format::RGBA8_Unorm_SRGB, const wchar_t* DebugName = L"DX12Texture");
internal texture texture_create(render_backend* Backend, const char* Path, bool GenerateMips = false);
internal void texture_destroy(render_backend* Backend, texture* Texture);
