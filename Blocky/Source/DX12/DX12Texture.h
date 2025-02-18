#pragma once

// TODO: Remove
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "stb_image.h"

struct texture
{
    ID3D12Resource* Handle;
    u64 Width;
    u64 Height;
    DXGI_FORMAT Format;
    D3D12_CPU_DESCRIPTOR_HANDLE SRVDescriptor;
};

internal texture TextureCreate(ID3D12Device* Device, ID3D12CommandAllocator* CommandAllocator, ID3D12GraphicsCommandList* CommandList, ID3D12CommandQueue* CommandQueue, buffer Pixels, u32 Width, u32 Height, DXGI_FORMAT Format = DXGI_FORMAT_R8G8B8A8_UNORM);

internal texture TextureCreate(ID3D12Device* Device, ID3D12CommandAllocator* CommandAllocator, ID3D12GraphicsCommandList* CommandList, ID3D12CommandQueue* CommandQueue, const char* Path);

internal void TextureDestroy(texture* Texture);
