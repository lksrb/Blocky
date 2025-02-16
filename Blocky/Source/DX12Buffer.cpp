#include "DX12Buffer.h"

internal dx12_buffer DX12BufferCreate(ID3D12Device* Device, D3D12_RESOURCE_STATES ResourceState, D3D12_HEAP_TYPE HeapType, u64 Size)
{
    dx12_buffer Buffer = {};

    D3D12_HEAP_PROPERTIES HeapProperties;
    HeapProperties.Type = HeapType;
    HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    HeapProperties.CreationNodeMask = 1; // Exactly one bit of this UINT must be set. Using only single adapter.
    HeapProperties.VisibleNodeMask = 1; // Exactly one bit of this UINT must be set.Using only single adapter.

    D3D12_RESOURCE_DESC Desc;
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    Desc.Width = Size;
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.Format = DXGI_FORMAT_UNKNOWN;
    Desc.SampleDesc.Count = 1;
    Desc.SampleDesc.Quality = 0;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    DxAssert(Device->CreateCommittedResource(
        &HeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &Desc,
        ResourceState,
        nullptr,
        IID_PPV_ARGS(&Buffer.Handle)));

    Buffer.Size = Size;

    return Buffer;
}

internal void DX12BufferDestroy(dx12_buffer* Buffer)
{
    Buffer->Handle->Release();
    Buffer->Handle = nullptr;
    Buffer->Size = 0;
}

internal dx12_vertex_buffer DX12VertexBufferCreate(ID3D12Device* Device, u64 Size)
{
    dx12_vertex_buffer Result = {};
    Result.Buffer = DX12BufferCreate(Device, D3D12_RESOURCE_STATE_COMMON, D3D12_HEAP_TYPE_DEFAULT, Size);
    Result.IntermediateBuffer = DX12BufferCreate(Device, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_HEAP_TYPE_UPLOAD, Size);

    Result.IntermediateBuffer.Handle->Map(0, nullptr, &Result.MappedIntermediateData);
    return Result;
}

internal void DX12VertexBufferDestroy(dx12_vertex_buffer* VertexBuffer)
{
    VertexBuffer->IntermediateBuffer.Handle->Unmap(0, nullptr);

    DX12BufferDestroy(&VertexBuffer->Buffer);
    DX12BufferDestroy(&VertexBuffer->IntermediateBuffer);
}

internal void DX12VertexBufferSendData(dx12_vertex_buffer* VertexBuffer, ID3D12GraphicsCommandList* CommandList, void* Data, u64 DataSize)
{
    Assert(VertexBuffer->Buffer.Size >= DataSize, "Buffer overload!");

    if (DataSize == 0)
        return;

    memcpy(VertexBuffer->MappedIntermediateData, Data, DataSize);

    auto Barrier = GameRendererTransition(VertexBuffer->Buffer.Handle, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COPY_DEST);
    CommandList->ResourceBarrier(1, &Barrier);

    CommandList->CopyBufferRegion(VertexBuffer->Buffer.Handle, 0, VertexBuffer->IntermediateBuffer.Handle, 0, DataSize);

    Barrier = GameRendererTransition(VertexBuffer->Buffer.Handle, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    CommandList->ResourceBarrier(1, &Barrier);
}

internal dx12_index_buffer DX12IndexBufferCreate(ID3D12Device* Device, ID3D12CommandAllocator* CommandAllocator, ID3D12GraphicsCommandList* CommandList, ID3D12CommandQueue* CommandQueue, u32* Data, u32 Count)
{
    dx12_index_buffer IndexBuffer = {};
    IndexBuffer.Buffer = DX12BufferCreate(Device, D3D12_RESOURCE_STATE_COMMON, D3D12_HEAP_TYPE_DEFAULT, Count * sizeof(u32));

    dx12_buffer Intermediate = DX12BufferCreate(Device, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_HEAP_TYPE_UPLOAD, Count * sizeof(u32));
    void* MappedPtr = nullptr;
    Intermediate.Handle->Map(0, nullptr, &MappedPtr);
    memcpy(MappedPtr, Data, Count * sizeof(u32));
    Intermediate.Handle->Unmap(0, nullptr);
    GameRendererSubmitToQueueImmidiate(Device, CommandAllocator, CommandList, CommandQueue,
    [&IndexBuffer, &Intermediate, Count](ID3D12GraphicsCommandList* CommandList)
    {
        CommandList->CopyBufferRegion(IndexBuffer.Buffer.Handle, 0, Intermediate.Handle, 0, Count * sizeof(u32));
    });

    DX12BufferDestroy(&Intermediate);

    return IndexBuffer;
}

internal void DX12IndexBufferDestroy(dx12_index_buffer* IndexBuffer)
{
    DX12BufferDestroy(&IndexBuffer->Buffer);
}


