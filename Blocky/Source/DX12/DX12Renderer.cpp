#include "DX12Renderer.h"

#include "DX12Texture.cpp"
#include "DX12Buffer.cpp"

internal d3d12_render_backend* d3d12_render_backend_create(arena* Arena, const win32_window& Window)
{
    d3d12_render_backend* Backend = arena_new(Arena, d3d12_render_backend);

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
        DxAssert(Backend->Device->QueryInterface(IID_PPV_ARGS(&g_DebugInfoQueue)));
        g_DebugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, false);
        g_DebugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, false);
        g_DebugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, false);
        g_DebugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_INFO, false);
        //InfoQueue->Release();
        //Backend->DebugInterface->Release();

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
        HRESULT Result = (Backend->Factory->CreateSwapChainForHwnd(Backend->DirectCommandQueue, Window.Handle, &Desc, &FullScreenDesc, nullptr, &SwapChain1));

        DumpInfoQueue();
        // Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
        // will be handled manually.
        DxAssert(Backend->Factory->MakeWindowAssociation(Window.Handle, DXGI_MWA_NO_ALT_ENTER));
        SwapChain1->QueryInterface(IID_PPV_ARGS(&Backend->SwapChain));
        SwapChain1->Release();

        //Backend->SwapChain->SetMaximumFrameLatency(FIF);

        Backend->CurrentBackBufferIndex = Backend->SwapChain->GetCurrentBackBufferIndex();
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
        DxAssert(Backend->Device->CreateDescriptorHeap(&Desc, IID_PPV_ARGS(&Backend->RTVDescriptorHeap)));

        // Place rtv descriptor sequentially in memory
        u32 RTVDescriptorSize = Backend->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle = Backend->RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        for (u32 i = 0; i < FIF; ++i)
        {
            DxAssert(Backend->SwapChain->GetBuffer(i, IID_PPV_ARGS(&Backend->BackBuffers[i])));
            Backend->BackBuffers[i]->SetName(L"SwapchainRenderTargetTexture");
            Backend->Device->CreateRenderTargetView(Backend->BackBuffers[i], nullptr, RtvHandle);

            Backend->RTVHandles[i] = RtvHandle;
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
        DxAssert(Backend->Device->CreateDescriptorHeap(&DsvHeapDesc, IID_PPV_ARGS(&Backend->DSVDescriptorHeap)));

        D3D12_CPU_DESCRIPTOR_HANDLE DsvHandle = Backend->DSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        u32 DSVDescriptorSize = Backend->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

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

            DxAssert(Backend->Device->CreateCommittedResource(
                &HeapProperties,
                D3D12_HEAP_FLAG_NONE,
                &DepthStencilDesc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE,
                &OptimizedClearValue,
                IID_PPV_ARGS(&Backend->DepthBuffers[i])
            ));

            // Create depth-stencil view
            {
                D3D12_DEPTH_STENCIL_VIEW_DESC Desc = {};
                Desc.Format = DXGI_FORMAT_D32_FLOAT;
                Desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
                Desc.Texture2D.MipSlice = 0;
                Desc.Flags = D3D12_DSV_FLAG_NONE;

                Backend->Device->CreateDepthStencilView(Backend->DepthBuffers[i], &Desc, DsvHandle);

                // Increment by the size of the dsv descriptor size
                Backend->DSVHandles[i] = DsvHandle;
                DsvHandle.ptr += DSVDescriptorSize;
            }
        }
    }

    d3d12_render_backend_initialize_pipeline(Arena, Backend);

    return Backend;
}

internal void d3d12_render_backend_destroy(d3d12_render_backend* Backend)
{
    // Wait for GPU to finish
    d3d12_render_backend_flush(Backend);

    for (u32 i = 0; i < FIF; i++)
    {
        // Command allocators
        Backend->DirectCommandAllocators[i]->Release();

        // Depth testing
        Backend->DepthBuffers[i]->Release();
        Backend->BackBuffers[i]->Release();

        DX12VertexBufferDestroy(&Backend->Quad.VertexBuffers[i]);
    }

    DX12PipelineDestroy(&Backend->Quad.Pipeline);
    DX12IndexBufferDestroy(&Backend->Quad.IndexBuffer);
    texture_destroy(&Backend->WhiteTexture);
    Backend->SwapChain->Release();
    Backend->DSVDescriptorHeap->Release();
    Backend->RTVDescriptorHeap->Release();
    Backend->SRVDescriptorHeap->Release();
    Backend->Fence->Release();
    Backend->DirectCommandList->Release();
    Backend->DirectCommandQueue->Release();
    Backend->RootSignature->Release();
    Backend->Factory->Release();
    Backend->Device->Release();

#if DX12_ENABLE_DEBUG_LAYER
    // Report all memory leaks
    Backend->DxgiDebugInterface->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_ALL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
    DX12DumpInfoQueue(g_DebugInfoQueue);
#endif

    // Zero everything to make sure nothing can reference this
    memset(Backend, 0, sizeof(d3d12_render_backend));
}

internal void d3d12_render_backend_initialize_pipeline(arena* Arena, d3d12_render_backend* Backend)
{
    auto Device = Backend->Device;

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

        // Root signature
        ID3DBlob* Error;
        ID3DBlob* Signature;
        DxAssert(D3D12SerializeRootSignature(&Desc, D3D_ROOT_SIGNATURE_VERSION_1, &Signature, &Error));
        DxAssert(Device->CreateRootSignature(0, Signature->GetBufferPointer(), Signature->GetBufferSize(), IID_PPV_ARGS(&Backend->RootSignature)));
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

        Backend->Quad.Pipeline = DX12GraphicsPipelineCreate(Device, Backend->RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/Quad.hlsl", D3D12_CULL_MODE_NONE);

        for (u32 i = 0; i < FIF; i++)
        {
            Backend->Quad.VertexBuffers[i] = DX12VertexBufferCreate(Device, sizeof(quad_vertex) * c_MaxQuadVertices);
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
            Backend->Quad.IndexBuffer = DX12IndexBufferCreate(Device, Backend->DirectCommandAllocators[0], Backend->DirectCommandList, Backend->DirectCommandQueue, QuadIndices, MaxQuadIndexCount);
        }
    }

    // Basic Cuboids - if a cuboid does not have unique texture for each quad, then we can use this
    // TODO: We could even create a pipeline for NoRotNoScale cuboids and pass only translation vector
    {
        auto& Cuboid = Backend->Cuboid;

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

        Cuboid.Pipeline = DX12GraphicsPipelineCreate(Device, Backend->Cuboid.RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/Cuboid.hlsl", D3D12_CULL_MODE_BACK);

        Cuboid.PositionsVertexBuffer = DX12VertexBufferCreate(Device, sizeof(c_CuboidVertices));

        DX12SubmitToQueueImmidiate(Device, Backend->DirectCommandAllocators[0], Backend->DirectCommandList, Backend->DirectCommandQueue, [Backend, &Cuboid](ID3D12GraphicsCommandList* CommandList)
        {
            DX12VertexBufferSendData(&Cuboid.PositionsVertexBuffer, CommandList, c_CuboidVertices, sizeof(c_CuboidVertices));
        });

        for (u32 i = 0; i < FIF; i++)
        {
            Cuboid.TransformVertexBuffers[i] = DX12VertexBufferCreate(Device, sizeof(cuboid_transform_vertex_data) * c_MaxCubePerBatch);
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

        Backend->QuadedCuboid.Pipeline = DX12GraphicsPipelineCreate(Device, Backend->RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/QuadedCuboid.hlsl");

        for (u32 i = 0; i < FIF; i++)
        {
            Backend->QuadedCuboid.VertexBuffers[i] = DX12VertexBufferCreate(Device, sizeof(quaded_cuboid_vertex) * c_MaxQuadedCuboidVertices);
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

        Backend->HUD.Pipeline = DX12GraphicsPipelineCreate(Device, Backend->RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/HUD.hlsl");

        for (u32 i = 0; i < FIF; i++)
        {
            Backend->HUD.VertexBuffers[i] = DX12VertexBufferCreate(Device, sizeof(hud_quad_vertex) * c_MaxHUDQuadVertices);
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

            // Root signature
            ID3DBlob* Error;
            ID3DBlob* Signature;
            DxAssert(D3D12SerializeRootSignature(&Desc, D3D_ROOT_SIGNATURE_VERSION_1, &Signature, &Error));
            DxAssert(Device->CreateRootSignature(0, Signature->GetBufferPointer(), Signature->GetBufferSize(), IID_PPV_ARGS(&Backend->Skybox.RootSignature)));
        }

        D3D12_INPUT_ELEMENT_DESC InputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        Backend->Skybox.Pipeline = DX12GraphicsPipelineCreate(Device, Backend->Skybox.RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/Skybox.hlsl", D3D12_CULL_MODE_BACK, false);
        Backend->Skybox.VertexBuffer = DX12VertexBufferCreate(Device, sizeof(c_SkyboxVertices));

        DX12SubmitToQueueImmidiate(Device, Backend->DirectCommandAllocators[0], Backend->DirectCommandList, Backend->DirectCommandQueue, [Backend](ID3D12GraphicsCommandList* CommandList)
        {
            DX12VertexBufferSendData(&Backend->Skybox.VertexBuffer, CommandList, c_SkyboxVertices, sizeof(c_SkyboxVertices));
        });
        Backend->Skybox.IndexBuffer = DX12IndexBufferCreate(Device, Backend->DirectCommandAllocators[0], Backend->DirectCommandList, Backend->DirectCommandQueue, c_SkyboxIndices, CountOf(c_SkyboxIndices));
    }

    // Distant quads
    {
        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC InputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        Backend->DistantQuad.Pipeline = DX12GraphicsPipelineCreate(Device, Backend->Skybox.RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/DistantQuad.hlsl", D3D12_CULL_MODE_NONE, false);

        for (u32 i = 0; i < FIF; i++)
        {
            Backend->DistantQuad.VertexBuffers[i] = DX12VertexBufferCreate(Device, sizeof(distant_quad_vertex) * c_MaxHUDQuadVertices);
        }
    }

    // Create white texture
    {
        u32 WhiteColor = 0xffffffff;
        buffer Buffer = { &WhiteColor, sizeof(u32) };
        Backend->WhiteTexture = texture_create(Backend, 1, 1, Buffer);
    }

    // Create descriptor heap that holds texture and uav descriptors
    {
        D3D12_DESCRIPTOR_HEAP_DESC Desc = {};
        Desc.NumDescriptors = c_MaxTexturesPerDrawCall; // + 1 for light environment
        Desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        Desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; // Must be visible to shaders
        Desc.NodeMask = 0;
        DxAssert(Backend->Device->CreateDescriptorHeap(&Desc, IID_PPV_ARGS(&Backend->SRVDescriptorHeap)));
    }

    // Describe and create a SRV for the white texture.
    auto SRV = Backend->SRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
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
        Device->CreateShaderResourceView(Backend->WhiteTexture.Handle, &Desc, SRV);

        SRV.ptr += Increment;
    }

    // Create light environment constant buffer for each frame
    for (u32 i = 0; i < FIF; i++)
    {
        Backend->LightEnvironmentConstantBuffers[i] = DX12ConstantBufferCreate(Device, sizeof(light_environment));
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

            // Root signature
            ID3DBlob* Error;
            ID3DBlob* Signature;
            DxAssert(D3D12SerializeRootSignature(&Desc, D3D_ROOT_SIGNATURE_VERSION_1, &Signature, &Error));
            DxAssert(Device->CreateRootSignature(0, Signature->GetBufferPointer(), Signature->GetBufferSize(), IID_PPV_ARGS(&Backend->ShadowPass.RootSignature)));
        }
        Backend->ShadowPass.Pipeline = DX12GraphicsPipelineCreate(Device, Backend->ShadowPass.RootSignature, InputElementDescs, CountOf(InputElementDescs), L"Resources/Shadow.hlsl", D3D12_CULL_MODE_FRONT, true, 0);

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
            DxAssert(Backend->Device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&Backend->ShadowPass.DSVDescriptorHeap)));

            // SRV
            HeapDesc = {};
            HeapDesc.NumDescriptors = FIF;
            HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            DxAssert(Backend->Device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&Backend->ShadowPass.SRVDescriptorHeap)));
        }

        D3D12_CPU_DESCRIPTOR_HANDLE DsvHandle = Backend->ShadowPass.DSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        u32 DSVDescriptorSize = Backend->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

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

            DxAssert(Backend->Device->CreateCommittedResource(
                &HeapProperties,
                D3D12_HEAP_FLAG_NONE,
                &DepthStencilDesc,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                &OptimizedClearValue,
                IID_PPV_ARGS(&Backend->ShadowPass.ShadowMaps[i])
            ));

            const wchar_t* DebugNames[]{
                L"ShadowMap0",
                L"ShadowMap1"
            };

            Backend->ShadowPass.ShadowMaps[i]->SetName(DebugNames[i]);

            // Update the depth-stencil view.
            D3D12_DEPTH_STENCIL_VIEW_DESC DSV = {};
            DSV.Format = DXGI_FORMAT_D32_FLOAT;
            DSV.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            DSV.Texture2D.MipSlice = 0;
            DSV.Flags = D3D12_DSV_FLAG_NONE;

            Backend->Device->CreateDepthStencilView(Backend->ShadowPass.ShadowMaps[i], &DSV, DsvHandle);

            // Increment by the size of the dsv descriptor size
            Backend->ShadowPass.DSVHandles[i] = DsvHandle;
            DsvHandle.ptr += DSVDescriptorSize;
        }

        // Shadow map
        for (u32 i = 0; i < FIF; i++)
        {
            auto SRVDescriptor = Backend->ShadowPass.SRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
            auto DescriptorSize = Backend->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC Desc = {};
                Desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                Desc.Format = DXGI_FORMAT_R32_FLOAT;
                Desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                Desc.Texture2D.MipLevels = 1;
                Desc.Texture2D.MostDetailedMip = 0;
                Desc.Texture2D.PlaneSlice = 0;
                Desc.Texture2D.ResourceMinLODClamp = 0.0f;
                Backend->Device->CreateShaderResourceView(Backend->ShadowPass.ShadowMaps[i], &Desc, SRVDescriptor);

                Backend->ShadowPass.SRVHandles[i] = SRVDescriptor;
                SRVDescriptor.ptr += DescriptorSize;
            }
        }
    }
#endif
}

internal void d3d12_render_backend_render(d3d12_render_backend* Backend, const game_renderer* Renderer)
{
    // Get current frame stuff
    auto CommandList = Backend->DirectCommandList;
    auto CurrentBackBufferIndex = Backend->CurrentBackBufferIndex;

    auto DirectCommandAllocator = Backend->DirectCommandAllocators[CurrentBackBufferIndex];
    auto BackBuffer = Backend->BackBuffers[CurrentBackBufferIndex];
    auto RTV = Backend->RTVHandles[CurrentBackBufferIndex];
    auto DSV = Backend->DSVHandles[CurrentBackBufferIndex];
    auto& FrameFenceValue = Backend->FrameFenceValues[CurrentBackBufferIndex];

    // TODO: Figure out if this function needs to be called every frame.
    DXGI_SWAP_CHAIN_DESC SwapChainDesc;
    DxAssert(Backend->SwapChain->GetDesc(&SwapChainDesc));

    // Reset state
    DxAssert(DirectCommandAllocator->Reset());

    // Copy texture's descriptor to our renderer's descriptor
    if (true)
    {
        //debug_cycle_counter C("Copy");
        // We cannot use CopyDescriptorsSimple to copy everything at once because it assumes that source descriptors are ordered... and TextureStack's ptrs are not.
        // For now we keep both even though the newer variant is slower. In Release they seem to be equal. But with not many textures its unfair to judge the newer variant.
        // TODO: Come back to this and stress test

        // First element of the texture stack will be the white texture
        //Renderer->TextureStack[0] = Backend->WhiteTexture.SRVDescriptor;
#if 1
    auto DstSRV = Backend->SRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        auto DescriptorSize = Backend->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        DstSRV.ptr += DescriptorSize;
        for (u32 i = 1; i < Renderer->CurrentTextureStackIndex; i++)
        {
            Backend->Device->CopyDescriptorsSimple(1, DstSRV, Renderer->TextureStack[i]->SRVDescriptor, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            DstSRV.ptr += DescriptorSize;
        }
#else // Newer
        u32 NumDescriptors = Backend->CurrentTextureStackIndex - 1;
        if (NumDescriptors > 0)
        {
            auto DstSRV = Backend->SRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
            auto DescriptorSize = Backend->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            // Skip white texture
            DstSRV.ptr += DescriptorSize;

            Backend->Device->CopyDescriptors(1, &DstSRV, &NumDescriptors, NumDescriptors, &Backend->TextureStack[1], nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            );
        }
#endif
    }

    DxAssert(CommandList->Reset(DirectCommandAllocator, Backend->Cuboid.Pipeline.Handle));

    // Set light environment data
    {
        DX12ConstantBufferSetData(&Backend->LightEnvironmentConstantBuffers[CurrentBackBufferIndex], &Renderer->LightEnvironment, sizeof(light_environment));
    }

    // Copy vertex data to gpu buffer
    {
        // Quads - general purpose
        DX12VertexBufferSendData(&Backend->Quad.VertexBuffers[CurrentBackBufferIndex], Backend->DirectCommandList, Renderer->Quad.VertexDataBase, sizeof(quad_vertex) * Renderer->Quad.IndexCount);

        // Cuboid made out of quads - alive entities
        DX12VertexBufferSendData(&Backend->QuadedCuboid.VertexBuffers[CurrentBackBufferIndex], Backend->DirectCommandList, Renderer->QuadedCuboid.VertexDataBase, sizeof(quaded_cuboid_vertex) * Renderer->QuadedCuboid.IndexCount);

        // Cuboid - used for blocks
        DX12VertexBufferSendData(&Backend->Cuboid.TransformVertexBuffers[CurrentBackBufferIndex], Backend->DirectCommandList, Renderer->Cuboid.InstanceData, Renderer->Cuboid.InstanceCount * sizeof(cuboid_transform_vertex_data));

        // Distant quads - sun, moon, starts, etc
        DX12VertexBufferSendData(&Backend->DistantQuad.VertexBuffers[CurrentBackBufferIndex], Backend->DirectCommandList, Renderer->DistantQuad.VertexDataBase, Renderer->DistantQuad.IndexCount * sizeof(distant_quad_vertex));

        // HUD
        DX12VertexBufferSendData(&Backend->HUD.VertexBuffers[CurrentBackBufferIndex], Backend->DirectCommandList, Renderer->HUD.VertexDataBase, sizeof(hud_quad_vertex) * Renderer->HUD.IndexCount);
    }

#if ENABLE_SHADOW_PASS
    // Render shadow maps
    {
        auto& ShadowPass = Backend->ShadowPass;
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

            CommandList->SetGraphicsRoot32BitConstants(0, sizeof(Renderer->RenderData.ShadowPassBuffer) / 4, &Renderer->RenderData.ShadowPassBuffer, 0);

            // Bind vertex positions and transforms
            DX12CmdSetVertexBuffers2(CommandList, 0, Backend->Cuboid.PositionsVertexBuffer.Buffer.Handle, Backend->Cuboid.PositionsVertexBuffer.Buffer.Size, sizeof(cuboid_vertex), Backend->Cuboid.TransformVertexBuffers[CurrentBackBufferIndex].Buffer.Handle, Renderer->Cuboid.InstanceCount * sizeof(cuboid_transform_vertex_data), sizeof(cuboid_transform_vertex_data));

            // Bind index buffer
            DX12CmdSetIndexBuffer(CommandList, Backend->Quad.IndexBuffer.Buffer.Handle, 36 * sizeof(u32), DXGI_FORMAT_R32_UINT);

            // Issue draw call
            CommandList->DrawIndexedInstanced(36, Renderer->Cuboid.InstanceCount, 0, 0, 0);
        }

        // From depth write to resource
        DX12CmdTransition(CommandList, ShadowMap, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
#endif

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

    // Render a skybox
    if (true)
    {
        auto& Skybox = Backend->Skybox;
        CommandList->SetGraphicsRootSignature(Skybox.RootSignature);
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
        CommandList->SetGraphicsRootSignature(Backend->Cuboid.RootSignature);
#if ENABLE_SHADOW_PASS
        //SRVPTR.ptr += CurrentBackBufferIndex * Renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
#endif
        CommandList->SetDescriptorHeaps(1, (ID3D12DescriptorHeap* const*)&Backend->SRVDescriptorHeap);
        auto GPUSRV = Backend->SRVDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
        CommandList->SetGraphicsRootDescriptorTable(2, GPUSRV);

        CommandList->SetPipelineState(Backend->Cuboid.Pipeline.Handle);

        CommandList->SetGraphicsRoot32BitConstants(0, sizeof(cuboid_buffer_data) / 4, &Renderer->RenderData.CuboidBuffer, 0);
        CommandList->SetGraphicsRootConstantBufferView(1, Backend->LightEnvironmentConstantBuffers[CurrentBackBufferIndex].Buffer.Handle->GetGPUVirtualAddress());

        DX12CmdSetVertexBuffers2(CommandList, 0, Backend->Cuboid.PositionsVertexBuffer.Buffer.Handle, Backend->Cuboid.PositionsVertexBuffer.Buffer.Size, sizeof(cuboid_vertex), Backend->Cuboid.TransformVertexBuffers[CurrentBackBufferIndex].Buffer.Handle, Renderer->Cuboid.InstanceCount * sizeof(cuboid_transform_vertex_data), sizeof(cuboid_transform_vertex_data));

        // Bind index buffer
        DX12CmdSetIndexBuffer(CommandList, Backend->Quad.IndexBuffer.Buffer.Handle, 36 * sizeof(u32), DXGI_FORMAT_R32_UINT);

        // Issue draw call
        CommandList->DrawIndexedInstanced(36, Renderer->Cuboid.InstanceCount, 0, 0, 0);
    }

    // Render distant objects
    if (Renderer->DistantQuad.IndexCount > 0 && true)
    {
        CommandList->SetGraphicsRootSignature(Backend->Skybox.RootSignature);
        CommandList->SetPipelineState(Backend->DistantQuad.Pipeline.Handle);
        CommandList->SetGraphicsRoot32BitConstants(0, sizeof(distant_quad_buffer_data) / 4, &Renderer->RenderData.DistantObjectBuffer, 0);

        // Bind vertex positions
        DX12CmdSetVertexBuffer(CommandList, 0, Backend->DistantQuad.VertexBuffers[CurrentBackBufferIndex].Buffer.Handle, Renderer->Quad.IndexCount * sizeof(distant_quad_vertex), sizeof(distant_quad_vertex));

        // Bind index buffer
        DX12CmdSetIndexBuffer(CommandList, Backend->Quad.IndexBuffer.Buffer.Handle, Renderer->DistantQuad.IndexCount * sizeof(u32), DXGI_FORMAT_R32_UINT);

        CommandList->DrawIndexedInstanced(Renderer->DistantQuad.IndexCount, 1, 0, 0, 0);
    }

    CommandList->SetGraphicsRootSignature(Backend->RootSignature);

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
    if (Renderer->Quad.IndexCount > 0 && true)
    {
        CommandList->SetPipelineState(Backend->Quad.Pipeline.Handle);

        // Bind vertex buffer
        DX12CmdSetVertexBuffer(CommandList, 0, Backend->Quad.VertexBuffers[CurrentBackBufferIndex].Buffer.Handle, Renderer->Quad.IndexCount * sizeof(quad_vertex), sizeof(quad_vertex));

        // Bind index buffer
        DX12CmdSetIndexBuffer(CommandList, Backend->Quad.IndexBuffer.Buffer.Handle, Renderer->Quad.IndexCount * sizeof(u32), DXGI_FORMAT_R32_UINT);

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

    // Rendered frame needs to be transitioned to present state
    DX12CmdTransition(CommandList, BackBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    // Finalize the command list
    DxAssert(CommandList->Close());

    Backend->DirectCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&CommandList);

    DxAssert(Backend->SwapChain->Present(Backend->VSync, 0));

    // Wait for GPU to finish presenting
    FrameFenceValue = d3d12_render_backend_signal(Backend->DirectCommandQueue, Backend->Fence, &Backend->FenceValue);
    d3d12_render_backend_wait_for_fence_value(Backend->Fence, FrameFenceValue, Backend->DirectFenceEvent);

    // Move to another back buffer
    Backend->CurrentBackBufferIndex = Backend->SwapChain->GetCurrentBackBufferIndex();

    // Log dx12 stuff
    DumpInfoQueue();
}

internal void d3d12_render_backend_resize_swapchain(d3d12_render_backend* Renderer, u32 RequestWidth, u32 RequestHeight)
{
    // Flush the GPU queue to make sure the swap chain's back buffers are not being referenced by an in-flight command list.
    d3d12_render_backend_flush(Renderer);

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

internal u64 d3d12_render_backend_signal(ID3D12CommandQueue* CommandQueue, ID3D12Fence* Fence, u64* FenceValue)
{
    u64& RefFenceValue = *FenceValue;
    u64 FenceValueForSignal = ++RefFenceValue;
    DxAssert(CommandQueue->Signal(Fence, FenceValueForSignal));
    return FenceValueForSignal;
}

internal void d3d12_render_backend_wait_for_fence_value(ID3D12Fence* Fence, u64 FenceValue, HANDLE FenceEvent, u32 Duration)
{
    if (Fence->GetCompletedValue() < FenceValue)
    {
        Fence->SetEventOnCompletion(FenceValue, FenceEvent);
        WaitForSingleObject(FenceEvent, Duration);
    }
}

internal void d3d12_render_backend_flush(d3d12_render_backend* Renderer)
{
    u64 FenceValueForSignal = d3d12_render_backend_signal(Renderer->DirectCommandQueue, Renderer->Fence, &Renderer->FenceValue);
    d3d12_render_backend_wait_for_fence_value(Renderer->Fence, FenceValueForSignal, Renderer->DirectFenceEvent);
}
