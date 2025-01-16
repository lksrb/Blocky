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

struct dx12_game_renderer
{
    ID3D12Device2* Device;
    ID3D12Debug* DebugInterface;
    ID3D12CommandQueue* CommandQueue;
    IDXGISwapChain4* SwapChain;
    ID3D12Resource* BackBuffers[FIF];
    ID3D12GraphicsCommandList* CommandList;
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
};

static dx12_game_renderer CreateDX12GameRenderer(dx12_game_renderer& Renderer, game_window Window)
{
    // Enable debug layer
    // This is sort of like validation layers in Vulkan
    HAssert(D3D12GetDebugInterface(IID_PPV_ARGS(&Renderer.DebugInterface)));
    Renderer.DebugInterface->EnableDebugLayer();

    // Get Adapter
    // NOTE: Vulkan Physical device?

    //IDXGIFactory4* DxGiFactory;
    //HAssert(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&DxGiFactory)));
    //

    // Create device
    {
        D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
        // NOTE: pAdapter is optional, when nullptr it defaultly selects one
        HAssert(D3D12CreateDevice(nullptr, featureLevel, IID_PPV_ARGS(&Renderer.Device)));
    }

    // Setup debug interface
    {
        ID3D12InfoQueue* InfoQueue = nullptr;
        Renderer.Device->QueryInterface(IID_PPV_ARGS(&InfoQueue));
        InfoQueue->QueryInterface(IID_PPV_ARGS(&InfoQueue));

        InfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
        InfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        InfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, false);
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

            HAssert(InfoQueue->PushStorageFilter(&NewFilter));
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
        swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;// CheckTearingSupport() ? : 0;

        HAssert(Factory4->MakeWindowAssociation(Window.WindowHandle, DXGI_MWA_NO_ALT_ENTER));
        HAssert(Factory4->CreateSwapChainForHwnd(Renderer.CommandQueue, Window.WindowHandle, &swapChainDesc, nullptr, nullptr, &SwapChain1));
        SwapChain1->QueryInterface(IID_PPV_ARGS(&Renderer.SwapChain));
        SwapChain1->Release();
        Factory4->Release();

        //        Renderer.SwapChain->SetMaximumFrameLatency(FIF);

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

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = Renderer.RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

        for (u32 i = 0; i < FIF; ++i)
        {
            ID3D12Resource* backBuffer;
            HAssert(Renderer.SwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

            Renderer.Device->CreateRenderTargetView(backBuffer, nullptr, rtvHandle);

            Renderer.BackBuffers[i] = backBuffer;

            rtvHandle.ptr += Renderer.RTVDescriptorSize;
        }
    }

    // Create command allocator
    for (u32 i = 0; i < FIF; i++)
    {
        HAssert(Renderer.Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&Renderer.CommandAllocators[i])));
    }

    // NOTE: Basically the same as vulkan command buffer pool
    // Create a command list
    {
        HAssert(Renderer.Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, Renderer.CommandAllocators[Renderer.CurrentBackBufferIndex], nullptr, IID_PPV_ARGS(&Renderer.CommandList)));
        HAssert(Renderer.CommandList->Close());
    }

    // GPU sync objects

    // Fence
    {
        HAssert(Renderer.Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Renderer.Fence)));

        // Event
        Renderer.FenceEvent = CreateEvent(nullptr, false, false, nullptr);
        Assert(Renderer.FenceEvent != INVALID_HANDLE_VALUE, "Could not create fence event.");
    }

    auto Signal = [](ID3D12CommandQueue* CommandQueue, ID3D12Fence* Fence, u64* FenceValue)
    {
        u64& RefFenceValue = *FenceValue;
        u64 FenceValueForSignal = ++RefFenceValue;
        HAssert(CommandQueue->Signal(Fence, FenceValueForSignal));
        return FenceValueForSignal;
    };

    auto WaitForFenceValue = [](ID3D12Fence* Fence, u64 FenceValue, HANDLE FenceEvent, u32 Duration = UINT32_MAX)
    {
        if (Fence->GetCompletedValue() < FenceValue)
        {
            Fence->SetEventOnCompletion(FenceValue, FenceEvent);
            WaitForSingleObject(FenceEvent, Duration);
        }
    };

    auto Flush = [&](ID3D12CommandQueue* commandQueue, ID3D12Fence* Fence, uint64_t* FenceValue, HANDLE FenceEvent)
    {
        u64 FenceValueForSignal = Signal(commandQueue, Fence, FenceValue);
        WaitForFenceValue(Fence, FenceValueForSignal, FenceEvent);
    };

    auto Transition = [](
       _In_ ID3D12Resource* pResource,
       D3D12_RESOURCE_STATES stateBefore,
       D3D12_RESOURCE_STATES stateAfter,
       UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
       D3D12_RESOURCE_BARRIER_FLAGS flags = D3D12_RESOURCE_BARRIER_FLAG_NONE) -> D3D12_RESOURCE_BARRIER
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

    // Game loop
    {
        ShowWindow(Window.WindowHandle, SW_SHOW);

        LARGE_INTEGER LastCounter;
        QueryPerformanceCounter(&LastCounter);

        // NOTE: This value represent how many increments of performance counter is happening
        LARGE_INTEGER CounterFrequency;
        QueryPerformanceFrequency(&CounterFrequency);
        bool IsRunning = true;
        bool IsMinimized = false;
        f32 TimeStep = 0.0f;

        while (IsRunning)
        {
            // Process events
            MSG Message;
            while (PeekMessage(&Message, nullptr, 0, 0, PM_REMOVE))
            {
                if (Message.message == WM_QUIT)
                {
                    IsRunning = false;
                }

                TranslateMessage(&Message);
                DispatchMessage(&Message);
            }

            IsMinimized = IsIconic(Window.WindowHandle);

            // Render
            if (!IsMinimized)
            {
                // Reset state
                auto CommandAllocator = Renderer.CommandAllocators[Renderer.CurrentBackBufferIndex];
                //Trace("%d", Renderer.CurrentBackBufferIndex);
                auto BackBuffer = Renderer.BackBuffers[Renderer.CurrentBackBufferIndex];
                auto CommandList = Renderer.CommandList;

                CommandAllocator->Reset();
                Renderer.CommandList->Reset(CommandAllocator, nullptr);

                // Frame that was presented needs to be set to render target again
                auto Barrier = Transition(BackBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
                CommandList->ResourceBarrier(1, &Barrier);

                v4 ClearColor = { 0.8f, 0.2f, 0.6f, 1.0f };

                auto RTV = Renderer.RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
                RTV.ptr += u64(Renderer.CurrentBackBufferIndex * Renderer.RTVDescriptorSize);
                CommandList->ClearRenderTargetView(RTV, &ClearColor.x, 0, nullptr);

                // Present
                {
                    // Rendered frame needs to be transitioned to present state
                    auto barrier = Transition(BackBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
                    CommandList->ResourceBarrier(1, &barrier);
                }

                // Finalize the command list
                CommandList->Close();

                Renderer.CommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&CommandList);

                Renderer.FrameFenceValues[Renderer.CurrentBackBufferIndex] = Signal(Renderer.CommandQueue, Renderer.Fence, &Renderer.FenceValue);

                Renderer.SwapChain->Present(Renderer.VSync ? 1 : 0, 0);

                Renderer.CurrentBackBufferIndex = Renderer.SwapChain->GetCurrentBackBufferIndex();

                // Wait for GPU to finish presenting
                WaitForFenceValue(Renderer.Fence, Renderer.FrameFenceValues[Renderer.CurrentBackBufferIndex], Renderer.FenceEvent);
            }

            // Timestep
            LARGE_INTEGER EndCounter;
            QueryPerformanceCounter(&EndCounter);

            i64 CounterElapsed = EndCounter.QuadPart - LastCounter.QuadPart;
            TimeStep = (CounterElapsed / static_cast<f32>(CounterFrequency.QuadPart));
            LastCounter = EndCounter;
        }
    }

    Flush(Renderer.CommandQueue, Renderer.Fence, &Renderer.FenceValue, Renderer.FenceEvent); 

    ExitProcess(0);

    return Renderer;

}

