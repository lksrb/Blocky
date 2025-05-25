#include "DX12Renderer.h"

#include "DX12Texture.cpp"
#include "DX12Buffer.cpp"

internal game_renderer GameRendererCreate(const game_window& Window)
{
    game_renderer Renderer = {};

    GameRendererInitD3D(&Renderer, Window);
    GameRendererInitD3DPipeline(&Renderer);

    return Renderer;
}

internal void GameRendererDestroy(game_renderer* Renderer)
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

        DX12VertexBufferDestroy(&Renderer->QuadVertexBuffers[i]);
    }

    DX12PipelineDestroy(&Renderer->QuadPipeline);
    DX12IndexBufferDestroy(&Renderer->QuadIndexBuffer);
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
    memset(Renderer, 0, sizeof(game_renderer));
}

internal void GameRendererInitD3D(game_renderer* Renderer, const game_window& Window)
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

        //dx12_texture DepthBuffer = DX12TextureCreate(Renderer->Device, Renderer->DirectCommandList, {}, Window.ClientAreaWidth, Window.ClientAreaHeight, DXGI_FORMAT_D32_FLOAT);

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

internal void GameRendererInitD3DPipeline(game_renderer* Renderer)
{
    auto Device = Renderer->Device;

    // Root Signature
    {
        // Sampler
        D3D12_STATIC_SAMPLER_DESC Sampler = {};
        Sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        Sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        Sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        Sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        Sampler.MipLODBias = 0;
        Sampler.MaxAnisotropy = 1;
        Sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        Sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        Sampler.MinLOD = 0.0f;
        Sampler.MaxLOD = D3D12_FLOAT32_MAX;
        Sampler.ShaderRegister = 0;
        Sampler.RegisterSpace = 0;
        Sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

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
        Desc.NumStaticSamplers = 1;
        Desc.pStaticSamplers = &Sampler;
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

        Renderer->QuadPipeline = DX12GraphicsPipelineCreate(Device, Renderer->RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/Quad.hlsl", D3D12_CULL_MODE_NONE);

        for (u32 i = 0; i < FIF; i++)
        {
            Renderer->QuadVertexBuffers[i] = DX12VertexBufferCreate(Device, sizeof(quad_vertex) * c_MaxQuadVertices);
        }

        Renderer->QuadVertexDataBase = VmAllocArray(quad_vertex, c_MaxQuadVertices);
        Renderer->QuadVertexDataPtr = Renderer->QuadVertexDataBase;

        // For better debugging
        // TODO: Remove
        memset(Renderer->QuadVertexDataBase, 0, sizeof(quad_vertex) * c_MaxQuadVertices);

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
            Renderer->QuadIndexBuffer = DX12IndexBufferCreate(Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, QuadIndices, MaxQuadIndexCount);
        }
    }

    // Basic Cuboids - if a cuboid does not have unique texture for each quad, then we can use this
    // TODO: We could even create a pipeline for NoRotNoScale cuboids and pass only translation vector
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

        Renderer->CuboidPipeline = DX12GraphicsPipelineCreate(Device, Renderer->RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/Cuboid.hlsl");

        Renderer->CuboidPositionsVertexBuffer = DX12VertexBufferCreate(Device, sizeof(c_CuboidVertices));

        DX12SubmitToQueueImmidiate(Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, [Renderer](ID3D12GraphicsCommandList* CommandList)
        {
            DX12VertexBufferSendData(&Renderer->CuboidPositionsVertexBuffer, CommandList, c_CuboidVertices, sizeof(c_CuboidVertices));
        });

        for (u32 i = 0; i < FIF; i++)
        {
            Renderer->CuboidTransformVertexBuffers[i] = DX12VertexBufferCreate(Device, sizeof(cuboid_transform_vertex_data) * c_MaxCubePerBatch);
        }
        Renderer->CuboidInstanceData = VmAllocArray(cuboid_transform_vertex_data, c_MaxCubePerBatch);
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

        Renderer->QuadedCuboidPipeline = DX12GraphicsPipelineCreate(Device, Renderer->RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/QuadedCuboid.hlsl");

        for (u32 i = 0; i < FIF; i++)
        {
            Renderer->QuadedCuboidVertexBuffers[i] = DX12VertexBufferCreate(Device, sizeof(quaded_cuboid_vertex) * c_MaxQuadedCuboidVertices);
        }

        Renderer->QuadedCuboidVertexDataBase = VmAllocArray(quaded_cuboid_vertex, c_MaxQuadedCuboidVertices);
        Renderer->QuadedCuboidVertexDataPtr = Renderer->QuadedCuboidVertexDataBase;

        // For better debugging
        // TODO: Remove
        memset(Renderer->QuadedCuboidVertexDataBase, 0, sizeof(quaded_cuboid_vertex) * c_MaxQuadedCuboidVertices);
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

        Renderer->HUDQuadPipeline = DX12GraphicsPipelineCreate(Device, Renderer->RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/HUD.hlsl");

        for (u32 i = 0; i < FIF; i++)
        {
            Renderer->HUDQuadVertexBuffers[i] = DX12VertexBufferCreate(Device, sizeof(hud_quad_vertex) * c_MaxHUDQuadVertices);
        }

        Renderer->HUDQuadVertexDataBase = VmAllocArray(hud_quad_vertex, c_MaxHUDQuadVertices);;
        Renderer->HUDQuadVertexDataPtr = Renderer->HUDQuadVertexDataBase;

        // For better debugging
        // TODO: Remove
        memset(Renderer->HUDQuadVertexDataBase, 0, sizeof(hud_quad_vertex) * c_MaxHUDQuadVertices);

        // TODO: Maybe our own index buffer?

        // Quad Index buffer
        if (0)
        {
            u32 MaxHUDQuadIndexCount = bkm::Max(c_MaxHUDQuadIndices, c_MaxHUDQuadIndices);
            u32* HUDQuadIndices = VmAllocArray(u32, MaxHUDQuadIndexCount);
            u32 Offset = 0;
            for (u32 i = 0; i < MaxHUDQuadIndexCount; i += 6)
            {
                HUDQuadIndices[i + 0] = Offset + 0;
                HUDQuadIndices[i + 1] = Offset + 1;
                HUDQuadIndices[i + 2] = Offset + 2;

                HUDQuadIndices[i + 3] = Offset + 2;
                HUDQuadIndices[i + 4] = Offset + 3;
                HUDQuadIndices[i + 5] = Offset + 0;

                Offset += 4;
            }
            //Renderer->HUDQuadIndexBuffer = DX12IndexBufferCreate(Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, HUDQuadIndices, MaxHUDQuadIndexCount);
        }
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

        // For better debugging
        // TODO: Remove
        memset(Renderer->DistantQuad.VertexDataBase, 0, sizeof(distant_quad_vertex) * c_MaxHUDQuadVertices);
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

    // Create light environment constant buffer for each frame
    for (u32 i = 0; i < FIF; i++)
    {
        Renderer->LightEnvironmentConstantBuffers[i] = DX12ConstantBufferCreate(Device, sizeof(light_environment));
    }
}

internal void GameRendererResizeSwapChain(game_renderer* Renderer, u32 RequestWidth, u32 RequestHeight)
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

internal void GameRendererRender(game_renderer* Renderer)
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

    DxAssert(CommandList->Reset(DirectCommandAllocator, Renderer->CuboidPipeline.Handle));

    // Frame that was presented needs to be set to render target again
    auto Barrier = DX12Transition(BackBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    CommandList->ResourceBarrier(1, &Barrier);

    // Set light environment data
    {
        DX12ConstantBufferSetData(&Renderer->LightEnvironmentConstantBuffers[CurrentBackBufferIndex], &Renderer->LightEnvironment, sizeof(light_environment));
    }

    // Copy vertex data to gpu buffer
    {
        // Quads - general purpose
        DX12VertexBufferSendData(&Renderer->QuadVertexBuffers[CurrentBackBufferIndex], Renderer->DirectCommandList, Renderer->QuadVertexDataBase, sizeof(quad_vertex) * Renderer->QuadIndexCount);

        // Cuboid made out of quads - alive entities
        DX12VertexBufferSendData(&Renderer->QuadedCuboidVertexBuffers[CurrentBackBufferIndex], Renderer->DirectCommandList, Renderer->QuadedCuboidVertexDataBase, sizeof(quaded_cuboid_vertex) * Renderer->QuadedCuboidIndexCount);

        // Cuboid - used for blocks
        DX12VertexBufferSendData(&Renderer->CuboidTransformVertexBuffers[CurrentBackBufferIndex], Renderer->DirectCommandList, Renderer->CuboidInstanceData, Renderer->CuboidInstanceCount * sizeof(cuboid_transform_vertex_data));

        // Distant quads - sun, moon, starts, etc
        DX12VertexBufferSendData(&Renderer->DistantQuad.VertexBuffers[CurrentBackBufferIndex], Renderer->DirectCommandList, Renderer->DistantQuad.VertexDataBase, Renderer->DistantQuad.IndexCount * sizeof(distant_quad_vertex));

        // HUD
        DX12VertexBufferSendData(&Renderer->HUDQuadVertexBuffers[CurrentBackBufferIndex], Renderer->DirectCommandList, Renderer->HUDQuadVertexDataBase, sizeof(hud_quad_vertex) * Renderer->HUDQuadIndexCount);
    }

    // Set and clear render target view
    v4 ClearColor = { 0.2f, 0.3f, 0.8f, 1.0f };
    CommandList->ClearRenderTargetView(RTV, &ClearColor.x, 0, nullptr);
    CommandList->ClearDepthStencilView(DSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    CommandList->OMSetRenderTargets(1, &RTV, false, &DSV);

    D3D12_VIEWPORT Viewport;
    Viewport.TopLeftX = 0;
    Viewport.TopLeftY = 0;
    Viewport.Width = (FLOAT)SwapChainDesc.BufferDesc.Width;
    Viewport.Height = (FLOAT)SwapChainDesc.BufferDesc.Height;
    Viewport.MinDepth = D3D12_MIN_DEPTH;
    Viewport.MaxDepth = D3D12_MAX_DEPTH;
    CommandList->RSSetViewports(1, &Viewport);

    D3D12_RECT ScissorRect;
    ScissorRect.left = 0;
    ScissorRect.top = 0;
    ScissorRect.right = SwapChainDesc.BufferDesc.Width;
    ScissorRect.bottom = SwapChainDesc.BufferDesc.Height;
    CommandList->RSSetScissorRects(1, &ScissorRect);

    CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Render a skybox
    {
        CommandList->SetGraphicsRootSignature(Renderer->Skybox.RootSignature);
        CommandList->SetPipelineState(Renderer->Skybox.Pipeline.Handle);

        CommandList->SetGraphicsRoot32BitConstants(0, sizeof(Renderer->RenderData.SkyboxRootSignatureBuffer) / 4, &Renderer->RenderData.SkyboxRootSignatureBuffer, 0);

        // Bind vertex positions
        local_persist D3D12_VERTEX_BUFFER_VIEW SkyboxVertexBufferView;
        SkyboxVertexBufferView.BufferLocation = Renderer->Skybox.VertexBuffer.Buffer.Handle->GetGPUVirtualAddress();
        SkyboxVertexBufferView.SizeInBytes = (u32)Renderer->Skybox.VertexBuffer.Buffer.Size;
        SkyboxVertexBufferView.StrideInBytes = sizeof(v3);
        CommandList->IASetVertexBuffers(0, 1, &SkyboxVertexBufferView);

        // Bind index buffer
        local_persist D3D12_INDEX_BUFFER_VIEW SkyboxIndexBufferView;
        SkyboxIndexBufferView.BufferLocation = Renderer->Skybox.IndexBuffer.Buffer.Handle->GetGPUVirtualAddress();
        SkyboxIndexBufferView.SizeInBytes = sizeof(c_SkyboxIndices);
        SkyboxIndexBufferView.Format = DXGI_FORMAT_R32_UINT;
        CommandList->IASetIndexBuffer(&SkyboxIndexBufferView);

        CommandList->DrawIndexedInstanced(36, 1, 0, 0, 0);
    }

    // Render distant objects
    if (Renderer->DistantQuad.IndexCount > 0)
    {
        CommandList->SetGraphicsRootSignature(Renderer->Skybox.RootSignature);
        CommandList->SetPipelineState(Renderer->DistantQuad.Pipeline.Handle);

        CommandList->SetGraphicsRoot32BitConstants(0, sizeof(distant_quad_root_signature_constant_buffer) / 4, &Renderer->RenderData.DistantObjectRootSignatureBuffer, 0);

        // Bind vertex positions
        local_persist D3D12_VERTEX_BUFFER_VIEW VertexBufferView;
        VertexBufferView.BufferLocation = Renderer->DistantQuad.VertexBuffers[CurrentBackBufferIndex].Buffer.Handle->GetGPUVirtualAddress();
        VertexBufferView.SizeInBytes = Renderer->QuadIndexCount * sizeof(distant_quad_vertex);
        VertexBufferView.StrideInBytes = sizeof(distant_quad_vertex);
        CommandList->IASetVertexBuffers(0, 1, &VertexBufferView);

        // Bind index buffer
        local_persist D3D12_INDEX_BUFFER_VIEW IndexBufferView;
        IndexBufferView.BufferLocation = Renderer->QuadIndexBuffer.Buffer.Handle->GetGPUVirtualAddress();
        IndexBufferView.SizeInBytes = Renderer->DistantQuad.IndexCount * sizeof(u32);
        IndexBufferView.Format = DXGI_FORMAT_R32_UINT;
        CommandList->IASetIndexBuffer(&IndexBufferView);

        CommandList->DrawIndexedInstanced(Renderer->DistantQuad.IndexCount, 1, 0, 0, 0);
    }

    // Set root constants
    // Number of 32 bit values - 16 floats in 4x4 matrix
    CommandList->SetGraphicsRootSignature(Renderer->RootSignature);
    CommandList->SetDescriptorHeaps(1, (ID3D12DescriptorHeap* const*)&Renderer->SRVDescriptorHeap);
    CommandList->SetGraphicsRootDescriptorTable(2, Renderer->SRVDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

    // Render instanced cuboids
    if (Renderer->CuboidInstanceCount > 0)
    {
        CommandList->SetPipelineState(Renderer->CuboidPipeline.Handle);

        CommandList->SetGraphicsRoot32BitConstants(0, sizeof(cuboid_root_signature_constant_buffer) / 4, &Renderer->RenderData.CuboidRootSignatureBuffer, 0);

        CommandList->SetGraphicsRootConstantBufferView(1, Renderer->LightEnvironmentConstantBuffers[CurrentBackBufferIndex].Buffer.Handle->GetGPUVirtualAddress());

        // Bind vertex positions
        local_persist D3D12_VERTEX_BUFFER_VIEW CuboidVertexBufferView;
        CuboidVertexBufferView.BufferLocation = Renderer->CuboidPositionsVertexBuffer.Buffer.Handle->GetGPUVirtualAddress();
        CuboidVertexBufferView.SizeInBytes = (u32)Renderer->CuboidPositionsVertexBuffer.Buffer.Size;
        CuboidVertexBufferView.StrideInBytes = sizeof(cuboid_vertex);

        // Bind transforms
        local_persist D3D12_VERTEX_BUFFER_VIEW TransformVertexBufferView;
        TransformVertexBufferView.BufferLocation = Renderer->CuboidTransformVertexBuffers[CurrentBackBufferIndex].Buffer.Handle->GetGPUVirtualAddress();
        TransformVertexBufferView.SizeInBytes = Renderer->CuboidInstanceCount * sizeof(cuboid_transform_vertex_data);
        TransformVertexBufferView.StrideInBytes = sizeof(cuboid_transform_vertex_data);

        D3D12_VERTEX_BUFFER_VIEW VertexBufferViews[] = { CuboidVertexBufferView, TransformVertexBufferView };
        CommandList->IASetVertexBuffers(0, 2, VertexBufferViews);

        // Bind index buffer
        local_persist D3D12_INDEX_BUFFER_VIEW IndexBufferView;
        IndexBufferView.BufferLocation = Renderer->QuadIndexBuffer.Buffer.Handle->GetGPUVirtualAddress();
        IndexBufferView.SizeInBytes = 36 * sizeof(u32);
        IndexBufferView.Format = DXGI_FORMAT_R32_UINT;
        CommandList->IASetIndexBuffer(&IndexBufferView);

        // Issue draw call
        CommandList->DrawIndexedInstanced(36, Renderer->CuboidInstanceCount, 0, 0, 0);
    }

    // Render quaded cuboids
    if (Renderer->QuadedCuboidIndexCount > 0)
    {
        CommandList->SetPipelineState(Renderer->QuadedCuboidPipeline.Handle);

        CommandList->SetGraphicsRoot32BitConstants(0, sizeof(cuboid_root_signature_constant_buffer) / 4, &Renderer->RenderData.CuboidRootSignatureBuffer, 0);
        CommandList->SetGraphicsRootConstantBufferView(1, Renderer->LightEnvironmentConstantBuffers[CurrentBackBufferIndex].Buffer.Handle->GetGPUVirtualAddress());

        // Bind cuboid vertex with normals
        local_persist D3D12_VERTEX_BUFFER_VIEW VertexBufferView;
        VertexBufferView.BufferLocation = Renderer->QuadedCuboidVertexBuffers[CurrentBackBufferIndex].Buffer.Handle->GetGPUVirtualAddress();
        VertexBufferView.SizeInBytes = (u32)Renderer->QuadedCuboidVertexBuffers[CurrentBackBufferIndex].Buffer.Size;
        VertexBufferView.StrideInBytes = sizeof(quaded_cuboid_vertex);
        CommandList->IASetVertexBuffers(0, 1, &VertexBufferView);

        // Bind index buffer
        local_persist D3D12_INDEX_BUFFER_VIEW IndexBufferView;
        IndexBufferView.BufferLocation = Renderer->QuadIndexBuffer.Buffer.Handle->GetGPUVirtualAddress();
        IndexBufferView.SizeInBytes = Renderer->QuadedCuboidIndexCount * sizeof(u32);
        IndexBufferView.Format = DXGI_FORMAT_R32_UINT;
        CommandList->IASetIndexBuffer(&IndexBufferView);

        // Issue draw call
        CommandList->DrawIndexedInstanced(Renderer->QuadedCuboidIndexCount, 1, 0, 0, 0);
    }

    // Render quads
    if (Renderer->QuadIndexCount > 0)
    {
        CommandList->SetPipelineState(Renderer->QuadPipeline.Handle);

        // Bind vertex buffer
        local_persist D3D12_VERTEX_BUFFER_VIEW QuadVertexBufferView;
        QuadVertexBufferView.BufferLocation = Renderer->QuadVertexBuffers[CurrentBackBufferIndex].Buffer.Handle->GetGPUVirtualAddress();
        QuadVertexBufferView.StrideInBytes = sizeof(quad_vertex);
        QuadVertexBufferView.SizeInBytes = Renderer->QuadIndexCount * sizeof(quad_vertex);
        CommandList->IASetVertexBuffers(0, 1, &QuadVertexBufferView);

        // Bind index buffer
        local_persist D3D12_INDEX_BUFFER_VIEW QuadIndexBufferView;
        QuadIndexBufferView.BufferLocation = Renderer->QuadIndexBuffer.Buffer.Handle->GetGPUVirtualAddress();
        QuadIndexBufferView.Format = DXGI_FORMAT_R32_UINT;
        QuadIndexBufferView.SizeInBytes = Renderer->QuadIndexCount * sizeof(u32);
        CommandList->IASetIndexBuffer(&QuadIndexBufferView);

        // TODO: For now just share the first half of the signature buffer, this needs some sort of distinction between HUD and Game stuff
        CommandList->SetGraphicsRoot32BitConstants(0, 16, &Renderer->RenderData.CuboidRootSignatureBuffer.ViewProjection, 0);

        // Issue draw call
        CommandList->DrawIndexedInstanced(Renderer->QuadIndexCount, 1, 0, 0, 0);
    }

    // Render HUD
    if (Renderer->HUDQuadIndexCount > 0)
    {
        CommandList->SetPipelineState(Renderer->HUDQuadPipeline.Handle);

        // Bind vertex buffer
        local_persist D3D12_VERTEX_BUFFER_VIEW HUDQuadVertexBufferView;
        HUDQuadVertexBufferView.BufferLocation = Renderer->HUDQuadVertexBuffers[CurrentBackBufferIndex].Buffer.Handle->GetGPUVirtualAddress();
        HUDQuadVertexBufferView.StrideInBytes = sizeof(hud_quad_vertex);
        HUDQuadVertexBufferView.SizeInBytes = Renderer->HUDQuadIndexCount * sizeof(hud_quad_vertex);
        CommandList->IASetVertexBuffers(0, 1, &HUDQuadVertexBufferView);

        // Bind index buffer
        local_persist D3D12_INDEX_BUFFER_VIEW HUDQuadIndexBufferView;
        HUDQuadIndexBufferView.BufferLocation = Renderer->QuadIndexBuffer.Buffer.Handle->GetGPUVirtualAddress();
        HUDQuadIndexBufferView.Format = DXGI_FORMAT_R32_UINT;
        HUDQuadIndexBufferView.SizeInBytes = Renderer->HUDQuadIndexCount * sizeof(u32);
        CommandList->IASetIndexBuffer(&HUDQuadIndexBufferView);

        CommandList->SetGraphicsRoot32BitConstants(0, 16, &Renderer->RenderData.HUDRootSignatureBuffer.Projection, 0);

        // Issue draw call
        CommandList->DrawIndexedInstanced(Renderer->HUDQuadIndexCount, 1, 0, 0, 0);
    }

    // Present transition
    {
        // Rendered frame needs to be transitioned to present state
        auto Barrier = DX12Transition(BackBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        CommandList->ResourceBarrier(1, &Barrier);
    }

    // Finalize the command list
    DxAssert(CommandList->Close());

    Renderer->DirectCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&CommandList);

    DxAssert(Renderer->SwapChain->Present(Renderer->VSync, 0));

    // Wait for GPU to finish presenting
    FrameFenceValue = GameRendererSignal(Renderer->DirectCommandQueue, Renderer->Fence, &Renderer->FenceValue);
    GameRendererWaitForFenceValue(Renderer->Fence, FrameFenceValue, Renderer->DirectFenceEvent);

    // Reset rendered scene state

    // Reset light environment
    Renderer->LightEnvironment.Clear();

    // Reset indices
    Renderer->QuadIndexCount = 0;
    Renderer->QuadVertexDataPtr = Renderer->QuadVertexDataBase;

    // HUD
    Renderer->HUDQuadIndexCount = 0;
    Renderer->HUDQuadVertexDataPtr = Renderer->HUDQuadVertexDataBase;

    Renderer->QuadedCuboidVertexDataPtr = Renderer->QuadedCuboidVertexDataBase;
    Renderer->QuadedCuboidIndexCount = 0;

    // Distant quads
    Renderer->DistantQuad.IndexCount = 0;
    Renderer->DistantQuad.VertexDataPtr = Renderer->DistantQuad.VertexDataBase;

    // Reset Cuboid indices
    Renderer->CuboidInstanceCount = 0;

    // Reset textures
    Renderer->CurrentTextureStackIndex = 1;

    for (u32 i = 1; i < Renderer->CurrentTextureStackIndex; i++)
    {
        Renderer->TextureStack[i] = {};
    }

    // Move to another back buffer
    Renderer->CurrentBackBufferIndex = Renderer->SwapChain->GetCurrentBackBufferIndex();

    // Log dx12 stuff
    DumpInfoQueue();
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

internal void GameRendererFlush(game_renderer* Renderer)
{
    u64 FenceValueForSignal = GameRendererSignal(Renderer->DirectCommandQueue, Renderer->Fence, &Renderer->FenceValue);
    GameRendererWaitForFenceValue(Renderer->Fence, FenceValueForSignal, Renderer->DirectFenceEvent);
}

// ===============================================================================================================
//                                                   RENDERER API                                               
// ===============================================================================================================

internal void GameRendererSetRenderData(game_renderer* Renderer, const v3& CameraPosition, const m4& View, const m4& Projection, const m4& InverseView, const m4& HUDProjection, f32 Time)
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
}

internal void GameRendererSubmitQuad(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v2& Scale, const v4& Color)
{
    Assert(Renderer->QuadIndexCount < c_MaxQuadIndices, "Renderer->QuadIndexCount < c_MaxQuadIndices");

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
        Renderer->QuadVertexDataPtr->Position = Transform * c_QuadVertexPositions[i];
        Renderer->QuadVertexDataPtr->Color = Color;
        Renderer->QuadVertexDataPtr->TexCoord = Coords[i];
        Renderer->QuadVertexDataPtr->TexIndex = 0;
        Renderer->QuadVertexDataPtr++;
    }

    Renderer->QuadIndexCount += 6;
}

internal void GameRendererSubmitQuad(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v2& Scale, const texture& Texture, const v4& Color)
{
    Assert(Renderer->QuadIndexCount < c_MaxQuadIndices, "Renderer->QuadIndexCount < c_MaxQuadIndices");
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
        Renderer->QuadVertexDataPtr->Position = Transform * c_QuadVertexPositions[i];
        Renderer->QuadVertexDataPtr->Color = Color;
        Renderer->QuadVertexDataPtr->TexCoord = Coords[i];
        Renderer->QuadVertexDataPtr->TexIndex = TextureIndex;
        Renderer->QuadVertexDataPtr++;
    }

    Renderer->QuadIndexCount += 6;
}

internal void GameRendererSubmitDistantQuad(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v2& Scale, const texture& Texture, const v4& Color)
{
#if 1
    m4 Transform = bkm::Translate(m4(1.0f), Translation) * bkm::ToM4(qtn(Rotation));
#else
    local_persist f32 MadeUpTime = 0.0f;
    m4& CameraView = Renderer->RenderData.CuboidRootSignatureBuffer.View;
    v3 CameraPosition = Renderer->RenderData.CameraPosition;

    f32 Distance = 100.0f;
    float angle = MadeUpTime; // speed of day-night cycle (radians per second)
    v3 baseDir = v3(0.0f, 1.0f, 0.0f);

    // Rotate baseDir around X axis to simulate sun path
    float cosA = bkm::Cos(angle);
    float sinA = bkm::Sin(angle);
    v3 SunDirection = v3(
        baseDir.x,
        baseDir.y * cosA - baseDir.z * sinA,
        baseDir.y * sinA + baseDir.z * cosA
    );

    //TraceV3(SunDirection);

    SunDirection = bkm::Normalize(SunDirection);
    v3 SunPosition = SunDirection * Distance;

    // Direction from sun to camera
    v3 dirToCamera = bkm::Normalize(CameraPosition - SunPosition);

    TraceV3(dirToCamera);

    // World up vector
    v3 up = v3(0.0f, 1.0f, 0.0f);

    // Compute right vector
    v3 right = bkm::Normalize(bkm::Cross(up, dirToCamera));
    v3 correctedUp = bkm::Cross(dirToCamera, right);

    // Build rotation matrix for quad facing camera
    m4 rotation(1.0f);
    rotation[0] = v4(right, 0.0f);
    rotation[1] = v4(correctedUp, 0.0f);
    rotation[2] = v4(-dirToCamera, 0.0f);
    rotation[3] = v4(0, 0, 0, 1);

    // Final model matrix
    m4 Transform = bkm::Translate(m4(1.0f), SunPosition) * rotation * bkm::Scale(m4(1.0), v3(10));

    MadeUpTime += TimeStep;
#endif

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

internal void GameRendererSubmitBillboardQuad(game_renderer* Renderer, const v3& Translation, const v2& Scale, const texture& Texture, const v4& Color)
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

internal void GameRendererSubmitQuadCustom(game_renderer* Renderer, v3 VertexPositions[4], const texture& Texture, const v4& Color)
{
    Assert(Renderer->QuadIndexCount < c_MaxQuadIndices, "Renderer->QuadIndexCount < c_MaxQuadIndices");
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
        Renderer->QuadVertexDataPtr->Position = v4(VertexPositions[i], 1.0f);
        Renderer->QuadVertexDataPtr->Color = Color;
        Renderer->QuadVertexDataPtr->TexCoord = Coords[i];
        Renderer->QuadVertexDataPtr->TexIndex = TextureIndex;
        Renderer->QuadVertexDataPtr++;
    }

    Renderer->QuadIndexCount += 6;
}

internal void GameRendererSubmitCuboid(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v3& Scale, const texture& Texture, const v4& Color)
{
    Assert(Renderer->CuboidInstanceCount < c_MaxCubePerBatch, "Renderer->CuboidInstanceCount < c_MaxCuboidsPerBatch");

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

    auto& Cuboid = Renderer->CuboidInstanceData[Renderer->CuboidInstanceCount];

#if ENABLE_SIMD
    XMMATRIX XmmScale = XMMatrixScalingFromVector(XMVectorSet(Scale.x, Scale.y, Scale.z, 0.0f));
    XMMATRIX XmmRot = XMMatrixRotationQuaternion(XMQuaternionRotationRollPitchYaw(Rotation.x, Rotation.y, Rotation.z));
    XMMATRIX XmmTrans = XMMatrixTranslationFromVector(XMVectorSet(Translation.x, Translation.y, Translation.z, 0.0f));
    XMMATRIX XmmTransform = XmmScale * XmmRot * XmmTrans;

    Cuboid.Color = Color;
    Cuboid.XmmTransform = XmmTransform;
    Cuboid.TextureIndex = TextureIndex;

    Renderer->CuboidInstanceCount++;
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

internal void GameRendererSubmitCuboid(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v3& Scale, const v4& Color)
{
    Assert(Renderer->CuboidInstanceCount < c_MaxCubePerBatch, "Renderer->CuboidInstanceCount < c_MaxCuboidsPerBatch");

    auto& Cuboid = Renderer->CuboidInstanceData[Renderer->CuboidInstanceCount];

#if ENABLE_SIMD
    XMMATRIX XmmScale = XMMatrixScalingFromVector(XMVectorSet(Scale.x, Scale.y, Scale.z, 0.0f));
    XMMATRIX XmmRot = XMMatrixRotationQuaternion(XMQuaternionRotationRollPitchYaw(Rotation.x, Rotation.y, Rotation.z));
    XMMATRIX XmmTrans = XMMatrixTranslationFromVector(XMVectorSet(Translation.x, Translation.y, Translation.z, 0.0f));

    XMMATRIX XmmTransform = XmmScale * XmmRot * XmmTrans;

    Cuboid.Color = Color;
    Cuboid.XmmTransform = XmmTransform;
    Cuboid.TextureIndex = 0;
    Renderer->CuboidInstanceCount++;
#else
    m4 Transform = bkm::Translate(m4(1.0f), Translation)
        * bkm::ToM4(qtn(Rotation))
        * bkm::Scale(m4(1.0f), Scale);

    Cuboid.Color = Color;
    Cuboid.Transform = Transform;
    Cuboid.TextureIndex = 0;
    Renderer->CuboidInstanceCount++;
#endif
}

internal void GameRendererSubmitCuboid(game_renderer* Renderer, const v3& Translation, const texture& Texture, const v4& Color)
{
    Assert(Renderer->CuboidInstanceCount < c_MaxCubePerBatch, "Renderer->CuboidInstanceCount < c_MaxCuboidsPerBatch");

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

    auto& Cuboid = Renderer->CuboidInstanceData[Renderer->CuboidInstanceCount];

    // TODO: We can do better by simply copying data and then calculate everything at one swoop
#if ENABLE_SIMD
    Cuboid.XmmTransform = XMMatrixTranslationFromVector(XMVectorSet(Translation.x, Translation.y, Translation.z, 0.0f));

    Cuboid.Color = Color;
    Cuboid.TextureIndex = TextureIndex;
    Renderer->CuboidInstanceCount++;
#else
    Cuboid.Color = Color;
    Cuboid.Transform = bkm::Translate(m4(1.0f), Translation);
    Cuboid.TextureIndex = TextureIndex;
    Renderer->CuboidInstanceCount++;
#endif
}

internal void GameRendererSubmitCuboid(game_renderer* Renderer, const v3& Translation, const v4& Color)
{
    Assert(Renderer->CuboidInstanceCount < c_MaxCubePerBatch, "Renderer->CuboidInstanceCount < c_MaxCuboidsPerBatch");

    auto& Cuboid = Renderer->CuboidInstanceData[Renderer->CuboidInstanceCount];

#if ENABLE_SIMD
    Cuboid.XmmTransform = XMMatrixTranslationFromVector(XMVectorSet(Translation.x, Translation.y, Translation.z, 0.0f));
    Cuboid.Color = Color;
    Cuboid.TextureIndex = 0;
    Renderer->CuboidInstanceCount++;
#else
    Cuboid.Color = Color;
    Cuboid.Transform = bkm::Translate(m4(1.0f), Translation);
    Cuboid.TextureIndex = 0;
    Renderer->CuboidInstanceCount++;
#endif
}

internal void GameRendererSubmitQuadedCuboid(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v3& Scale, const texture& Texture, const texture_block_coords& TextureCoords, const v4& Color)
{
    m4 Transform = bkm::Translate(m4(1.0f), Translation)
        * bkm::ToM4(qtn(Rotation))
        * bkm::Scale(m4(1.0f), Scale);

    GameRendererSubmitQuadedCuboid(Renderer, Transform, Texture, TextureCoords, Color);
}

internal void GameRendererSubmitQuadedCuboid(game_renderer* Renderer, const m4& Transform, const texture& Texture, const texture_block_coords& TextureCoords, const v4& Color)
{
    Assert(Renderer->QuadedCuboidIndexCount < bkm::Max(c_MaxQuadIndices, c_MaxQuadedCuboidIndices), "Renderer->QuadedCuboidIndexCount < bkm::Max(c_MaxQuadIndices, c_MaxQuadedCuboidIndices)");
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
        Renderer->QuadedCuboidVertexDataPtr->Position = Transform * c_CuboidVertices[i].Position;
        Renderer->QuadedCuboidVertexDataPtr->Normal = InversedTransposedMatrix * c_CuboidVertices[i].Normal;
        Renderer->QuadedCuboidVertexDataPtr->Color = Color;
        Renderer->QuadedCuboidVertexDataPtr->TexCoord = TextureCoords.TextureCoords[j].Coords[i % 4];
        Renderer->QuadedCuboidVertexDataPtr->TexIndex = TextureIndex;
        Renderer->QuadedCuboidVertexDataPtr++;

        // Each quad is 4 indexed vertices, so we 
        if (i != 0 && (i + 1) % 4 == 0)
        {
            j++;
        }
    }

    Renderer->QuadedCuboidIndexCount += 36;
}

internal void GameRendererSubmitDirectionalLight(game_renderer* Renderer, const v3& Direction, f32 Intensity, const v3& Radiance)
{
    directional_light& DirLight = Renderer->LightEnvironment.EmplaceDirectionalLight();
    DirLight.Direction = Direction;
    DirLight.Intensity = Intensity;
    DirLight.Radiance = Radiance;
}

internal void GameRendererSubmitPointLight(game_renderer* Renderer, const v3& Position, f32 Radius, f32 FallOff, const v3& Radiance, f32 Intensity)
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

internal void GameRendererHUDSubmitQuad(game_renderer* Renderer, v3 VertexPositions[4], const texture& Texture, const texture_coords& Coords, const v4& Color)
{
    Assert(Renderer->HUDQuadIndexCount < c_MaxHUDQuadIndices, "Renderer->HUDQuadIndexCount < c_MaxHUDQuadIndices");
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
        Renderer->HUDQuadVertexDataPtr->Position = v4(VertexPositions[i], 1.0f);
        Renderer->HUDQuadVertexDataPtr->Color = Color;
        Renderer->HUDQuadVertexDataPtr->TexCoord = Coords[i];
        Renderer->HUDQuadVertexDataPtr->TexIndex = TextureIndex;
        Renderer->HUDQuadVertexDataPtr++;
    }

    Renderer->HUDQuadIndexCount += 6;
}
