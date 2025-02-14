#pragma once

// TODO: Remove
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

struct texture
{
    ID3D12Resource* Resource;
    u64 Width;
    u64 Height;
    DXGI_FORMAT Format;
};
