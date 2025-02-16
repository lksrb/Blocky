#pragma once

struct dx12_buffer
{
    ID3D12Resource* Handle;
    u64 Size;
};

struct dx12_vertex_buffer
{
    dx12_buffer IntermediateBuffer;
    dx12_buffer Buffer;
    void* MappedIntermediateData = nullptr;
};

struct dx12_index_buffer
{
    dx12_buffer Buffer;
};

internal dx12_index_buffer DX12IndexBufferCreate(ID3D12Device* Device, ID3D12CommandAllocator* CommandAllocator, ID3D12GraphicsCommandList* CommandList, ID3D12CommandQueue* CommandQueue, u32* Data, u32 Count);
internal void DX12IndexBufferDestroy(dx12_index_buffer* IndexBuffer);

internal dx12_buffer DX12BufferCreate(ID3D12Device* Device, D3D12_RESOURCE_STATES ResourceState, D3D12_HEAP_TYPE HeapType, u64 Size);
internal void DX12BufferDestroy(dx12_buffer* Buffer);

internal dx12_vertex_buffer DX12VertexBufferCreate(ID3D12Device* Device, u64 Size);
internal void DX12VertexBufferDestroy(dx12_vertex_buffer* VertexBuffer);
internal void DX12VertexBufferSendData(dx12_vertex_buffer* VertexBuffer, ID3D12GraphicsCommandList* CommandList, void* Data, u64 DataSize);
