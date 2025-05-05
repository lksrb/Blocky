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
        HRESULT Result = (Renderer->Factory->CreateSwapChainForHwnd(Renderer->DirectCommandQueue, Window.WindowHandle, &Desc, &FullScreenDesc, nullptr, &SwapChain1));

        DumpInfoQueue();
        // Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
        // will be handled manually.
        DxAssert(Renderer->Factory->MakeWindowAssociation(Window.WindowHandle, DXGI_MWA_NO_ALT_ENTER));
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
        Parameters[0].Constants.Num32BitValues = sizeof(Renderer->CuboidRootSignatureBuffer) / 4;
        Parameters[0].Constants.ShaderRegister = 0;  // b0
        Parameters[0].Constants.RegisterSpace = 0;
        Parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

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
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXINDEX", 0, DXGI_FORMAT_R32_UINT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        Renderer->QuadPipeline = DX12GraphicsPipelineCreate(Device, Renderer->RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/Quad.hlsl");

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
            u32* QuadIndices = VmAllocArray(u32, c_MaxQuadIndices);
            u32 Offset = 0;
            for (u32 i = 0; i < c_MaxQuadIndices; i += 6)
            {
                QuadIndices[i + 0] = Offset + 0;
                QuadIndices[i + 1] = Offset + 1;
                QuadIndices[i + 2] = Offset + 2;

                QuadIndices[i + 3] = Offset + 2;
                QuadIndices[i + 4] = Offset + 3;
                QuadIndices[i + 5] = Offset + 0;

                Offset += 4;
            }
            Renderer->QuadIndexBuffer = DX12IndexBufferCreate(Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, QuadIndices, c_MaxQuadIndices);
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

    // FAST Custom cuboid
    {
        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC InputElementDescs[] =
        {
            // Per vertex
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            //{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

            // Per instance
            { "TRANSFORMA", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TRANSFORMB", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
            { "TRANSFORMC", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TRANSFORMD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },

            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 2, DXGI_FORMAT_R32G32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 3, DXGI_FORMAT_R32G32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },

            { "TEXCOORD", 4, DXGI_FORMAT_R32G32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 5, DXGI_FORMAT_R32G32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 6, DXGI_FORMAT_R32G32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 7, DXGI_FORMAT_R32G32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },

            { "TEXCOORD", 8, DXGI_FORMAT_R32G32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 9, DXGI_FORMAT_R32G32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 10, DXGI_FORMAT_R32G32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 11, DXGI_FORMAT_R32G32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },

            { "TEXCOORD", 12, DXGI_FORMAT_R32G32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 13, DXGI_FORMAT_R32G32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 14, DXGI_FORMAT_R32G32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 15, DXGI_FORMAT_R32G32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },

            { "TEXCOORD", 16, DXGI_FORMAT_R32G32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 17, DXGI_FORMAT_R32G32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 18, DXGI_FORMAT_R32G32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 19, DXGI_FORMAT_R32G32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },

            { "TEXCOORD", 20, DXGI_FORMAT_R32G32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 21, DXGI_FORMAT_R32G32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 22, DXGI_FORMAT_R32G32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 23, DXGI_FORMAT_R32G32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },

            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "TEXINDEX", 0, DXGI_FORMAT_R32_UINT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        };

        Renderer->FastCuboidPipeline = DX12GraphicsPipelineCreate(Device, Renderer->RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/FastCuboid.hlsl");

        for (u32 i = 0; i < FIF; i++)
        {
            Renderer->FastCuboidTransformVertexBuffers[i] = DX12VertexBufferCreate(Device, sizeof(fast_cuboid_transform_vertex_data) * c_MaxCubePerBatch);
        }
        // TODO: MEMORY POOLS
        Renderer->FastCuboidInstanceData = VmAllocArray(fast_cuboid_transform_vertex_data, c_MaxCubePerBatch);

        Renderer->FastCuboidVertexBufferPositions = DX12VertexBufferCreate(Device, sizeof(c_CuboidVerticesPositions));

        DX12SubmitToQueueImmidiate(Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, [Renderer](ID3D12GraphicsCommandList* CommandList)
        {
            DX12VertexBufferSendData(&Renderer->FastCuboidVertexBufferPositions, CommandList, c_CuboidVerticesPositions, sizeof(c_CuboidVerticesPositions));
        });
    }

    // Create white texture
    {
        u32 WhiteColor = 0xffffffff;
        buffer Buffer = { &WhiteColor, sizeof(u32) };
        Renderer->WhiteTexture = TextureCreate(Renderer->Device, Renderer->DirectCommandAllocators[0], Renderer->DirectCommandList, Renderer->DirectCommandQueue, Buffer, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM);

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
        Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        Desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        Desc.Texture2D.MipLevels = 1;
        Desc.Texture2D.MostDetailedMip = 0;
        Desc.Texture2D.PlaneSlice = 0;
        Desc.Texture2D.ResourceMinLODClamp = 0.0f;
        Device->CreateShaderResourceView(Renderer->WhiteTexture.Handle, &Desc, SRV);

        SRV.ptr += Increment;
    }

    // Create light environment
    {
        Renderer->LightEnvironmentConstantBuffer = DX12ConstantBufferCreate(Device, sizeof(light_environment));
       /* directional_light Light;
        Light.Direction = float3(1.0, -1.0, 1.0);
        Light.Intensity = 0.5;
        Light.Radiance = float3(1.0, 1.0, 1.0);*/
        point_light& Light = Renderer->LightEnvironment.EmplacePointLight();
        Light.Position = v3(10, 20, 10);
        Light.Radius = 10.0;
        Light.FallOff = 1.0;
        Light.Radiance = v3(1.0, 1.0, 1.0);
        Light.Intensity = 1.0f;

        DX12ConstantBufferSetData(&Renderer->LightEnvironmentConstantBuffer, &Renderer->LightEnvironment, sizeof(light_environment));
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

internal void GameRendererRender(game_renderer* Renderer, u32 Width, u32 Height)
{
    // Get current frame stuff
    auto CommandList = Renderer->DirectCommandList;
    auto CurrentBackBufferIndex = Renderer->CurrentBackBufferIndex;

    auto DirectCommandAllocator = Renderer->DirectCommandAllocators[CurrentBackBufferIndex];
    auto BackBuffer = Renderer->BackBuffers[CurrentBackBufferIndex];
    auto RTV = Renderer->RTVHandles[CurrentBackBufferIndex];
    auto DSV = Renderer->DSVHandles[CurrentBackBufferIndex];
    auto& FrameFenceValue = Renderer->FrameFenceValues[CurrentBackBufferIndex];

    // Reset state
    DirectCommandAllocator->Reset();

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

    CommandList->Reset(DirectCommandAllocator, Renderer->QuadPipeline.Handle);

    // Frame that was presented needs to be set to render target again
    auto Barrier = DX12Transition(BackBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    CommandList->ResourceBarrier(1, &Barrier);

    // Copy vertex data to gpu buffer
    {
        DX12VertexBufferSendData(&Renderer->QuadVertexBuffers[CurrentBackBufferIndex], Renderer->DirectCommandList, Renderer->QuadVertexDataBase, sizeof(quad_vertex) * Renderer->QuadIndexCount);

        DX12VertexBufferSendData(&Renderer->CuboidTransformVertexBuffers[CurrentBackBufferIndex], Renderer->DirectCommandList, Renderer->CuboidInstanceData, Renderer->CuboidInstanceCount * sizeof(cuboid_transform_vertex_data));

        DX12VertexBufferSendData(&Renderer->FastCuboidTransformVertexBuffers[CurrentBackBufferIndex], Renderer->DirectCommandList, Renderer->FastCuboidInstanceData, Renderer->FastCuboidInstanceCount * sizeof(fast_cuboid_transform_vertex_data));
    }

    // Set and clear render target view
    v4 ClearColor = { 0.2f, 0.3f, 0.8f, 1.0f };
    CommandList->ClearRenderTargetView(RTV, &ClearColor.x, 0, nullptr);
    CommandList->ClearDepthStencilView(DSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    CommandList->OMSetRenderTargets(1, &RTV, false, &DSV);

    D3D12_VIEWPORT Viewport;
    Viewport.TopLeftX = 0;
    Viewport.TopLeftY = 0;
    Viewport.Width = (FLOAT)Width;
    Viewport.Height = (FLOAT)Height;
    Viewport.MinDepth = D3D12_MIN_DEPTH;
    Viewport.MaxDepth = D3D12_MAX_DEPTH;
    CommandList->RSSetViewports(1, &Viewport);

    D3D12_RECT ScissorRect;
    ScissorRect.left = 0;
    ScissorRect.top = 0;
    ScissorRect.right = Width;
    ScissorRect.bottom = Height;
    CommandList->RSSetScissorRects(1, &ScissorRect);

    // Set root constants
    // Number of 32 bit values - 16 floats in 4x4 matrix
    CommandList->SetGraphicsRootSignature(Renderer->RootSignature);
    CommandList->SetDescriptorHeaps(1, (ID3D12DescriptorHeap* const*)&Renderer->SRVDescriptorHeap);
    CommandList->SetGraphicsRootDescriptorTable(2, Renderer->SRVDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
    CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Render instanced cuboids
    if (Renderer->CuboidInstanceCount > 0)
    {
        CommandList->SetPipelineState(Renderer->CuboidPipeline.Handle);

        CommandList->SetGraphicsRoot32BitConstants(0, sizeof(Renderer->CuboidRootSignatureBuffer) / 4, &Renderer->CuboidRootSignatureBuffer, 0);

        CommandList->SetGraphicsRootConstantBufferView(1, Renderer->LightEnvironmentConstantBuffer.Buffer.Handle->GetGPUVirtualAddress());

        // Bind vertex positions
        local_persist D3D12_VERTEX_BUFFER_VIEW CuboidVertexBufferView;
        CuboidVertexBufferView.BufferLocation = Renderer->CuboidPositionsVertexBuffer.Buffer.Handle->GetGPUVirtualAddress();
        CuboidVertexBufferView.SizeInBytes = (u32)Renderer->CuboidPositionsVertexBuffer.Buffer.Size;
        CuboidVertexBufferView.StrideInBytes = sizeof(basic_cuboid_vertex);

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

    // Render fast instanced cuboids
    if (Renderer->FastCuboidInstanceCount > 0)
    {
        CommandList->SetPipelineState(Renderer->FastCuboidPipeline.Handle);

        CommandList->SetGraphicsRoot32BitConstants(0, 16, &Renderer->CuboidRootSignatureBuffer, 0);

        // Bind vertex positions
        local_persist D3D12_VERTEX_BUFFER_VIEW FastCuboidVertexBufferView;
        FastCuboidVertexBufferView.BufferLocation = Renderer->FastCuboidVertexBufferPositions.Buffer.Handle->GetGPUVirtualAddress();
        FastCuboidVertexBufferView.SizeInBytes = (u32)Renderer->FastCuboidVertexBufferPositions.Buffer.Size;
        FastCuboidVertexBufferView.StrideInBytes = sizeof(v4);

        // Bind transforms
        local_persist D3D12_VERTEX_BUFFER_VIEW TransformVertexBufferView;
        TransformVertexBufferView.BufferLocation = Renderer->FastCuboidTransformVertexBuffers[CurrentBackBufferIndex].Buffer.Handle->GetGPUVirtualAddress();
        TransformVertexBufferView.SizeInBytes = Renderer->FastCuboidInstanceCount * sizeof(fast_cuboid_transform_vertex_data);
        TransformVertexBufferView.StrideInBytes = sizeof(fast_cuboid_transform_vertex_data);

        D3D12_VERTEX_BUFFER_VIEW VertexBufferViews[] = { FastCuboidVertexBufferView, TransformVertexBufferView };
        CommandList->IASetVertexBuffers(0, 2, VertexBufferViews);

        // Bind index buffer
        local_persist D3D12_INDEX_BUFFER_VIEW IndexBufferView;
        IndexBufferView.BufferLocation = Renderer->QuadIndexBuffer.Buffer.Handle->GetGPUVirtualAddress();
        IndexBufferView.SizeInBytes = 36 * sizeof(u32);
        IndexBufferView.Format = DXGI_FORMAT_R32_UINT;
        CommandList->IASetIndexBuffer(&IndexBufferView);

        // Issue draw call
        CommandList->DrawIndexedInstanced(36, Renderer->FastCuboidInstanceCount, 0, 0, 0);
    }

    // Render quads
    if (1)
    {
        if (Renderer->QuadIndexCount > 0)
        {
            CommandList->SetPipelineState(Renderer->QuadPipeline.Handle);

            // Do not share depth
            CommandList->ClearDepthStencilView(DSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

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

            CommandList->SetGraphicsRoot32BitConstants(0, 16, &Renderer->QuadRootSignatureBuffer.ViewProjection, 0);

            // Issue draw call
            CommandList->DrawIndexedInstanced(Renderer->QuadIndexCount, 1, 0, 0, 0);
        }
    }

    // Present transition
    {
        // Rendered frame needs to be transitioned to present state
        auto Barrier = DX12Transition(BackBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        CommandList->ResourceBarrier(1, &Barrier);
    }

    // Finalize the command list
    CommandList->Close();

    Renderer->DirectCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&CommandList);

    Renderer->SwapChain->Present(Renderer->VSync, 0);

    // Wait for GPU to finish presenting
    FrameFenceValue = GameRendererSignal(Renderer->DirectCommandQueue, Renderer->Fence, &Renderer->FenceValue);
    GameRendererWaitForFenceValue(Renderer->Fence, FrameFenceValue, Renderer->DirectFenceEvent);

    // Reset indices
    Renderer->QuadIndexCount = 0;
    Renderer->QuadVertexDataPtr = Renderer->QuadVertexDataBase;

    // Reset Cuboid indices
    Renderer->CuboidInstanceCount = 0;
    Renderer->FastCuboidInstanceCount = 0;

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
internal void GameRendererSetViewProjectionQuad(game_renderer* Renderer, const m4& ViewProjection)
{
    Renderer->QuadRootSignatureBuffer.ViewProjection = ViewProjection;
}

internal void GameRendererSetViewProjectionCuboid(game_renderer* Renderer, const m4& ViewProjection, const m4& InverseView)
{
    Renderer->CuboidRootSignatureBuffer.ViewProjection = ViewProjection;
    Renderer->CuboidRootSignatureBuffer.InverseView = InverseView;
}

internal void GameRendererSubmitQuad(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v2& Scale, const v4& Color)
{
    Assert(Renderer->QuadIndexCount < c_MaxQuadIndices, "DrawLayer.IndexCount < c_MaxQuadIndices");

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
        Renderer->QuadVertexDataPtr->Position = v3(Transform * c_QuadVertexPositions[i]);
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
        Renderer->QuadVertexDataPtr->Position = v3(Transform * c_QuadVertexPositions[i]);
        Renderer->QuadVertexDataPtr->Color = Color;
        Renderer->QuadVertexDataPtr->TexCoord = Coords[i];
        Renderer->QuadVertexDataPtr->TexIndex = TextureIndex;
        Renderer->QuadVertexDataPtr++;
    }

    Renderer->QuadIndexCount += 6;
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
        Renderer->QuadVertexDataPtr->Position = VertexPositions[i];
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

internal void GameRendererSubmitCuboidNoRotScale(game_renderer* Renderer, const v3& Translation, const texture& Texture, const v4& Color)
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

internal void GameRendererSubmitCuboidNoRotScale(game_renderer* Renderer, const v3& Translation, const v4& Color)
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


internal void GameRendererSubmitCustomCuboid(game_renderer* Renderer, const m4& Transform, const texture& Texture, texture_coords TextureCoords[6], const v4& Color)
{
    Assert(Renderer->FastCuboidInstanceCount < c_MaxCubePerBatch, "Renderer->FastCuboidInstanceCount < c_MaxCuboidsPerBatch");

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

    auto& Cuboid = Renderer->FastCuboidInstanceData[Renderer->FastCuboidInstanceCount];
    Cuboid.Color = Color;
    Cuboid.Transform = Transform;
    Cuboid.TextureIndex = TextureIndex;

    // Copy texture coords
    for (i32 i = 0; i < 6; i++)
    {
        Cuboid.TextureCoords.TextureCoords[i] = TextureCoords[i];
    }

    Renderer->FastCuboidInstanceCount++;
}

internal void GameRendererSubmitCustomCuboid(game_renderer* Renderer, const v3& Translation, const v3& Rotation, const v3& Scale, const texture& Texture, texture_coords TextureCoords[6], const v4& Color)
{
    Assert(Renderer->FastCuboidInstanceCount < c_MaxCubePerBatch, "Renderer->FastCuboidInstanceCount < c_MaxCuboidsPerBatch");
    auto& Cuboid = Renderer->FastCuboidInstanceData[Renderer->FastCuboidInstanceCount];

    // TODO: Better lookup?
    // Hashtables are overkill, we can have a table indexed with enums and simply say that:
    //  - Index 0 is a white texture
    //  - Index 1 is a sprite-sheet texture for blocks
    //  - Index 2 is a sprite-sheet texture for alive entities
    // Something like that so we can avoid this 
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

    Cuboid.Color = Color;
    Cuboid.TextureIndex = TextureIndex;

    // Copy texture coords
    // TODO: Remove this
    for (i32 i = 0; i < 6; i++)
    {
        Cuboid.TextureCoords.TextureCoords[i] = TextureCoords[i];
    }

#if ENABLE_SIMD
    XMMATRIX XmmScale = XMMatrixScalingFromVector(XMVectorSet(Scale.x, Scale.y, Scale.z, 0.0f));
    XMMATRIX XmmRot = XMMatrixRotationQuaternion(XMQuaternionRotationRollPitchYaw(Rotation.x, Rotation.y, Rotation.z));
    XMMATRIX XmmTrans = XMMatrixTranslationFromVector(XMVectorSet(Translation.x, Translation.y, Translation.z, 0.0f));

    XMMATRIX XmmTransform = XmmScale * XmmRot * XmmTrans;
#else
    Cuboid.Transform =
        bkm::Translate(m4(1.0f), Translation)
        * bkm::ToM4(qtn(Rotation))
        * bkm::Scale(m4(1.0f), Scale);
#endif
    Cuboid.XmmTransform = /*XMMatrixTranspose*/(XmmTransform); // Avoid doing it on the GPU multiple times

    Renderer->FastCuboidInstanceCount++;
}
