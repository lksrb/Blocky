#include "DX12Texture.h"

inline u32 calculate_mip_count(u32 width, u32 height)
{
    return (u32)bkm::Floor(bkm::Log2((f32)bkm::Min(width, height))) + 1;
}

//Union used for shader constants
struct DWParam
{
    DWParam(FLOAT f) : Float(f) {}
    DWParam(UINT u) : Uint(u) {}

    void operator= (FLOAT f) { Float = f; }
    void operator= (UINT u) { Uint = u; }

    union
    {
        FLOAT Float;
        UINT Uint;
    };
};

internal void texture_generate_mips(render_backend* Backend, ID3D12Resource* TextureResource, u32 Width, u32 Height, u32 MipCount, DXGI_FORMAT Format)
{
    Assert(MipCount > 1, "No mips to generate.");
    auto Device = Backend->Device;

    // KNOWN ISSUE: We create this every time we generate mips but whatever
    dx12_pipeline MipMapPipeline;
    ID3D12DescriptorHeap* descriptorHeap;
    dx12_root_signature MipMapRootSignature;
    const u32 DescriptorSize = Backend->DescriptorSizes.CBV_SRV_UAV;

    {
        //The compute shader expects 2 floats, the source texture and the destination texture
        CD3DX12_DESCRIPTOR_RANGE srvCbvRanges[2];
        CD3DX12_ROOT_PARAMETER rootParameters[3];
        srvCbvRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
        srvCbvRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);
        rootParameters[0].InitAsConstants(2, 0);
        rootParameters[1].InitAsDescriptorTable(1, &srvCbvRanges[0]);
        rootParameters[2].InitAsDescriptorTable(1, &srvCbvRanges[1]);

        //Static sampler used to get the linearly interpolated color for the mipmaps
        D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.MipLODBias = 0.0f;
        samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        samplerDesc.MinLOD = 0.0f;
        samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
        samplerDesc.MaxAnisotropy = 0;
        samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
        samplerDesc.ShaderRegister = 0;
        samplerDesc.RegisterSpace = 0;
        samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        //Create the root signature for the mipmap compute shader from the parameters and sampler above
        CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init(CountOf(rootParameters), rootParameters, 1, &samplerDesc, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        MipMapRootSignature = dx12_root_signature_create(Device, rootSignatureDesc);

        //Create the descriptor heap with layout: source texture - destination texture
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = 2 * (MipCount - 1);
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&descriptorHeap));

        // Create mipgen pipeline 
        MipMapPipeline = dx12_compute_pipeline_create(Device, MipMapRootSignature, L"Resources/MipGen.hlsl", L"CSMain");
    }

    dx12_submit_to_queue_immidiately(Device, Backend->DirectCommandAllocators[0], Backend->DirectCommandList, Backend->DirectCommandQueue, [&](ID3D12GraphicsCommandList* CommandList)
    {
        //Prepare the shader resource view description for the source texture
        D3D12_SHADER_RESOURCE_VIEW_DESC srcTextureSRVDesc = {};
        srcTextureSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srcTextureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

        //Prepare the unordered access view description for the destination texture
        D3D12_UNORDERED_ACCESS_VIEW_DESC destTextureUAVDesc = {};
        destTextureUAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

        //Set root signature, pso and descriptor heap
        CommandList->SetComputeRootSignature(MipMapRootSignature.Handle);
        CommandList->SetPipelineState(MipMapPipeline.Handle);
        CommandList->SetDescriptorHeaps(1, &descriptorHeap);

        //CPU handle for the first descriptor on the descriptor heap, used to fill the heap
        CD3DX12_CPU_DESCRIPTOR_HANDLE currentCPUHandle(descriptorHeap->GetCPUDescriptorHandleForHeapStart(), 0, DescriptorSize);

        //GPU handle for the first descriptor on the descriptor heap, used to initialize the descriptor tables
        CD3DX12_GPU_DESCRIPTOR_HANDLE currentGPUHandle(descriptorHeap->GetGPUDescriptorHandleForHeapStart(), 0, DescriptorSize);

        //Transition from pixel shader resource to unordered access
        dx12_cmd_transition(CommandList, TextureResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        //Loop through the mipmaps copying from the bigger mipmap to the smaller one with downsampling in a compute shader
        for (uint32_t TopMip = 0; TopMip < MipCount - 1; TopMip++)
        {
            //Get mipmap dimensions
            uint32_t dstWidth = std::max(Width >> (TopMip + 1), 1u);
            uint32_t dstHeight = std::max(Height >> (TopMip + 1), 1u);

            //Create shader resource view for the source texture in the descriptor heap
            srcTextureSRVDesc.Format = Format;
            srcTextureSRVDesc.Texture2D.MipLevels = 1;
            srcTextureSRVDesc.Texture2D.MostDetailedMip = TopMip;
            Device->CreateShaderResourceView(TextureResource, &srcTextureSRVDesc, currentCPUHandle);
            currentCPUHandle.Offset(1, DescriptorSize);

            //Create unordered access view for the destination texture in the descriptor heap
            destTextureUAVDesc.Format = Format;
            destTextureUAVDesc.Texture2D.MipSlice = TopMip + 1;
            Device->CreateUnorderedAccessView(TextureResource, nullptr, &destTextureUAVDesc, currentCPUHandle);
            currentCPUHandle.Offset(1, DescriptorSize);

            //Pass the destination texture pixel size to the shader as constants
            CommandList->SetComputeRoot32BitConstant(0, DWParam(1.0f / dstWidth).Uint, 0);
            CommandList->SetComputeRoot32BitConstant(0, DWParam(1.0f / dstHeight).Uint, 1);

            //Pass the source and destination texture views to the shader via descriptor tables
            CommandList->SetComputeRootDescriptorTable(1, currentGPUHandle);
            currentGPUHandle.Offset(1, DescriptorSize);
            CommandList->SetComputeRootDescriptorTable(2, currentGPUHandle);
            currentGPUHandle.Offset(1, DescriptorSize);

            //Dispatch the compute shader with one thread per 8x8 pixels
            CommandList->Dispatch(std::max(dstWidth / 8, 1u), std::max(dstHeight / 8, 1u), 1);

            //Wait for all accesses to the destination texture UAV to be finished before generating the next mipmap, as it will be the source texture for the next mipmap
            auto UAVRB = CD3DX12_RESOURCE_BARRIER::UAV(TextureResource);
            CommandList->ResourceBarrier(1, &UAVRB);
        }

        dx12_cmd_transition(CommandList, TextureResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    });

    dx12_pipeline_destroy(&MipMapPipeline);
    dx12_root_signature_destroy(&MipMapRootSignature);
    descriptorHeap->Release();
}

internal texture texture_create(render_backend* Backend, u32 Width, u32 Height, buffer Pixels, bool GenerateMips, texture_format NewFormat, const wchar_t* DebugName)
{
    Assert(Pixels.Data && Pixels.Size, "Cannot create texture from empty buffer!");
    Assert(Pixels.Size <= Width * Height * 4, "Buffer is larger than Width * Height * FormatChannels!"); // TODO: Support more formats

    auto Device = Backend->Device;
    auto CommandAllocator = Backend->DirectCommandAllocators[0];
    auto CommandList = Backend->DirectCommandList;
    auto CommandQueue = Backend->DirectCommandQueue;

    // Note: SRGB textures are automatically gamma corrected before read from the shader
    const auto Format = DXGI_FORMAT_R8G8B8A8_UNORM;

    texture Texture = {};
    Texture.Width = Width;
    Texture.Height = Height;
    Texture.Format = Format;
    const u32 Channels = 4; // RGBA for now

    // Describe and create a Texture2D.
    D3D12_RESOURCE_DESC TextureDesc = {};
    {
        TextureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        TextureDesc.MipLevels = GenerateMips ? calculate_mip_count(Width, Height) : 1;
        TextureDesc.Format = Format;
        TextureDesc.Width = Width;
        TextureDesc.Height = Height;
        TextureDesc.DepthOrArraySize = 1;
        TextureDesc.SampleDesc.Count = 1;
        TextureDesc.SampleDesc.Quality = 0;
        TextureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        //TextureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        TextureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

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
            IID_PPV_ARGS(&Texture.Handle)));

        Texture.Width = Width;
        Texture.Height = Height;
        Texture.Handle->SetName(DebugName);
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
            Dst.pResource = Texture.Handle;
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

        dx12_submit_to_queue_immidiately(Device, CommandAllocator, CommandList, CommandQueue, [&](ID3D12GraphicsCommandList* CommandList)
        {
            CommandList->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
            dx12_cmd_transition(CommandList, Texture.Handle, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        });

        UploadBuffer->Release();
    }

    Assert(Backend->OfflineTextureHeapIndex < 1024, "Descriptor heap reached maximum capacity!");

    if (TextureDesc.MipLevels > 1)
    {
        texture_generate_mips(Backend, Texture.Handle, Width, Height, TextureDesc.MipLevels, Format);
    }

    // Describe and create a SRV for the white texture.
    Texture.SRVDescriptor = Backend->OfflineTextureHeap.Handle->GetCPUDescriptorHandleForHeapStart();
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC Desc = {};
        Desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        Desc.Format = Format;
        Desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        Desc.Texture2D.MipLevels = TextureDesc.MipLevels;
        Desc.Texture2D.MostDetailedMip = 0;
        Desc.Texture2D.PlaneSlice = 0;
        Desc.Texture2D.ResourceMinLODClamp = 0.0f;
        Texture.SRVDescriptor.ptr += Backend->OfflineTextureHeapIndex * Backend->DescriptorSizes.CBV_SRV_UAV; // Free slot
        Device->CreateShaderResourceView(Texture.Handle, &Desc, Texture.SRVDescriptor);

        Backend->OfflineTextureHeapIndex++;
    }

    return Texture;
}

internal texture texture_create(render_backend* Backend, const char* Path, bool GenerateMips)
{
    // Load the image into memory
    int Width, Height, Channels;

    // Load texture file
    stbi_set_flip_vertically_on_load(1);
    u8* Pixels = stbi_load(Path, &Width, &Height, &Channels, 4);
    Assert(Channels == 4, "Failed to enforce 4 channels!");

    return texture_create(Backend, Width, Height, buffer{ Pixels, (u64)Width * Height * 4 }, GenerateMips);
}

internal void texture_destroy(render_backend* Backend, texture* Texture)
{
    Texture->Handle->Release();
    Texture->Handle = nullptr;
    Texture->Width = 0;
    Texture->Height = 0;
}
