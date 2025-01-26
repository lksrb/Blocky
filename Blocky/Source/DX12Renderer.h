#pragma once

// DirectX 12
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")

// D3D12 extension
//#include <d3dx12.h>

#define HAssert(x) Assert(SUCCEEDED(x), #x)

#define DX12_ENABLE_DEBUG_LAYER

#ifdef DX12_ENABLE_DEBUG_LAYER
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")

#endif

#include <d3dx12.h>

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
    ID3D12CommandQueue* CommandQueue;
    IDXGISwapChain4* SwapChain;
    ID3D12Resource* BackBuffers[FIF];
    ID3D12GraphicsCommandList2* CommandList;
    ID3D12CommandAllocator* CommandAllocators[FIF];
    ID3D12DescriptorHeap* RTVDescriptorHeap;
    u32 RTVDescriptorSize;
    u32 CurrentBackBufferIndex;

    bool VSync = true;

    // Fence
    ID3D12Fence* Fence;
    u64 FenceValue;
    u64 FrameFenceValues[FIF];
    HANDLE FenceEvent;

    // Describes resources used in the shader
    ID3D12RootSignature* RootSignature;

    // Describes the rendering process
    ID3D12PipelineState* PipelineState;

    D3D12_VIEWPORT Viewport;
    D3D12_RECT ScissorRect;

    // Resources
    ID3D12Resource* VertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW VertexBufferView;
};

static void DX12RendererDumpInfoQueue(dx12_game_renderer* Renderer)
{
    auto InfoQueue = Renderer->DebugInfoQueue;
    const UINT64 messageCount = InfoQueue->GetNumStoredMessages();

    for (UINT64 i = 0; i < messageCount; ++i)
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
    HAssert(CommandQueue->Signal(Fence, FenceValueForSignal));
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

static void DX12RendererFlush(ID3D12CommandQueue* commandQueue, ID3D12Fence* Fence, uint64_t* FenceValue, HANDLE FenceEvent)
{
    u64 FenceValueForSignal = DX12RendererSignal(commandQueue, Fence, FenceValue);
    DX12RendererWaitForFenceValue(Fence, FenceValueForSignal, FenceEvent);
};

static D3D12_RESOURCE_BARRIER  DX12RendererTransition(
   _In_ ID3D12Resource* pResource,
   D3D12_RESOURCE_STATES stateBefore,
   D3D12_RESOURCE_STATES stateAfter,
   UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
   D3D12_RESOURCE_BARRIER_FLAGS flags = D3D12_RESOURCE_BARRIER_FLAG_NONE)
{
    D3D12_RESOURCE_BARRIER result;
    result.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    result.Flags = flags;
    result.Transition.pResource = pResource;
    result.Transition.StateBefore = stateBefore;
    result.Transition.StateAfter = stateAfter;
    result.Transition.Subresource = subresource;
    return result;
};

void GetHardwareAdapter(
    IDXGIFactory1* pFactory,
    IDXGIAdapter1** ppAdapter,
    bool requestHighPerformanceAdapter = true)
{
    *ppAdapter = nullptr;

    IDXGIAdapter1* adapter;

    IDXGIFactory6* factory6;
    if (SUCCEEDED(pFactory->QueryInterface(IID_PPV_ARGS(&factory6))))
    {
        for (
            UINT adapterIndex = 0;
            SUCCEEDED(factory6->EnumAdapterByGpuPreference(
                adapterIndex,
                requestHighPerformanceAdapter == true ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED,
                IID_PPV_ARGS(&adapter)));
                ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                // Don't select the Basic Render Driver adapter.
                // If you want a software adapter, pass in "/warp" on the command line.
                continue;
            }

            // Check to see whether the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
            {
                break;
            }
        }
    }

    if (adapter == nullptr)
    {
        for (UINT adapterIndex = 0; SUCCEEDED(pFactory->EnumAdapters1(adapterIndex, &adapter)); ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                // Don't select the Basic Render Driver adapter.
                // If you want a software adapter, pass in "/warp" on the command line.
                continue;
            }

            // Check to see whether the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
            {
                break;
            }
        }
    }

    *ppAdapter = adapter;
}

static dx12_game_renderer CreateDX12GameRenderer(game_window Window)
{
    dx12_game_renderer Renderer = {};

    // Enable debug layer
    // This is sort of like validation layers in Vulkan
    HAssert(D3D12GetDebugInterface(IID_PPV_ARGS(&Renderer.DebugInterface)));
    Renderer.DebugInterface->EnableDebugLayer();

    // Create device
    {
        // NOTE: pAdapter is optional, when nullptr it defaultly selects one

        IDXGIFactory4* factory;
        HAssert(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory)));

        IDXGIAdapter1* hardwareAdapter;
        GetHardwareAdapter(factory, &hardwareAdapter);
        HAssert(D3D12CreateDevice(hardwareAdapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Renderer.Device)));
    }

    // Setup debug interface
    {
        Renderer.Device->QueryInterface(IID_PPV_ARGS(&Renderer.DebugInfoQueue));
        Renderer.DebugInfoQueue->QueryInterface(IID_PPV_ARGS(&Renderer.DebugInfoQueue));
        Renderer.DebugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
        Renderer.DebugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        Renderer.DebugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, false);
        Renderer.DebugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_INFO, false);
        //InfoQueue->Release();
        //Renderer.DebugInterface->Release();

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
            NewFilter.DenyList.NumSeverities = _countof(Severities);
            NewFilter.DenyList.pSeverityList = Severities;
            NewFilter.DenyList.NumIDs = _countof(DenyIds);
            NewFilter.DenyList.pIDList = DenyIds;

            HAssert(Renderer.DebugInfoQueue->PushStorageFilter(&NewFilter));
        }
    }

    // Create command queue
    {
        D3D12_COMMAND_QUEUE_DESC desc = {};
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; // Most general - For draw, compute and copy commands
        desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        desc.NodeMask = 0;

        HAssert(Renderer.Device->CreateCommandQueue(&desc, IID_PPV_ARGS(&Renderer.CommandQueue)));
    }

    // Create swapchain
    {
        IDXGISwapChain1* SwapChain1;
        IDXGIFactory4* Factory4;
        HAssert(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&Factory4)));

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

        HAssert(Factory4->CreateSwapChainForHwnd(Renderer.CommandQueue, Window.WindowHandle, &swapChainDesc, nullptr, nullptr, &SwapChain1));

        // Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
        // will be handled manually.
        HAssert(Factory4->MakeWindowAssociation(Window.WindowHandle, DXGI_MWA_NO_ALT_ENTER));
        SwapChain1->QueryInterface(IID_PPV_ARGS(&Renderer.SwapChain));
        SwapChain1->Release();
        Factory4->Release();

        Renderer.SwapChain->SetMaximumFrameLatency(FIF);

        Renderer.CurrentBackBufferIndex = Renderer.SwapChain->GetCurrentBackBufferIndex();
    }

    // Create a Descriptor Heap
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = FIF;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask = 1;
        HAssert(Renderer.Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&Renderer.RTVDescriptorHeap)));
    }

    // Update Render Target Views
    {
        Renderer.RTVDescriptorSize = Renderer.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle = Renderer.RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

        for (u32 i = 0; i < FIF; ++i)
        {
            ID3D12Resource* BackBuffer;
            HAssert(Renderer.SwapChain->GetBuffer(i, IID_PPV_ARGS(&BackBuffer)));

            Renderer.Device->CreateRenderTargetView(BackBuffer, nullptr, RtvHandle);

            Renderer.BackBuffers[i] = BackBuffer;

            RtvHandle.ptr += Renderer.RTVDescriptorSize;
        }
    }

    // Create command allocator
    for (u32 i = 0; i < FIF; i++)
    {
        HAssert(Renderer.Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&Renderer.CommandAllocators[i])));
    }

    // NOTE: Basically the same as vulkan command buffer pool
    // Create a command list
    HAssert(Renderer.Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, Renderer.CommandAllocators[Renderer.CurrentBackBufferIndex], nullptr, IID_PPV_ARGS(&Renderer.CommandList)));
    HAssert(Renderer.CommandList->Close());

    // Fence
    HAssert(Renderer.Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Renderer.Fence)));
    Renderer.FenceEvent = CreateEvent(nullptr, false, false, nullptr);
    Assert(Renderer.FenceEvent != INVALID_HANDLE_VALUE, "Could not create fence event.");

    // RESOURCE STUFF

    // Root signature
    {
        /*  D3D12_ROOT_SIGNATURE_DESC Desc = {};
          Desc.NumParameters = 0;
          Desc.pParameters = nullptr;
          Desc.NumStaticSamplers = 0;
          Desc.pStaticSamplers = nullptr;
          Desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

         */

        ID3DBlob* Error;
        ID3DBlob* Signature;
        CD3DX12_ROOT_SIGNATURE_DESC Desc;
        Desc.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        HAssert(D3D12SerializeRootSignature(&Desc, D3D_ROOT_SIGNATURE_VERSION_1, &Signature, &Error));

        HAssert(Renderer.Device->CreateRootSignature(0, Signature->GetBufferPointer(), Signature->GetBufferSize(), IID_PPV_ARGS(&Renderer.RootSignature)));
    }

    // Shaders
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

        HAssert(D3DCompileFromFile(L"Resources/Shader.hlsl", nullptr, nullptr, "VSMain", "vs_5_1", CompileFlags, 0, &VertexShader, &ErrorMessage));
        //Err("%s", ErrorMessage->GetBufferPointer());
        HAssert(D3DCompileFromFile(L"Resources/Shader.hlsl", nullptr, nullptr, "PSMain", "ps_5_1", CompileFlags, 0, &PixelShader, &ErrorMessage));

        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC InputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        //// Describe and create the graphics pipeline state object (PSO).
        //D3D12_GRAPHICS_PIPELINE_STATE_DESC PipelineDesc = {};
        //PipelineDesc.InputLayout = { InputElementDescs, CountOf(InputElementDescs) };
        //PipelineDesc.pRootSignature = Renderer.RootSignature;
        //PipelineDesc.VS = { reinterpret_cast<UINT8*>(VertexShader->GetBufferPointer()), VertexShader->GetBufferSize() };
        //PipelineDesc.PS = { reinterpret_cast<UINT8*>(PixelShader->GetBufferPointer()), PixelShader->GetBufferSize() };
        //PipelineDesc.DepthStencilState = {};
        //PipelineDesc.DepthStencilState.DepthEnable = FALSE;
        //PipelineDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        //PipelineDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        //PipelineDesc.DepthStencilState.StencilEnable = FALSE;

        //// Rasterizer state
        //D3D12_RASTERIZER_DESC RasterizerStateDesc = {};
        //RasterizerStateDesc.FillMode = D3D12_FILL_MODE_SOLID;
        //RasterizerStateDesc.CullMode = D3D12_CULL_MODE_BACK;
        //RasterizerStateDesc.FrontCounterClockwise = FALSE;
        //RasterizerStateDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        //RasterizerStateDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        //RasterizerStateDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        //RasterizerStateDesc.DepthClipEnable = TRUE;
        //RasterizerStateDesc.MultisampleEnable = FALSE;
        //RasterizerStateDesc.AntialiasedLineEnable = FALSE;
        //RasterizerStateDesc.ForcedSampleCount = 0;
        //RasterizerStateDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        //// Blend state
        //D3D12_BLEND_DESC BlendStateDest = {};
        //BlendStateDest.AlphaToCoverageEnable = FALSE;
        //BlendStateDest.IndependentBlendEnable = FALSE;
        //const D3D12_RENDER_TARGET_BLEND_DESC DefaultRenderTargetBlendDesc =
        //{
        //    FALSE,FALSE,
        //    D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        //    D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        //    D3D12_LOGIC_OP_NOOP,
        //    D3D12_COLOR_WRITE_ENABLE_ALL,
        //};
        //for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        //    BlendStateDest.RenderTarget[i] = DefaultRenderTargetBlendDesc;


        D3D12_GRAPHICS_PIPELINE_STATE_DESC PipelineDesc = {};
        PipelineDesc.InputLayout = { InputElementDescs, CountOf(InputElementDescs) };
        PipelineDesc.pRootSignature = Renderer.RootSignature;
        PipelineDesc.VS = CD3DX12_SHADER_BYTECODE(VertexShader);
        PipelineDesc.PS = CD3DX12_SHADER_BYTECODE(PixelShader);
        PipelineDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        PipelineDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        PipelineDesc.SampleMask = UINT_MAX;
        PipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        PipelineDesc.NumRenderTargets = 1;
        PipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        PipelineDesc.SampleDesc.Count = 1;
        auto P = &Renderer.Device;

        DX12RendererDumpInfoQueue(&Renderer);
        HRESULT HR = Renderer.Device->CreateGraphicsPipelineState(&PipelineDesc, IID_PPV_ARGS(&Renderer.PipelineState));
        DX12RendererDumpInfoQueue(&Renderer);

    }

    return Renderer;
}

static void DX12RendererResizeSwapChain(dx12_game_renderer* Renderer, u32 RequestWidth, u32 RequestHeight)
{
    // Flush the GPU queue to make sure the swap chain's back buffers are not being referenced by an in-flight command list.
    DX12RendererFlush(Renderer->CommandQueue, Renderer->Fence, &Renderer->FenceValue, Renderer->FenceEvent);

    // Reset fence values
    for (u32 i = 0; i < FIF; i++)
    {
        Renderer->BackBuffers[i]->Release();
        Renderer->FrameFenceValues[i] = Renderer->FrameFenceValues[Renderer->CurrentBackBufferIndex];
    }

    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    HAssert(Renderer->SwapChain->GetDesc(&swapChainDesc));
    HAssert(Renderer->SwapChain->ResizeBuffers(FIF, RequestWidth, RequestHeight, swapChainDesc.BufferDesc.Format, swapChainDesc.Flags));

    Renderer->CurrentBackBufferIndex = Renderer->SwapChain->GetCurrentBackBufferIndex();

    Renderer->RTVDescriptorSize = Renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = Renderer->RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

    for (u32 i = 0; i < FIF; ++i)
    {
        ID3D12Resource* backBuffer;
        HAssert(Renderer->SwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

        Renderer->Device->CreateRenderTargetView(backBuffer, nullptr, rtvHandle);

        Renderer->BackBuffers[i] = backBuffer;

        rtvHandle.ptr += Renderer->RTVDescriptorSize;
    }

    Warn("SwapChain resized to %d %d", RequestWidth, RequestHeight);

}

static void DX12RendererRender(dx12_game_renderer* Renderer)
{
    // Reset state
    auto CommandAllocator = Renderer->CommandAllocators[Renderer->CurrentBackBufferIndex];
    //Trace("%d", Renderer->CurrentBackBufferIndex);
    auto BackBuffer = Renderer->BackBuffers[Renderer->CurrentBackBufferIndex];
    auto CommandList = Renderer->CommandList;

    CommandAllocator->Reset();
    Renderer->CommandList->Reset(CommandAllocator, nullptr);

    // Frame that was presented needs to be set to render target again
    auto Barrier = DX12RendererTransition(BackBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    CommandList->ResourceBarrier(1, &Barrier);

    v4 ClearColor = { 0.8f, 0.5f, 0.9f, 1.0f };

    auto RTV = Renderer->RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    RTV.ptr += u64(Renderer->CurrentBackBufferIndex * Renderer->RTVDescriptorSize);
    CommandList->ClearRenderTargetView(RTV, &ClearColor.x, 0, nullptr);

    // Present
    {
        // Rendered frame needs to be transitioned to present state
        auto barrier = DX12RendererTransition(BackBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        CommandList->ResourceBarrier(1, &barrier);
    }

    // Finalize the command list
    CommandList->Close();

    Renderer->CommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&CommandList);

    Renderer->FrameFenceValues[Renderer->CurrentBackBufferIndex] = DX12RendererSignal(Renderer->CommandQueue, Renderer->Fence, &Renderer->FenceValue);

    Renderer->SwapChain->Present(Renderer->VSync ? 1 : 0, 0);

    Renderer->CurrentBackBufferIndex = Renderer->SwapChain->GetCurrentBackBufferIndex();

    // Wait for GPU to finish presenting
    DX12RendererWaitForFenceValue(Renderer->Fence, Renderer->FrameFenceValues[Renderer->CurrentBackBufferIndex], Renderer->FenceEvent);
}
