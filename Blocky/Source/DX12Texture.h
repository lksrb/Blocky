#pragma once

// TODO: Remove
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

struct dx12_texture
{
    ID3D12Resource* Resource;
    u64 Width;
    u64 Height;
};

static dx12_texture DX12TextureCreate(ID3D12Device* Device, ID3D12GraphicsCommandList* CommandList, const char* Path)
{
    dx12_texture Texture = {};

    // Load texture file
    stbi_set_flip_vertically_on_load(1);

    // We required 32 bits per color
    const int EnforceChannels = 4;

    // Load the image into memory
    int Width, Height, Channels;

    u8* Pixels = stbi_load(Path, &Width, &Height, &Channels, EnforceChannels);
    Assert(Channels == EnforceChannels, "4 channels not ensured!");

    // Describe and create a Texture2D.
    D3D12_RESOURCE_DESC Desc = {};
    {
        Desc.MipLevels = 1;
        Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        Desc.Width = Width;
        Desc.Height = Height;
        Desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        Desc.DepthOrArraySize = 1;
        Desc.SampleDesc.Count = 1;
        Desc.SampleDesc.Quality = 0;
        Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

        D3D12_HEAP_PROPERTIES HeapProperties = {};
        HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

        DxAssert(Device->CreateCommittedResource(
            &HeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &Desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&Texture.Resource)));

        Texture.Width = Width;
        Texture.Width = Height;
    }

    // Create the GPU upload buffer.
    ID3D12Resource* TextureUploadBuffer = nullptr;
    {
        D3D12_HEAP_PROPERTIES UploadHeapProps = {};
        UploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC uploadBufferDesc = {};
        uploadBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        uploadBufferDesc.Width = GetRequiredIntermediateSize(Texture.Resource, 0, 1);
        uploadBufferDesc.Height = 1;
        uploadBufferDesc.DepthOrArraySize = 1;
        uploadBufferDesc.MipLevels = 1;
        uploadBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        uploadBufferDesc.SampleDesc.Count = 1;
        uploadBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        DxAssert(Device->CreateCommittedResource(
            &UploadHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &uploadBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&TextureUploadBuffer)));
    }

    /*   dx12_buffer TextureUploadBuffer = DX12BufferCreate(Device, D3D12_RESOURCE_STATE_VIDEO_DECODE_READ, D3D12_HEAP_TYPE_UPLOAD, TextureWidth * TextureHeight * Channels);*/

    void* MappedMemory;
    D3D12_RANGE readRange = { 0, 0 };
    TextureUploadBuffer->Map(0, &readRange, &MappedMemory);
    memcpy(MappedMemory, Pixels, Width * Height * Channels);
    TextureUploadBuffer->Unmap(0, nullptr);

    // Copy data to the intermediate upload heap and then schedule a copy 
    // from the upload heap to the Texture2D.
    D3D12_SUBRESOURCE_DATA TextureData = {};
    TextureData.pData = Pixels;
    TextureData.RowPitch = Width * Channels;
    TextureData.SlicePitch = TextureData.RowPitch * Height;

    // Copy data from the buffer to the texture on the gpu
    D3D12_TEXTURE_COPY_LOCATION Dst = {};
    Dst.pResource = Texture.Resource;
    Dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    Dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION Src = {};
    Src.pResource = TextureUploadBuffer;
    Src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    Device->GetCopyableFootprints(&Desc, 0, 1, 0, &Src.PlacedFootprint, nullptr, nullptr, nullptr);

    CommandList->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);

    //auto Barrier = DX12RendererTransition(Texture.Resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    //CommandList->ResourceBarrier(1, &Barrier);

    return Texture;
}

static void DX12TextureDestroy(dx12_texture* Texture)
{
    Texture->Resource->Release();
    Texture->Resource = nullptr;
    Texture->Width = 0;
    Texture->Height = 0;
}
