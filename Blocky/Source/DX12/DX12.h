// DirectX 12 Header File
// This file contains everything to get DirectX12 building including some extra components like DirectXMath.h or d3dx12.h
#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3d12shader.h>
#include <dxcapi.h>
#include <DirectXMath.h>
using namespace DirectX;

#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

// D3D12 extension
#include "d3dx12.h"

// TODO: Better approach?
inline ID3D12InfoQueue* g_DebugInfoQueue; // Created by GameRenderer
internal void DX12DumpInfoQueue(ID3D12InfoQueue* InfoQueue);
#define DumpInfoQueue() DX12DumpInfoQueue(g_DebugInfoQueue)
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

internal D3D12_RESOURCE_BARRIER DX12CmdTransition(ID3D12GraphicsCommandList* CommandList, ID3D12Resource* Resource, D3D12_RESOURCE_STATES StateBefore, D3D12_RESOURCE_STATES StateAfter, UINT Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_BARRIER_FLAGS Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE)
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

internal void DX12CmdSetViewport(ID3D12GraphicsCommandList* CommandList, f32 TopLeftX, f32 TopLeftY, f32 Width, f32 Height, f32 MinDepth = D3D12_MIN_DEPTH, f32 MaxDepth = D3D12_MAX_DEPTH)
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

internal void DX12CmdSetScissorRect(ID3D12GraphicsCommandList* CommandList, i32 Left, i32 Top, i32 Right, i32 Bottom)
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

// Blocking API for submitting stuff to GPU
// NOTE: Inefficient
template<typename F>
internal void DX12SubmitToQueueImmidiate(ID3D12Device* Device, ID3D12CommandAllocator* CommandAllocator, ID3D12GraphicsCommandList* CommandList, ID3D12CommandQueue* CommandQueue, F&& Func)
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

internal void DX12DumpInfoQueue(ID3D12InfoQueue* InfoQueue)
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
