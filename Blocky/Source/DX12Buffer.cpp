#include "DX12Buffer.h"

static dx12_buffer DX12BufferCreate(ID3D12Device* Device, D3D12_RESOURCE_STATES ResourceState, D3D12_HEAP_TYPE HeapType, u64 Size)
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
        IID_PPV_ARGS(&Buffer.Resource)));

    Buffer.Size = Size;

    return Buffer;
}

static void DX12BufferDestroy(dx12_buffer* Buffer)
{
    Buffer->Resource->Release();
    Buffer->Resource = nullptr;
    Buffer->Size = 0;
}

static dx12_vertex_buffer DX12VertexBufferCreate(ID3D12Device* Device, u64 Size)
{
    dx12_vertex_buffer Result = {};
    Result.Buffer = DX12BufferCreate(Device, D3D12_RESOURCE_STATE_COMMON, D3D12_HEAP_TYPE_DEFAULT, Size);
    Result.IntermediateBuffer = DX12BufferCreate(Device, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_HEAP_TYPE_UPLOAD, Size);

    Result.IntermediateBuffer.Resource->Map(0, nullptr, &Result.MappedIntermediateData);
    return Result;
}

static void DX12VertexBufferDestroy(dx12_vertex_buffer* VertexBuffer)
{
    VertexBuffer->IntermediateBuffer.Resource->Unmap(0, nullptr);

    DX12BufferDestroy(&VertexBuffer->Buffer);
    DX12BufferDestroy(&VertexBuffer->IntermediateBuffer);
}

static void DX12VertexBufferSendData(dx12_vertex_buffer* VertexBuffer, ID3D12GraphicsCommandList* CommandList, void* Data, u64 DataSize)
{
    Assert(VertexBuffer->Buffer.Size >= DataSize, "Buffer overload!");

    if (DataSize == 0)
        return;

    memcpy(VertexBuffer->MappedIntermediateData, Data, DataSize);

    auto Barrier = GameRendererTransition(VertexBuffer->Buffer.Resource, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COPY_DEST);
    CommandList->ResourceBarrier(1, &Barrier);

    CommandList->CopyBufferRegion(VertexBuffer->Buffer.Resource, 0, VertexBuffer->IntermediateBuffer.Resource, 0, DataSize);

    Barrier = GameRendererTransition(VertexBuffer->Buffer.Resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    CommandList->ResourceBarrier(1, &Barrier);
}
