#include "DX12Texture.h"

internal ID3D12DescriptorHeap* g_OfflineTextureHeap = nullptr;
internal u32 g_OfflineTextureHeapIndex = 0; // Simply increment every time a texture is created. TODO: Freelist allocator

internal texture DX12TextureCreate(ID3D12Device* Device, ID3D12CommandAllocator* CommandAllocator, ID3D12GraphicsCommandList* CommandList, ID3D12CommandQueue* CommandQueue, buffer Pixels, u32 Width, u32 Height, DXGI_FORMAT Format)
{
    Assert(Pixels.Data && Pixels.Size, "Cannot create texture from empty buffer!");
    Assert(Pixels.Size <= Width * Height * 4, "Buffer is larger than Width * Height * FormatChannels!"); // TODO: Support more formats

    texture Texture = {};
    Texture.Width = Width;
    Texture.Height = Height;
    Texture.Format = Format;
    const u32 Channels = 4; // RGBA for now

    // Describe and create a Texture2D.
    D3D12_RESOURCE_DESC TextureDesc = {};
    {
        TextureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        TextureDesc.MipLevels = 1;
        TextureDesc.Format = Format;
        TextureDesc.Width = Width;
        TextureDesc.Height = Height;
        TextureDesc.DepthOrArraySize = 1;
        TextureDesc.SampleDesc.Count = 1;
        TextureDesc.SampleDesc.Quality = 0;
        TextureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        TextureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        D3D12_HEAP_PROPERTIES HeapProperties = {};
        HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
        HeapProperties.CreationNodeMask = 1;
        HeapProperties.VisibleNodeMask = 1;
        HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;

        DxAssert(Device->CreateCommittedResource(
            &HeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &TextureDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&Texture.Resource)));

        Texture.Width = Width;
        Texture.Height = Height;
        Texture.Resource->SetName(L"DX12Texture");
    }

    // Create the GPU upload buffer.
    ID3D12Resource* UploadBuffer = nullptr;
    {
        UINT64 RequiredSize;
        Device->GetCopyableFootprints(&TextureDesc, 0, 1, 0, nullptr, nullptr, nullptr, &RequiredSize);

        D3D12_HEAP_PROPERTIES UploadHeapProps = {};
        UploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC Desc = {};
        Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        Desc.Width = RequiredSize;
        Desc.Height = 1;
        Desc.DepthOrArraySize = 1;
        Desc.MipLevels = 1;
        Desc.Format = DXGI_FORMAT_UNKNOWN;
        Desc.SampleDesc.Count = 1;
        Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        DxAssert(Device->CreateCommittedResource(
            &UploadHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &Desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&UploadBuffer)));

        UploadBuffer->SetName(L"TextureUploadBuffer");
    }

    // Copy cpu buffer to gpu
    {
        D3D12_TEXTURE_COPY_LOCATION Dst = {};
        {
            Dst.pResource = Texture.Resource;
            Dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            Dst.SubresourceIndex = 0;
        }

        D3D12_TEXTURE_COPY_LOCATION Src = {};
        {
            Src.pResource = UploadBuffer;
            Src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            Device->GetCopyableFootprints(&TextureDesc, 0, 1, 0, &Src.PlacedFootprint, nullptr, nullptr, nullptr);
        }

        // Copy data from the cpu to upload buffer
        // NOTE: This weird copy needs to happen since copy buffer to a texture
        void* MappedMemory;
        D3D12_RANGE ReadRange = { 0, Pixels.Size };
        UploadBuffer->Map(0, &ReadRange, &MappedMemory);
        {
            BYTE* DstPtr = static_cast<BYTE*>(MappedMemory);
            BYTE* SrcPtr = static_cast<BYTE*>(Pixels.Data);

            for (UINT Row = 0; Row < Height; ++Row)
            {
                memcpy(DstPtr, SrcPtr, Width * 4);                  // Only copy the actual width in bytes
                DstPtr += Src.PlacedFootprint.Footprint.RowPitch;   // Move to next row in GPU memory
                SrcPtr += Width * 4;                                // Move to next row in CPU buffer
            }
        }
        UploadBuffer->Unmap(0, nullptr);

        GameRendererSubmitToQueueImmidiate(Device, CommandAllocator, CommandList, CommandQueue, [&](ID3D12GraphicsCommandList* CommandList)
        {
            CommandList->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
            auto Barrier = GameRendererTransition(Texture.Resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            CommandList->ResourceBarrier(1, &Barrier);
        });
    }

    // Hacky but will work for some time
    if (!g_OfflineTextureHeap)
    {
        D3D12_DESCRIPTOR_HEAP_DESC Desc = {};
        Desc.NumDescriptors = 1024; // TODO: Some sufficient number
        Desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        Desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE; // Invisible to shaders
        Desc.NodeMask = 0;
        DxAssert(Device->CreateDescriptorHeap(&Desc, IID_PPV_ARGS(&g_OfflineTextureHeap)));
    }

    Assert(g_OfflineTextureHeapIndex < 1024, "Descriptor heap reached maximum capacity!");

    // Describe and create a SRV for the white texture.
    Texture.SRVDescriptor = g_OfflineTextureHeap->GetCPUDescriptorHandleForHeapStart();
    auto DescriptorSize = Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC Desc = {};
        Desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        Desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        Desc.Texture2D.MipLevels = 1;
        Desc.Texture2D.MostDetailedMip = 0;
        Desc.Texture2D.PlaneSlice = 0;
        Desc.Texture2D.ResourceMinLODClamp = 0.0f;
        Texture.SRVDescriptor.ptr += g_OfflineTextureHeapIndex * DescriptorSize; // Free slot
        Device->CreateShaderResourceView(Texture.Resource, &Desc, Texture.SRVDescriptor);

        g_OfflineTextureHeapIndex++;
    }

    return Texture;
}

internal texture DX12TextureCreate(ID3D12Device* Device, ID3D12CommandAllocator* CommandAllocator, ID3D12GraphicsCommandList* CommandList, ID3D12CommandQueue* CommandQueue, const char* Path)
{
    // Load the image into memory
    int Width, Height, Channels;

    // Load texture file
    stbi_set_flip_vertically_on_load(1);
    u8* Pixels = stbi_load(Path, &Width, &Height, &Channels, 4);
    Assert(Channels == 4, "Failed to enforce 4 channels!");

    return DX12TextureCreate(Device, CommandAllocator, CommandList, CommandQueue, buffer{ Pixels, (u64)Width * Height * 4 }, Width, Height);
}

internal void DX12TextureDestroy(texture* Texture)
{
    // TODO: Freelist
    Texture->Resource->Release();
    Texture->Resource = nullptr;
    Texture->Width = 0;
    Texture->Height = 0;
}
