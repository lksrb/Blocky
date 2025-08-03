#include "DX12Renderer.h"

#include "DX12Texture.cpp"
#include "DX12Buffer.cpp"

internal d3d12_render_backend* d3d12_render_backend_create(arena* Arena, const win32_window& Window)
{
    d3d12_render_backend* Backend = arena_new(Arena, d3d12_render_backend);
    GameRendererInitD3D(Backend, Window);
    GameRendererInitD3DPipeline(Backend);
    return Backend;
}

internal void d3d12_render_backend_destroy(d3d12_render_backend* Renderer)
{
    // Wait for GPU to finish
    GameRendererFlush(Renderer);

    for (u32 i = 0; i < FIF; i++)
    {
        // Command allocators
        Renderer->DirectCommandAllocators[i]->Release();

        // Depth testing
        Renderer->DepthBuffers[i]->Release();
        Renderer->BackBuffers[i]->Release();

        DX12VertexBufferDestroy(&Renderer->Quad.VertexBuffers[i]);
    }

    DX12PipelineDestroy(&Renderer->Quad.Pipeline);
    DX12IndexBufferDestroy(&Renderer->Quad.IndexBuffer);
    TextureDestroy(&Renderer->WhiteTexture);
    Renderer->SwapChain->Release();
    Renderer->DSVDescriptorHeap->Release();
    Renderer->RTVDescriptorHeap->Release();
    Renderer->SRVDescriptorHeap->Release();
    Renderer->Fence->Release();
    Renderer->DirectCommandList->Release();
    Renderer->DirectCommandQueue->Release();
    Renderer->RootSignature->Release();
    Renderer->Factory->Release();
    Renderer->Device->Release();

#if DX12_ENABLE_DEBUG_LAYER
    // Report all memory leaks
    Renderer->DxgiDebugInterface->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_ALL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
    DX12DumpInfoQueue(g_DebugInfoQueue);
#endif

    // Zero everything to make sure nothing can reference this
    memset(Renderer, 0, sizeof(d3d12_render_backend));
}

internal void GameRendererInitD3D(d3d12_render_backend* Renderer, const win32_window& Window)
{
    // Enable debug layer
    if (DX12_ENABLE_DEBUG_LAYER)
    {
        // Enable debug layer
        // This is sort of like validation layers in Vulkan
        DxAssert(D3D12GetDebugInterface(IID_PPV_ARGS(&Renderer->DebugInterface)));
        Renderer->DebugInterface->EnableDebugLayer();

#if BK_DEBUG
        // Enable DXGI debug - for more memory leaks checking
        DxAssert(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&Renderer->DxgiDebugInterface)));
        Renderer->DxgiDebugInterface->EnableLeakTrackingForThread();
#endif
    }

    // Create device

    // This takes ~300 ms, insane
    DxAssert(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Renderer->Device)));

    // Create factory
    DxAssert(CreateDXGIFactory2(DX12_ENABLE_DEBUG_LAYER ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS(&Renderer->Factory)));

    // Debug info queue
    if (DX12_ENABLE_DEBUG_LAYER)
    {
        DxAssert(Renderer->Device->QueryInterface(IID_PPV_ARGS(&g_DebugInfoQueue)));
        g_DebugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, false);
        g_DebugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, false);
        g_DebugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, false);
        g_DebugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_INFO, false);
        //InfoQueue->Release();
        //Renderer->DebugInterface->Release();

        // Suppressing some warning
        {
            // Suppress whole categories of messages
            //D3D12_MESSAGE_CATEGORY Categories[] = {};

            // Suppress messages based on their severity level
            D3D12_MESSAGE_SEVERITY Severities[] =
            {
                D3D12_MESSAGE_SEVERITY_INFO
            };

            // Suppress individual messages by their ID
            D3D12_MESSAGE_ID DenyIds[] = {
                D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,   // I'm really not sure how to avoid this message.
                D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,                         // This warning occurs when using capture frame while graphics debugging.
                D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,                       // This warning occurs when using capture frame while graphics debugging.
            };

            D3D12_INFO_QUEUE_FILTER NewFilter = {};
            //NewFilter.DenyList.NumCategories = _countof(Categories);
            //NewFilter.DenyList.pCategoryList = Categories;
            NewFilter.DenyList.NumSeverities = CountOf(Severities);
            NewFilter.DenyList.pSeverityList = Severities;
            NewFilter.DenyList.NumIDs = CountOf(DenyIds);
            NewFilter.DenyList.pIDList = DenyIds;

            DxAssert(g_DebugInfoQueue->PushStorageFilter(&NewFilter));
        }
    }

    // Create direct command queue
    {
        // Create direct command allocator
        for (u32 i = 0; i < FIF; i++)
        {
            DxAssert(Renderer->Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&Renderer->DirectCommandAllocators[i])));
        }

        D3D12_COMMAND_QUEUE_DESC Desc = {};
        Desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; // Most general - For draw, compute and copy commands
        Desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
        Desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        Desc.NodeMask = 0;
        DxAssert(Renderer->Device->CreateCommandQueue(&Desc, IID_PPV_ARGS(&Renderer->DirectCommandQueue)));

        // Create direct command list
        {
            DxAssert(Renderer->Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, Renderer->DirectCommandAllocators[Renderer->CurrentBackBufferIndex], nullptr, IID_PPV_ARGS(&Renderer->DirectCommandList)));

            // Command lists are created in the recording state, but there is nothing
            // to record yet. The main loop expects it to be closed, so close it now.
            DxAssert(Renderer->DirectCommandList->Close());
        }

        // Fence
        DxAssert(Renderer->Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Renderer->Fence)));
        Renderer->DirectFenceEvent = CreateEvent(nullptr, false, false, nullptr);
        Assert(Renderer->DirectFenceEvent != INVALID_HANDLE_VALUE, "Could not create fence event.");
    }

    // Create swapchain
    {
        DXGI_SWAP_CHAIN_DESC1 Desc = {};
        Desc.Width = Window.ClientAreaWidth;
        Desc.Height = Window.ClientAreaHeight;
        Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        Desc.Stereo = false;
        Desc.SampleDesc = { 1, 0 }; // Anti-aliasing needs to be done manually in D3D12
        Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        Desc.BufferCount = FIF;
        Desc.Scaling = DXGI_SCALING_STRETCH;
        Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        Desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        // It is recommended to always allow tearing if tearing support is available.
        // TODO: More robustness needed
        Desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

        DXGI_SWAP_CHAIN_FULLSCREEN_DESC FullScreenDesc = {};
        FullScreenDesc.Windowed = true;

        IDXGISwapChain1* SwapChain1;
        HRESULT Result = (Renderer->Factory->CreateSwapChainForHwnd(Renderer->DirectCommandQueue, Window.Handle, &Desc, &FullScreenDesc, nullptr, &SwapChain1));

        DumpInfoQueue();
        // Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
        // will be handled manually.
        DxAssert(Renderer->Factory->MakeWindowAssociation(Window.Handle, DXGI_MWA_NO_ALT_ENTER));
        SwapChain1->QueryInterface(IID_PPV_ARGS(&Renderer->SwapChain));
        SwapChain1->Release();

        //Renderer->SwapChain->SetMaximumFrameLatency(FIF);

        Renderer->CurrentBackBufferIndex = Renderer->SwapChain->GetCurrentBackBufferIndex();
    }

    // Views are descriptors located in the GPU memory.
    // They describe how to memory of particular resource is layed out

    // Create Render Target Views
    {
        // Create a Descriptor Heap
        D3D12_DESCRIPTOR_HEAP_DESC Desc = {};
        Desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        Desc.NumDescriptors = FIF;
        Desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        Desc.NodeMask = 0;
        DxAssert(Renderer->Device->CreateDescriptorHeap(&Desc, IID_PPV_ARGS(&Renderer->RTVDescriptorHeap)));

        // Place rtv descriptor sequentially in memory
        u32 RTVDescriptorSize = Renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle = Renderer->RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        for (u32 i = 0; i < FIF; ++i)
        {
            DxAssert(Renderer->SwapChain->GetBuffer(i, IID_PPV_ARGS(&Renderer->BackBuffers[i])));
            Renderer->BackBuffers[i]->SetName(L"SwapchainRenderTargetTexture");
            Renderer->Device->CreateRenderTargetView(Renderer->BackBuffers[i], nullptr, RtvHandle);

            Renderer->RTVHandles[i] = RtvHandle;
            RtvHandle.ptr += RTVDescriptorSize;
        }
    }

    // Create depth resources
    {
        // Create the descriptor heap for the depth-stencil view.
        D3D12_DESCRIPTOR_HEAP_DESC DsvHeapDesc = {};
        DsvHeapDesc.NumDescriptors = FIF;
        DsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        DsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        DxAssert(Renderer->Device->CreateDescriptorHeap(&DsvHeapDesc, IID_PPV_ARGS(&Renderer->DSVDescriptorHeap)));

        D3D12_CPU_DESCRIPTOR_HANDLE DsvHandle = Renderer->DSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        u32 DSVDescriptorSize = Renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

        // Create depth buffers
        for (u32 i = 0; i < FIF; i++)
        {
            D3D12_CLEAR_VALUE OptimizedClearValue = {};
            OptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
            OptimizedClearValue.DepthStencil = { 1.0f, 0 };

            D3D12_HEAP_PROPERTIES HeapProperties = {};
            HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

            D3D12_RESOURCE_DESC DepthStencilDesc = {};
            DepthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            DepthStencilDesc.Width = Window.ClientAreaWidth;
            DepthStencilDesc.Height = Window.ClientAreaHeight;
            DepthStencilDesc.DepthOrArraySize = 1;
            DepthStencilDesc.MipLevels = 1;
            DepthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
            DepthStencilDesc.SampleDesc = { 1, 0 };
            DepthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

            DxAssert(Renderer->Device->CreateCommittedResource(
                &HeapProperties,
                D3D12_HEAP_FLAG_NONE,
                &DepthStencilDesc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE,
                &OptimizedClearValue,
                IID_PPV_ARGS(&Renderer->DepthBuffers[i])
            ));

            // Create depth-stencil view
            {
                D3D12_DEPTH_STENCIL_VIEW_DESC Desc = {};
                Desc.Format = DXGI_FORMAT_D32_FLOAT;
                Desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
                Desc.Texture2D.MipSlice = 0;
                Desc.Flags = D3D12_DSV_FLAG_NONE;

                Renderer->Device->CreateDepthStencilView(Renderer->DepthBuffers[i], &Desc, DsvHandle);

                // Increment by the size of the dsv descriptor size
                Renderer->DSVHandles[i] = DsvHandle;
                DsvHandle.ptr += DSVDescriptorSize;
            }
        }
    }
}

internal void GameRendererInitD3DPipeline(d3d12_render_backend* Renderer)
{
    auto Device = Renderer->Device;

    // Root Signature
    {
        D3D12_STATIC_SAMPLER_DESC Samplers[1] = {};

        // Sampler
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

        D3D12_DESCRIPTOR_RANGE Ranges[1] = {};
        Ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        Ranges[0].NumDescriptors = c_MaxTexturesPerDrawCall; // Find upper driver limit
        Ranges[0].BaseShaderRegister = 0;
        Ranges[0].RegisterSpace = 0;
        Ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER Parameters[3] = {};
        Parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        Parameters[0].Constants.Num32BitValues = sizeof(cuboid_root_signature_constant_buffer) / 4;
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

        // Root signature
        ID3DBlob* Error;
        ID3DBlob* Signature;
        DxAssert(D3D12SerializeRootSignature(&Desc, D3D_ROOT_SIGNATURE_VERSION_1, &Signature, &Error));
        DxAssert(Device->CreateRootSignature(0, Signature->GetBufferPointer(), Signature->GetBufferSize(), IID_PPV_ARGS(&Renderer->RootSignature)));
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

        Renderer->Quad.Pipeline = DX12GraphicsPipelineCreate(Device, Renderer->RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/Quad.hlsl", D3D12_CULL_MODE_NONE);

        for (u32 i = 0; i < FIF; i++)
        {
            Renderer->Quad.VertexBuffers[i] = DX12VertexBufferCreate(Device, sizeof(quad_vertex) * c_MaxQuadVertices);
        }

        Renderer->Quad.VertexDataBase = VmAllocArray(quad_vertex, c_MaxQuadVertices);
        Renderer->Quad.VertexDataPtr = Renderer->Quad.VertexDataBase;

        // Quad Index buffer
        {
            u32 MaxQuadIndexCount = bkm::Max(c_MaxQuadedCuboidIndices, c_MaxQuadIndices);
            u32* QuadIndices = VmAllocArray(u32, MaxQuadIndexCount);
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
            Renderer->Quad.IndexBuffer = DX12IndexBufferCreate(Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, QuadIndices, MaxQuadIndexCount);
        }
    }

    // Basic Cuboids - if a cuboid does not have unique texture for each quad, then we can use this
    // TODO: We could even create a pipeline for NoRotNoScale cuboids and pass only translation vector
    {
        auto& Cuboid = Renderer->Cuboid;

        // Root Signature
        {
            D3D12_STATIC_SAMPLER_DESC Samplers[1] = {};

            // Sampler
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

            D3D12_DESCRIPTOR_RANGE Ranges[1] = {};
            Ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            Ranges[0].NumDescriptors = 1; // Find upper driver limit
            Ranges[0].BaseShaderRegister = 0;
            Ranges[0].RegisterSpace = 0;
            Ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_ROOT_PARAMETER Parameters[3] = {};
            Parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            Parameters[0].Constants.Num32BitValues = sizeof(cuboid_root_signature_constant_buffer) / 4;
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

            // Root signature
            ID3DBlob* Error;
            ID3DBlob* Signature;
            DxAssert(D3D12SerializeRootSignature(&Desc, D3D_ROOT_SIGNATURE_VERSION_1, &Signature, &Error));
            DxAssert(Device->CreateRootSignature(0, Signature->GetBufferPointer(), Signature->GetBufferSize(), IID_PPV_ARGS(&Cuboid.RootSignature)));
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

        Cuboid.Pipeline = DX12GraphicsPipelineCreate(Device, Renderer->Cuboid.RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/Cuboid.hlsl", D3D12_CULL_MODE_BACK);

        Cuboid.PositionsVertexBuffer = DX12VertexBufferCreate(Device, sizeof(c_CuboidVertices));

        DX12SubmitToQueueImmidiate(Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, [Renderer, &Cuboid](ID3D12GraphicsCommandList* CommandList)
        {
            DX12VertexBufferSendData(&Cuboid.PositionsVertexBuffer, CommandList, c_CuboidVertices, sizeof(c_CuboidVertices));
        });

        for (u32 i = 0; i < FIF; i++)
        {
            Cuboid.TransformVertexBuffers[i] = DX12VertexBufferCreate(Device, sizeof(cuboid_transform_vertex_data) * c_MaxCubePerBatch);
        }

        Cuboid.InstanceData = VmAllocArray(cuboid_transform_vertex_data, c_MaxCubePerBatch);
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

        Renderer->QuadedCuboid.Pipeline = DX12GraphicsPipelineCreate(Device, Renderer->RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/QuadedCuboid.hlsl");

        for (u32 i = 0; i < FIF; i++)
        {
            Renderer->QuadedCuboid.VertexBuffers[i] = DX12VertexBufferCreate(Device, sizeof(quaded_cuboid_vertex) * c_MaxQuadedCuboidVertices);
        }

        Renderer->QuadedCuboid.VertexDataBase = VmAllocArray(quaded_cuboid_vertex, c_MaxQuadedCuboidVertices);
        Renderer->QuadedCuboid.VertexDataPtr = Renderer->QuadedCuboid.VertexDataBase;
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

        Renderer->HUD.Pipeline = DX12GraphicsPipelineCreate(Device, Renderer->RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/HUD.hlsl");

        for (u32 i = 0; i < FIF; i++)
        {
            Renderer->HUD.VertexBuffers[i] = DX12VertexBufferCreate(Device, sizeof(hud_quad_vertex) * c_MaxHUDQuadVertices);
        }

        Renderer->HUD.VertexDataBase = VmAllocArray(hud_quad_vertex, c_MaxHUDQuadVertices);;
        Renderer->HUD.VertexDataPtr = Renderer->HUD.VertexDataBase;
        // TODO: Maybe our own index buffer?
    }

    // Skybox
    {
        // Skybox Root Signature
        {
            D3D12_ROOT_PARAMETER Parameters[1] = {};
            Parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            Parameters[0].Constants.Num32BitValues = sizeof(skybox_quad_root_signature_constant_buffer) / 4;
            Parameters[0].Constants.ShaderRegister = 0;  // b0
            Parameters[0].Constants.RegisterSpace = 0;
            Parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            D3D12_ROOT_SIGNATURE_DESC Desc = {};
            Desc.NumParameters = CountOf(Parameters);
            Desc.pParameters = Parameters;
            Desc.NumStaticSamplers = 0;
            Desc.pStaticSamplers = nullptr;
            Desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            // Root signature
            ID3DBlob* Error;
            ID3DBlob* Signature;
            DxAssert(D3D12SerializeRootSignature(&Desc, D3D_ROOT_SIGNATURE_VERSION_1, &Signature, &Error));
            DxAssert(Device->CreateRootSignature(0, Signature->GetBufferPointer(), Signature->GetBufferSize(), IID_PPV_ARGS(&Renderer->Skybox.RootSignature)));
        }

        D3D12_INPUT_ELEMENT_DESC InputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        Renderer->Skybox.Pipeline = DX12GraphicsPipelineCreate(Device, Renderer->Skybox.RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/Skybox.hlsl", D3D12_CULL_MODE_BACK, false);
        Renderer->Skybox.VertexBuffer = DX12VertexBufferCreate(Device, sizeof(c_SkyboxVertices));

        DX12SubmitToQueueImmidiate(Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, [Renderer](ID3D12GraphicsCommandList* CommandList)
        {
            DX12VertexBufferSendData(&Renderer->Skybox.VertexBuffer, CommandList, c_SkyboxVertices, sizeof(c_SkyboxVertices));
        });
        Renderer->Skybox.IndexBuffer = DX12IndexBufferCreate(Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, c_SkyboxIndices, CountOf(c_SkyboxIndices));
    }

    // Distant quads
    {
        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC InputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        Renderer->DistantQuad.Pipeline = DX12GraphicsPipelineCreate(Device, Renderer->Skybox.RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/DistantQuad.hlsl", D3D12_CULL_MODE_NONE, false);

        for (u32 i = 0; i < FIF; i++)
        {
            Renderer->DistantQuad.VertexBuffers[i] = DX12VertexBufferCreate(Device, sizeof(distant_quad_vertex) * c_MaxHUDQuadVertices);
        }

        Renderer->DistantQuad.VertexDataBase = VmAllocArray(distant_quad_vertex, c_MaxHUDQuadVertices);
        Renderer->DistantQuad.VertexDataPtr = Renderer->DistantQuad.VertexDataBase;
    }

    // Create white texture
    {
        u32 WhiteColor = 0xffffffff;
        buffer Buffer = { &WhiteColor, sizeof(u32) };
        Renderer->WhiteTexture = TextureCreate(Renderer->Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, Buffer, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);

        // First element of the texture stack will be the white texture
        Renderer->TextureStack[0] = Renderer->WhiteTexture.SRVDescriptor;
    }

    // Create descriptor heap that holds texture and uav descriptors
    {
        D3D12_DESCRIPTOR_HEAP_DESC Desc = {};
        Desc.NumDescriptors = c_MaxTexturesPerDrawCall; // + 1 for light environment
        Desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        Desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; // Must be visible to shaders
        Desc.NodeMask = 0;
        DxAssert(Renderer->Device->CreateDescriptorHeap(&Desc, IID_PPV_ARGS(&Renderer->SRVDescriptorHeap)));
    }

    // Describe and create a SRV for the white texture.
#if 0
    auto SRV = Renderer->SRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    auto Increment = Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (u32 i = 0; i < c_MaxTexturesPerDrawCall; i++)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC Desc = {};
        Desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        Desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        Desc.Texture2D.MipLevels = 1;
        Desc.Texture2D.MostDetailedMip = 0;
        Desc.Texture2D.PlaneSlice = 0;
        Desc.Texture2D.ResourceMinLODClamp = 0.0f;
        Device->CreateShaderResourceView(Renderer->WhiteTexture.Handle, &Desc, SRV);

        SRV.ptr += Increment;
    }
#endif

    // Create light environment constant buffer for each frame
    for (u32 i = 0; i < FIF; i++)
    {
        Renderer->LightEnvironmentConstantBuffers[i] = DX12ConstantBufferCreate(Device, sizeof(light_environment));
    }

    // Shadow Pass
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
            Parameters[0].Constants.Num32BitValues = sizeof(shadow_pass_root_signature_constant_buffer) / 4;
            Parameters[0].Constants.ShaderRegister = 0;  // b0
            Parameters[0].Constants.RegisterSpace = 0;
            Parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

            D3D12_ROOT_SIGNATURE_DESC Desc = {};
            Desc.NumParameters = CountOf(Parameters);
            Desc.pParameters = Parameters;
            Desc.NumStaticSamplers = 0;
            Desc.pStaticSamplers = nullptr;
            Desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            // Root signature
            ID3DBlob* Error;
            ID3DBlob* Signature;
            DxAssert(D3D12SerializeRootSignature(&Desc, D3D_ROOT_SIGNATURE_VERSION_1, &Signature, &Error));
            DxAssert(Device->CreateRootSignature(0, Signature->GetBufferPointer(), Signature->GetBufferSize(), IID_PPV_ARGS(&Renderer->ShadowPass.RootSignature)));
        }

        Renderer->ShadowPass.Pipeline = DX12GraphicsPipelineCreate(Device, Renderer->ShadowPass.RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/Shadow.hlsl", D3D12_CULL_MODE_FRONT, true, 0);

        // Create resources
        // Create resources
        // Create resources

        // Our very own descriptor heaps
        {
            // Create the descriptor heap for the depth-stencil view.
            D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
            HeapDesc.NumDescriptors = FIF;
            HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            DxAssert(Renderer->Device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&Renderer->ShadowPass.DSVDescriptorHeap)));

            // SRV
            HeapDesc = {};
            HeapDesc.NumDescriptors = FIF;
            HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            DxAssert(Renderer->Device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&Renderer->ShadowPass.SRVDescriptorHeap)));
        }

        D3D12_CPU_DESCRIPTOR_HANDLE DsvHandle = Renderer->ShadowPass.DSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        u32 DSVDescriptorSize = Renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

        // Create shadowmap buffers
        for (u32 i = 0; i < FIF; i++)
        {
            D3D12_CLEAR_VALUE OptimizedClearValue = {};
            OptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
            OptimizedClearValue.DepthStencil = { 1.0f, 0 };

            auto HeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

            D3D12_RESOURCE_DESC DepthStencilDesc = {};
            DepthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            DepthStencilDesc.Width = 1024;
            DepthStencilDesc.Height = 1024;
            DepthStencilDesc.DepthOrArraySize = 1;
            DepthStencilDesc.MipLevels = 1;
            DepthStencilDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            DepthStencilDesc.SampleDesc.Count = 1;  // No MSAA
            DepthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

            DxAssert(Renderer->Device->CreateCommittedResource(
                &HeapProperties,
                D3D12_HEAP_FLAG_NONE,
                &DepthStencilDesc,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                &OptimizedClearValue,
                IID_PPV_ARGS(&Renderer->ShadowPass.ShadowMaps[i])
            ));

            const wchar_t* DebugNames[]{
                L"ShadowMap0",
                L"ShadowMap1"
            };

            Renderer->ShadowPass.ShadowMaps[i]->SetName(DebugNames[i]);

            // Update the depth-stencil view.
            D3D12_DEPTH_STENCIL_VIEW_DESC DSV = {};
            DSV.Format = DXGI_FORMAT_D32_FLOAT;
            DSV.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            DSV.Texture2D.MipSlice = 0;
            DSV.Flags = D3D12_DSV_FLAG_NONE;

            Renderer->Device->CreateDepthStencilView(Renderer->ShadowPass.ShadowMaps[i], &DSV, DsvHandle);

            // Increment by the size of the dsv descriptor size
            Renderer->ShadowPass.DSVHandles[i] = DsvHandle;
            DsvHandle.ptr += DSVDescriptorSize;
        }

        // Shadow map
        for (u32 i = 0; i < FIF; i++)
        {
            auto SRVDescriptor = Renderer->ShadowPass.SRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
            auto DescriptorSize = Renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC Desc = {};
                Desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                Desc.Format = DXGI_FORMAT_R32_FLOAT;
                Desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                Desc.Texture2D.MipLevels = 1;
                Desc.Texture2D.MostDetailedMip = 0;
                Desc.Texture2D.PlaneSlice = 0;
                Desc.Texture2D.ResourceMinLODClamp = 0.0f;
                Renderer->Device->CreateShaderResourceView(Renderer->ShadowPass.ShadowMaps[i], &Desc, SRVDescriptor);

                Renderer->ShadowPass.SRVHandles[i] = SRVDescriptor;
                SRVDescriptor.ptr += DescriptorSize;
            }
        }
    }
}

internal void d3d12_render_backend_render(d3d12_render_backend* Renderer)
{
    // Get current frame stuff
    auto CommandList = Renderer->DirectCommandList;
    auto CurrentBackBufferIndex = Renderer->CurrentBackBufferIndex;

    auto DirectCommandAllocator = Renderer->DirectCommandAllocators[CurrentBackBufferIndex];
    auto BackBuffer = Renderer->BackBuffers[CurrentBackBufferIndex];
    auto RTV = Renderer->RTVHandles[CurrentBackBufferIndex];
    auto DSV = Renderer->DSVHandles[CurrentBackBufferIndex];
    auto& FrameFenceValue = Renderer->FrameFenceValues[CurrentBackBufferIndex];

    // TODO: Figure out if this function needs to be called every frame.
    DXGI_SWAP_CHAIN_DESC SwapChainDesc;
    DxAssert(Renderer->SwapChain->GetDesc(&SwapChainDesc));

    // Reset state
    DxAssert(DirectCommandAllocator->Reset());

    // Copy texture's descriptor to our renderer's descriptor
    if (false)
    {
        //debug_cycle_counter C("Copy");
        // We cannot use CopyDescriptorsSimple to copy everything at once because it assumes that source descriptors are ordered... and TextureStack's ptrs are not.
        // For now we keep both even though the newer variant is slower. In Release they seem to be equal. But with not many textures its unfair to judge the newer variant.
        // TODO: Come back to this and stress test
#if 1
        auto DstSRV = Renderer->SRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        auto DescriptorSize = Renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        DstSRV.ptr += DescriptorSize;
        for (u32 i = 1; i < Renderer->CurrentTextureStackIndex; i++)
        {
            Renderer->Device->CopyDescriptorsSimple(1, DstSRV, Renderer->TextureStack[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            DstSRV.ptr += DescriptorSize;
        }
#else // Newer
        u32 NumDescriptors = Renderer->CurrentTextureStackIndex - 1;
        if (NumDescriptors > 0)
        {
            auto DstSRV = Renderer->SRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
            auto DescriptorSize = Renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            // Skip white texture
            DstSRV.ptr += DescriptorSize;

            Renderer->Device->CopyDescriptors(1, &DstSRV, &NumDescriptors, NumDescriptors, &Renderer->TextureStack[1], nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            );
        }
#endif
    }

    DxAssert(CommandList->Reset(DirectCommandAllocator, Renderer->Cuboid.Pipeline.Handle));

    // Set light environment data
    {
        DX12ConstantBufferSetData(&Renderer->LightEnvironmentConstantBuffers[CurrentBackBufferIndex], &Renderer->LightEnvironment, sizeof(light_environment));
    }

    // Copy vertex data to gpu buffer
    {
        // Quads - general purpose
        DX12VertexBufferSendData(&Renderer->Quad.VertexBuffers[CurrentBackBufferIndex], Renderer->DirectCommandList, Renderer->Quad.VertexDataBase, sizeof(quad_vertex) * Renderer->Quad.IndexCount);

        // Cuboid made out of quads - alive entities
        DX12VertexBufferSendData(&Renderer->QuadedCuboid.VertexBuffers[CurrentBackBufferIndex], Renderer->DirectCommandList, Renderer->QuadedCuboid.VertexDataBase, sizeof(quaded_cuboid_vertex) * Renderer->QuadedCuboid.IndexCount);

        // Cuboid - used for blocks
        DX12VertexBufferSendData(&Renderer->Cuboid.TransformVertexBuffers[CurrentBackBufferIndex], Renderer->DirectCommandList, Renderer->Cuboid.InstanceData, Renderer->Cuboid.InstanceCount * sizeof(cuboid_transform_vertex_data));

        // Distant quads - sun, moon, starts, etc
        DX12VertexBufferSendData(&Renderer->DistantQuad.VertexBuffers[CurrentBackBufferIndex], Renderer->DirectCommandList, Renderer->DistantQuad.VertexDataBase, Renderer->DistantQuad.IndexCount * sizeof(distant_quad_vertex));

        // HUD
        DX12VertexBufferSendData(&Renderer->HUD.VertexBuffers[CurrentBackBufferIndex], Renderer->DirectCommandList, Renderer->HUD.VertexDataBase, sizeof(hud_quad_vertex) * Renderer->HUD.IndexCount);
    }

    // Render shadow maps
    {
        auto& ShadowPass = Renderer->ShadowPass;
        auto ShadowMap = ShadowPass.ShadowMaps[CurrentBackBufferIndex];

        // From resource to depth write
        DX12CmdTransition(CommandList, ShadowMap, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        DX12CmdSetViewport(CommandList, 0, 0, 1024.0f, 1024.0f);
        DX12CmdSetScissorRect(CommandList, 0, 0, 1024, 1024);

        auto ShadowPassDSV = ShadowPass.DSVHandles[CurrentBackBufferIndex];
        CommandList->ClearDepthStencilView(ShadowPassDSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        CommandList->OMSetRenderTargets(0, nullptr, false, &ShadowPassDSV);
        CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Render instanced cuboids
        if (Renderer->Cuboid.InstanceCount > 0)
        {
            CommandList->SetGraphicsRootSignature(ShadowPass.RootSignature);
            CommandList->SetPipelineState(ShadowPass.Pipeline.Handle);

            CommandList->SetGraphicsRoot32BitConstants(0, sizeof(Renderer->RenderData.ShadowPassRootSignatureBuffer) / 4, &Renderer->RenderData.ShadowPassRootSignatureBuffer, 0);

            // Bind vertex positions and transforms
            DX12CmdSetVertexBuffers2(CommandList, 0, Renderer->Cuboid.PositionsVertexBuffer.Buffer.Handle, Renderer->Cuboid.PositionsVertexBuffer.Buffer.Size, sizeof(cuboid_vertex), Renderer->Cuboid.TransformVertexBuffers[CurrentBackBufferIndex].Buffer.Handle, Renderer->Cuboid.InstanceCount * sizeof(cuboid_transform_vertex_data), sizeof(cuboid_transform_vertex_data));

            // Bind index buffer
            DX12CmdSetIndexBuffer(CommandList, Renderer->Quad.IndexBuffer.Buffer.Handle, 36 * sizeof(u32), DXGI_FORMAT_R32_UINT);

            // Issue draw call
            CommandList->DrawIndexedInstanced(36, Renderer->Cuboid.InstanceCount, 0, 0, 0);
        }

        // From depth write to resource
        DX12CmdTransition(CommandList, ShadowMap, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    // Render rest of the scene
    // Render rest of the scene
    // Render rest of the scene
    // Render rest of the scene
    // Render rest of the scene

    // Frame that was presented needs to be set to render target again
    DX12CmdTransition(CommandList, BackBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

    // Set and clear render target view
    v4 ClearColor = { 0.2f, 0.3f, 0.8f, 1.0f };
    CommandList->ClearRenderTargetView(RTV, &ClearColor.x, 0, nullptr);
    CommandList->ClearDepthStencilView(DSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    CommandList->OMSetRenderTargets(1, &RTV, false, &DSV);

    DX12CmdSetViewport(CommandList, 0, 0, (FLOAT)SwapChainDesc.BufferDesc.Width, (FLOAT)SwapChainDesc.BufferDesc.Height);
    DX12CmdSetScissorRect(CommandList, 0, 0, SwapChainDesc.BufferDesc.Width, SwapChainDesc.BufferDesc.Height);

    CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Render instanced cuboids
    if (Renderer->Cuboid.InstanceCount > 0)
    {
        // Set root constants
        // Number of 32 bit values - 16 floats in 4x4 matrix
        CommandList->SetGraphicsRootSignature(Renderer->Cuboid.RootSignature);
        CommandList->SetDescriptorHeaps(1, (ID3D12DescriptorHeap* const*)&Renderer->ShadowPass.SRVDescriptorHeap);
        auto SRVPTR = Renderer->ShadowPass.SRVDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
        //SRVPTR.ptr += CurrentBackBufferIndex * Renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        CommandList->SetGraphicsRootDescriptorTable(2, SRVPTR);

        CommandList->SetPipelineState(Renderer->Cuboid.Pipeline.Handle);

        CommandList->SetGraphicsRoot32BitConstants(0, sizeof(cuboid_root_signature_constant_buffer) / 4, &Renderer->RenderData.CuboidRootSignatureBuffer, 0);
        CommandList->SetGraphicsRootConstantBufferView(1, Renderer->LightEnvironmentConstantBuffers[CurrentBackBufferIndex].Buffer.Handle->GetGPUVirtualAddress());

        DX12CmdSetVertexBuffers2(CommandList, 0, Renderer->Cuboid.PositionsVertexBuffer.Buffer.Handle, Renderer->Cuboid.PositionsVertexBuffer.Buffer.Size, sizeof(cuboid_vertex), Renderer->Cuboid.TransformVertexBuffers[CurrentBackBufferIndex].Buffer.Handle, Renderer->Cuboid.InstanceCount * sizeof(cuboid_transform_vertex_data), sizeof(cuboid_transform_vertex_data));

        // Bind index buffer
        DX12CmdSetIndexBuffer(CommandList, Renderer->Quad.IndexBuffer.Buffer.Handle, 36 * sizeof(u32), DXGI_FORMAT_R32_UINT);

        // Issue draw call
        CommandList->DrawIndexedInstanced(36, Renderer->Cuboid.InstanceCount, 0, 0, 0);
    }

    // Render a skybox
    if (false)
    {
        auto& Skybox = Renderer->Skybox;
        CommandList->SetGraphicsRootSignature(Skybox.RootSignature);
        CommandList->SetPipelineState(Skybox.Pipeline.Handle);

        CommandList->SetGraphicsRoot32BitConstants(0, sizeof(Renderer->RenderData.SkyboxRootSignatureBuffer) / 4, &Renderer->RenderData.SkyboxRootSignatureBuffer, 0);

        // Bind vertex positions
        DX12CmdSetVertexBuffer(CommandList, 0, Skybox.VertexBuffer.Buffer.Handle, Skybox.VertexBuffer.Buffer.Size, sizeof(v3));

        // Bind index buffer
        DX12CmdSetIndexBuffer(CommandList, Skybox.IndexBuffer.Buffer.Handle, sizeof(c_SkyboxIndices), DXGI_FORMAT_R32_UINT);

        CommandList->DrawIndexedInstanced(36, 1, 0, 0, 0);
    }

    // Render distant objects
    if (Renderer->DistantQuad.IndexCount > 0 && false)
    {
        CommandList->SetGraphicsRootSignature(Renderer->Skybox.RootSignature);
        CommandList->SetPipelineState(Renderer->DistantQuad.Pipeline.Handle);
        CommandList->SetGraphicsRoot32BitConstants(0, sizeof(distant_quad_root_signature_constant_buffer) / 4, &Renderer->RenderData.DistantObjectRootSignatureBuffer, 0);

        // Bind vertex positions
        DX12CmdSetVertexBuffer(CommandList, 0, Renderer->DistantQuad.VertexBuffers[CurrentBackBufferIndex].Buffer.Handle, Renderer->Quad.IndexCount * sizeof(distant_quad_vertex), sizeof(distant_quad_vertex));

        // Bind index buffer
        DX12CmdSetIndexBuffer(CommandList, Renderer->Quad.IndexBuffer.Buffer.Handle, Renderer->DistantQuad.IndexCount * sizeof(u32), DXGI_FORMAT_R32_UINT);

        CommandList->DrawIndexedInstanced(Renderer->DistantQuad.IndexCount, 1, 0, 0, 0);
    }

    CommandList->SetGraphicsRootSignature(Renderer->RootSignature);

    // Render quaded cuboids
    if (Renderer->QuadedCuboid.IndexCount > 0 && false)
    {
        CommandList->SetPipelineState(Renderer->QuadedCuboid.Pipeline.Handle);

        CommandList->SetGraphicsRoot32BitConstants(0, sizeof(cuboid_root_signature_constant_buffer) / 4, &Renderer->RenderData.CuboidRootSignatureBuffer, 0);
        CommandList->SetGraphicsRootConstantBufferView(1, Renderer->LightEnvironmentConstantBuffers[CurrentBackBufferIndex].Buffer.Handle->GetGPUVirtualAddress());

        // Bind vertex buffer
        DX12CmdSetVertexBuffer(CommandList, 0, Renderer->QuadedCuboid.VertexBuffers[CurrentBackBufferIndex].Buffer.Handle, Renderer->QuadedCuboid.VertexBuffers[CurrentBackBufferIndex].Buffer.Size, sizeof(quaded_cuboid_vertex));

        // Bind index buffer
        DX12CmdSetIndexBuffer(CommandList, Renderer->Quad.IndexBuffer.Buffer.Handle, Renderer->QuadedCuboid.IndexCount * sizeof(u32), DXGI_FORMAT_R32_UINT);

        // Issue draw call
        CommandList->DrawIndexedInstanced(Renderer->QuadedCuboid.IndexCount, 1, 0, 0, 0);
    }

    // Render quads
    if (Renderer->Quad.IndexCount > 0 && false)
    {
        CommandList->SetPipelineState(Renderer->Quad.Pipeline.Handle);

        // Bind vertex buffer
        DX12CmdSetVertexBuffer(CommandList, 0, Renderer->Quad.VertexBuffers[CurrentBackBufferIndex].Buffer.Handle, Renderer->Quad.IndexCount * sizeof(quad_vertex), sizeof(quad_vertex));

        // Bind index buffer
        DX12CmdSetIndexBuffer(CommandList, Renderer->Quad.IndexBuffer.Buffer.Handle, Renderer->Quad.IndexCount * sizeof(u32), DXGI_FORMAT_R32_UINT);

        // TODO: For now just share the first half of the signature buffer, this needs some sort of distinction between HUD and Game stuff
        CommandList->SetGraphicsRoot32BitConstants(0, 16, &Renderer->RenderData.CuboidRootSignatureBuffer.ViewProjection, 0);

        // Issue draw call
        CommandList->DrawIndexedInstanced(Renderer->Quad.IndexCount, 1, 0, 0, 0);
    }

    // Render HUD
    if (Renderer->HUD.IndexCount > 0 && false)
    {
        CommandList->SetPipelineState(Renderer->HUD.Pipeline.Handle);

        // Bind vertex buffer
        DX12CmdSetVertexBuffer(CommandList, 0, Renderer->HUD.VertexBuffers[CurrentBackBufferIndex].Buffer.Handle, Renderer->HUD.IndexCount * sizeof(hud_quad_vertex), sizeof(hud_quad_vertex));

        // Bind index buffer
        DX12CmdSetIndexBuffer(CommandList, Renderer->Quad.IndexBuffer.Buffer.Handle, Renderer->HUD.IndexCount * sizeof(u32), DXGI_FORMAT_R32_UINT);

        CommandList->SetGraphicsRoot32BitConstants(0, 16, &Renderer->RenderData.HUDRootSignatureBuffer.Projection, 0);

        // Issue draw call
        CommandList->DrawIndexedInstanced(Renderer->HUD.IndexCount, 1, 0, 0, 0);
    }

    // Rendered frame needs to be transitioned to present state
    DX12CmdTransition(CommandList, BackBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    // Finalize the command list
    DxAssert(CommandList->Close());

    Renderer->DirectCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&CommandList);

    DxAssert(Renderer->SwapChain->Present(Renderer->VSync, 0));

    // Wait for GPU to finish presenting
    FrameFenceValue = GameRendererSignal(Renderer->DirectCommandQueue, Renderer->Fence, &Renderer->FenceValue);
    GameRendererWaitForFenceValue(Renderer->Fence, FrameFenceValue, Renderer->DirectFenceEvent);

    // Reset rendered state
    GameRendererResetState(Renderer);

    // Move to another back buffer
    Renderer->CurrentBackBufferIndex = Renderer->SwapChain->GetCurrentBackBufferIndex();

    // Log dx12 stuff
    DumpInfoQueue();
}

internal void d3d12_render_backend_resize_swapchain(d3d12_render_backend* Renderer, u32 RequestWidth, u32 RequestHeight)
{
    // Flush the GPU queue to make sure the swap chain's back buffers are not being referenced by an in-flight command list.
    GameRendererFlush(Renderer);

    // Reset fence values and release back buffers
    for (u32 i = 0; i < FIF; i++)
    {
        Renderer->BackBuffers[i]->Release();
        Renderer->FrameFenceValues[i] = Renderer->FrameFenceValues[Renderer->CurrentBackBufferIndex];
    }

    DXGI_SWAP_CHAIN_DESC SwapChainDesc;
    DxAssert(Renderer->SwapChain->GetDesc(&SwapChainDesc));
    DxAssert(Renderer->SwapChain->ResizeBuffers(FIF, RequestWidth, RequestHeight, SwapChainDesc.BufferDesc.Format, SwapChainDesc.Flags));
    DxAssert(Renderer->SwapChain->GetDesc(&SwapChainDesc));

    Renderer->CurrentBackBufferIndex = Renderer->SwapChain->GetCurrentBackBufferIndex();

    // Place rtv descriptor sequentially in memory
    {
        u32 RTVDescriptorSize = Renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle = Renderer->RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        for (u32 i = 0; i < FIF; ++i)
        {
            DxAssert(Renderer->SwapChain->GetBuffer(i, IID_PPV_ARGS(&Renderer->BackBuffers[i])));
            Renderer->Device->CreateRenderTargetView(Renderer->BackBuffers[i], nullptr, RtvHandle);

            Renderer->RTVHandles[i] = RtvHandle;
            RtvHandle.ptr += RTVDescriptorSize;
        }
    }

    // Recreate depth buffers
    {
        D3D12_CPU_DESCRIPTOR_HANDLE DsvHandle = Renderer->DSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        u32 DSVDescriptorSize = Renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

        // Create depth buffers
        for (u32 i = 0; i < FIF; i++)
        {
            // Release the old one
            Renderer->DepthBuffers[i]->Release();

            D3D12_CLEAR_VALUE OptimizedClearValue = {};
            OptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
            OptimizedClearValue.DepthStencil = { 1.0f, 0 };

            auto HeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

            D3D12_RESOURCE_DESC DepthStencilDesc = {};
            DepthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            DepthStencilDesc.Width = RequestWidth;
            DepthStencilDesc.Height = RequestHeight;
            DepthStencilDesc.DepthOrArraySize = 1;
            DepthStencilDesc.MipLevels = 1;
            DepthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
            DepthStencilDesc.SampleDesc.Count = 1;  // No MSAA
            DepthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

            DxAssert(Renderer->Device->CreateCommittedResource(
                &HeapProperties,
                D3D12_HEAP_FLAG_NONE,
                &DepthStencilDesc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE,
                &OptimizedClearValue,
                IID_PPV_ARGS(&Renderer->DepthBuffers[i])
            ));

            // Update the depth-stencil view.
            D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
            dsv.Format = DXGI_FORMAT_D32_FLOAT;
            dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            dsv.Texture2D.MipSlice = 0;
            dsv.Flags = D3D12_DSV_FLAG_NONE;

            Renderer->Device->CreateDepthStencilView(Renderer->DepthBuffers[i], &dsv, DsvHandle);

            // Increment by the size of the dsv descriptor size
            Renderer->DSVHandles[i] = DsvHandle;
            DsvHandle.ptr += DSVDescriptorSize;
        }
    }

    Warn("SwapChain resized to %d %d", RequestWidth, RequestHeight);
}

internal u64 GameRendererSignal(ID3D12CommandQueue* CommandQueue, ID3D12Fence* Fence, u64* FenceValue)
{
    u64& RefFenceValue = *FenceValue;
    u64 FenceValueForSignal = ++RefFenceValue;
    DxAssert(CommandQueue->Signal(Fence, FenceValueForSignal));
    return FenceValueForSignal;
}

internal void GameRendererWaitForFenceValue(ID3D12Fence* Fence, u64 FenceValue, HANDLE FenceEvent, u32 Duration)
{
    if (Fence->GetCompletedValue() < FenceValue)
    {
        Fence->SetEventOnCompletion(FenceValue, FenceEvent);
        WaitForSingleObject(FenceEvent, Duration);
    }
}

internal void GameRendererFlush(d3d12_render_backend* Renderer)
{
    u64 FenceValueForSignal = GameRendererSignal(Renderer->DirectCommandQueue, Renderer->Fence, &Renderer->FenceValue);
    GameRendererWaitForFenceValue(Renderer->Fence, FenceValueForSignal, Renderer->DirectFenceEvent);
}

internal void GameRendererResetState(d3d12_render_backend* Renderer)
{
    // Reset light environment
    Renderer->LightEnvironment.Clear();

    // Reset indices
    Renderer->Quad.IndexCount = 0;
    Renderer->Quad.VertexDataPtr = Renderer->Quad.VertexDataBase;

    // HUD
    Renderer->HUD.IndexCount = 0;
    Renderer->HUD.VertexDataPtr = Renderer->HUD.VertexDataBase;

    Renderer->QuadedCuboid.VertexDataPtr = Renderer->QuadedCuboid.VertexDataBase;
    Renderer->QuadedCuboid.IndexCount = 0;

    // Distant quads
    Renderer->DistantQuad.IndexCount = 0;
    Renderer->DistantQuad.VertexDataPtr = Renderer->DistantQuad.VertexDataBase;

    // Reset Cuboid indices
    Renderer->Cuboid.InstanceCount = 0;

    // Reset textures
    Renderer->CurrentTextureStackIndex = 1;

    for (u32 i = 1; i < Renderer->CurrentTextureStackIndex; i++)
    {
        Renderer->TextureStack[i] = {};
    }
}

// ===============================================================================================================
//                                                   RENDERER API                                               
// ===============================================================================================================

internal void GameRendererSetRenderData(d3d12_render_backend* Renderer, const v3& CameraPosition, const m4& View, const m4& Projection, const m4& InverseView, const m4& HUDProjection, f32 Time, m4 LightSpaceMatrixTemp)
{
    auto& RenderData = Renderer->RenderData;
    RenderData.CuboidRootSignatureBuffer.View = View;
    RenderData.CuboidRootSignatureBuffer.ViewProjection = Projection * View;
    RenderData.Projection = Projection;
    RenderData.CameraPosition = CameraPosition;
    RenderData.HUDRootSignatureBuffer.Projection = HUDProjection;
    RenderData.SkyboxRootSignatureBuffer.InverseViewProjection = bkm::Inverse(RenderData.CuboidRootSignatureBuffer.ViewProjection);
    RenderData.SkyboxRootSignatureBuffer.Time = Time;

    // Distants quads
    RenderData.DistantObjectRootSignatureBuffer.ViewProjectionNoTranslation = Projection * m4(m3(View)); // Remove translation

    // Shadow pass
    Renderer->RenderData.CuboidRootSignatureBuffer.LightSpaceMatrix = LightSpaceMatrixTemp;
    RenderData.ShadowPassRootSignatureBuffer.LightSpaceMatrix = LightSpaceMatrixTemp;
}

internal void GameRendererSubmitQuad(d3d12_render_backend* Renderer, const v3& Translation, const v3& Rotation, const v2& Scale, const v4& Color)
{
    Assert(Renderer->Quad.IndexCount < c_MaxQuadIndices, "Renderer->QuadIndexCount < c_MaxQuadIndices");

    m4 Transform = bkm::Translate(m4(1.0f), Translation)
        * bkm::ToM4(qtn(Rotation))
        * bkm::Scale(m4(1.0f), v3(Scale, 1.0f));

    v2 Coords[4];
    Coords[0] = { 0.0f, 0.0f };
    Coords[1] = { 1.0f, 0.0f };
    Coords[2] = { 1.0f, 1.0f };
    Coords[3] = { 0.0f, 1.0f };

    for (u32 i = 0; i < CountOf(c_QuadVertexPositions); i++)
    {
        Renderer->Quad.VertexDataPtr->Position = Transform * c_QuadVertexPositions[i];
        Renderer->Quad.VertexDataPtr->Color = Color;
        Renderer->Quad.VertexDataPtr->TexCoord = Coords[i];
        Renderer->Quad.VertexDataPtr->TexIndex = 0;
        Renderer->Quad.VertexDataPtr++;
    }

    Renderer->Quad.IndexCount += 6;
}

internal void GameRendererSubmitQuad(d3d12_render_backend* Renderer, const v3& Translation, const v3& Rotation, const v2& Scale, const texture& Texture, const v4& Color)
{
    Assert(Renderer->Quad.IndexCount < c_MaxQuadIndices, "Renderer->QuadIndexCount < c_MaxQuadIndices");
    Assert(Texture.Handle != nullptr, "Texture is invalid!");

    // TODO: ID system, pointers are unreliable
    u32 TextureIndex = 0;
    for (u32 i = 1; i < Renderer->CurrentTextureStackIndex; i++)
    {
        if (Renderer->TextureStack[i].ptr == Texture.SRVDescriptor.ptr)
        {
            TextureIndex = i;
            break;
        }
    }

    if (TextureIndex == 0)
    {
        Assert(Renderer->CurrentTextureStackIndex < c_MaxTexturesPerDrawCall, "Renderer->TextureStackIndex < c_MaxTexturesPerDrawCall");
        TextureIndex = Renderer->CurrentTextureStackIndex;
        Renderer->TextureStack[TextureIndex] = Texture.SRVDescriptor;
        Renderer->CurrentTextureStackIndex++;
    }

    m4 Transform = bkm::Translate(m4(1.0f), Translation)
        * bkm::ToM4(qtn(Rotation))
        * bkm::Scale(m4(1.0f), v3(Scale, 1.0f));

    v2 Coords[4];
    Coords[0] = { 0.0f, 0.0f };
    Coords[1] = { 1.0f, 0.0f };
    Coords[2] = { 1.0f, 1.0f };
    Coords[3] = { 0.0f, 1.0f };

    for (u32 i = 0; i < CountOf(c_QuadVertexPositions); i++)
    {
        Renderer->Quad.VertexDataPtr->Position = Transform * c_QuadVertexPositions[i];
        Renderer->Quad.VertexDataPtr->Color = Color;
        Renderer->Quad.VertexDataPtr->TexCoord = Coords[i];
        Renderer->Quad.VertexDataPtr->TexIndex = TextureIndex;
        Renderer->Quad.VertexDataPtr++;
    }

    Renderer->Quad.IndexCount += 6;
}

internal void GameRendererSubmitDistantQuad(d3d12_render_backend* Renderer, const v3& Translation, const v3& Rotation, const texture& Texture, const v4& Color)
{
    m4 Transform = bkm::Translate(m4(1.0f), Translation) * bkm::ToM4(qtn(Rotation));

    v2 Coords[4];
    Coords[0] = { 0.0f, 0.0f };
    Coords[1] = { 1.0f, 0.0f };
    Coords[2] = { 1.0f, 1.0f };
    Coords[3] = { 0.0f, 1.0f };

    for (u32 i = 0; i < CountOf(c_QuadVertexPositions); i++)
    {
        Renderer->DistantQuad.VertexDataPtr->Position = Transform * c_QuadVertexPositions[i];
        Renderer->DistantQuad.VertexDataPtr++;
    }

    Renderer->DistantQuad.IndexCount += 6;
}

internal void GameRendererSubmitBillboardQuad(d3d12_render_backend* Renderer, const v3& Translation, const v2& Scale, const texture& Texture, const v4& Color)
{
    m4& CameraView = Renderer->RenderData.CuboidRootSignatureBuffer.View;

    v3 CamRightWS(CameraView[0][0], CameraView[1][0], CameraView[2][0]);
    v3 CamUpWS(CameraView[0][1], CameraView[1][1], CameraView[2][1]);

    v3 Positions[4];
    Positions[0] = Translation + CamRightWS * c_QuadVertexPositions[0].x * Scale.x + CamUpWS * c_QuadVertexPositions[0].y * Scale.y;
    Positions[1] = Translation + CamRightWS * c_QuadVertexPositions[1].x * Scale.x + CamUpWS * c_QuadVertexPositions[1].y * Scale.y;
    Positions[2] = Translation + CamRightWS * c_QuadVertexPositions[2].x * Scale.x + CamUpWS * c_QuadVertexPositions[2].y * Scale.y;
    Positions[3] = Translation + CamRightWS * c_QuadVertexPositions[3].x * Scale.x + CamUpWS * c_QuadVertexPositions[3].y * Scale.y;

    GameRendererSubmitQuadCustom(Renderer, Positions, Texture, Color);
}

internal void GameRendererSubmitQuadCustom(d3d12_render_backend* Renderer, v3 VertexPositions[4], const texture& Texture, const v4& Color)
{
    Assert(Renderer->Quad.IndexCount < c_MaxQuadIndices, "Renderer->QuadIndexCount < c_MaxQuadIndices");
    Assert(Texture.Handle != nullptr, "Texture handle is nullptr!");

    // TODO: ID system, pointers are unreliable
    u32 TextureIndex = 0;
    for (u32 i = 1; i < Renderer->CurrentTextureStackIndex; i++)
    {
        if (Renderer->TextureStack[i].ptr == Texture.SRVDescriptor.ptr)
        {
            TextureIndex = i;
            break;
        }
    }

    if (TextureIndex == 0)
    {
        Assert(Renderer->CurrentTextureStackIndex < c_MaxTexturesPerDrawCall, "Renderer->TextureStackIndex < c_MaxTexturesPerDrawCall");
        TextureIndex = Renderer->CurrentTextureStackIndex;
        Renderer->TextureStack[TextureIndex] = Texture.SRVDescriptor;
        Renderer->CurrentTextureStackIndex++;
    }

    v2 Coords[4];
    Coords[0] = { 0.0f, 0.0f };
    Coords[1] = { 1.0f, 0.0f };
    Coords[2] = { 1.0f, 1.0f };
    Coords[3] = { 0.0f, 1.0f };

    for (u32 i = 0; i < 4; i++)
    {
        Renderer->Quad.VertexDataPtr->Position = v4(VertexPositions[i], 1.0f);
        Renderer->Quad.VertexDataPtr->Color = Color;
        Renderer->Quad.VertexDataPtr->TexCoord = Coords[i];
        Renderer->Quad.VertexDataPtr->TexIndex = TextureIndex;
        Renderer->Quad.VertexDataPtr++;
    }

    Renderer->Quad.IndexCount += 6;
}

internal void GameRendererSubmitCuboid(d3d12_render_backend* Renderer, const v3& Translation, const v3& Rotation, const v3& Scale, const texture& Texture, const v4& Color)
{
    Assert(Renderer->Cuboid.InstanceCount < c_MaxCubePerBatch, "Renderer->CuboidInstanceCount < c_MaxCuboidsPerBatch");

    u32 TextureIndex = 0;
    for (u32 i = 1; i < Renderer->CurrentTextureStackIndex; i++)
    {
        if (Renderer->TextureStack[i].ptr == Texture.SRVDescriptor.ptr)
        {
            TextureIndex = i;
            break;
        }
    }

    if (TextureIndex == 0)
    {
        Assert(Renderer->CurrentTextureStackIndex < c_MaxTexturesPerDrawCall, "Renderer->TextureStackIndex < c_MaxTexturesPerDrawCall");
        TextureIndex = Renderer->CurrentTextureStackIndex;
        Renderer->TextureStack[TextureIndex] = Texture.SRVDescriptor;
        Renderer->CurrentTextureStackIndex++;
    }

    auto& Cuboid = Renderer->Cuboid.InstanceData[Renderer->Cuboid.InstanceCount];

#if ENABLE_SIMD
    XMMATRIX XmmScale = XMMatrixScalingFromVector(XMVectorSet(Scale.x, Scale.y, Scale.z, 0.0f));
    XMMATRIX XmmRot = XMMatrixRotationQuaternion(XMQuaternionRotationRollPitchYaw(Rotation.x, Rotation.y, Rotation.z));
    XMMATRIX XmmTrans = XMMatrixTranslationFromVector(XMVectorSet(Translation.x, Translation.y, Translation.z, 0.0f));
    XMMATRIX XmmTransform = XmmScale * XmmRot * XmmTrans;

    Cuboid.Color = Color;
    Cuboid.XmmTransform = XmmTransform;
    Cuboid.TextureIndex = TextureIndex;

    Renderer->Cuboid.InstanceCount++;
#else
    m4 Transform = bkm::Translate(m4(1.0f), Translation)
        * bkm::ToM4(qtn(Rotation))
        * bkm::Scale(m4(1.0f), Scale);

    Cuboid.Color = Color;
    Cuboid.Transform = Transform;
    Cuboid.TextureIndex = TextureIndex;
    Renderer->CuboidInstanceCount++;
#endif
}

internal void GameRendererSubmitCuboid(d3d12_render_backend* Renderer, const v3& Translation, const v3& Rotation, const v3& Scale, const v4& Color)
{
    Assert(Renderer->Cuboid.InstanceCount < c_MaxCubePerBatch, "Renderer->CuboidInstanceCount < c_MaxCuboidsPerBatch");

    auto& Cuboid = Renderer->Cuboid.InstanceData[Renderer->Cuboid.InstanceCount];

#if ENABLE_SIMD
    XMMATRIX XmmScale = XMMatrixScalingFromVector(XMVectorSet(Scale.x, Scale.y, Scale.z, 0.0f));
    XMMATRIX XmmRot = XMMatrixRotationQuaternion(XMQuaternionRotationRollPitchYaw(Rotation.x, Rotation.y, Rotation.z));
    XMMATRIX XmmTrans = XMMatrixTranslationFromVector(XMVectorSet(Translation.x, Translation.y, Translation.z, 0.0f));

    XMMATRIX XmmTransform = XmmScale * XmmRot * XmmTrans;

    Cuboid.Color = Color;
    Cuboid.XmmTransform = XmmTransform;
    Cuboid.TextureIndex = 0;
    Renderer->Cuboid.InstanceCount++;
#else
    m4 Transform = bkm::Translate(m4(1.0f), Translation)
        * bkm::ToM4(qtn(Rotation))
        * bkm::Scale(m4(1.0f), Scale);

    Cuboid.Color = Color;
    Cuboid.Transform = Transform;
    Cuboid.TextureIndex = 0;
    Renderer->Cuboid.InstanceCount++;
#endif
}

internal void GameRendererSubmitCuboid(d3d12_render_backend* Renderer, const v3& Translation, const texture& Texture, const v4& Color)
{
    Assert(Renderer->Cuboid.InstanceCount < c_MaxCubePerBatch, "Renderer->CuboidInstanceCount < c_MaxCuboidsPerBatch");

    u32 TextureIndex = 0;
    for (u32 i = 1; i < Renderer->CurrentTextureStackIndex; i++)
    {
        if (Renderer->TextureStack[i].ptr == Texture.SRVDescriptor.ptr)
        {
            TextureIndex = i;
            break;
        }
    }

    if (TextureIndex == 0)
    {
        Assert(Renderer->CurrentTextureStackIndex < c_MaxTexturesPerDrawCall, "Renderer->TextureStackIndex < c_MaxTexturesPerDrawCall");
        TextureIndex = Renderer->CurrentTextureStackIndex;
        Renderer->TextureStack[TextureIndex] = Texture.SRVDescriptor;
        Renderer->CurrentTextureStackIndex++;
    }

    auto& Cuboid = Renderer->Cuboid.InstanceData[Renderer->Cuboid.InstanceCount];

    // TODO: We can do better by simply copying data and then calculate everything at one swoop
#if ENABLE_SIMD
    Cuboid.XmmTransform = XMMatrixTranslationFromVector(XMVectorSet(Translation.x, Translation.y, Translation.z, 0.0f));

    Cuboid.Color = Color;
    Cuboid.TextureIndex = TextureIndex;
    Renderer->Cuboid.InstanceCount++;
#else
    Cuboid.Color = Color;
    Cuboid.Transform = bkm::Translate(m4(1.0f), Translation);
    Cuboid.TextureIndex = TextureIndex;
    Renderer->CuboidInstanceCount++;
#endif
}

internal void GameRendererSubmitCuboid(d3d12_render_backend* Renderer, const v3& Translation, const v4& Color)
{
    Assert(Renderer->Cuboid.InstanceCount < c_MaxCubePerBatch, "Renderer->CuboidInstanceCount < c_MaxCuboidsPerBatch");

    auto& Cuboid = Renderer->Cuboid.InstanceData[Renderer->Cuboid.InstanceCount];

#if ENABLE_SIMD
    Cuboid.XmmTransform = XMMatrixTranslationFromVector(XMVectorSet(Translation.x, Translation.y, Translation.z, 0.0f));
    Cuboid.Color = Color;
    Cuboid.TextureIndex = 0;
    Renderer->Cuboid.InstanceCount++;
#else
    Cuboid.Color = Color;
    Cuboid.Transform = bkm::Translate(m4(1.0f), Translation);
    Cuboid.TextureIndex = 0;
    Renderer->Cuboid.InstanceCount++;
#endif
}

internal void GameRendererSubmitQuadedCuboid(d3d12_render_backend* Renderer, const v3& Translation, const v3& Rotation, const v3& Scale, const texture& Texture, const texture_block_coords& TextureCoords, const v4& Color)
{
    m4 Transform = bkm::Translate(m4(1.0f), Translation)
        * bkm::ToM4(qtn(Rotation))
        * bkm::Scale(m4(1.0f), Scale);

    GameRendererSubmitQuadedCuboid(Renderer, Transform, Texture, TextureCoords, Color);
}

internal void GameRendererSubmitQuadedCuboid(d3d12_render_backend* Renderer, const m4& Transform, const texture& Texture, const texture_block_coords& TextureCoords, const v4& Color)
{
    Assert(Renderer->QuadedCuboid.IndexCount < bkm::Max(c_MaxQuadIndices, c_MaxQuadedCuboidIndices), "Renderer->QuadedCuboidIndexCount < bkm::Max(c_MaxQuadIndices, c_MaxQuadedCuboidIndices)");
    Assert(Texture.Handle != nullptr, "Texture is invalid!");

    // TODO: ID system, pointers are unreliable
    u32 TextureIndex = 0;
    for (u32 i = 1; i < Renderer->CurrentTextureStackIndex; i++)
    {
        if (Renderer->TextureStack[i].ptr == Texture.SRVDescriptor.ptr)
        {
            TextureIndex = i;
            break;
        }
    }

    if (TextureIndex == 0)
    {
        Assert(Renderer->CurrentTextureStackIndex < c_MaxTexturesPerDrawCall, "Renderer->TextureStackIndex < c_MaxTexturesPerDrawCall");
        TextureIndex = Renderer->CurrentTextureStackIndex;
        Renderer->TextureStack[TextureIndex] = Texture.SRVDescriptor;
        Renderer->CurrentTextureStackIndex++;
    }

    m3 InversedTransposedMatrix = m3(bkm::Transpose(bkm::Inverse(Transform)));

    u32 j = 0;
    for (u32 i = 0; i < CountOf(c_CuboidVertices); i++)
    {
        Renderer->QuadedCuboid.VertexDataPtr->Position = Transform * c_CuboidVertices[i].Position;
        Renderer->QuadedCuboid.VertexDataPtr->Normal = InversedTransposedMatrix * c_CuboidVertices[i].Normal;
        Renderer->QuadedCuboid.VertexDataPtr->Color = Color;
        Renderer->QuadedCuboid.VertexDataPtr->TexCoord = TextureCoords.TextureCoords[j].Coords[i % 4];
        Renderer->QuadedCuboid.VertexDataPtr->TexIndex = TextureIndex;
        Renderer->QuadedCuboid.VertexDataPtr++;

        // Each quad is 4 indexed vertices, so we 
        if (i != 0 && (i + 1) % 4 == 0)
        {
            j++;
        }
    }

    Renderer->QuadedCuboid.IndexCount += 36;
}

internal void GameRendererSubmitDirectionalLight(d3d12_render_backend* Renderer, const v3& Direction, f32 Intensity, const v3& Radiance)
{
    directional_light& DirLight = Renderer->LightEnvironment.EmplaceDirectionalLight();
    DirLight.Direction = Direction;
    DirLight.Intensity = Intensity;
    DirLight.Radiance = Radiance;
}

internal void GameRendererSubmitPointLight(d3d12_render_backend* Renderer, const v3& Position, f32 Radius, f32 FallOff, const v3& Radiance, f32 Intensity)
{
    point_light& Light = Renderer->LightEnvironment.EmplacePointLight();
    Light.Position = Position;
    Light.Radius = Radius;
    Light.FallOff = FallOff;
    Light.Radiance = Radiance;
    Light.Intensity = Intensity;
}

// ===============================================================================================================
//                                             RENDERER API - HUD                                             
// ===============================================================================================================

internal void GameRendererHUDSubmitQuad(d3d12_render_backend* Renderer, v3 VertexPositions[4], const texture& Texture, const texture_coords& Coords, const v4& Color)
{
    Assert(Renderer->HUD.IndexCount < c_MaxHUDQuadIndices, "Renderer->HUDQuadIndexCount < c_MaxHUDQuadIndices");
    Assert(Texture.Handle != nullptr, "Texture is invalid!");

    // TODO: ID system, pointers are unreliable
    u32 TextureIndex = 0;
    for (u32 i = 1; i < Renderer->CurrentTextureStackIndex; i++)
    {
        if (Renderer->TextureStack[i].ptr == Texture.SRVDescriptor.ptr)
        {
            TextureIndex = i;
            break;
        }
    }

    if (TextureIndex == 0)
    {
        Assert(Renderer->CurrentTextureStackIndex < c_MaxTexturesPerDrawCall, "Renderer->CurrentTextureStackIndex < c_MaxTexturesPerDrawCall");
        TextureIndex = Renderer->CurrentTextureStackIndex;
        Renderer->TextureStack[TextureIndex] = Texture.SRVDescriptor;
        Renderer->CurrentTextureStackIndex++;
    }

    for (u32 i = 0; i < 4; i++)
    {
        Renderer->HUD.VertexDataPtr->Position = v4(VertexPositions[i], 1.0f);
        Renderer->HUD.VertexDataPtr->Color = Color;
        Renderer->HUD.VertexDataPtr->TexCoord = Coords[i];
        Renderer->HUD.VertexDataPtr->TexIndex = TextureIndex;
        Renderer->HUD.VertexDataPtr++;
    }

    Renderer->HUD.IndexCount += 6;
}
