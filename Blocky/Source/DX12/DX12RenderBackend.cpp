#include "DX12RenderBackend.h"

#include "DX12Texture.cpp"
#include "DX12Buffer.cpp"

internal dx12_render_backend* dx12_render_backend_create(arena* Arena, const win32_window& Window)
{
    dx12_render_backend* Backend = arena_new(Arena, dx12_render_backend);

    // Enable debug layer
    if (DX12_ENABLE_DEBUG_LAYER)
    {
        // Enable debug layer
        // This is sort of like validation layers in Vulkan
        DxAssert(D3D12GetDebugInterface(IID_PPV_ARGS(&Backend->DebugInterface)));
        Backend->DebugInterface->EnableDebugLayer();

#if BK_DEBUG
        // Enable DXGI debug - for more memory leaks checking
        DxAssert(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&Backend->DxgiDebugInterface)));
        Backend->DxgiDebugInterface->EnableLeakTrackingForThread();
#endif
    }

    // Create device

    // This takes ~300 ms, insane
    DxAssert(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Backend->Device)));

    // Create factory
    DxAssert(CreateDXGIFactory2(DX12_ENABLE_DEBUG_LAYER ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS(&Backend->Factory)));

    // Debug info queue
    if (DX12_ENABLE_DEBUG_LAYER)
    {
        g_DebugInfoQueue = dx12_info_queue_create(Backend->Device);
    }

    // Create direct command queue
    {
        // Create direct command allocator
        for (u32 i = 0; i < FIF; i++)
        {
            DxAssert(Backend->Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&Backend->DirectCommandAllocators[i])));
        }

        D3D12_COMMAND_QUEUE_DESC Desc = {};
        Desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; // Most general - For draw, compute and copy commands
        Desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
        Desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        Desc.NodeMask = 0;
        DxAssert(Backend->Device->CreateCommandQueue(&Desc, IID_PPV_ARGS(&Backend->DirectCommandQueue)));

        // Create direct command list
        {
            DxAssert(Backend->Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, Backend->DirectCommandAllocators[Backend->CurrentBackBufferIndex], nullptr, IID_PPV_ARGS(&Backend->DirectCommandList)));

            // Command lists are created in the recording state, but there is nothing
            // to record yet. The main loop expects it to be closed, so close it now.
            DxAssert(Backend->DirectCommandList->Close());
        }

        // Fence
        DxAssert(Backend->Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Backend->Fence)));
        Backend->DirectFenceEvent = CreateEvent(nullptr, false, false, nullptr);
        Assert(Backend->DirectFenceEvent != INVALID_HANDLE_VALUE, "Could not create fence event.");
    }

    // Create swapchain
    {
        Backend->SwapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        Backend->SwapChain = dx12_create_swapchain(Backend->Factory, Backend->DirectCommandQueue, Window.Handle, Window.ClientAreaWidth, Window.ClientAreaHeight, Backend->SwapChainFormat, FIF);

        Backend->CurrentBackBufferIndex = Backend->SwapChain->GetCurrentBackBufferIndex();

        //Backend->SwapChain->SetMaximumFrameLatency(FIF); // TODO: This is something newer
        //Backend->FrameLatencyEvent = Backend->SwapChain->GetFrameLatencyWaitableObject();
    }

    // Views are descriptors located in the GPU memory.
    // They describe how memory of particular resource is layed out

    // Cache descriptor sizes, they should never change unless ID3D12 is recreated.
    Backend->DescriptorSizes.RTV = Backend->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    Backend->DescriptorSizes.DSV = Backend->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    Backend->DescriptorSizes.CBV_SRV_UAV = Backend->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // View allocators
    Backend->RTVAllocator.Create(Arena, Backend->Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, false, 1024);
    Backend->DSVAllocator.Create(Arena, Backend->Device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, false, 1024);
    Backend->SRVCBVUAV_Allocator.Create(Arena, Backend->Device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true, 1024);

    // Create Render Target Views
    {
        D3D12_RENDER_TARGET_VIEW_DESC RtvDesc = {};
        RtvDesc.Format = Backend->SwapChainFormat;
        RtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        for (u32 i = 0; i < FIF; ++i)
        {
            DxAssert(Backend->SwapChain->GetBuffer(i, IID_PPV_ARGS(&Backend->SwapChainBackbuffers[i])));
            Backend->SwapChainBackbuffers[i]->SetName(L"SwapchainRenderTargetTexture");
            Backend->SwapChainBufferRTVViews[i] = Backend->RTVAllocator.Alloc();
            Backend->Device->CreateRenderTargetView(Backend->SwapChainBackbuffers[i], &RtvDesc, Backend->SwapChainBufferRTVViews[i].CPU);
        }
    }

    dx12_render_backend_initialize_pipeline(Arena, Backend);

    return Backend;
}

internal void dx12_render_backend_destroy(dx12_render_backend* Backend)
{
    // Wait for GPU to finish
    dx12_render_backend_flush(Backend);

    for (u32 i = 0; i < FIF; i++)
    {
        Backend->MainPass.RenderBuffers[i]->Release();
        Backend->MainPass.DepthBuffers[i]->Release();


        Backend->DirectCommandAllocators[i]->Release();
        Backend->SwapChainBackbuffers[i]->Release();

        // Quad
        dx12_vertex_buffer_destroy(&Backend->Quad.VertexBuffers[i]);

        // Cuboid
        dx12_vertex_buffer_destroy(&Backend->Cuboid.TransformVertexBuffers[i]);
        dx12_vertex_buffer_destroy(&Backend->QuadedCuboid.VertexBuffers[i]);

        // Distant Quad
        dx12_vertex_buffer_destroy(&Backend->DistantQuad.VertexBuffers[i]);

        // Light buffers
        dx12_constant_buffer_destroy(&Backend->LightEnvironmentConstantBuffers[i]);

        // HUD
        dx12_vertex_buffer_destroy(&Backend->HUD.VertexBuffers[i]);

        // Shadows
#if ENABLE_SHADOW_PASS
        Backend->ShadowPass.ShadowMaps[i]->Release();
#endif
    }

    // Quad
    dx12_pipeline_destroy(&Backend->Quad.Pipeline);
    dx12_index_buffer_destroy(&Backend->Quad.IndexBuffer);

    // Cuboid
    dx12_pipeline_destroy(&Backend->Cuboid.Pipeline);
    dx12_vertex_buffer_destroy(&Backend->Cuboid.PositionsVertexBuffer);

    dx12_root_signature_destroy(&Backend->Cuboid.RootSignature);

    // Quaded cuboid
    dx12_pipeline_destroy(&Backend->QuadedCuboid.Pipeline);

    // Skybox
    dx12_vertex_buffer_destroy(&Backend->Skybox.VertexBuffer);
    dx12_index_buffer_destroy(&Backend->Skybox.IndexBuffer);
    dx12_pipeline_destroy(&Backend->Skybox.Pipeline);
    dx12_root_signature_destroy(&Backend->Skybox.RootSignature);

    // Distant Quad
    dx12_pipeline_destroy(&Backend->DistantQuad.Pipeline);

    // Fullscreen Pass
    dx12_pipeline_destroy(&Backend->FullscreenPass.Pipeline);
    dx12_root_signature_destroy(&Backend->FullscreenPass.RootSignature);

    // Shadows
#if ENABLE_SHADOW_PASS
    dx12_pipeline_destroy(&Backend->ShadowPass.Pipeline);
    dx12_root_signature_destroy(&Backend->ShadowPass.RootSignature);
    //dx12_descriptor_heap_destroy(&Backend->ShadowPass.DSVDescriptorHeap);
#endif

    // HUD
    dx12_pipeline_destroy(&Backend->HUD.Pipeline);

    // Main pass
    texture_destroy(Backend, &Backend->WhiteTexture);
    //dx12_descriptor_heap_destroy(&Backend->MainPass.RTVDescriptorHeap);
    //dx12_descriptor_heap_destroy(&Backend->MainPass.DSVDescriptorHeap);
    //dx12_descriptor_heap_destroy(&Backend->MainPass.SRVDescriptorHeap);

    //dx12_descriptor_heap_destroy(&Backend->TextureDescriptorHeap);

    dx12_root_signature_destroy(&Backend->RootSignature);

    // dx12_descriptor_heap_destroy(&Backend->OfflineTextureHeap);

    // Swapchain
    //dx12_descriptor_heap_destroy(&Backend->RTVDescriptorHeap);
    Backend->SwapChain->Release();
    Backend->Fence->Release();
    Backend->DirectCommandList->Release();
    Backend->DirectCommandQueue->Release();
    Backend->Factory->Release();
    Backend->Device->Release();

#if DX12_ENABLE_DEBUG_LAYER
    // Report all memory leaks
    Backend->DxgiDebugInterface->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_ALL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
    Backend->DxgiDebugInterface->Release();
    dx12_info_queue_dump(g_DebugInfoQueue);
    g_DebugInfoQueue->Release();
#endif
}

internal void dx12_render_backend_initialize_pipeline(arena* Arena, dx12_render_backend* Backend)
{
    auto Device = Backend->Device;
    Backend->MainPass.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    //Backend->MainPass.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

    DXGI_SWAP_CHAIN_DESC SwapChainDesc;
    DxAssert(Backend->SwapChain->GetDesc(&SwapChainDesc));

    // Storage for offlines textures that the game might need, assuming that its cheaper to copy descriptor rather than creating them on the spot
    //Backend->OfflineTextureHeap = dx12_descriptor_heap_create(Device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, false, 1024);

    // Main pass
    {
        auto& MainPass = Backend->MainPass;

        // Create render resources
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC Desc = {};
            Desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            Desc.Format = MainPass.Format;
            Desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            Desc.Texture2D.MipLevels = 1;
            Desc.Texture2D.MostDetailedMip = 0;
            Desc.Texture2D.PlaneSlice = 0;
            Desc.Texture2D.ResourceMinLODClamp = 0.0f;

            const bool AllowUnorderedAccess = true;
            for (u32 i = 0; i < FIF; i++)
            {
                MainPass.RenderBuffers[i] = dx12_render_target_create(Device, MainPass.Format, 
                    SwapChainDesc.BufferDesc.Width, SwapChainDesc.BufferDesc.Height, AllowUnorderedAccess, L"MainPassBuffer");

                // RTV
                MainPass.RenderBuffersRTVViews[i] = Backend->RTVAllocator.Alloc();
                Backend->Device->CreateRenderTargetView(MainPass.RenderBuffers[i], nullptr, MainPass.RenderBuffersRTVViews[i].CPU);
                
                // SRV
                MainPass.RenderBuffersSRVViews[i] = Backend->SRVCBVUAV_Allocator.Alloc();
                Backend->Device->CreateShaderResourceView(MainPass.RenderBuffers[i], &Desc, MainPass.RenderBuffersSRVViews[i].CPU);
            }
        }

        // Generate mip maps
        {

        }

        // Create depth resources 
        {
            const DXGI_FORMAT Format = DXGI_FORMAT_D32_FLOAT;

            // Create depth buffers
            for (u32 i = 0; i < FIF; i++)
            {
                MainPass.DepthBuffers[i] = dx12_depth_buffer_create(Device, SwapChainDesc.BufferDesc.Width, SwapChainDesc.BufferDesc.Height, Format, Format, L"MainPassDepthBuffer");

                // Create depth-stencil view
                D3D12_DEPTH_STENCIL_VIEW_DESC Desc = {};
                Desc.Format = Format;
                Desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
                Desc.Texture2D.MipSlice = 0;
                Desc.Flags = D3D12_DSV_FLAG_NONE;
                // AAAAAAAAA these are inferred I think
                
                MainPass.DepthBuffersViews[i] = Backend->DSVAllocator.Alloc();
                Backend->Device->CreateDepthStencilView(MainPass.DepthBuffers[i], &Desc, MainPass.DepthBuffersViews[i].CPU);
            }
        }
    }

    // Bloom Pass
    {
        auto& BloomPass = Backend->BloomPass;

        // Descriptor range for one UAV (u0)
        D3D12_DESCRIPTOR_RANGE Ranges[2] = {};
        Ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        Ranges[0].NumDescriptors = 1;
        Ranges[0].BaseShaderRegister = 0;
        Ranges[0].RegisterSpace = 0;
        Ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        // Descriptor range for two SRV (t0, t1)
        Ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        Ranges[1].NumDescriptors = 2; // u_Texture, u_BloomTexture
        Ranges[1].BaseShaderRegister = 0;
        Ranges[1].RegisterSpace = 0;
        Ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        // One sampler (s0)
        D3D12_STATIC_SAMPLER_DESC Samplers[1] = {};

        // Sampler
        //Samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        Samplers[0].Filter = D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR; // TODO: Check which one
        Samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        Samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        Samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        Samplers[0].MipLODBias = 0;
        Samplers[0].MaxAnisotropy = 1;
        Samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        Samplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
        Samplers[0].MinLOD = 0.0f;
        Samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
        Samplers[0].ShaderRegister = 0;
        Samplers[0].RegisterSpace = 0;
        Samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // Root parameter: descriptor table with one range
        D3D12_ROOT_PARAMETER RootParam = {};
        RootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        RootParam.DescriptorTable.NumDescriptorRanges = CountOf(Ranges) - 1;
        RootParam.DescriptorTable.pDescriptorRanges = Ranges;
        RootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // Root signature description
        D3D12_ROOT_SIGNATURE_DESC RootSigDesc = {};
        RootSigDesc.NumParameters = 1;
        RootSigDesc.pParameters = &RootParam;
        RootSigDesc.NumStaticSamplers = CountOf(Samplers) - 1;
        RootSigDesc.pStaticSamplers = Samplers;
        RootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        BloomPass.RootSignature = dx12_root_signature_create(Backend->Device, RootSigDesc);
        BloomPass.PipelineCompute = dx12_compute_pipeline_create(Backend->Device, BloomPass.RootSignature, L"Resources/Bloom.hlsl", L"CSMain");

        //auto Texture1 = dx12_render_target_create(Device, MainPass.Format, SwapChainDesc.BufferDesc.Width, SwapChainDesc.BufferDesc.Height, false, L"BloomTexture1");
        //auto Texture2 = dx12_render_target_create(Device, MainPass.Format, SwapChainDesc.BufferDesc.Width, SwapChainDesc.BufferDesc.Height, false, L"BloomTexture2");
    }

    // Root Signature
    {
        D3D12_STATIC_SAMPLER_DESC Samplers[1] = {};

        // Sampler
        Samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        Samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        Samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        Samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        Samplers[0].MipLODBias = 0;
        Samplers[0].MaxAnisotropy = 1;
        Samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        Samplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
        Samplers[0].MinLOD = 0.0f;
        Samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
        Samplers[0].ShaderRegister = 0;
        Samplers[0].RegisterSpace = 0;
        Samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_DESCRIPTOR_RANGE Ranges[1] = {};
        Ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        Ranges[0].NumDescriptors = c_MaxTexturesPerDrawCall; // Find upper driver limit
        Ranges[0].BaseShaderRegister = 0;
        Ranges[0].RegisterSpace = 0;
        Ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER Parameters[3] = {};
        Parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        Parameters[0].Constants.Num32BitValues = sizeof(cuboid_buffer_data) / 4;
        Parameters[0].Constants.ShaderRegister = 0;  // b0
        Parameters[0].Constants.RegisterSpace = 0;
        Parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

        // Light environment
        Parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        Parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        Parameters[1].Descriptor.ShaderRegister = 1; // b1
        Parameters[1].Descriptor.RegisterSpace = 0;

        // Descriptor table
        Parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        Parameters[2].DescriptorTable.NumDescriptorRanges = CountOf(Ranges);
        Parameters[2].DescriptorTable.pDescriptorRanges = Ranges;
        Parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC Desc = {};
        Desc.NumParameters = CountOf(Parameters);
        Desc.pParameters = Parameters;
        Desc.NumStaticSamplers = CountOf(Samplers);
        Desc.pStaticSamplers = Samplers;
        Desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        Backend->RootSignature = dx12_root_signature_create(Device, Desc);
    }

    // Quad Pipeline
    {
        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC InputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXINDEX", 0, DXGI_FORMAT_R32_UINT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        Backend->Quad.Pipeline = dx12_graphics_pipeline_create(Device, Backend->RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/Quad.hlsl", Backend->MainPass.Format, D3D12_CULL_MODE_NONE);

        for (u32 i = 0; i < FIF; i++)
        {
            Backend->Quad.VertexBuffers[i] = dx12_vertex_buffer_create(Device, sizeof(quad_vertex) * c_MaxQuadVertices);
        }

        // Quad Index buffer
        {
            u32 MaxQuadIndexCount = bkm::Max(c_MaxQuadedCuboidIndices, c_MaxQuadIndices);
            u32* QuadIndices = arena_new_array(Arena, u32, MaxQuadIndexCount);
            u32 Offset = 0;
            for (u32 i = 0; i < MaxQuadIndexCount; i += 6)
            {
                QuadIndices[i + 0] = Offset + 0;
                QuadIndices[i + 1] = Offset + 1;
                QuadIndices[i + 2] = Offset + 2;

                QuadIndices[i + 3] = Offset + 2;
                QuadIndices[i + 4] = Offset + 3;
                QuadIndices[i + 5] = Offset + 0;

                Offset += 4;
            }
            Backend->Quad.IndexBuffer = dx12_index_buffer_create(Device, Backend->DirectCommandAllocators[0], Backend->DirectCommandList, Backend->DirectCommandQueue, QuadIndices, MaxQuadIndexCount);
        }
    }

    // Basic Cuboids - if a cuboid does not have unique texture for each quad, then we can use this
    // TODO: We could even create a pipeline for NoRotNoScale cuboids and pass only translation vector
    {
        auto& Cuboid = Backend->Cuboid;

        // Root Signature
        {
            D3D12_STATIC_SAMPLER_DESC Samplers[2] = {};

            // Game Texture Sampler
            Samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
            Samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            Samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            Samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            Samplers[0].MipLODBias = 0;
            Samplers[0].MaxAnisotropy = 1;
            Samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            Samplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
            Samplers[0].MinLOD = 0.0f;
            Samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
            Samplers[0].ShaderRegister = 0; // s0
            Samplers[0].RegisterSpace = 0; // space0
            Samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            // Shadow pass
            Samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
            Samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            Samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            Samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            Samplers[1].MipLODBias = 0;
            Samplers[1].MaxAnisotropy = 1;
            Samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            Samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
            Samplers[1].MinLOD = 0.0f;
            Samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
            Samplers[1].ShaderRegister = 1; // s0
            Samplers[1].RegisterSpace = 0; // space0
            Samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            // Game Textures
            D3D12_DESCRIPTOR_RANGE TextureRange = {};
            TextureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            TextureRange.NumDescriptors = c_MaxTexturesPerDrawCall + FIF; // Find upper driver limit
            TextureRange.BaseShaderRegister = 0;
            TextureRange.RegisterSpace = 0;
            TextureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            // Shadow Map
            D3D12_DESCRIPTOR_RANGE ShadowMapRange = {};
            ShadowMapRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            ShadowMapRange.NumDescriptors = FIF;
            ShadowMapRange.BaseShaderRegister = 0; // t0
            ShadowMapRange.RegisterSpace = 0; // space0
            ShadowMapRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_ROOT_PARAMETER Parameters[3] = {};
            Parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            Parameters[0].Constants.Num32BitValues = sizeof(cuboid_buffer_data) / 4;
            Parameters[0].Constants.ShaderRegister = 0;  // b0
            Parameters[0].Constants.RegisterSpace = 0;
            Parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

            // Light environment
            Parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            Parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            Parameters[1].Descriptor.ShaderRegister = 1; // b1
            Parameters[1].Descriptor.RegisterSpace = 0;

            // Textures
            Parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            Parameters[2].DescriptorTable.NumDescriptorRanges = 1;
            Parameters[2].DescriptorTable.pDescriptorRanges = &TextureRange;
            Parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_ROOT_SIGNATURE_DESC Desc = {};
            Desc.NumParameters = CountOf(Parameters);
            Desc.pParameters = Parameters;
            Desc.NumStaticSamplers = CountOf(Samplers);
            Desc.pStaticSamplers = Samplers;
            Desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            Cuboid.RootSignature = dx12_root_signature_create(Device, Desc);

            //dx12_root_signature CuboidRootSignature = dx12_root_signature_create(Device, Desc);
        }

        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC InputElementDescs[] =
        {
            // Per vertex
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

            // Per instance
            { "TRANSFORMA", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TRANSFORMB", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
            { "TRANSFORMC", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TRANSFORMD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TEXINDEX", 0, DXGI_FORMAT_R32_UINT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        };

        Cuboid.Pipeline = dx12_graphics_pipeline_create(Device, Backend->Cuboid.RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/Cuboid.hlsl", Backend->MainPass.Format, D3D12_CULL_MODE_BACK);

        Cuboid.PositionsVertexBuffer = dx12_vertex_buffer_create(Device, sizeof(c_CuboidVertices));

        dx12_submit_to_queue_immidiately(Device, Backend->DirectCommandAllocators[0], Backend->DirectCommandList, Backend->DirectCommandQueue, [Backend, &Cuboid](ID3D12GraphicsCommandList* CommandList)
        {
            dx12_vertex_buffer_set_data(&Cuboid.PositionsVertexBuffer, CommandList, c_CuboidVertices, sizeof(c_CuboidVertices));
        });

        for (u32 i = 0; i < FIF; i++)
        {
            Cuboid.TransformVertexBuffers[i] = dx12_vertex_buffer_create(Device, sizeof(cuboid_transform_vertex_data) * c_MaxCubePerBatch);
        }
    }

    // Quaded cuboid pipeline
    {
        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC InputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXINDEX", 0, DXGI_FORMAT_R32_UINT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        Backend->QuadedCuboid.Pipeline = dx12_graphics_pipeline_create(Device, Backend->RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/QuadedCuboid.hlsl", Backend->MainPass.Format);

        for (u32 i = 0; i < FIF; i++)
        {
            Backend->QuadedCuboid.VertexBuffers[i] = dx12_vertex_buffer_create(Device, sizeof(quaded_cuboid_vertex) * c_MaxQuadedCuboidVertices);
        }
    }

    // HUD renderer
    {
        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC InputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXINDEX", 0, DXGI_FORMAT_R32_UINT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        Backend->HUD.Pipeline = dx12_graphics_pipeline_create(Device, Backend->RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/HUD.hlsl", Backend->MainPass.Format);

        for (u32 i = 0; i < FIF; i++)
        {
            Backend->HUD.VertexBuffers[i] = dx12_vertex_buffer_create(Device, sizeof(hud_quad_vertex) * c_MaxHUDQuadVertices);
        }
    }

    // Skybox
    {
        // Skybox Root Signature
        {
            D3D12_ROOT_PARAMETER Parameters[1] = {};
            Parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            Parameters[0].Constants.Num32BitValues = sizeof(skybox_quad_buffer_data) / 4;
            Parameters[0].Constants.ShaderRegister = 0;  // b0
            Parameters[0].Constants.RegisterSpace = 0;
            Parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            D3D12_ROOT_SIGNATURE_DESC Desc = {};
            Desc.NumParameters = CountOf(Parameters);
            Desc.pParameters = Parameters;
            Desc.NumStaticSamplers = 0;
            Desc.pStaticSamplers = nullptr;
            Desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            Backend->Skybox.RootSignature = dx12_root_signature_create(Device, Desc);
        }

        D3D12_INPUT_ELEMENT_DESC InputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        Backend->Skybox.Pipeline = dx12_graphics_pipeline_create(Device, Backend->Skybox.RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/Skybox.hlsl", Backend->MainPass.Format, D3D12_CULL_MODE_BACK, false);
        Backend->Skybox.VertexBuffer = dx12_vertex_buffer_create(Device, sizeof(c_SkyboxVertices));

        dx12_submit_to_queue_immidiately(Device, Backend->DirectCommandAllocators[0], Backend->DirectCommandList, Backend->DirectCommandQueue, [Backend](ID3D12GraphicsCommandList* CommandList)
        {
            dx12_vertex_buffer_set_data(&Backend->Skybox.VertexBuffer, CommandList, c_SkyboxVertices, sizeof(c_SkyboxVertices));
        });
        Backend->Skybox.IndexBuffer = dx12_index_buffer_create(Device, Backend->DirectCommandAllocators[0], Backend->DirectCommandList, Backend->DirectCommandQueue, c_SkyboxIndices, CountOf(c_SkyboxIndices));
    }

    // Distant quads
    {
        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC InputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        Backend->DistantQuad.Pipeline = dx12_graphics_pipeline_create(Device, Backend->Skybox.RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/DistantQuad.hlsl", Backend->MainPass.Format, D3D12_CULL_MODE_NONE, false);

        for (u32 i = 0; i < FIF; i++)
        {
            Backend->DistantQuad.VertexBuffers[i] = dx12_vertex_buffer_create(Device, sizeof(distant_quad_vertex) * c_MaxHUDQuadVertices);
        }
    }

    // Create white texture
    {
        u32 WhiteColor = 0xffffffff;
        buffer Buffer = { &WhiteColor, sizeof(u32) };
        Backend->WhiteTexture = texture_create(Backend, 1, 1, Buffer);

        D3D12_SHADER_RESOURCE_VIEW_DESC Desc = {};
        Desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        Desc.Format = Backend->WhiteTexture.Format;
        Desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        Desc.Texture2D.MipLevels = 1;
        Desc.Texture2D.MostDetailedMip = 0;
        Desc.Texture2D.PlaneSlice = 0;
        Desc.Texture2D.ResourceMinLODClamp = 0.0f;

        Backend->WhiteTextureDescriptorHandle = Backend->SRVCBVUAV_Allocator.Alloc();
        Device->CreateShaderResourceView(Backend->WhiteTexture.Handle, &Desc, Backend->WhiteTextureDescriptorHandle.CPU);
    }

    // Shadow Pass
#if ENABLE_SHADOW_PASS
    {
        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC InputElementDescs[] =
        {
            // Per vertex
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

            // Per instance
            { "TRANSFORMA", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TRANSFORMB", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
            { "TRANSFORMC", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TRANSFORMD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TEXINDEX", 0, DXGI_FORMAT_R32_UINT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        };

        // Root Signature
        {
            D3D12_ROOT_PARAMETER Parameters[1] = {};
            Parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            Parameters[0].Constants.Num32BitValues = sizeof(shadow_pass_buffer_data) / 4;
            Parameters[0].Constants.ShaderRegister = 0;  // b0
            Parameters[0].Constants.RegisterSpace = 0;
            Parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

            D3D12_ROOT_SIGNATURE_DESC Desc = {};
            Desc.NumParameters = CountOf(Parameters);
            Desc.pParameters = Parameters;
            Desc.NumStaticSamplers = 0;
            Desc.pStaticSamplers = nullptr;
            Desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            Backend->ShadowPass.RootSignature = dx12_root_signature_create(Device, Desc);
        }
        Backend->ShadowPass.Pipeline = dx12_graphics_pipeline_create(Device, Backend->ShadowPass.RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/Shadow.hlsl", Backend->MainPass.Format, D3D12_CULL_MODE_FRONT, true, 0);

        // Create resources
        const wchar_t* DebugNames[]{
            L"ShadowMap0",
            L"ShadowMap1"
        };

        // Create shadowmap buffers
        for (u32 i = 0; i < FIF; i++)
        {
            Backend->ShadowPass.ShadowMaps[i] = dx12_depth_buffer_create(Device, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_D32_FLOAT, DebugNames[i], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

            // Update the depth-stencil view.
            D3D12_DEPTH_STENCIL_VIEW_DESC DSV = {};
            DSV.Format = DXGI_FORMAT_D32_FLOAT;
            DSV.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            DSV.Texture2D.MipSlice = 0;
            DSV.Flags = D3D12_DSV_FLAG_NONE;

            Backend->ShadowPass.ShadowMapsDSVViews[i] = Backend->DSVAllocator.Alloc();
            Backend->Device->CreateDepthStencilView(Backend->ShadowPass.ShadowMaps[i], &DSV, Backend->ShadowPass.ShadowMapsDSVViews[i].CPU);
        }

        // Shadow map
        for (u32 i = 0; i < FIF; i++)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC Desc = {};
            Desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            Desc.Format = DXGI_FORMAT_R32_FLOAT;
            Desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            Desc.Texture2D.MipLevels = 1;
            Desc.Texture2D.MostDetailedMip = 0;
            Desc.Texture2D.PlaneSlice = 0;
            Desc.Texture2D.ResourceMinLODClamp = 0.0f;
            Backend->ShadowPass.ShadowMapsSRVViews[i] = Backend->SRVCBVUAV_Allocator.Alloc();
            Backend->Device->CreateShaderResourceView(Backend->ShadowPass.ShadowMaps[i], &Desc, Backend->ShadowPass.ShadowMapsSRVViews[i].CPU);
        }
    }
#endif

    // Create light environment constant buffer for each frame
    for (u32 i = 0; i < FIF; i++)
    {
        Backend->LightEnvironmentConstantBuffers[i] = dx12_constant_buffer_create(Device, sizeof(light_environment));
    }

    // Fullscreen pipeline
    {
        // Root Signature
        {
            D3D12_DESCRIPTOR_RANGE MainPassRange = {};
            MainPassRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            MainPassRange.NumDescriptors = 1;
            MainPassRange.BaseShaderRegister = 0; // t0
            MainPassRange.RegisterSpace = 0; // space0
            MainPassRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_STATIC_SAMPLER_DESC Samplers[1] = {};
            Samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
            Samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            Samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            Samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            Samplers[0].MipLODBias = 0;
            Samplers[0].MaxAnisotropy = 1;
            Samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            Samplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
            Samplers[0].MinLOD = 0.0f;
            Samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
            Samplers[0].ShaderRegister = 0;
            Samplers[0].RegisterSpace = 0;
            Samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_ROOT_PARAMETER Parameters[1] = {};
            Parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            Parameters[0].DescriptorTable.NumDescriptorRanges = 1;
            Parameters[0].DescriptorTable.pDescriptorRanges = &MainPassRange;
            Parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_ROOT_SIGNATURE_DESC Desc = {};
            Desc.NumParameters = CountOf(Parameters);
            Desc.pParameters = Parameters;
            Desc.NumStaticSamplers = CountOf(Samplers);
            Desc.pStaticSamplers = Samplers;
            //Desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            Backend->FullscreenPass.RootSignature = dx12_root_signature_create(Device, Desc);
        }
        Backend->FullscreenPass.Pipeline = dx12_graphics_pipeline_create(Device, Backend->FullscreenPass.RootSignature, nullptr, 0, L"Resources/Fullscreen.hlsl", Backend->SwapChainFormat, D3D12_CULL_MODE_FRONT, false, 1);
    }
}

internal void d3d12_render_backend_render(dx12_render_backend* Backend, const game_renderer* Renderer, ImDrawData* ImGuiDrawData, ID3D12DescriptorHeap* ImGuiDescriptorHeap)
{
    // Set and clear render target view
    const v4 ClearColor = { 0.2f, 0.3f, 0.8f, 1.0f };

    // Get current frame stuff
    auto CommandList = Backend->DirectCommandList;
    auto CurrentBackBufferIndex = Backend->CurrentBackBufferIndex;

    auto DirectCommandAllocator = Backend->DirectCommandAllocators[CurrentBackBufferIndex];
    auto SwapChainBackBuffer = Backend->SwapChainBackbuffers[CurrentBackBufferIndex];
    auto SwapChainRTV = Backend->SwapChainBufferRTVViews[CurrentBackBufferIndex];
    auto& FrameFenceValue = Backend->FrameFenceValues[CurrentBackBufferIndex];

    // TODO: Figure out if this function needs to be called every frame.
    DXGI_SWAP_CHAIN_DESC SwapChainDesc;
    DxAssert(Backend->SwapChain->GetDesc(&SwapChainDesc));

    // Reset state
    DxAssert(DirectCommandAllocator->Reset());

    // Copy texture's descriptor to our renderer's descriptor
    if (true)
    {
        auto DstSRV = Backend->TextureDescriptorHeap.cpu_base();

        // Skip white texture
        DstSRV.ptr += Backend->DescriptorSizes.CBV_SRV_UAV;
        // Textures
        for (u32 i = 1; i < Renderer->CurrentTextureStackIndex; i++)
        {
            Backend->Device->CopyDescriptorsSimple(1, DstSRV, Renderer->TextureStack[i]->SRVDescriptor, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            DstSRV.ptr += Backend->DescriptorSizes.CBV_SRV_UAV;
        }
    }

    DxAssert(CommandList->Reset(DirectCommandAllocator, Backend->Cuboid.Pipeline.Handle));

    // Set light environment data
    {
        dx12_constant_buffer_set_data(&Backend->LightEnvironmentConstantBuffers[CurrentBackBufferIndex], &Renderer->LightEnvironment, sizeof(light_environment));
    }

    // Copy vertex data to gpu buffer
    {
        // Quads - general purpose
        dx12_vertex_buffer_set_data(&Backend->Quad.VertexBuffers[CurrentBackBufferIndex], Backend->DirectCommandList, Renderer->Quad.VertexDataBase, sizeof(quad_vertex) * Renderer->Quad.IndexCount);

        // Cuboid made out of quads - alive entities
        dx12_vertex_buffer_set_data(&Backend->QuadedCuboid.VertexBuffers[CurrentBackBufferIndex], Backend->DirectCommandList, Renderer->QuadedCuboid.VertexDataBase, sizeof(quaded_cuboid_vertex) * Renderer->QuadedCuboid.IndexCount);

        // Cuboid - used for blocks
        dx12_vertex_buffer_set_data(&Backend->Cuboid.TransformVertexBuffers[CurrentBackBufferIndex], Backend->DirectCommandList, Renderer->Cuboid.InstanceData, Renderer->Cuboid.InstanceCount * sizeof(cuboid_transform_vertex_data));

        // Distant quads - sun, moon, starts, etc
        dx12_vertex_buffer_set_data(&Backend->DistantQuad.VertexBuffers[CurrentBackBufferIndex], Backend->DirectCommandList, Renderer->DistantQuad.VertexDataBase, Renderer->DistantQuad.IndexCount * sizeof(distant_quad_vertex));

        // HUD
        dx12_vertex_buffer_set_data(&Backend->HUD.VertexBuffers[CurrentBackBufferIndex], Backend->DirectCommandList, Renderer->HUD.VertexDataBase, sizeof(hud_quad_vertex) * Renderer->HUD.IndexCount);
    }

    // MAIN PASS
    // MAIN PASS
    // MAIN PASS

    auto MainPassRTV = Backend->MainPass.RTVHandles[CurrentBackBufferIndex];
    auto MainPassDSV = Backend->MainPass.DSVHandles[CurrentBackBufferIndex];
    auto MainPassRenderBuffer = Backend->MainPass.RenderBuffers[CurrentBackBufferIndex];

#if ENABLE_SHADOW_PASS
    // Render shadow maps
    if (true)
    {
        auto& ShadowPass = Backend->ShadowPass;
        auto ShadowMap = ShadowPass.ShadowMaps[CurrentBackBufferIndex];

        // From resource to depth write
        dx12_cmd_transition(CommandList, ShadowMap, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        dx12_cmd_set_viewport(CommandList, 0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
        dx12_cmd_set_scrissor_rect(CommandList, 0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);

        auto ShadowPassDSV = ShadowPass.DSVHandles[CurrentBackBufferIndex];
        CommandList->ClearDepthStencilView(ShadowPassDSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        CommandList->OMSetRenderTargets(0, nullptr, false, &ShadowPassDSV);
        CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Render instanced cuboids
        if (Renderer->Cuboid.InstanceCount > 0)
        {
            CommandList->SetGraphicsRootSignature(ShadowPass.RootSignature.Handle);
            CommandList->SetPipelineState(ShadowPass.Pipeline.Handle);

            CommandList->SetGraphicsRoot32BitConstants(0, sizeof(Renderer->RenderData.ShadowPassBuffer) / 4, &Renderer->RenderData.ShadowPassBuffer, 0);

            // Bind vertex positions and transforms
            DX12CmdSetVertexBuffers2(CommandList, 0, Backend->Cuboid.PositionsVertexBuffer.Buffer.Handle, Backend->Cuboid.PositionsVertexBuffer.Buffer.Size, sizeof(cuboid_vertex), Backend->Cuboid.TransformVertexBuffers[CurrentBackBufferIndex].Buffer.Handle, Renderer->Cuboid.InstanceCount * sizeof(cuboid_transform_vertex_data), sizeof(cuboid_transform_vertex_data));

            // Bind index buffer
            DX12CmdSetIndexBuffer(CommandList, Backend->Quad.IndexBuffer.Buffer.Handle, 36 * sizeof(u32), DXGI_FORMAT_R32_UINT);

            // Issue draw call
            CommandList->DrawIndexedInstanced(36, Renderer->Cuboid.InstanceCount, 0, 0, 0);
        }

        // From depth write to resource
        dx12_cmd_transition(CommandList, ShadowMap, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
#endif

    // Render rest of the scene
    // Render rest of the scene
    // Render rest of the scene
    // Render rest of the scene
    // Render rest of the scene

    // Frame that was presented needs to be set to render target again
    dx12_cmd_transition(CommandList, MainPassRenderBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    CommandList->ClearRenderTargetView(MainPassRTV, &ClearColor.x, 0, nullptr);
    CommandList->ClearDepthStencilView(MainPassDSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    CommandList->OMSetRenderTargets(1, &MainPassRTV, false, &MainPassDSV);

    dx12_cmd_set_viewport(CommandList, 0, 0, (f32)SwapChainDesc.BufferDesc.Width, (f32)SwapChainDesc.BufferDesc.Height);
    dx12_cmd_set_scrissor_rect(CommandList, 0, 0, SwapChainDesc.BufferDesc.Width, SwapChainDesc.BufferDesc.Height);

    CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Render a skybox
    if (false)
    {
        auto& Skybox = Backend->Skybox;
        CommandList->SetGraphicsRootSignature(Skybox.RootSignature.Handle);
        CommandList->SetPipelineState(Skybox.Pipeline.Handle);

        CommandList->SetGraphicsRoot32BitConstants(0, sizeof(Renderer->RenderData.SkyboxBuffer) / 4, &Renderer->RenderData.SkyboxBuffer, 0);

        // Bind vertex positions
        DX12CmdSetVertexBuffer(CommandList, 0, Skybox.VertexBuffer.Buffer.Handle, Skybox.VertexBuffer.Buffer.Size, sizeof(v3));

        // Bind index buffer
        DX12CmdSetIndexBuffer(CommandList, Skybox.IndexBuffer.Buffer.Handle, sizeof(c_SkyboxIndices), DXGI_FORMAT_R32_UINT);

        CommandList->DrawIndexedInstanced(36, 1, 0, 0, 0);
    }

    // Render instanced cuboids
    if (Renderer->Cuboid.InstanceCount > 0)
    {
        // Set root constants
        // Number of 32 bit values - 16 floats in 4x4 matrix
        CommandList->SetGraphicsRootSignature(Backend->Cuboid.RootSignature.Handle);

        CommandList->SetDescriptorHeaps(1, (ID3D12DescriptorHeap* const*)&Backend->TextureDescriptorHeap);

        CommandList->SetPipelineState(Backend->Cuboid.Pipeline.Handle);

        CommandList->SetGraphicsRoot32BitConstants(0, sizeof(cuboid_buffer_data) / 4, &Renderer->RenderData.CuboidBuffer, 0);
        CommandList->SetGraphicsRootConstantBufferView(1, Backend->LightEnvironmentConstantBuffers[CurrentBackBufferIndex].Buffer.Handle->GetGPUVirtualAddress());

        CommandList->SetGraphicsRootDescriptorTable(2, Backend->TextureDescriptorHeap.gpu_base());

        DX12CmdSetVertexBuffers2(CommandList, 0, Backend->Cuboid.PositionsVertexBuffer.Buffer.Handle, Backend->Cuboid.PositionsVertexBuffer.Buffer.Size, sizeof(cuboid_vertex), Backend->Cuboid.TransformVertexBuffers[CurrentBackBufferIndex].Buffer.Handle, Renderer->Cuboid.InstanceCount * sizeof(cuboid_transform_vertex_data), sizeof(cuboid_transform_vertex_data));

        // Bind index buffer
        DX12CmdSetIndexBuffer(CommandList, Backend->Quad.IndexBuffer.Buffer.Handle, 36 * sizeof(u32), DXGI_FORMAT_R32_UINT);

        // Issue draw call
        CommandList->DrawIndexedInstanced(36, Renderer->Cuboid.InstanceCount, 0, 0, 0);
    }

    // Render distant objects
    if (Renderer->DistantQuad.IndexCount > 0 && false)
    {
        CommandList->SetGraphicsRootSignature(Backend->Skybox.RootSignature.Handle);
        CommandList->SetPipelineState(Backend->DistantQuad.Pipeline.Handle);
        CommandList->SetGraphicsRoot32BitConstants(0, sizeof(distant_quad_buffer_data) / 4, &Renderer->RenderData.DistantObjectBuffer, 0);

        // Bind vertex positions
        DX12CmdSetVertexBuffer(CommandList, 0, Backend->DistantQuad.VertexBuffers[CurrentBackBufferIndex].Buffer.Handle, Renderer->DistantQuad.IndexCount * sizeof(distant_quad_vertex), sizeof(distant_quad_vertex));

        // Bind index buffer
        DX12CmdSetIndexBuffer(CommandList, Backend->Quad.IndexBuffer.Buffer.Handle, Renderer->DistantQuad.IndexCount * sizeof(u32), DXGI_FORMAT_R32_UINT);

        CommandList->DrawIndexedInstanced(Renderer->DistantQuad.IndexCount, 1, 0, 0, 0);
    }

    CommandList->SetGraphicsRootSignature(Backend->RootSignature.Handle);

    // Render quaded cuboids
    if (Renderer->QuadedCuboid.IndexCount > 0 && true)
    {
        CommandList->SetPipelineState(Backend->QuadedCuboid.Pipeline.Handle);

        CommandList->SetGraphicsRoot32BitConstants(0, sizeof(cuboid_buffer_data) / 4, &Renderer->RenderData.CuboidBuffer, 0);
        CommandList->SetGraphicsRootConstantBufferView(1, Backend->LightEnvironmentConstantBuffers[CurrentBackBufferIndex].Buffer.Handle->GetGPUVirtualAddress());

        // Bind vertex buffer
        DX12CmdSetVertexBuffer(CommandList, 0, Backend->QuadedCuboid.VertexBuffers[CurrentBackBufferIndex].Buffer.Handle, Backend->QuadedCuboid.VertexBuffers[CurrentBackBufferIndex].Buffer.Size, sizeof(quaded_cuboid_vertex));

        // Bind index buffer
        DX12CmdSetIndexBuffer(CommandList, Backend->Quad.IndexBuffer.Buffer.Handle, Renderer->QuadedCuboid.IndexCount * sizeof(u32), DXGI_FORMAT_R32_UINT);

        // Issue draw call
        CommandList->DrawIndexedInstanced(Renderer->QuadedCuboid.IndexCount, 1, 0, 0, 0);
    }

    // Render quads
    if (Renderer->Quad.IndexCount > 0)
    {
        auto& Quad = Backend->Quad;
        CommandList->SetGraphicsRootSignature(Backend->RootSignature.Handle);
        CommandList->SetPipelineState(Quad.Pipeline.Handle);

        // Bind vertex buffer
        DX12CmdSetVertexBuffer(CommandList, 0, Quad.VertexBuffers[CurrentBackBufferIndex].Buffer.Handle, Renderer->Quad.IndexCount * sizeof(quad_vertex), sizeof(quad_vertex));

        // Bind index buffer
        DX12CmdSetIndexBuffer(CommandList, Quad.IndexBuffer.Buffer.Handle, Renderer->Quad.IndexCount * sizeof(u32), DXGI_FORMAT_R32_UINT);

        // TODO: For now just share the first half of the signature buffer, this needs some sort of distinction between HUD and Game stuff
        CommandList->SetGraphicsRoot32BitConstants(0, 16, &Renderer->RenderData.CuboidBuffer.ViewProjection, 0);

        // Issue draw call
        CommandList->DrawIndexedInstanced(Renderer->Quad.IndexCount, 1, 0, 0, 0);
    }

    // Render HUD
    if (Renderer->HUD.IndexCount > 0 && true)
    {
        CommandList->SetPipelineState(Backend->HUD.Pipeline.Handle);

        // Bind vertex buffer
        DX12CmdSetVertexBuffer(CommandList, 0, Backend->HUD.VertexBuffers[CurrentBackBufferIndex].Buffer.Handle, Renderer->HUD.IndexCount * sizeof(hud_quad_vertex), sizeof(hud_quad_vertex));

        // Bind index buffer
        DX12CmdSetIndexBuffer(CommandList, Backend->Quad.IndexBuffer.Buffer.Handle, Renderer->HUD.IndexCount * sizeof(u32), DXGI_FORMAT_R32_UINT);

        CommandList->SetGraphicsRoot32BitConstants(0, 16, &Renderer->RenderData.HUDBuffer.Projection, 0);

        // Issue draw call
        CommandList->DrawIndexedInstanced(Renderer->HUD.IndexCount, 1, 0, 0, 0);
    }

    bool EnableBloomPass = true;
    dx12_cmd_transition(CommandList, MainPassRenderBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, EnableBloomPass ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // Bloom Pass
    if (EnableBloomPass)
    {
        auto& BloomPass = Backend->BloomPass;
        // Take MainPassRenderBuffer, do magic and output it 

        //Set root signature, pso and descriptor heap
        CommandList->SetPipelineState(BloomPass.PipelineCompute.Handle);
        CommandList->SetComputeRootSignature(BloomPass.RootSignature.Handle);

        ID3D12DescriptorHeap* Heaps[] = { Backend->MainPass.SRVDescriptorHeap.Handle };
        CommandList->SetDescriptorHeaps(1, Heaps);
        CommandList->SetComputeRootDescriptorTable(0, Backend->MainPass.GPUSRVHandles[CurrentBackBufferIndex]);

        // Dispatch the compute shader with one thread per 8x8 pixels
        u32 Width = SwapChainDesc.BufferDesc.Width;
        u32 Height = SwapChainDesc.BufferDesc.Height;
        u32 ThreadGroupX = (Width + 16 - 1) / 16; // ceil division
        u32 ThreadGroupY = (Height + 16 - 1) / 16;
        CommandList->Dispatch(ThreadGroupX, ThreadGroupY, 1);

        dx12_cmd_transition(CommandList, MainPassRenderBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    //
    // Copy main pass render target to swapchain target and optionally apply some post-processing
    //
    dx12_cmd_transition(CommandList, SwapChainBackBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    // Clearning not needed
    //CommandList->ClearRenderTargetView(SwapChainRTV, &ClearColor.x, 0, nullptr);
    CommandList->OMSetRenderTargets(1, &SwapChainRTV, false, nullptr);

    bool DisplayImage = true;
    if (DisplayImage)
    {
        dx12_cmd_set_viewport(CommandList, 0, 0, (f32)SwapChainDesc.BufferDesc.Width, (f32)SwapChainDesc.BufferDesc.Height);
        dx12_cmd_set_scrissor_rect(CommandList, 0, 0, SwapChainDesc.BufferDesc.Width, SwapChainDesc.BufferDesc.Height);

        CommandList->SetPipelineState(Backend->FullscreenPass.Pipeline.Handle);
        CommandList->SetGraphicsRootSignature(Backend->FullscreenPass.RootSignature.Handle);

        ID3D12DescriptorHeap* Heaps[] = { Backend->MainPass.SRVDescriptorHeap.Handle };
        CommandList->SetDescriptorHeaps(1, Heaps);

        CommandList->SetGraphicsRootDescriptorTable(0, Backend->MainPass.GPUSRVHandles[CurrentBackBufferIndex]);

        CommandList->DrawInstanced(3, 1, 0, 0);
    }

    // Render debug UI
    bool RenderImGui = true;
    if (RenderImGui)
    {
        CommandList->SetDescriptorHeaps(1, &ImGuiDescriptorHeap);
        ImGui_ImplDX12_RenderDrawData(ImGuiDrawData, CommandList);
    }

    // Rendered frame needs to be transitioned to present state
    dx12_cmd_transition(CommandList, SwapChainBackBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    // Finalize the command list
    DxAssert(CommandList->Close());

    Backend->DirectCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&CommandList);

    DxAssert(Backend->SwapChain->Present(Backend->VSync, 0));

    // Wait for GPU to finish presenting
    FrameFenceValue = dx12_render_backend_signal(Backend->DirectCommandQueue, Backend->Fence, &Backend->FenceValue);
    dx12_render_backend_wait_for_fence_value(Backend->Fence, FrameFenceValue, Backend->DirectFenceEvent);
    //WaitForSingleObjectEx(Backend->FrameLatencyEvent, INFINITE, FALSE);

    // Move to another back buffer
    Backend->CurrentBackBufferIndex = Backend->SwapChain->GetCurrentBackBufferIndex();

    // Log dx12 stuff
    DumpInfoQueue();
}

internal void d3d12_render_backend_resize_swapchain(dx12_render_backend* Backend, u32 RequestWidth, u32 RequestHeight)
{
    // Flush the GPU queue to make sure the swap chain's back buffers are not being referenced by an in-flight command list.
    dx12_render_backend_flush(Backend);

    // Reset fence values and release back buffers
    for (u32 i = 0; i < FIF; i++)
    {
        Backend->SwapChainBackbuffers[i]->Release();
        Backend->FrameFenceValues[i] = Backend->FrameFenceValues[Backend->CurrentBackBufferIndex];
    }

    DXGI_SWAP_CHAIN_DESC SwapChainDesc;

    // Resize swapchain render buffer
    {
        DxAssert(Backend->SwapChain->GetDesc(&SwapChainDesc));
        DxAssert(Backend->SwapChain->ResizeBuffers(FIF, RequestWidth, RequestHeight, SwapChainDesc.BufferDesc.Format, SwapChainDesc.Flags));
        DxAssert(Backend->SwapChain->GetDesc(&SwapChainDesc));

        Backend->CurrentBackBufferIndex = Backend->SwapChain->GetCurrentBackBufferIndex();

        // Place rtv descriptor sequentially in memory
        {
            D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle = Backend->RTVDescriptorHeap.cpu_base();
            for (u32 i = 0; i < FIF; ++i)
            {
                DxAssert(Backend->SwapChain->GetBuffer(i, IID_PPV_ARGS(&Backend->SwapChainBackbuffers[i])));
                Backend->Device->CreateRenderTargetView(Backend->SwapChainBackbuffers[i], nullptr, RtvHandle);

                Backend->SwapChainBufferRTVCPUHandles[i] = RtvHandle;
                RtvHandle.ptr += Backend->DescriptorSizes.RTV;
            }
        }
    }

    // MainPass - Resize render buffers and recreate RTV and DSV views
    {
        auto& MainPass = Backend->MainPass;
        //D3D12_RENDER_TARGET_VIEW_DESC RtvDesc = {};

        D3D12_SHADER_RESOURCE_VIEW_DESC SrvDesc = {};
        SrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        SrvDesc.Format = Backend->MainPass.Format;
        SrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        SrvDesc.Texture2D.MipLevels = 1;
        SrvDesc.Texture2D.MostDetailedMip = 0;
        SrvDesc.Texture2D.PlaneSlice = 0;
        SrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        D3D12_DEPTH_STENCIL_VIEW_DESC DsvDesc = {};
        DsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        DsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        DsvDesc.Texture2D.MipSlice = 0;
        DsvDesc.Flags = D3D12_DSV_FLAG_NONE;

        auto RtvHandle = MainPass.RTVDescriptorHeap.cpu_base();
        auto SrvHandle = MainPass.SRVDescriptorHeap.cpu_base();
        auto GPUSrvHandle = MainPass.SRVDescriptorHeap.gpu_base();
        auto DsvHandle = MainPass.DSVDescriptorHeap.cpu_base();;

        const bool AllowUnorderedAccess = true;
        for (u32 i = 0; i < FIF; i++)
        {
            // Render buffers
            {
                MainPass.RenderBuffers[i]->Release();
                MainPass.RenderBuffers[i] = dx12_render_target_create(Backend->Device, SrvDesc.Format, SwapChainDesc.BufferDesc.Width, SwapChainDesc.BufferDesc.Height, AllowUnorderedAccess, L"MainPassBuffer");

                // Render Target View
                Backend->Device->CreateRenderTargetView(MainPass.RenderBuffers[i], nullptr, RtvHandle);

                // Shader Resouces View
                Backend->Device->CreateShaderResourceView(MainPass.RenderBuffers[i], &SrvDesc, SrvHandle);

                // GPU handles
                MainPass.GPUSRVHandles[i] = GPUSrvHandle;
                GPUSrvHandle.ptr += Backend->DescriptorSizes.CBV_SRV_UAV;

                // CPU handles
                SrvHandle.ptr += Backend->DescriptorSizes.CBV_SRV_UAV;
                MainPass.RTVHandles[i] = RtvHandle;
                RtvHandle.ptr += Backend->DescriptorSizes.RTV;
            }

            // Depth buffers
            {
                MainPass.DepthBuffers[i]->Release();
                MainPass.DepthBuffers[i] = dx12_depth_buffer_create(Backend->Device, SwapChainDesc.BufferDesc.Width, SwapChainDesc.BufferDesc.Height, DsvDesc.Format, DsvDesc.Format, L"MainPassDepthBuffer");

                Backend->Device->CreateDepthStencilView(MainPass.DepthBuffers[i], &DsvDesc, DsvHandle);

                // Increment by the size of the dsv descriptor size
                MainPass.DSVHandles[i] = DsvHandle;
                DsvHandle.ptr += Backend->DescriptorSizes.DSV;
            }
        }
    }

    Warn("SwapChain resized to %d %d", SwapChainDesc.BufferDesc.Width, SwapChainDesc.BufferDesc.Height);
    Assert(RequestWidth == SwapChainDesc.BufferDesc.Width && RequestHeight == SwapChainDesc.BufferDesc.Height, "Requested size is not equal actual swapchain size!");
}

internal u64 dx12_render_backend_signal(ID3D12CommandQueue* CommandQueue, ID3D12Fence* Fence, u64* FenceValue)
{
    u64& RefFenceValue = *FenceValue;
    u64 FenceValueForSignal = ++RefFenceValue;
    DxAssert(CommandQueue->Signal(Fence, FenceValueForSignal));
    return FenceValueForSignal;
}

internal void dx12_render_backend_wait_for_fence_value(ID3D12Fence* Fence, u64 FenceValue, HANDLE FenceEvent, u32 Duration)
{
    if (Fence->GetCompletedValue() < FenceValue)
    {
        Fence->SetEventOnCompletion(FenceValue, FenceEvent);
        WaitForSingleObject(FenceEvent, Duration);
    }
}

internal void dx12_render_backend_flush(dx12_render_backend* Renderer)
{
    u64 FenceValueForSignal = dx12_render_backend_signal(Renderer->DirectCommandQueue, Renderer->Fence, &Renderer->FenceValue);
    dx12_render_backend_wait_for_fence_value(Renderer->Fence, FenceValueForSignal, Renderer->DirectFenceEvent);
}
