// DirectX 12 Header File
// This file contains everything to get DirectX12 building including some extra components like DirectXMath.h or d3dx12.h
#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3d12shader.h>
#include <dxcapi.h>
#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

// D3D12 extension
#include "d3dx12.h"

// TODO: Better approach?
inline ID3D12InfoQueue* g_DebugInfoQueue;
internal void dx12_info_queue_dump(ID3D12InfoQueue* InfoQueue);
#define DumpInfoQueue() dx12_info_queue_dump(g_DebugInfoQueue)
#define DxAssert(x) do { HRESULT __Result = x; if (FAILED(__Result)) { DumpInfoQueue(); Assert(false, #x); } } while(0)
#define FIF 2

#if BK_DEBUG
#define DX12_ENABLE_DEBUG_LAYER 1
#else
#define DX12_ENABLE_DEBUG_LAYER 0
#endif

#if DX12_ENABLE_DEBUG_LAYER
#include <dxgidebug.h>
#include <d3d12sdklayers.h>

#pragma comment(lib, "dxguid.lib")
#endif

#if OBSOLETE
internal D3D12_RESOURCE_BARRIER DX12Transition(ID3D12Resource* Resource, D3D12_RESOURCE_STATES StateBefore, D3D12_RESOURCE_STATES StateAfter, UINT Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_BARRIER_FLAGS Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE)
{
    D3D12_RESOURCE_BARRIER Result;
    Result.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Result.Flags = Flags;
    Result.Transition.pResource = Resource;
    Result.Transition.StateBefore = StateBefore;
    Result.Transition.StateAfter = StateAfter;
    Result.Transition.Subresource = Subresource;
    return Result;
};
#endif

internal D3D12_RESOURCE_BARRIER dx12_cmd_transition(ID3D12GraphicsCommandList* CommandList, ID3D12Resource* Resource, D3D12_RESOURCE_STATES StateBefore, D3D12_RESOURCE_STATES StateAfter, UINT Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_BARRIER_FLAGS Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE)
{
    D3D12_RESOURCE_BARRIER Result;
    Result.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Result.Flags = Flags;
    Result.Transition.pResource = Resource;
    Result.Transition.StateBefore = StateBefore;
    Result.Transition.StateAfter = StateAfter;
    Result.Transition.Subresource = Subresource;

    CommandList->ResourceBarrier(1, &Result);
    return Result;
};

internal void dx12_cmd_set_viewport(ID3D12GraphicsCommandList* CommandList, f32 TopLeftX, f32 TopLeftY, f32 Width, f32 Height, f32 MinDepth = D3D12_MIN_DEPTH, f32 MaxDepth = D3D12_MAX_DEPTH)
{
    D3D12_VIEWPORT Viewport;
    Viewport.TopLeftX = TopLeftX;
    Viewport.TopLeftY = TopLeftY;
    Viewport.Width = Width;
    Viewport.Height = Height;
    Viewport.MinDepth = MinDepth;
    Viewport.MaxDepth = MaxDepth;
    CommandList->RSSetViewports(1, &Viewport);
}

internal void dx12_cmd_set_scrissor_rect(ID3D12GraphicsCommandList* CommandList, i32 Left, i32 Top, i32 Right, i32 Bottom)
{
    D3D12_RECT ScissorRect;
    ScissorRect.left = Left;
    ScissorRect.top = Top;
    ScissorRect.right = Right;
    ScissorRect.bottom = Bottom;
    CommandList->RSSetScissorRects(1, &ScissorRect);
}

internal void DX12CmdSetIndexBuffer(ID3D12GraphicsCommandList* CommandList, ID3D12Resource* BufferHandle, u32 SizeInBytes, DXGI_FORMAT Format)
{
    // Bind index buffer
    local_persist D3D12_INDEX_BUFFER_VIEW IndexBufferView;
    IndexBufferView.BufferLocation = BufferHandle->GetGPUVirtualAddress();
    IndexBufferView.SizeInBytes = SizeInBytes;
    IndexBufferView.Format = Format;
    CommandList->IASetIndexBuffer(&IndexBufferView);
}

internal void DX12CmdSetVertexBuffer(ID3D12GraphicsCommandList* CommandList, u32 StartSlot, ID3D12Resource* BufferHandle, u32 SizeInBytes, u32 StrideInBytes)
{
    local_persist D3D12_VERTEX_BUFFER_VIEW VertexBufferView;
    VertexBufferView.BufferLocation = BufferHandle->GetGPUVirtualAddress();
    VertexBufferView.SizeInBytes = SizeInBytes;
    VertexBufferView.StrideInBytes = StrideInBytes;
    CommandList->IASetVertexBuffers(StartSlot, 1, &VertexBufferView);
}

internal void DX12CmdSetVertexBuffers2(ID3D12GraphicsCommandList* CommandList, u32 StartSlot, ID3D12Resource* BufferHandle0, u32 SizeInBytes0, u32 StrideInBytes0, ID3D12Resource* BufferHandle1, u32 SizeInBytes1, u32 StrideInBytes1)
{
    // Bind vertex positions
    local_persist D3D12_VERTEX_BUFFER_VIEW VertexBufferViews[2];
    VertexBufferViews[0].BufferLocation = BufferHandle0->GetGPUVirtualAddress();
    VertexBufferViews[0].SizeInBytes = SizeInBytes0;
    VertexBufferViews[0].StrideInBytes = StrideInBytes0;

    // Bind transforms
    VertexBufferViews[1].BufferLocation = BufferHandle1->GetGPUVirtualAddress();
    VertexBufferViews[1].SizeInBytes = SizeInBytes1;
    VertexBufferViews[1].StrideInBytes = StrideInBytes1;

    CommandList->IASetVertexBuffers(0, 2, VertexBufferViews);
}

internal ID3D12Resource* dx12_render_target_create(ID3D12Device* Device, DXGI_FORMAT Format, u32 Width, u32 Height, const wchar_t* DebugName = L"RenderTarget")
{
    ID3D12Resource* Resource = nullptr;

    D3D12_RESOURCE_DESC Desc = {};
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Desc.MipLevels = 1;
    Desc.Format = Format;
    Desc.Width = Width;
    Desc.Height = Height;
    Desc.DepthOrArraySize = 1;
    Desc.SampleDesc.Count = 1;
    Desc.SampleDesc.Quality = 0;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES HeapProperties = {};
    HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    HeapProperties.CreationNodeMask = 1;
    HeapProperties.VisibleNodeMask = 1;
    HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;

    DxAssert(Device->CreateCommittedResource(&HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&Resource)));
    Resource->SetName(DebugName);
    return Resource;
}

internal ID3D12Resource* dx12_depth_buffer_create(ID3D12Device* Device, u32 Width, u32 Height, DXGI_FORMAT Format, DXGI_FORMAT ClearFormat, const wchar_t* DebugName = L"DepthBuffer", D3D12_RESOURCE_STATES InitialState = D3D12_RESOURCE_STATE_DEPTH_WRITE)
{
    ID3D12Resource* Resource = nullptr;

    D3D12_CLEAR_VALUE OptimizedClearValue = {};
    OptimizedClearValue.Format = ClearFormat;
    OptimizedClearValue.DepthStencil = { 1.0f, 0 }; // No need for abstracting this

    D3D12_RESOURCE_DESC Desc = {};
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Desc.MipLevels = 1;
    Desc.Format = Format;
    Desc.Width = Width;
    Desc.Height = Height;
    Desc.DepthOrArraySize = 1;
    Desc.SampleDesc.Count = 1;
    Desc.SampleDesc.Quality = 0;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_HEAP_PROPERTIES HeapProperties = {};
    HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    HeapProperties.CreationNodeMask = 1;
    HeapProperties.VisibleNodeMask = 1;
    HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;

    DxAssert(Device->CreateCommittedResource(&HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc, InitialState, &OptimizedClearValue, IID_PPV_ARGS(&Resource)));
    Resource->SetName(DebugName);
    return Resource;
}

internal IDXGISwapChain4* dx12_create_swapchain(IDXGIFactory4* Factory, ID3D12CommandQueue* CommandQueue, HWND WindowHandle, u32 Width, u32 Height, DXGI_FORMAT Format, u32 FramesInFlight)
{
    IDXGISwapChain4* SwapChain = nullptr;
    DXGI_SWAP_CHAIN_DESC1 Desc = {};
    Desc.Width = Width;
    Desc.Height = Height;
    Desc.Format = Format;
    Desc.Stereo = false;
    Desc.SampleDesc = { 1, 0 }; // Anti-aliasing needs to be done manually in D3D12
    Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    Desc.BufferCount = FramesInFlight;
    Desc.Scaling = DXGI_SCALING_STRETCH;
    Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // Default
    Desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    // It is recommended to always allow tearing if tearing support is available.
    // TODO: More robustness needed
    Desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    bool WaitableSwapchain = false;
    if (WaitableSwapchain)
    {
        Desc.Flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    }

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC FullScreenDesc = {};
    FullScreenDesc.Windowed = true;

    IDXGISwapChain1* TempSwapChain1;
    HRESULT Result = (Factory->CreateSwapChainForHwnd(CommandQueue, WindowHandle, &Desc, &FullScreenDesc, nullptr, &TempSwapChain1));

    // Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
    // will be handled manually.
    DxAssert(Factory->MakeWindowAssociation(WindowHandle, DXGI_MWA_NO_ALT_ENTER));
    TempSwapChain1->QueryInterface(IID_PPV_ARGS(&SwapChain));
    TempSwapChain1->Release();

    return SwapChain;
}

struct dx12_descriptor_heap
{
    ID3D12DescriptorHeap* Handle;
    u32 DescriptorHeapCapacity;
    bool ShaderVisible;
    D3D12_DESCRIPTOR_HEAP_TYPE Type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
};

internal dx12_descriptor_heap dx12_descriptor_heap_create(ID3D12Device* Device, D3D12_DESCRIPTOR_HEAP_TYPE Type, bool ShaderVisible, u32 DescriptorHeapCapacity)
{
    dx12_descriptor_heap Result = {};

    D3D12_DESCRIPTOR_HEAP_DESC Desc = {};
    Desc.NumDescriptors = DescriptorHeapCapacity;
    Desc.Type = Type;
    Desc.Flags = ShaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    Desc.NodeMask = 0; // Set this to zero, we do not have multiple D3d12 devices
    DxAssert(Device->CreateDescriptorHeap(&Desc, IID_PPV_ARGS(&Result.Handle)));

    Result.ShaderVisible = ShaderVisible;
    Result.Type = Desc.Type;
    Result.DescriptorHeapCapacity = Desc.NumDescriptors;

    return Result;
}

internal void dx12_descriptor_heap_destroy(dx12_descriptor_heap* DescriptorHeap)
{
    DescriptorHeap->Handle->Release();
    *DescriptorHeap = {};
}

// Blocking API for submitting stuff to GPU
// NOTE: Inefficient
template<typename F>
internal void dx12_submit_to_queue_immidiately(ID3D12Device* Device, ID3D12CommandAllocator* CommandAllocator, ID3D12GraphicsCommandList* CommandList, ID3D12CommandQueue* CommandQueue, F&& Func)
{
    // Reset
    DxAssert(CommandAllocator->Reset());
    DxAssert(CommandList->Reset(CommandAllocator, nullptr));

    // Record stuff
    Func(CommandList);

    // Finish recording
    CommandList->Close();

    // Execute command list
    CommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&CommandList);

    // Wait for completion
    {
        UINT64 FenceValue = 0;
        HANDLE FenceEvent = CreateEvent(nullptr, false, false, nullptr);

        ID3D12Fence* Fence;

        // Create a simple fence for synchronization
        DxAssert(Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence)));

        DxAssert(CommandQueue->Signal(Fence, ++FenceValue));

        // Step 3: Wait until the GPU reaches the fence
        if (SUCCEEDED(Fence->SetEventOnCompletion(FenceValue, FenceEvent)))
        {
            WaitForSingleObject(FenceEvent, INFINITE);
        }
        CloseHandle(FenceEvent);

        Fence->Release();
    }
}

internal ID3D12InfoQueue* dx12_info_queue_create(ID3D12Device* Device)
{
    ID3D12InfoQueue* DebugInfoQueue = nullptr;
    DxAssert(Device->QueryInterface(IID_PPV_ARGS(&DebugInfoQueue)));
    DebugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, false);
    DebugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, false);
    DebugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, false);
    DebugInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_INFO, false);
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

        DxAssert(DebugInfoQueue->PushStorageFilter(&NewFilter));
    }

    return DebugInfoQueue;
}

internal void dx12_info_queue_dump(ID3D12InfoQueue* InfoQueue)
{
    if (!InfoQueue)
        return;

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
                Trace("[DX12] TRACE: %s", Message->pDescription);
                break;
            }
            case D3D12_MESSAGE_SEVERITY_INFO:
            {
                Info("[DX12] INFO: %s", Message->pDescription);
                break;
            }
            case D3D12_MESSAGE_SEVERITY_WARNING:
            {
                Warn("[DX12] WARNING: %s", Message->pDescription);
                break;
            }
            case D3D12_MESSAGE_SEVERITY_ERROR:
            case D3D12_MESSAGE_SEVERITY_CORRUPTION:
            {
                Err("[DX12] ERROR: %s", Message->pDescription);
                break;
            }
        }
    }

    // Clear the messages from the queue
    InfoQueue->ClearStoredMessages();
}

struct dx12_root_signature_description
{};

struct dx12_root_signature
{
    ID3D12RootSignature* Handle;
};

internal dx12_root_signature dx12_root_signature_create(ID3D12Device* Device, const D3D12_ROOT_SIGNATURE_DESC& Desc)
{
    dx12_root_signature Result = {};

    // Root signature
    ID3DBlob* Error;
    ID3DBlob* Signature;
    DxAssert(D3D12SerializeRootSignature(&Desc, D3D_ROOT_SIGNATURE_VERSION_1, &Signature, &Error));
    DxAssert(Device->CreateRootSignature(0, Signature->GetBufferPointer(), Signature->GetBufferSize(), IID_PPV_ARGS(&Result.Handle)));

    return Result;
}

internal void dx12_root_signature_destroy(dx12_root_signature* RootSignature)
{
    RootSignature->Handle->Release();
    RootSignature->Handle = nullptr;
}
