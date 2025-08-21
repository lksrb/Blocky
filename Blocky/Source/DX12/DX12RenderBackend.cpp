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
            DxAssert(Backend->Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, Backend->DirectCommandAllocators[0], nullptr, IID_PPV_ARGS(&Backend->DirectCommandList)));

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

    // Cache descriptor sizes, they should never change unless ID3D12 is recreated.
    Backend->DescriptorSizes.RTV = Backend->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    Backend->DescriptorSizes.DSV = Backend->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    Backend->DescriptorSizes.CBV_SRV_UAV = Backend->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // View allocators
    Backend->RTVAllocator.Create(Arena, Backend->Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, false, 1024);
    Backend->DSVAllocator.Create(Arena, Backend->Device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, false, 1024);
    Backend->SRVCBVUAV_Allocator.Create(Arena, Backend->Device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true, 1024);

    // CPU-Only, for dynamic textures
    Backend->SRVCBVUAV_CPU_ONLY_Allocator.Create(Arena, Backend->Device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, false, 1024);

    // Create Render Target Views
    {
        for (u32 i = 0; i < FIF; ++i)
        {
            DxAssert(Backend->SwapChain->GetBuffer(i, IID_PPV_ARGS(&Backend->SwapChainBackbuffers[i])));
            Backend->SwapChainBackbuffers[i]->SetName(L"SwapchainRenderTargetTexture");
            Backend->SwapChainBufferRTVViews[i] = Backend->RTVAllocator.Alloc();
            Backend->Device->CreateRenderTargetView(Backend->SwapChainBackbuffers[i], nullptr, Backend->SwapChainBufferRTVViews[i].CPU);
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

    // Shadow Pass
#if ENABLE_SHADOW_PASS
    dx12_pipeline_destroy(&Backend->ShadowPass.Pipeline);
    dx12_root_signature_destroy(&Backend->ShadowPass.RootSignature);
#endif

    // Bloom Pass
    dx12_pipeline_destroy(&Backend->BloomPass.PipelineCompute);
    dx12_root_signature_destroy(&Backend->BloomPass.RootSignature);

    // Composite Pass
    dx12_pipeline_destroy(&Backend->CompositePass.Pipeline);
    dx12_root_signature_destroy(&Backend->CompositePass.RootSignature);

    // HUD
    dx12_pipeline_destroy(&Backend->HUD.Pipeline);

    // Main pass
    texture_destroy(Backend, &Backend->WhiteTexture);

    dx12_root_signature_destroy(&Backend->RootSignature);

    // Views
    Backend->SRVCBVUAV_Allocator.Destroy();
    Backend->SRVCBVUAV_CPU_ONLY_Allocator.Destroy();
    Backend->RTVAllocator.Destroy();
    Backend->DSVAllocator.Destroy();

    // Swapchain
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

    // Main pass
    {
        auto& MainPass = Backend->MainPass;

        // Create render resources
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
            Backend->Device->CreateShaderResourceView(MainPass.RenderBuffers[i], nullptr, MainPass.RenderBuffersSRVViews[i].CPU);
        }

        // Create depth resources
        for (u32 i = 0; i < FIF; i++)
        {
            MainPass.DepthBuffers[i] = dx12_depth_buffer_create(Device, SwapChainDesc.BufferDesc.Width, SwapChainDesc.BufferDesc.Height, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D32_FLOAT, L"MainPassDepthBuffer");

            MainPass.DepthBuffersViews[i] = Backend->DSVAllocator.Alloc();
            Backend->Device->CreateDepthStencilView(MainPass.DepthBuffers[i], nullptr, MainPass.DepthBuffersViews[i].CPU);
        }
    }

    // Bloom Pass
    {
        auto& BloomPass = Backend->BloomPass;

        // Descriptor range for one UAV (u0)
        D3D12_DESCRIPTOR_RANGE Ranges[3] = {};
        Ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        Ranges[0].NumDescriptors = 1;
        Ranges[0].BaseShaderRegister = 0;
        Ranges[0].RegisterSpace = 0;
        Ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        // Descriptor range for first RV (t0)
        Ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        Ranges[1].NumDescriptors = 1; // u_Texture
        Ranges[1].BaseShaderRegister = 0;
        Ranges[1].RegisterSpace = 0;
        Ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        Ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        Ranges[2].NumDescriptors = 1; // u_BloomTexture
        Ranges[2].BaseShaderRegister = 1;
        Ranges[2].RegisterSpace = 0;
        Ranges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        // One sampler (s0)
        D3D12_STATIC_SAMPLER_DESC Samplers[1] = {};

        // Sampler
        //Samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        Samplers[0].Filter = D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR; // TODO: Check which one
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
        Samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_PARAMETER Parameters[4] = {};
        Parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        Parameters[0].Constants.Num32BitValues = sizeof(bloom_pass_buffer_data) / 4;
        Parameters[0].Constants.ShaderRegister = 0;  // b0
        Parameters[0].Constants.RegisterSpace = 0;
        Parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        Parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        Parameters[1].DescriptorTable.NumDescriptorRanges = 1;
        Parameters[1].DescriptorTable.pDescriptorRanges = &Ranges[0];
        Parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        Parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        Parameters[2].DescriptorTable.NumDescriptorRanges = 1;
        Parameters[2].DescriptorTable.pDescriptorRanges = &Ranges[1];
        Parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        Parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        Parameters[3].DescriptorTable.NumDescriptorRanges = 1;
        Parameters[3].DescriptorTable.pDescriptorRanges = &Ranges[2];
        Parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // Root signature description
        D3D12_ROOT_SIGNATURE_DESC RootSigDesc = {};
        RootSigDesc.NumParameters = CountOf(Parameters);
        RootSigDesc.pParameters = Parameters;
        RootSigDesc.NumStaticSamplers = CountOf(Samplers);
        RootSigDesc.pStaticSamplers = Samplers;
        RootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        BloomPass.RootSignature = dx12_root_signature_create(Backend->Device, RootSigDesc);
        BloomPass.PipelineCompute = dx12_compute_pipeline_create(Backend->Device, BloomPass.RootSignature, L"Resources/Bloom.hlsl", L"CSMain");

        const wchar_t* DebugNames[]{
            L"BloomTexture0",
            L"BloomTexture1",
            L"BloomTexture2"
        };

        // Calculate bloom size
        v2i ViewportSize = v2i((i32)SwapChainDesc.BufferDesc.Width, (i32)SwapChainDesc.BufferDesc.Height);
        v2i BloomTextureSize = v2i(ViewportSize + v2i(1)) / 2;
        BloomTextureSize += v2i(BloomPass.BloomComputeWorkgroupSize) - (BloomTextureSize % BloomPass.BloomComputeWorkgroupSize);
        const u32 Mips = 6; // Fixed size for now
        auto Format = Backend->MainPass.Format;

        // Bloom textures
        for (u32 FrameIndex = 0; FrameIndex < FIF; FrameIndex++)
        {
            // Bloom Texture 0
            {
                auto& BloomTexture0 = Backend->BloomPass.BloomTexture0[FrameIndex];

                BloomTexture0.Handle = dx12_render_target_create(Device, Format, BloomTextureSize.x, BloomTextureSize.y, Mips, true, DebugNames[0]);

                dx12_render_backend_generate_mips(Backend, BloomTexture0.Handle, SwapChainDesc.BufferDesc.Width, SwapChainDesc.BufferDesc.Height, Mips, Format);

                BloomTexture0.ShaderResourceView = Backend->SRVCBVUAV_Allocator.Alloc();
                Backend->Device->CreateShaderResourceView(BloomTexture0.Handle, nullptr, BloomTexture0.ShaderResourceView.CPU);

                for (u32 MipLevel = 0; MipLevel < Mips; MipLevel++)
                {
                    D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
                    UAVDesc.Format = Format;
                    UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                    UAVDesc.Texture2D.MipSlice = MipLevel;

                    BloomTexture0.ComputeMipViews[MipLevel] = Backend->SRVCBVUAV_Allocator.Alloc();
                    Backend->Device->CreateUnorderedAccessView(BloomTexture0.Handle, nullptr, &UAVDesc, BloomTexture0.ComputeMipViews[MipLevel].CPU);
                }
            }

            // Bloom Texture 1
            {
                auto& BloomTexture1 = Backend->BloomPass.BloomTexture1[FrameIndex];

                BloomTexture1.Handle = dx12_render_target_create(Device, Format, BloomTextureSize.x, BloomTextureSize.y, Mips, true, DebugNames[1]);

                dx12_render_backend_generate_mips(Backend, BloomTexture1.Handle, SwapChainDesc.BufferDesc.Width, SwapChainDesc.BufferDesc.Height, Mips, Format);

                BloomTexture1.ShaderResourceView = Backend->SRVCBVUAV_Allocator.Alloc();
                Backend->Device->CreateShaderResourceView(BloomTexture1.Handle, nullptr, BloomTexture1.ShaderResourceView.CPU);

                for (u32 MipLevel = 0; MipLevel < Mips; MipLevel++)
                {
                    D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
                    UAVDesc.Format = Format;
                    UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                    UAVDesc.Texture2D.MipSlice = MipLevel;

                    BloomTexture1.ComputeMipViews[MipLevel] = Backend->SRVCBVUAV_Allocator.Alloc();
                    Backend->Device->CreateUnorderedAccessView(BloomTexture1.Handle, nullptr, &UAVDesc, BloomTexture1.ComputeMipViews[MipLevel].CPU);
                }
            }

            // Bloom Texture 2
            {
                auto& BloomTexture2 = Backend->BloomPass.BloomTexture2[FrameIndex];

                BloomTexture2.Handle = dx12_render_target_create(Device, Format, BloomTextureSize.x, BloomTextureSize.y, Mips, true, DebugNames[2]);

                dx12_render_backend_generate_mips(Backend, BloomTexture2.Handle, SwapChainDesc.BufferDesc.Width, SwapChainDesc.BufferDesc.Height, Mips, Format);

                BloomTexture2.ShaderResourceView = Backend->SRVCBVUAV_Allocator.Alloc();
                Backend->Device->CreateShaderResourceView(BloomTexture2.Handle, nullptr, BloomTexture2.ShaderResourceView.CPU);

                for (u32 MipLevel = 0; MipLevel < Mips; MipLevel++)
                {
                    D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
                    UAVDesc.Format = Format;
                    UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                    UAVDesc.Texture2D.MipSlice = MipLevel;

                    BloomTexture2.ComputeMipViews[MipLevel] = Backend->SRVCBVUAV_Allocator.Alloc();
                    Backend->Device->CreateUnorderedAccessView(BloomTexture2.Handle, nullptr, &UAVDesc, BloomTexture2.ComputeMipViews[MipLevel].CPU);
                }
            }
        }
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

    // Temp allocator for dynamic textures given from game_renderer
    {
        // White texture
        u32 WhiteColor = 0xffffffff;
        buffer Buffer = { &WhiteColor, sizeof(u32) };
        Backend->WhiteTexture = texture_create(Backend, 1, 1, Buffer);

        // View at index 0 contains white texture
        Backend->BaseTextureDescriptorHandle = Backend->SRVCBVUAV_Allocator.Alloc();
        Device->CreateShaderResourceView(Backend->WhiteTexture.Handle, nullptr, Backend->BaseTextureDescriptorHandle.CPU);

        // Filling in white texture as a default
        // NOTE: These allocations needs to be sequential, if not, we're gonna have a big problem
        for (u32 i = 1; i < c_MaxTexturesPerDrawCall; i++)
        {
            auto Handle = Backend->SRVCBVUAV_Allocator.Alloc();
            Device->CreateShaderResourceView(Backend->WhiteTexture.Handle, nullptr, Handle.CPU);
        }
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

            // This description is needed since shadow maps are created typeless.
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
            // This description is needed since shadow maps are created typeless.
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

    // CompositePass Pass
    {
        // Root Signature
        {
            D3D12_DESCRIPTOR_RANGE Ranges[2] = {};
            Ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            Ranges[0].NumDescriptors = 1;
            Ranges[0].BaseShaderRegister = 0;
            Ranges[0].RegisterSpace = 0;
            Ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            Ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            Ranges[1].NumDescriptors = 1;
            Ranges[1].BaseShaderRegister = 1;
            Ranges[1].RegisterSpace = 0;
            Ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

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

            D3D12_ROOT_PARAMETER Parameters[2] = {};
            Parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            Parameters[0].DescriptorTable.NumDescriptorRanges = 1;
            Parameters[0].DescriptorTable.pDescriptorRanges = &Ranges[0];
            Parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            Parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            Parameters[1].DescriptorTable.NumDescriptorRanges = 1;
            Parameters[1].DescriptorTable.pDescriptorRanges = &Ranges[1];
            Parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_ROOT_SIGNATURE_DESC Desc = {};
            Desc.NumParameters = CountOf(Parameters);
            Desc.pParameters = Parameters;
            Desc.NumStaticSamplers = CountOf(Samplers);
            Desc.pStaticSamplers = Samplers;
            Desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

            Backend->CompositePass.RootSignature = dx12_root_signature_create(Device, Desc);
        }
        Backend->CompositePass.Pipeline = dx12_graphics_pipeline_create(Device, Backend->CompositePass.RootSignature, nullptr, 0, L"Resources/Composite.hlsl", Backend->SwapChainFormat, D3D12_CULL_MODE_FRONT, false, 1);
    }

    // Create light environment constant buffer for each frame
    for (u32 i = 0; i < FIF; i++)
    {
        Backend->LightEnvironmentConstantBuffers[i] = dx12_constant_buffer_create(Device, sizeof(light_environment));
    }
}

internal void d3d12_render_backend_render(dx12_render_backend* Backend, const game_renderer* Renderer, ImDrawData* ImGuiDrawData)
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

    DxAssert(CommandList->Reset(DirectCommandAllocator, Backend->Cuboid.Pipeline.Handle));

    // Set light environment data
    dx12_constant_buffer_set_data(&Backend->LightEnvironmentConstantBuffers[CurrentBackBufferIndex], &Renderer->LightEnvironment, sizeof(light_environment));

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

    // Copy texture's descriptor to our renderer's descriptor
    {
        auto Base = Backend->BaseTextureDescriptorHandle;

        // Skip white texture
        Base.CPU.ptr += Backend->DescriptorSizes.CBV_SRV_UAV;

        // Copy descriptors from "offline" heap to "online" heap.
        for (u32 i = 1; i < Renderer->CurrentTextureStackIndex; i++)
        {
            auto Src = Renderer->TextureStack[i]->View.CPU;
            Backend->Device->CopyDescriptorsSimple(1, Base.CPU, Src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            Base.CPU.ptr += Backend->DescriptorSizes.CBV_SRV_UAV;
        }
    }

    // Bind all available heaps
    ID3D12DescriptorHeap* Heaps[] = { Backend->SRVCBVUAV_Allocator.m_Heap.Handle };
    CommandList->SetDescriptorHeaps(CountOf(Heaps), Heaps);

    //
    // Main Pass
    // Major rendering is happening here
    //

    auto MainPassRTV = Backend->MainPass.RenderBuffersRTVViews[CurrentBackBufferIndex];
    auto MainPassDSV = Backend->MainPass.DepthBuffersViews[CurrentBackBufferIndex];
    auto MainPassRenderBuffer = Backend->MainPass.RenderBuffers[CurrentBackBufferIndex];

    // Render shadows
#if ENABLE_SHADOW_PASS
    bool EnableShadowPass = Renderer->EnableShadows;
    if (EnableShadowPass)
    {
        auto& ShadowPass = Backend->ShadowPass;
        auto ShadowMap = ShadowPass.ShadowMaps[CurrentBackBufferIndex];

        // From resource to depth write
        dx12_cmd_transition(CommandList, ShadowMap, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        dx12_cmd_set_viewport(CommandList, 0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
        dx12_cmd_set_scrissor_rect(CommandList, 0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);

        auto ShadowPassDSV = ShadowPass.ShadowMapsDSVViews[CurrentBackBufferIndex];
        CommandList->ClearDepthStencilView(ShadowPassDSV.CPU, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        CommandList->OMSetRenderTargets(0, nullptr, false, &ShadowPassDSV.CPU);
        CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Render instanced cuboids
        if (Renderer->Cuboid.InstanceCount > 0)
        {
            CommandList->SetGraphicsRootSignature(ShadowPass.RootSignature.Handle);
            CommandList->SetPipelineState(ShadowPass.Pipeline.Handle);

            CommandList->SetGraphicsRoot32BitConstants(0, sizeof(Renderer->RenderData.ShadowPassBuffer) / 4, &Renderer->RenderData.ShadowPassBuffer, 0);

            // Bind vertex positions and transforms
            dx12_cmd_set_2_vertex_buffers(CommandList, 0, Backend->Cuboid.PositionsVertexBuffer.Buffer.Handle, Backend->Cuboid.PositionsVertexBuffer.Buffer.Size, sizeof(cuboid_vertex), Backend->Cuboid.TransformVertexBuffers[CurrentBackBufferIndex].Buffer.Handle, Renderer->Cuboid.InstanceCount * sizeof(cuboid_transform_vertex_data), sizeof(cuboid_transform_vertex_data));

            // Bind index buffer
            dx12_cmd_set_index_buffer(CommandList, Backend->Quad.IndexBuffer.Buffer.Handle, 36 * sizeof(u32), DXGI_FORMAT_R32_UINT);

            // Issue draw call
            CommandList->DrawIndexedInstanced(36, Renderer->Cuboid.InstanceCount, 0, 0, 0);
        }

        // From depth write to resource
        dx12_cmd_transition(CommandList, ShadowMap, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    else // For the sake of simplicity, just clear it
    {
        auto ShadowMap = Backend->ShadowPass.ShadowMaps[CurrentBackBufferIndex];
        dx12_cmd_transition(CommandList, ShadowMap, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        auto ShadowPassDSV = Backend->ShadowPass.ShadowMapsDSVViews[CurrentBackBufferIndex];
        CommandList->ClearDepthStencilView(ShadowPassDSV.CPU, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        dx12_cmd_transition(CommandList, ShadowMap, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

#endif

    //
    // Render rest of the scene
    //

    // Frame that was presented needs to be set to render target again
    dx12_cmd_transition(CommandList, MainPassRenderBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    CommandList->ClearRenderTargetView(MainPassRTV.CPU, &ClearColor.x, 0, nullptr);
    CommandList->ClearDepthStencilView(MainPassDSV.CPU, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    CommandList->OMSetRenderTargets(1, &MainPassRTV.CPU, false, &MainPassDSV.CPU);

    dx12_cmd_set_viewport(CommandList, 0, 0, (f32)SwapChainDesc.BufferDesc.Width, (f32)SwapChainDesc.BufferDesc.Height);
    dx12_cmd_set_scrissor_rect(CommandList, 0, 0, SwapChainDesc.BufferDesc.Width, SwapChainDesc.BufferDesc.Height);

    CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const bool RenderSkybox = false;
    if (RenderSkybox)
    {
        auto& Skybox = Backend->Skybox;
        CommandList->SetGraphicsRootSignature(Skybox.RootSignature.Handle);
        CommandList->SetPipelineState(Skybox.Pipeline.Handle);

        CommandList->SetGraphicsRoot32BitConstants(0, sizeof(Renderer->RenderData.SkyboxBuffer) / 4, &Renderer->RenderData.SkyboxBuffer, 0);

        // Bind vertex positions
        dx12_cmd_set_vertex_buffer(CommandList, 0, Skybox.VertexBuffer.Buffer.Handle, Skybox.VertexBuffer.Buffer.Size, sizeof(v3));

        // Bind index buffer
        dx12_cmd_set_index_buffer(CommandList, Skybox.IndexBuffer.Buffer.Handle, sizeof(c_SkyboxIndices), DXGI_FORMAT_R32_UINT);

        CommandList->DrawIndexedInstanced(36, 1, 0, 0, 0);
    }

    const bool RenderInstancedCuboids = true;
    if (RenderInstancedCuboids && Renderer->Cuboid.InstanceCount > 0)
    {
        // Set root constants
        // Number of 32 bit values - 16 floats in 4x4 matrix
        CommandList->SetGraphicsRootSignature(Backend->Cuboid.RootSignature.Handle);

        CommandList->SetPipelineState(Backend->Cuboid.Pipeline.Handle);

        CommandList->SetGraphicsRoot32BitConstants(0, sizeof(cuboid_buffer_data) / 4, &Renderer->RenderData.CuboidBuffer, 0);
        CommandList->SetGraphicsRootConstantBufferView(1, Backend->LightEnvironmentConstantBuffers[CurrentBackBufferIndex].Buffer.Handle->GetGPUVirtualAddress());

        CommandList->SetGraphicsRootDescriptorTable(2, Backend->BaseTextureDescriptorHandle.GPU);

        dx12_cmd_set_2_vertex_buffers(CommandList, 0, Backend->Cuboid.PositionsVertexBuffer.Buffer.Handle, Backend->Cuboid.PositionsVertexBuffer.Buffer.Size, sizeof(cuboid_vertex), Backend->Cuboid.TransformVertexBuffers[CurrentBackBufferIndex].Buffer.Handle, Renderer->Cuboid.InstanceCount * sizeof(cuboid_transform_vertex_data), sizeof(cuboid_transform_vertex_data));

        // Bind index buffer
        dx12_cmd_set_index_buffer(CommandList, Backend->Quad.IndexBuffer.Buffer.Handle, 36 * sizeof(u32), DXGI_FORMAT_R32_UINT);

        // Issue draw call
        CommandList->DrawIndexedInstanced(36, Renderer->Cuboid.InstanceCount, 0, 0, 0);
    }

    // Render distant objects
    const bool RenderDistantObjects = false;
    if (RenderDistantObjects && Renderer->DistantQuad.IndexCount > 0)
    {
        CommandList->SetGraphicsRootSignature(Backend->Skybox.RootSignature.Handle);
        CommandList->SetPipelineState(Backend->DistantQuad.Pipeline.Handle);
        CommandList->SetGraphicsRoot32BitConstants(0, sizeof(distant_quad_buffer_data) / 4, &Renderer->RenderData.DistantObjectBuffer, 0);

        // Bind vertex positions
        dx12_cmd_set_vertex_buffer(CommandList, 0, Backend->DistantQuad.VertexBuffers[CurrentBackBufferIndex].Buffer.Handle, Renderer->DistantQuad.IndexCount * sizeof(distant_quad_vertex), sizeof(distant_quad_vertex));

        // Bind index buffer
        dx12_cmd_set_index_buffer(CommandList, Backend->Quad.IndexBuffer.Buffer.Handle, Renderer->DistantQuad.IndexCount * sizeof(u32), DXGI_FORMAT_R32_UINT);

        CommandList->DrawIndexedInstanced(Renderer->DistantQuad.IndexCount, 1, 0, 0, 0);
    }

    CommandList->SetGraphicsRootSignature(Backend->RootSignature.Handle);

    // Render quaded cuboids
    const bool RenderQuadedCuboid = true;
    if (RenderQuadedCuboid && Renderer->QuadedCuboid.IndexCount > 0)
    {
        CommandList->SetPipelineState(Backend->QuadedCuboid.Pipeline.Handle);

        CommandList->SetGraphicsRoot32BitConstants(0, sizeof(cuboid_buffer_data) / 4, &Renderer->RenderData.CuboidBuffer, 0);
        CommandList->SetGraphicsRootConstantBufferView(1, Backend->LightEnvironmentConstantBuffers[CurrentBackBufferIndex].Buffer.Handle->GetGPUVirtualAddress());

        // Bind vertex buffer
        dx12_cmd_set_vertex_buffer(CommandList, 0, Backend->QuadedCuboid.VertexBuffers[CurrentBackBufferIndex].Buffer.Handle, Backend->QuadedCuboid.VertexBuffers[CurrentBackBufferIndex].Buffer.Size, sizeof(quaded_cuboid_vertex));

        // Bind index buffer
        dx12_cmd_set_index_buffer(CommandList, Backend->Quad.IndexBuffer.Buffer.Handle, Renderer->QuadedCuboid.IndexCount * sizeof(u32), DXGI_FORMAT_R32_UINT);

        // Issue draw call
        CommandList->DrawIndexedInstanced(Renderer->QuadedCuboid.IndexCount, 1, 0, 0, 0);
    }

    // Render quads
    const bool RenderQuads = true;
    if (RenderQuads && Renderer->Quad.IndexCount > 0)
    {
        auto& Quad = Backend->Quad;
        CommandList->SetGraphicsRootSignature(Backend->RootSignature.Handle);
        CommandList->SetPipelineState(Quad.Pipeline.Handle);

        // Bind vertex buffer
        dx12_cmd_set_vertex_buffer(CommandList, 0, Quad.VertexBuffers[CurrentBackBufferIndex].Buffer.Handle, Renderer->Quad.IndexCount * sizeof(quad_vertex), sizeof(quad_vertex));

        // Bind index buffer
        dx12_cmd_set_index_buffer(CommandList, Quad.IndexBuffer.Buffer.Handle, Renderer->Quad.IndexCount * sizeof(u32), DXGI_FORMAT_R32_UINT);

        // TODO: For now just share the first half of the signature buffer, this needs some sort of distinction between HUD and Game stuff
        CommandList->SetGraphicsRoot32BitConstants(0, 16, &Renderer->RenderData.CuboidBuffer.ViewProjection, 0);

        // Issue draw call
        CommandList->DrawIndexedInstanced(Renderer->Quad.IndexCount, 1, 0, 0, 0);
    }

    // Render HUD
    const bool RenderHUD = true;
    if (RenderHUD && Renderer->HUD.IndexCount > 0)
    {
        CommandList->SetPipelineState(Backend->HUD.Pipeline.Handle);

        // Bind vertex buffer
        dx12_cmd_set_vertex_buffer(CommandList, 0, Backend->HUD.VertexBuffers[CurrentBackBufferIndex].Buffer.Handle, Renderer->HUD.IndexCount * sizeof(hud_quad_vertex), sizeof(hud_quad_vertex));

        // Bind index buffer
        dx12_cmd_set_index_buffer(CommandList, Backend->Quad.IndexBuffer.Buffer.Handle, Renderer->HUD.IndexCount * sizeof(u32), DXGI_FORMAT_R32_UINT);

        CommandList->SetGraphicsRoot32BitConstants(0, 16, &Renderer->RenderData.HUDBuffer.Projection, 0);

        // Issue draw call
        CommandList->DrawIndexedInstanced(Renderer->HUD.IndexCount, 1, 0, 0, 0);
    }

    const bool EnableBloomPass = Renderer->EnableBloom;
    dx12_cmd_transition(CommandList, MainPassRenderBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, EnableBloomPass ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // Bloom Pass
    // Take MainPassRenderBuffer, do magic and output it
    if (EnableBloomPass)
    {
        local_persist bloom_pass_buffer_data BloomPassData;
        BloomPassData.Params = v4(1.0f, 1.0f, 1.0f, 1.0f);

        const u32 Mips = 6;

        auto& BloomPass = Backend->BloomPass;

        auto BloomTexture0 = BloomPass.BloomTexture0[CurrentBackBufferIndex];
        auto BloomTexture1 = BloomPass.BloomTexture1[CurrentBackBufferIndex];
        auto BloomTexture2 = BloomPass.BloomTexture2[CurrentBackBufferIndex];

        const v2u BloomTextureSize = { (u32)BloomTexture0.Handle->GetDesc().Width, (u32)BloomTexture0.Handle->GetDesc().Height };

        auto get_mip_size = [](u32 Mip, u32 Width0, u32 Height0) -> std::pair<u32, u32>
        {
            while (Mip != 0)
            {
                Width0 /= 2;
                Height0 /= 2;
                Mip--;
            }

            return { Width0, Height0 };
        };

        u32 ThreadGroupsX = BloomTextureSize.x / BloomPass.BloomComputeWorkgroupSize;
        u32 ThreadGroupsY = BloomTextureSize.y / BloomPass.BloomComputeWorkgroupSize;

        //Set root signature, pso and descriptor heap
        CommandList->SetPipelineState(BloomPass.PipelineCompute.Handle);
        CommandList->SetComputeRootSignature(BloomPass.RootSignature.Handle);

        // First we need to copy main pass render target to the bloom texture, downsample it and prefilter it to separate bright pixels from darker ones
        {
            BloomPassData.Mode = bloom_pass_buffer_data::mode::PreFilter;
            BloomPassData.Params = v4(1.0f, 1.0f, 1.0f, 1.0f);
            BloomPassData.LOD = 0.0f;
            CommandList->SetComputeRoot32BitConstants(0, sizeof(BloomPassData) / 4, &BloomPassData, 0);

            CommandList->SetComputeRootDescriptorTable(1, BloomTexture0.ShaderResourceView.GPU);
            CommandList->SetComputeRootDescriptorTable(2, Backend->MainPass.RenderBuffersSRVViews[CurrentBackBufferIndex].GPU);

            // Unused
            CommandList->SetComputeRootDescriptorTable(3, BloomTexture2.ShaderResourceView.GPU);

            CommandList->Dispatch(ThreadGroupsX, ThreadGroupsY, 1);

            // Wait for it to finish
            dx12_cmd_wait_for_resource(CommandList, D3D12_RESOURCE_BARRIER_TYPE_UAV);
        }

        // Downsample
        if (1)
        {
            BloomPassData.Mode = bloom_pass_buffer_data::mode::DownSample;


            for (u32 MipLevel = 1; MipLevel < Mips; MipLevel++)
            {
                auto [MipWidth, MipHeight] = get_mip_size(MipLevel, BloomTextureSize.x, BloomTextureSize.y);

                ThreadGroupsX = (u32)bkm::Ceil((f32)MipWidth / (f32)BloomPass.BloomComputeWorkgroupSize);
                ThreadGroupsY = (u32)bkm::Ceil((f32)MipHeight / (f32)BloomPass.BloomComputeWorkgroupSize);

                // Copy and downsample from source mip to dest mip
                {
                    // Trace("%i %i", ThreadGroupsX, ThreadGroupsY);

                    // Output
                    CommandList->SetComputeRootDescriptorTable(1, BloomTexture1.ComputeMipViews[MipLevel].GPU);

                    // Input, this is the downsampled texture from previous pass
                    // MipLevel=1 BloomTexture0 makes sense because it contains downsampled main pass image
                    CommandList->SetComputeRootDescriptorTable(2, BloomTexture0.ShaderResourceView.GPU);

                    // Unused
                    CommandList->SetComputeRootDescriptorTable(3, BloomTexture2.ShaderResourceView.GPU);

                    BloomPassData.LOD = (f32)MipLevel - 1.0f;
                    CommandList->SetComputeRoot32BitConstants(0, sizeof(BloomPassData) / 4, &BloomPassData, 0);
                    CommandList->Dispatch(ThreadGroupsX, ThreadGroupsY, 1);
                }

                // Wait for it to finish
                dx12_cmd_wait_for_resource(CommandList, D3D12_RESOURCE_BARRIER_TYPE_UAV);

                // Just copy dest mip to source mip and repeat
                {
                    // Output
                    CommandList->SetComputeRootDescriptorTable(1, BloomTexture0.ComputeMipViews[MipLevel].GPU);

                    // Input
                    CommandList->SetComputeRootDescriptorTable(2, BloomTexture1.ShaderResourceView.GPU);

                    // Unused
                    CommandList->SetComputeRootDescriptorTable(3, BloomTexture2.ShaderResourceView.GPU);

                    BloomPassData.LOD = (f32)MipLevel;
                    CommandList->SetComputeRoot32BitConstants(0, sizeof(BloomPassData) / 4, &BloomPassData, 0);
                    CommandList->Dispatch(ThreadGroupsX, ThreadGroupsY, 1);
                }

                // Wait for it to finish
                dx12_cmd_wait_for_resource(CommandList, D3D12_RESOURCE_BARRIER_TYPE_UAV);
            }
        }

        // First upsample
        {
            BloomPassData.Mode = bloom_pass_buffer_data::mode::FirstUpSample;
            //ThreadGroupsX *= 2;
            //ThreadGroupsY *= 2;

            auto [MipWidth, MipHeight] = get_mip_size(Mips - 2, BloomTextureSize.x, BloomTextureSize.y);
            ThreadGroupsX = (u32)bkm::Ceil((f32)MipWidth / (f32)BloomPass.BloomComputeWorkgroupSize);
            ThreadGroupsY = (u32)bkm::Ceil((f32)MipHeight / (f32)BloomPass.BloomComputeWorkgroupSize);

            // Output
            CommandList->SetComputeRootDescriptorTable(1, BloomTexture2.ComputeMipViews[Mips - 2].GPU);

            // Input
            CommandList->SetComputeRootDescriptorTable(2, BloomTexture0.ShaderResourceView.GPU);

            // Unused
            CommandList->SetComputeRootDescriptorTable(3, BloomTexture2.ShaderResourceView.GPU);

            BloomPassData.LOD--;
            CommandList->SetComputeRoot32BitConstants(0, sizeof(BloomPassData) / 4, &BloomPassData, 0);

            CommandList->Dispatch(ThreadGroupsX, ThreadGroupsY, 1);

            // Wait for it to finish
            dx12_cmd_wait_for_resource(CommandList, D3D12_RESOURCE_BARRIER_TYPE_UAV);
        }

        // Upsample
        {
            BloomPassData.Mode = bloom_pass_buffer_data::mode::UpSample;

            for (int32_t Mip = Mips - 3; Mip >= 0; Mip--)
            {
                auto [MipWidth, MipHeight] = get_mip_size(Mip, BloomTextureSize.x, BloomTextureSize.y);
                ThreadGroupsX = (u32)bkm::Ceil((f32)MipWidth / (f32)BloomPass.BloomComputeWorkgroupSize);
                ThreadGroupsY = (u32)bkm::Ceil((f32)MipHeight / (f32)BloomPass.BloomComputeWorkgroupSize);

                // Output
                CommandList->SetComputeRootDescriptorTable(1, BloomTexture2.ComputeMipViews[Mip].GPU);

                // Input 1
                CommandList->SetComputeRootDescriptorTable(2, BloomTexture0.ShaderResourceView.GPU);

                // Input 2
                CommandList->SetComputeRootDescriptorTable(3, BloomTexture2.ShaderResourceView.GPU);

                BloomPassData.LOD = (f32)Mip;
                CommandList->SetComputeRoot32BitConstants(0, sizeof(BloomPassData) / 4, &BloomPassData, 0);
                CommandList->Dispatch(ThreadGroupsX, ThreadGroupsY, 1);

                // Wait for it to finish
                dx12_cmd_wait_for_resource(CommandList, D3D12_RESOURCE_BARRIER_TYPE_UAV);
            }
        }

        dx12_cmd_transition(CommandList, MainPassRenderBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    //
    // Copy main pass render target to swapchain target and optionally apply some post-processing
    //
    dx12_cmd_transition(CommandList, SwapChainBackBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

    const bool DisplayImage = true;
    if (DisplayImage)
    {
        CommandList->OMSetRenderTargets(1, &SwapChainRTV.CPU, false, nullptr);
        dx12_cmd_set_viewport(CommandList, 0, 0, (f32)SwapChainDesc.BufferDesc.Width, (f32)SwapChainDesc.BufferDesc.Height);
        dx12_cmd_set_scrissor_rect(CommandList, 0, 0, SwapChainDesc.BufferDesc.Width, SwapChainDesc.BufferDesc.Height);

        CommandList->SetPipelineState(Backend->CompositePass.Pipeline.Handle);
        CommandList->SetGraphicsRootSignature(Backend->CompositePass.RootSignature.Handle);

        // Main pass texture
        CommandList->SetGraphicsRootDescriptorTable(0, Backend->MainPass.RenderBuffersSRVViews[CurrentBackBufferIndex].GPU);

        // Bloom textute
        CommandList->SetGraphicsRootDescriptorTable(1, Backend->BloomPass.BloomTexture2[CurrentBackBufferIndex].ShaderResourceView.GPU);

        CommandList->DrawInstanced(3, 1, 0, 0);
    }

    // Render debug UI
    const bool RenderImGui = true;
    if (RenderImGui)
    {
        // ImGui uses our SRV heap
        ImGui_ImplDX12_RenderDrawData(ImGuiDrawData, CommandList);
    }

    // Rendered frame needs to be transitioned to present state
    dx12_cmd_transition(CommandList, SwapChainBackBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    // Finalize the command list
    DxAssert(CommandList->Close());

    Backend->DirectCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&CommandList);

    DxAssert(Backend->SwapChain->Present(Backend->VSync, 0));

    // Wait for GPU to finish presenting
    FrameFenceValue = dx12_signal(Backend->DirectCommandQueue, Backend->Fence, &Backend->FenceValue);
    dx12_wait_for_fence_value(Backend->Fence, FrameFenceValue, Backend->DirectFenceEvent);
    //WaitForSingleObjectEx(Backend->FrameLatencyEvent, INFINITE, FALSE);

    // Move to another back buffer
    Backend->CurrentBackBufferIndex = Backend->SwapChain->GetCurrentBackBufferIndex();

    // Log dx12 stuff
    DumpInfoQueue();
}

internal void d3d12_render_backend_resize_buffers(dx12_render_backend* Backend, u32 RequestWidth, u32 RequestHeight)
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

        // Rebind existing views
        for (u32 i = 0; i < FIF; ++i)
        {
            // There is no need to free the views since they do the description of the resource does not change at all
            DxAssert(Backend->SwapChain->GetBuffer(i, IID_PPV_ARGS(&Backend->SwapChainBackbuffers[i])));
            Backend->Device->CreateRenderTargetView(Backend->SwapChainBackbuffers[i], nullptr, Backend->SwapChainBufferRTVViews[i].CPU);
        }
    }

    // MainPass - Resize render buffers and recreate RTV and DSV views
    {
        auto& MainPass = Backend->MainPass;

        const bool AllowUnorderedAccess = true;
        for (u32 i = 0; i < FIF; i++)
        {
            // Render buffers
            {
                MainPass.RenderBuffers[i]->Release();
                MainPass.RenderBuffers[i] = dx12_render_target_create(Backend->Device, Backend->MainPass.Format, SwapChainDesc.BufferDesc.Width, SwapChainDesc.BufferDesc.Height, AllowUnorderedAccess, L"MainPassBuffer");

                // Render Target View
                Backend->Device->CreateRenderTargetView(MainPass.RenderBuffers[i], nullptr, MainPass.RenderBuffersRTVViews[i].CPU);

                // Shader Resouces View
                Backend->Device->CreateShaderResourceView(MainPass.RenderBuffers[i], nullptr, MainPass.RenderBuffersSRVViews[i].CPU);
            }

            // Depth buffers
            {
                MainPass.DepthBuffers[i]->Release();
                MainPass.DepthBuffers[i] = dx12_depth_buffer_create(Backend->Device, SwapChainDesc.BufferDesc.Width, SwapChainDesc.BufferDesc.Height, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D32_FLOAT, L"MainPassDepthBuffer");
                Backend->Device->CreateDepthStencilView(MainPass.DepthBuffers[i], nullptr, MainPass.DepthBuffersViews[i].CPU);
            }
        }
    }

    Warn("SwapChain resized to %d %d", SwapChainDesc.BufferDesc.Width, SwapChainDesc.BufferDesc.Height);
    Assert(RequestWidth == SwapChainDesc.BufferDesc.Width && RequestHeight == SwapChainDesc.BufferDesc.Height, "Requested size is not equal actual swapchain size!");
}

internal void dx12_render_backend_flush(dx12_render_backend* Renderer)
{
    u64 FenceValueForSignal = dx12_signal(Renderer->DirectCommandQueue, Renderer->Fence, &Renderer->FenceValue);
    dx12_wait_for_fence_value(Renderer->Fence, FenceValueForSignal, Renderer->DirectFenceEvent);
}

internal void dx12_render_backend_generate_mips(dx12_render_backend* Backend, ID3D12Resource* TextureResource, u32 Width, u32 Height, u32 MipCount, DXGI_FORMAT Format)
{
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
