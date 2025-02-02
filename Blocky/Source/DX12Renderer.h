#pragma once

// DirectX 12
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")

// D3D12 extension
#include <d3dx12.h>

#define DxAssert(x) Assert(SUCCEEDED(x), #x) 

#define DX12_ENABLE_DEBUG_LAYER

#ifdef DX12_ENABLE_DEBUG_LAYER
#include <dxgidebug.h>

#pragma comment(lib, "dxguid.lib")

#endif

static constexpr inline v4 c_QuadVertexPositions[4]
{
    { -0.5f, -0.5f, 0.0f, 1.0f },
    {  0.5f, -0.5f, 0.0f, 1.0f },
    {  0.5f,  0.5f, 0.0f, 1.0f },
    { -0.5f,  0.5f, 0.0f, 1.0f }
};

static D3D12_RESOURCE_BARRIER DX12RendererTransition(
   ID3D12Resource* pResource,
   D3D12_RESOURCE_STATES stateBefore,
   D3D12_RESOURCE_STATES stateAfter,
   UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
   D3D12_RESOURCE_BARRIER_FLAGS flags = D3D12_RESOURCE_BARRIER_FLAG_NONE)
{
    D3D12_RESOURCE_BARRIER Result;
    Result.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Result.Flags = flags;
    Result.Transition.pResource = pResource;
    Result.Transition.StateBefore = stateBefore;
    Result.Transition.StateAfter = stateAfter;
    Result.Transition.Subresource = subresource;
    return Result;
};

#include "DX12Buffer.h"

struct dx12_resource
{
    ID3D12Resource* Resource;
    ID3D12DescriptorHeap* Heap;
};

struct dx12_game_renderer
{
    ID3D12Device2* Device;
    ID3D12Debug* DebugInterface;
    ID3D12InfoQueue* DebugInfoQueue;

    ID3D12CommandAllocator* DirectCommandAllocators[FIF];
    ID3D12GraphicsCommandList2* DirectCommandList;
    ID3D12CommandQueue* DirectCommandQueue;

    ID3D12CommandAllocator* CopyCommandAllocators[FIF];
    ID3D12CommandQueue* CopyCommandQueue;
    ID3D12GraphicsCommandList2* CopyCommandList;

    IDXGISwapChain4* SwapChain;
    ID3D12Resource* BackBuffers[FIF];
    ID3D12DescriptorHeap* RTVDescriptorHeap;
    u32 RTVDescriptorSize;
    u32 CurrentBackBufferIndex;

    ID3D12Resource* DepthBuffer;
    ID3D12DescriptorHeap* DepthStencilHeap;
    bool DepthTesting = true;

    bool VSync = true;


    // Fence
    ID3D12Fence* Fence;
    u64 FenceValue;
    u64 FrameFenceValues[FIF];
    HANDLE DirectFenceEvent;

    // Describes resources used in the shader
    ID3D12RootSignature* RootSignature;

    D3D12_VIEWPORT Viewport;
    D3D12_RECT ScissorRect;

    // Quad
    ID3D12PipelineState* QuadPipelineState;
    dx12_vertex_buffer QuadVertexBuffer;
    dx12_buffer QuadIndexBuffer;
    quad_vertex QuadVertexDataBase[c_MaxQuadVertices];
    quad_vertex* QuadVertexDataPtr = QuadVertexDataBase;
    u32 QuadIndexCount = 0;
};

// Each face has to have a normal vector, so unfortunately we cannot encode cube as 8 vertices
static constexpr inline v4 c_CubeVertexPositionsCounterClockwise[24] =
{
    // Front face (+Z)
    { -0.5f, -0.5f,  0.5f, 1.0f },
    {  0.5f, -0.5f,  0.5f, 1.0f },
    {  0.5f,  0.5f,  0.5f, 1.0f },
    { -0.5f,  0.5f,  0.5f, 1.0f },

    // Back face (-Z)
    {  0.5f, -0.5f, -0.5f, 1.0f },
    { -0.5f, -0.5f, -0.5f, 1.0f },
    { -0.5f,  0.5f, -0.5f, 1.0f },
    {  0.5f,  0.5f, -0.5f, 1.0f },

    // Left face (-X)
    { -0.5f, -0.5f, -0.5f, 1.0f },
    { -0.5f, -0.5f,  0.5f, 1.0f },
    { -0.5f,  0.5f,  0.5f, 1.0f },
    { -0.5f,  0.5f, -0.5f, 1.0f },

    // Right face (+X)
    {  0.5f, -0.5f,  0.5f, 1.0f },
    {  0.5f, -0.5f, -0.5f, 1.0f },
    {  0.5f,  0.5f, -0.5f, 1.0f },
    {  0.5f,  0.5f,  0.5f, 1.0f },

    // Top face (+Y)
    { -0.5f,  0.5f,  0.5f, 1.0f },
    {  0.5f,  0.5f,  0.5f, 1.0f },
    {  0.5f,  0.5f, -0.5f, 1.0f },
    { -0.5f,  0.5f, -0.5f, 1.0f },

    // Bottom face (-Y)
    { -0.5f, -0.5f, -0.5f, 1.0f },
    {  0.5f, -0.5f, -0.5f, 1.0f },
    {  0.5f, -0.5f,  0.5f, 1.0f },
    { -0.5f, -0.5f,  0.5f, 1.0f }
};

static void DX12RendererDumpInfoQueue(ID3D12InfoQueue* InfoQueue)
{
    for (UINT64 i = 0; i < InfoQueue->GetNumStoredMessages(); ++i)
    {
        SIZE_T MessageLength = 0;

        // Get the length of the message
        HRESULT HR = InfoQueue->GetMessage(i, nullptr, &MessageLength);
        if (FAILED(HR))
        {
            Warn("Failed to get message length: HRESULT = 0x%08X\n", HR);
            continue;
        }

        // Allocate memory for the message
        auto Message = static_cast<D3D12_MESSAGE*>(alloca(MessageLength));
        if (!Message)
        {
            Warn("Failed to allocate memory for message.\n");
            continue;
        }

        // Retrieve the message
        HR = InfoQueue->GetMessage(i, Message, &MessageLength);
        if (FAILED(HR))
        {
            Warn("Failed to get message: HRESULT = 0x%08X\n", HR);
            continue;
        }

        // Print the message description
        Trace("Message %llu: %s\n", i, Message->pDescription);
    }

    // Optionally, clear the messages from the queue
    InfoQueue->ClearStoredMessages();
}

static u64 DX12RendererSignal(ID3D12CommandQueue* CommandQueue, ID3D12Fence* Fence, u64* FenceValue)
{
    u64& RefFenceValue = *FenceValue;
    u64 FenceValueForSignal = ++RefFenceValue;
    DxAssert(CommandQueue->Signal(Fence, FenceValueForSignal));
    return FenceValueForSignal;
}

static void DX12RendererWaitForFenceValue(ID3D12Fence* Fence, u64 FenceValue, HANDLE FenceEvent, u32 Duration = UINT32_MAX)
{
    if (Fence->GetCompletedValue() < FenceValue)
    {
        Fence->SetEventOnCompletion(FenceValue, FenceEvent);
        WaitForSingleObject(FenceEvent, Duration);
    }
}

static void DX12RendererFlush(dx12_game_renderer* Renderer)
{
    u64 FenceValueForSignal = DX12RendererSignal(Renderer->DirectCommandQueue, Renderer->Fence, &Renderer->FenceValue);
    DX12RendererWaitForFenceValue(Renderer->Fence, FenceValueForSignal, Renderer->DirectFenceEvent);
}

static IDXGIAdapter1* GetHardwareAdapter(IDXGIFactory1* Factory, bool RequestHighPerformanceAdapter = true)
{
    IDXGIAdapter1* Adapter = nullptr;

    IDXGIFactory6* factory6;
    if (SUCCEEDED(Factory->QueryInterface(IID_PPV_ARGS(&factory6))))
    {
        for (
            UINT adapterIndex = 0;
            SUCCEEDED(factory6->EnumAdapterByGpuPreference(
                adapterIndex,
                RequestHighPerformanceAdapter == true ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED,
                IID_PPV_ARGS(&Adapter)));
                ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            Adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                // Don't select the Basic Render Driver adapter.
                // If you want a software adapter, pass in "/warp" on the command line.
                continue;
            }

            // Check to see whether the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            if (SUCCEEDED(D3D12CreateDevice(Adapter, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
            {
                break;
            }
        }
    }

    if (Adapter == nullptr)
    {
        for (UINT adapterIndex = 0; SUCCEEDED(Factory->EnumAdapters1(adapterIndex, &Adapter)); ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            Adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                // Don't select the Basic Render Driver adapter.
                // If you want a software adapter, pass in "/warp" on the command line.
                continue;
            }

            // Check to see whether the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            if (SUCCEEDED(D3D12CreateDevice(Adapter, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
            {
                break;
            }
        }
    }

    return Adapter;
}

static void DX12RendererInitializeContext(dx12_game_renderer* Renderer, game_window Window)
{
    // Enable debug layer
    // This is sort of like validation layers in Vulkan
    DxAssert(D3D12GetDebugInterface(IID_PPV_ARGS(&Renderer->DebugInterface)));
    Renderer->DebugInterface->EnableDebugLayer();

    // Create device
    {
        IDXGIFactory4* Factory;
        DxAssert(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&Factory)));

        IDXGIAdapter1* HardwareAdapter = GetHardwareAdapter(Factory);
        DxAssert(D3D12CreateDevice(HardwareAdapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Renderer->Device)));
    }

    // Setup debug interface
    {
        DxAssert(Renderer->Device->QueryInterface(IID_PPV_ARGS(&Renderer->DebugInfoQueue)));
        Renderer->DebugInfoQueue->QueryInterface(IID_PPV_ARGS(&Renderer->DebugInfoQueue));
        Renderer->DebugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, false);
        Renderer->DebugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, false);
        Renderer->DebugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, false);
        Renderer->DebugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_INFO, false);
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

            DxAssert(Renderer->DebugInfoQueue->PushStorageFilter(&NewFilter));
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
        Desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        Desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        Desc.NodeMask = 0;
        DxAssert(Renderer->Device->CreateCommandQueue(&Desc, IID_PPV_ARGS(&Renderer->DirectCommandQueue)));

        // Create a direct command list
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

    // Create copy command queue
    {
        // Create direct command allocator
        for (u32 i = 0; i < FIF; i++)
        {
            DxAssert(Renderer->Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&Renderer->CopyCommandAllocators[i])));
        }

        D3D12_COMMAND_QUEUE_DESC Desc = {};
        Desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
        Desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        Desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        Desc.NodeMask = 0;
        DxAssert(Renderer->Device->CreateCommandQueue(&Desc, IID_PPV_ARGS(&Renderer->CopyCommandQueue)));

        // Create a copy command list
        {
            DxAssert(Renderer->Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, Renderer->CopyCommandAllocators[Renderer->CurrentBackBufferIndex], nullptr, IID_PPV_ARGS(&Renderer->CopyCommandList)));

            // Command lists are created in the recording state, but there is nothing
            // to record yet. The main loop expects it to be closed, so close it now.
            //DxAssert(Renderer->DirectCommandList->Close());
        }
    }

    // Create swapchain
    {
        IDXGISwapChain1* SwapChain1;
        IDXGIFactory4* Factory4;
        DxAssert(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&Factory4)));

        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.Width = Window.ClientAreaWidth;
        swapChainDesc.Height = Window.ClientAreaHeight;
        swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.Stereo = FALSE;
        swapChainDesc.SampleDesc = { 1, 0 };
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = FIF;
        swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        // It is recommended to always allow tearing if tearing support is available.
        // TODO: More robustness needed
        swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;// CheckTearingSupport() ? : 0;

        DxAssert(Factory4->CreateSwapChainForHwnd(Renderer->DirectCommandQueue, Window.WindowHandle, &swapChainDesc, nullptr, nullptr, &SwapChain1));

        // Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
        // will be handled manually.
        DxAssert(Factory4->MakeWindowAssociation(Window.WindowHandle, DXGI_MWA_NO_ALT_ENTER));
        SwapChain1->QueryInterface(IID_PPV_ARGS(&Renderer->SwapChain));
        SwapChain1->Release();
        Factory4->Release();

        Renderer->SwapChain->SetMaximumFrameLatency(FIF);

        Renderer->CurrentBackBufferIndex = Renderer->SwapChain->GetCurrentBackBufferIndex();
    }

    // Create a Descriptor Heap
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = FIF;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask = 1;
        DxAssert(Renderer->Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&Renderer->RTVDescriptorHeap)));
    }

    // Create Depth Stencil Heap
    {
        ID3D12DescriptorHeap* DSVHeap = nullptr;

        // Create the descriptor heap for the depth-stencil view.
        D3D12_DESCRIPTOR_HEAP_DESC DsvHeapDesc = {};
        DsvHeapDesc.NumDescriptors = 1;
        DsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        DsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        DxAssert(Renderer->Device->CreateDescriptorHeap(&DsvHeapDesc, IID_PPV_ARGS(&Renderer->DepthStencilHeap)));

        // Create depth buffer
        D3D12_CLEAR_VALUE OptimizedClearValue = {};
        OptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
        OptimizedClearValue.DepthStencil = { 1.0f, 0 };

        auto HeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

        D3D12_RESOURCE_DESC DepthStencilDesc = {};
        DepthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        DepthStencilDesc.Width = Window.ClientAreaWidth;
        DepthStencilDesc.Height = Window.ClientAreaHeight;
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
            IID_PPV_ARGS(&Renderer->DepthBuffer)
        ));

        // Update the depth-stencil view.
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
        dsv.Format = DXGI_FORMAT_D32_FLOAT;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsv.Texture2D.MipSlice = 0;
        dsv.Flags = D3D12_DSV_FLAG_NONE;

        Renderer->Device->CreateDepthStencilView(Renderer->DepthBuffer, &dsv, Renderer->DepthStencilHeap->GetCPUDescriptorHandleForHeapStart());
    }

    // Create Render Target Views
    {
        Renderer->RTVDescriptorSize = Renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle = Renderer->RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

        for (u32 i = 0; i < FIF; ++i)
        {
            ID3D12Resource* BackBuffer;
            DxAssert(Renderer->SwapChain->GetBuffer(i, IID_PPV_ARGS(&BackBuffer)));

            Renderer->Device->CreateRenderTargetView(BackBuffer, nullptr, RtvHandle);

            Renderer->BackBuffers[i] = BackBuffer;

            RtvHandle.ptr += Renderer->RTVDescriptorSize;
        }
    }
}

static void DX12RendererInitializePipeline(dx12_game_renderer* Renderer)
{
    auto Device = Renderer->Device;

    // Root Signature
    {
        D3D12_ROOT_PARAMETER Parameters[1] = {};
        Parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        Parameters[0].Constants.Num32BitValues = 16;  // 4x4 matrix (16 floats)
        Parameters[0].Constants.ShaderRegister = 0;  // Matches b0 in HLSL
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
        DxAssert(Device->CreateRootSignature(0, Signature->GetBufferPointer(), Signature->GetBufferSize(), IID_PPV_ARGS(&Renderer->RootSignature)));
    }

    // Quad Pipeline
    {
        // Graphics Pipeline State
        {
            ID3DBlob* VertexShader;
            ID3DBlob* PixelShader;

            ID3DBlob* ErrorMessage;

#if defined(BK_DEBUG)
            // Enable better shader debugging with the graphics debugging tools.
            UINT CompileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
            UINT CompileFlags = 0;
#endif

            DxAssert(D3DCompileFromFile(L"Resources/Shader.hlsl", nullptr, nullptr, "VSMain", "vs_5_1", CompileFlags, 0, &VertexShader, &ErrorMessage));
            DxAssert(D3DCompileFromFile(L"Resources/Shader.hlsl", nullptr, nullptr, "PSMain", "ps_5_1", CompileFlags, 0, &PixelShader, &ErrorMessage));

            // Define the vertex input layout.
            D3D12_INPUT_ELEMENT_DESC InputElementDescs[] =
            {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC PipelineDesc = {};

            // Rasterizer state
            PipelineDesc.RasterizerState = {};
            PipelineDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            PipelineDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
            PipelineDesc.RasterizerState.FrontCounterClockwise = TRUE;
            PipelineDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
            PipelineDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
            PipelineDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
            PipelineDesc.RasterizerState.DepthClipEnable = TRUE;
            PipelineDesc.RasterizerState.MultisampleEnable = FALSE;
            PipelineDesc.RasterizerState.AntialiasedLineEnable = FALSE;
            PipelineDesc.RasterizerState.ForcedSampleCount = 0;
            PipelineDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

            // Depth and stencil state
            PipelineDesc.DepthStencilState.DepthEnable = TRUE;
            PipelineDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            PipelineDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;  // Closer pixels are drawn
            PipelineDesc.DepthStencilState.StencilEnable = FALSE;  // Stencil disabled for now
            PipelineDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;  // Must match depth buffer format

            // Blend state
            PipelineDesc.BlendState = {};
            PipelineDesc.BlendState.AlphaToCoverageEnable = FALSE;
            PipelineDesc.BlendState.IndependentBlendEnable = FALSE;
            const D3D12_RENDER_TARGET_BLEND_DESC DefaultRenderTargetBlendDesc =
            {
                FALSE,FALSE,
                D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
                D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
                D3D12_LOGIC_OP_NOOP,
                D3D12_COLOR_WRITE_ENABLE_ALL,
            };
            for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
                PipelineDesc.BlendState.RenderTarget[i] = DefaultRenderTargetBlendDesc;

            PipelineDesc.InputLayout = { InputElementDescs, CountOf(InputElementDescs) };
            PipelineDesc.pRootSignature = Renderer->RootSignature;
            PipelineDesc.VS = CD3DX12_SHADER_BYTECODE(VertexShader);
            PipelineDesc.PS = CD3DX12_SHADER_BYTECODE(PixelShader);
            PipelineDesc.SampleMask = UINT_MAX;
            PipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            PipelineDesc.NumRenderTargets = 1;
            PipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            PipelineDesc.SampleDesc.Count = 1;

            DxAssert(Device->CreateGraphicsPipelineState(&PipelineDesc, IID_PPV_ARGS(&Renderer->QuadPipelineState)));
        }

        // Quad
        {
            Renderer->QuadVertexBuffer = DX12VertexBufferCreate(Device, sizeof(quad_vertex) * c_MaxQuadVertices);

            u32 QuadIndices[c_MaxQuadIndices];
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

            Renderer->QuadIndexBuffer = DX12BufferCreate(Device, D3D12_RESOURCE_STATE_COMMON, D3D12_HEAP_TYPE_DEFAULT, c_MaxQuadIndices * sizeof(u32));

            // Prepare for upload
            D3D12_SUBRESOURCE_DATA SubresourceData = {};
            SubresourceData.pData = QuadIndices;
            SubresourceData.RowPitch = c_MaxQuadIndices * sizeof(u32);
            SubresourceData.SlicePitch = SubresourceData.RowPitch;

            // Copy indices
            {
                dx12_buffer Intermediate = DX12BufferCreate(Device, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_HEAP_TYPE_UPLOAD, c_MaxQuadIndices * sizeof(u32));
                void* MappedPtr = nullptr;
                Intermediate.Resource->Map(0, nullptr, &MappedPtr);
                memcpy(MappedPtr, QuadIndices, c_MaxQuadIndices * sizeof(u32));
                Intermediate.Resource->Unmap(0, nullptr);

                Renderer->CopyCommandList->CopyBufferRegion(Renderer->QuadIndexBuffer.Resource, 0, Intermediate.Resource, 0, c_MaxQuadIndices * sizeof(u32));
                Renderer->CopyCommandList->Close();
                Renderer->CopyCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&Renderer->CopyCommandList);

                // TODO: Wait for copy command to finish
                //DX12BufferDestroy(&Intermediate);
            }
        }
    }
}

static dx12_game_renderer CreateDX12GameRenderer(game_window Window)
{
    dx12_game_renderer Renderer = {};

    DX12RendererInitializeContext(&Renderer, Window);
    DX12RendererInitializePipeline(&Renderer);

    return Renderer;
}

static void DX12RendererResizeSwapChain(dx12_game_renderer* Renderer, u32 RequestWidth, u32 RequestHeight)
{
    // Flush the GPU queue to make sure the swap chain's back buffers are not being referenced by an in-flight command list.
    DX12RendererFlush(Renderer);

    // Reset fence values and release back buffers
    for (u32 i = 0; i < FIF; i++)
    {
        Renderer->BackBuffers[i]->Release();
        Renderer->FrameFenceValues[i] = Renderer->FrameFenceValues[Renderer->CurrentBackBufferIndex];
    }

    // Recreate depth buffer
    {
        Renderer->DepthBuffer->Release();

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
            IID_PPV_ARGS(&Renderer->DepthBuffer)
        ));

        // Update the depth-stencil view.
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
        dsv.Format = DXGI_FORMAT_D32_FLOAT;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsv.Texture2D.MipSlice = 0;
        dsv.Flags = D3D12_DSV_FLAG_NONE;

        Renderer->Device->CreateDepthStencilView(Renderer->DepthBuffer, &dsv, Renderer->DepthStencilHeap->GetCPUDescriptorHandleForHeapStart());
    }

    DXGI_SWAP_CHAIN_DESC SwapChainDesc;
    DxAssert(Renderer->SwapChain->GetDesc(&SwapChainDesc));
    DxAssert(Renderer->SwapChain->ResizeBuffers(FIF, RequestWidth, RequestHeight, SwapChainDesc.BufferDesc.Format, SwapChainDesc.Flags));
    DxAssert(Renderer->SwapChain->GetDesc(&SwapChainDesc));

    Renderer->CurrentBackBufferIndex = Renderer->SwapChain->GetCurrentBackBufferIndex();

    Renderer->RTVDescriptorSize = Renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = Renderer->RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

    for (u32 i = 0; i < FIF; ++i)
    {
        ID3D12Resource* backBuffer;
        DxAssert(Renderer->SwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

        Renderer->Device->CreateRenderTargetView(backBuffer, nullptr, rtvHandle);

        Renderer->BackBuffers[i] = backBuffer;

        rtvHandle.ptr += Renderer->RTVDescriptorSize;
    }

    Warn("SwapChain resized to %d %d", RequestWidth, RequestHeight);
}

static void SubmitQuad(dx12_game_renderer* Renderer, v3 Translation, v3 Rotation, v3 Scale, v4 Color)
{
    m4 Transform = MyMath::Translate(m4(1.0f), Translation)
        * MyMath::ToM4(QTN(Rotation))
        * MyMath::Scale(m4(1.0f), Scale);

    for (u32 i = 0; i < CountOf(c_QuadVertexPositions); i++)
    {
        Renderer->QuadVertexDataPtr->Position = v3(Transform * c_QuadVertexPositions[i]);
        Renderer->QuadVertexDataPtr->Color = Color;
        Renderer->QuadVertexDataPtr++;
    }

    Renderer->QuadIndexCount += 6;
}

static void SubmitCube(dx12_game_renderer* Renderer, v3 Translation, v3 Rotation, v3 Scale, v4 Color)
{
    m4 Transform = MyMath::Translate(m4(1.0f), Translation)
        * MyMath::ToM4(QTN(Rotation))
        * MyMath::Scale(m4(1.0f), Scale);

    for (u32 i = 0; i < CountOf(c_CubeVertexPositions); i++)
    {
        Renderer->QuadVertexDataPtr->Position = v3(Transform * c_CubeVertexPositions[i]);
        Renderer->QuadVertexDataPtr->Color = Color;
        Renderer->QuadVertexDataPtr++;
    }

    Renderer->QuadIndexCount += 36;
}

static void DX12RendererRender(dx12_game_renderer* Renderer, m4 ViewProjection, u32 Width, u32 Height)
{
    // End render

    // Reset state
    auto DirectCommandAllocator = Renderer->DirectCommandAllocators[Renderer->CurrentBackBufferIndex];

    //Trace("%d", Renderer->CurrentBackBufferIndex);
    auto BackBuffer = Renderer->BackBuffers[Renderer->CurrentBackBufferIndex];
    auto CommandList = Renderer->DirectCommandList;

    DirectCommandAllocator->Reset();
    CommandList->Reset(DirectCommandAllocator, Renderer->QuadPipelineState);

    // Frame that was presented needs to be set to render target again
    auto Barrier = DX12RendererTransition(BackBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    CommandList->ResourceBarrier(1, &Barrier);

    v4 ClearColor = { 0.8f, 0.5f, 0.9f, 1.0f };
    //v4 ClearColor = { 0.2f, 0.3f, 0.8f, 1.0f };

    auto RTV = Renderer->RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    RTV.ptr += u64(Renderer->CurrentBackBufferIndex * Renderer->RTVDescriptorSize);

    auto DSV = Renderer->DepthStencilHeap->GetCPUDescriptorHandleForHeapStart();
    CommandList->ClearDepthStencilView(DSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // PrePass
    {
        // Copy vertex data to gpu buffer
        DX12VertexBufferSendData(&Renderer->QuadVertexBuffer, Renderer->DirectCommandList, Renderer->QuadVertexDataBase, sizeof(quad_vertex) * Renderer->QuadIndexCount);
    }

    // Render

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

    CommandList->ClearRenderTargetView(RTV, &ClearColor.x, 0, nullptr);
    CommandList->OMSetRenderTargets(1, &RTV, false, &DSV);
    CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Set root constants
    // Number of 32 bit values - 16 floats in 4x4 matrix
    CommandList->SetGraphicsRootSignature(Renderer->RootSignature);
    CommandList->SetGraphicsRoot32BitConstants(0, 16, &ViewProjection, 0);

    // Quads
    if (Renderer->QuadIndexCount > 0)
    {
        // Bind pipeline
        CommandList->SetPipelineState(Renderer->QuadPipelineState);

        // Bind vertex buffer
        static D3D12_VERTEX_BUFFER_VIEW QuadVertexBufferView;
        QuadVertexBufferView.BufferLocation = Renderer->QuadVertexBuffer.Buffer.Resource->GetGPUVirtualAddress();
        QuadVertexBufferView.StrideInBytes = sizeof(quad_vertex);
        QuadVertexBufferView.SizeInBytes = Renderer->QuadIndexCount * sizeof(quad_vertex);
        CommandList->IASetVertexBuffers(0, 1, &QuadVertexBufferView);

        // Bind index buffer
        static D3D12_INDEX_BUFFER_VIEW QuadIndexBufferView;
        QuadIndexBufferView.BufferLocation = Renderer->QuadIndexBuffer.Resource->GetGPUVirtualAddress();
        QuadIndexBufferView.Format = DXGI_FORMAT_R32_UINT;
        QuadIndexBufferView.SizeInBytes = c_MaxQuadIndices * sizeof(u32); // Maybe we could assign this value to match indices drawn instead of viewing the whole buffer
        CommandList->IASetIndexBuffer(&QuadIndexBufferView);

        // Issue draw call
        CommandList->DrawIndexedInstanced(Renderer->QuadIndexCount, 1, 0, 0, 0);
    }

    // Present transition
    {
        // Rendered frame needs to be transitioned to present state
        auto Barrier = DX12RendererTransition(BackBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        CommandList->ResourceBarrier(1, &Barrier);
    }

    // Finalize the command list
    CommandList->Close();

    Renderer->DirectCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&CommandList);

    Renderer->FrameFenceValues[Renderer->CurrentBackBufferIndex] = DX12RendererSignal(Renderer->DirectCommandQueue, Renderer->Fence, &Renderer->FenceValue);

    Renderer->SwapChain->Present(Renderer->VSync ? 1 : 0, 0);

    Renderer->CurrentBackBufferIndex = Renderer->SwapChain->GetCurrentBackBufferIndex();

    // Wait for GPU to finish presenting
    DX12RendererWaitForFenceValue(Renderer->Fence, Renderer->FrameFenceValues[Renderer->CurrentBackBufferIndex], Renderer->DirectFenceEvent);

    // Reset indices
    Renderer->QuadIndexCount = 0;
    Renderer->QuadVertexDataPtr = Renderer->QuadVertexDataBase;
}
