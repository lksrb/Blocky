/*
 * Note that this renderer is game specific so it does not have to be completely api-agnostic,
 * For example instead of having general set push constants functions, we can have set view projection function etc.
 * We will see in the future if this is a good approach
 */
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
#include "DX12Texture.h"

// Basically a push constant
// TODO: Whats the size of this
struct dx12_root_signature_constant_buffer
{
    m4 ViewProjection;
};

struct game_renderer
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

    ID3D12Resource* DepthBuffers[FIF];
    ID3D12DescriptorHeap* DSVDescriptorHeap;
    u32 DSVDescriptorSize;

    bool DepthTesting = true;

    bool VSync = true;

    // Fence
    ID3D12Fence* Fence;
    u64 FenceValue;
    u64 FrameFenceValues[FIF];
    HANDLE DirectFenceEvent;

    // Describes resources used in the shader
    ID3D12RootSignature* RootSignature;
    dx12_root_signature_constant_buffer RootSignatureBuffer;

    D3D12_VIEWPORT Viewport;
    D3D12_RECT ScissorRect;

    ID3D12DescriptorHeap* SRVDescriptorHeap;
    dx12_texture WhiteTexture;

    // Quad
    ID3D12PipelineState* QuadPipelineState;
    dx12_vertex_buffer QuadVertexBuffer[FIF];
    dx12_buffer QuadIndexBuffer;
    quad_vertex QuadVertexDataBase[c_MaxQuadVertices];
    quad_vertex* QuadVertexDataPtr = QuadVertexDataBase;
    u32 QuadIndexCount = 0;
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
            Warn("Failed to get message length: HRESULT = 0x%08X", HR);
            continue;
        }

        // Allocate memory for the message
        auto Message = static_cast<D3D12_MESSAGE*>(alloca(MessageLength));
        if (!Message)
        {
            Warn("Failed to allocate memory for message.");
            continue;
        }

        // Retrieve the message
        HR = InfoQueue->GetMessage(i, Message, &MessageLength);
        if (FAILED(HR))
        {
            Warn("Failed to get message: HRESULT = 0x%08X", HR);
            continue;
        }

        switch (Message->Severity)
        {
            case D3D12_MESSAGE_SEVERITY_MESSAGE:
            {
                Trace("[DX12]%s", Message->pDescription);
                break;
            }
            case D3D12_MESSAGE_SEVERITY_INFO:
            {
                Info("[DX12]%s", Message->pDescription);
                break;
            }
            case D3D12_MESSAGE_SEVERITY_WARNING:
            {
                Warn("[DX12]%s", Message->pDescription);
                break;
            }
            case D3D12_MESSAGE_SEVERITY_ERROR:
            case D3D12_MESSAGE_SEVERITY_CORRUPTION:
            {
                Err("[DX12]%s", Message->pDescription);
                break;
            }
        }
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

static void DX12RendererFlush(game_renderer* Renderer)
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

static void DX12RendererInitializeContext(game_renderer* Renderer, game_window Window)
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
            //DxAssert(Renderer->DirectCommandList->Close());
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
            DxAssert(Renderer->DirectCommandList->Close());
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

    // Create Render Target Views
    {
        // Create a Descriptor Heap
        D3D12_DESCRIPTOR_HEAP_DESC Desc = {};
        Desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        Desc.NumDescriptors = FIF;
        Desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        Desc.NodeMask = 1;
        DxAssert(Renderer->Device->CreateDescriptorHeap(&Desc, IID_PPV_ARGS(&Renderer->RTVDescriptorHeap)));

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

    // Create depth resources
    {
        // Create Depth Stencil Heap
        ID3D12DescriptorHeap* DSVHeap = nullptr;

        // Create the descriptor heap for the depth-stencil view.
        D3D12_DESCRIPTOR_HEAP_DESC DsvHeapDesc = {};
        DsvHeapDesc.NumDescriptors = FIF;
        DsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        DsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        DxAssert(Renderer->Device->CreateDescriptorHeap(&DsvHeapDesc, IID_PPV_ARGS(&Renderer->DSVDescriptorHeap)));

        D3D12_CPU_DESCRIPTOR_HANDLE DsvHandle = Renderer->DSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        Renderer->DSVDescriptorSize = Renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

        // Create depth buffers
        for (u32 i = 0; i < FIF; i++)
        {
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
            DsvHandle.ptr += Renderer->DSVDescriptorSize;
        }
    }

    // Creating the shader resource view descriptor
    {
        D3D12_DESCRIPTOR_HEAP_DESC Desc = {};
        Desc.NumDescriptors = 1;  // Number of SRVs (e.g., 1 texture)
        Desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        Desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; // Must be visible to shaders
        Desc.NodeMask = 0;
        DxAssert(Renderer->Device->CreateDescriptorHeap(&Desc, IID_PPV_ARGS(&Renderer->SRVDescriptorHeap)));
    }
}

static void DX12RendererInitializePipeline(game_renderer* Renderer)
{
    auto Device = Renderer->Device;

    // Root Signature
    {
        // Sampler
        D3D12_STATIC_SAMPLER_DESC Sampler = {};
        Sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        Sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        Sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        Sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        Sampler.MipLODBias = 0;
        Sampler.MaxAnisotropy = 0;
        Sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        Sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        Sampler.MinLOD = 0.0f;
        Sampler.MaxLOD = D3D12_FLOAT32_MAX;
        Sampler.ShaderRegister = 0;
        Sampler.RegisterSpace = 0;
        Sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_DESCRIPTOR_RANGE Ranges[1];
        Ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        Ranges[0].NumDescriptors = 1;
        Ranges[0].BaseShaderRegister = 0;
        Ranges[0].RegisterSpace = 0;
        //Ranges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;

        D3D12_ROOT_PARAMETER Parameters[2] = {};
        Parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        Parameters[0].Constants.Num32BitValues = 16;  // 4x4 matrix (16 floats)
        Parameters[0].Constants.ShaderRegister = 0;  // Matches b0 in HLSL
        Parameters[0].Constants.RegisterSpace = 0;
        Parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

        // Descriptor table
        Parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        Parameters[1].DescriptorTable.NumDescriptorRanges = CountOf(Ranges);
        Parameters[1].DescriptorTable.pDescriptorRanges = Ranges;
        Parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

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
                { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC PipelineDesc = {};

            // Rasterizer state
            PipelineDesc.RasterizerState = {};
            PipelineDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            PipelineDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
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
            DX12RendererDumpInfoQueue(Renderer->DebugInfoQueue);
        }

        // Quad
        {
            for (u32 i = 0; i < FIF; i++)
            {
                Renderer->QuadVertexBuffer[i] = DX12VertexBufferCreate(Device, sizeof(quad_vertex) * c_MaxQuadVertices);
            }

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

            // Copy indices
            {
                dx12_buffer Intermediate = DX12BufferCreate(Device, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_HEAP_TYPE_UPLOAD, c_MaxQuadIndices * sizeof(u32));
                void* MappedPtr = nullptr;
                Intermediate.Resource->Map(0, nullptr, &MappedPtr);
                memcpy(MappedPtr, QuadIndices, c_MaxQuadIndices * sizeof(u32));
                Intermediate.Resource->Unmap(0, nullptr);

                Renderer->CopyCommandList->CopyBufferRegion(Renderer->QuadIndexBuffer.Resource, 0, Intermediate.Resource, 0, c_MaxQuadIndices * sizeof(u32));

               

                // TODO: Wait for copy command to finish
                //DX12BufferDestroy(&Intermediate);
            }
        }

        // White texture
        {
            Renderer->WhiteTexture = DX12TextureCreate(Device, Renderer->CopyCommandList, "Resources/Textures/background.png");
            Renderer->CopyCommandList->Close();
            Renderer->CopyCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&Renderer->CopyCommandList);

            // Describe and create a SRV for the texture.
            D3D12_SHADER_RESOURCE_VIEW_DESC SrvDesc = {};
            SrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            SrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            SrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            SrvDesc.Texture2D.MipLevels = 1;
            Device->CreateShaderResourceView(Renderer->WhiteTexture.Resource, &SrvDesc, Renderer->SRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
        }
    }
}

static game_renderer CreateDX12GameRenderer(game_window Window)
{
    game_renderer Renderer = {};

    DX12RendererInitializeContext(&Renderer, Window);
    DX12RendererInitializePipeline(&Renderer);

    return Renderer;
}

static void DX12GameRendererDestroy(game_renderer* Renderer)
{
    // Wait for GPU to finish
    DX12RendererFlush(Renderer);

    IDXGIDebug1* dxgiDebug;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
    {
        dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_DETAIL);
        DX12RendererDumpInfoQueue(Renderer->DebugInfoQueue);
    }
}

static void DX12RendererResizeSwapChain(game_renderer* Renderer, u32 RequestWidth, u32 RequestHeight)
{
    // Flush the GPU queue to make sure the swap chain's back buffers are not being referenced by an in-flight command list.
    DX12RendererFlush(Renderer);

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

    // Recreate depth buffers
    {
        D3D12_CPU_DESCRIPTOR_HANDLE DsvHandle = Renderer->DSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        Renderer->DSVDescriptorSize = Renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

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
            DsvHandle.ptr += Renderer->DSVDescriptorSize;
        }
    }

    Warn("SwapChain resized to %d %d", RequestWidth, RequestHeight);
}

static void GameRendererSetViewProjection(game_renderer* Renderer, m4 ViewProjection)
{
    Renderer->RootSignatureBuffer.ViewProjection = ViewProjection;
}

static void GameRendererSubmitQuad(game_renderer* Renderer, v3 Translation, v3 Rotation, v3 Scale, v4 Color)
{
    m4 Transform = bkm::Translate(m4(1.0f), Translation)
        * bkm::ToM4(qtn(Rotation))
        * bkm::Scale(m4(1.0f), Scale);

    for (u32 i = 0; i < CountOf(c_QuadVertexPositions); i++)
    {
        Renderer->QuadVertexDataPtr->Position = v3(Transform * c_QuadVertexPositions[i]);
        Renderer->QuadVertexDataPtr->Color = Color;
        Renderer->QuadVertexDataPtr++;
    }

    Renderer->QuadIndexCount += 6;
}

static void GameRendererSubmitCube(game_renderer* Renderer, v3 Translation, v3 Rotation, v3 Scale, v4 Color)
{
    m4 Transform = bkm::Translate(m4(1.0f), Translation)
        * bkm::ToM4(qtn(Rotation))
        * bkm::Scale(m4(1.0f), Scale);

    v2 Coords[4];
    Coords[0] = { 0.0f, 0.0f };
    Coords[1] = { 1.0f, 0.0f };
    Coords[2] = { 1.0f, 1.0f };
    Coords[3] = { 0.0f, 1.0f };


    for (u32 i = 0; i < CountOf(c_CubeVertexPositions); i++)
    {
        Renderer->QuadVertexDataPtr->Position = v3(Transform * c_CubeVertexPositions[i]);
        Renderer->QuadVertexDataPtr->Color = Color;
        Renderer->QuadVertexDataPtr->TexCoord = Coords[i % 4];
        Renderer->QuadVertexDataPtr++;
    }

    Renderer->QuadIndexCount += 36;
}

static void DX12RendererRender(game_renderer* Renderer, u32 Width, u32 Height)
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

    //v4 ClearColor = { 0.8f, 0.5f, 0.9f, 1.0f };
    v4 ClearColor = { 0.2f, 0.3f, 0.8f, 1.0f };

    // Get current render target view
    auto RTV = Renderer->RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    RTV.ptr += u64(Renderer->CurrentBackBufferIndex * Renderer->RTVDescriptorSize);

    // Get current depth stencil view
    auto DSV = Renderer->DSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    DSV.ptr += u64(Renderer->CurrentBackBufferIndex * Renderer->DSVDescriptorSize);

    CommandList->ClearDepthStencilView(DSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // PrePass
    {
        // Copy vertex data to gpu buffer
        DX12VertexBufferSendData(&Renderer->QuadVertexBuffer[Renderer->CurrentBackBufferIndex], Renderer->DirectCommandList, Renderer->QuadVertexDataBase, sizeof(quad_vertex) * Renderer->QuadIndexCount);
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

    CommandList->SetDescriptorHeaps(1, (ID3D12DescriptorHeap* const*)&Renderer->SRVDescriptorHeap);
    CommandList->SetGraphicsRoot32BitConstants(0, 16, &Renderer->RootSignatureBuffer.ViewProjection, 0);

    // Quads
    if (Renderer->QuadIndexCount > 0)
    {
        // Bind pipeline
        CommandList->SetPipelineState(Renderer->QuadPipelineState);

        // Bind vertex buffer
        static D3D12_VERTEX_BUFFER_VIEW QuadVertexBufferView;
        QuadVertexBufferView.BufferLocation = Renderer->QuadVertexBuffer[Renderer->CurrentBackBufferIndex].Buffer.Resource->GetGPUVirtualAddress();
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
