#pragma once

struct dx12_buffer
{
    ID3D12Resource* Handle;
    u64 Size;
};
internal dx12_buffer DX12BufferCreate(ID3D12Device* Device, D3D12_RESOURCE_STATES ResourceState, D3D12_HEAP_TYPE HeapType, u64 Size);
internal void DX12BufferDestroy(dx12_buffer* Buffer);

struct dx12_index_buffer
{
    dx12_buffer Buffer;
};
internal dx12_index_buffer DX12IndexBufferCreate(ID3D12Device* Device, ID3D12CommandAllocator* CommandAllocator, ID3D12GraphicsCommandList* CommandList, ID3D12CommandQueue* CommandQueue, const u32* Data, u32 Count);
internal void DX12IndexBufferDestroy(dx12_index_buffer* IndexBuffer);

struct dx12_vertex_buffer
{
    dx12_buffer IntermediateBuffer;
    dx12_buffer Buffer;
    void* MappedIntermediateData;
};
internal dx12_vertex_buffer DX12VertexBufferCreate(ID3D12Device* Device, u64 Size);
internal void DX12VertexBufferDestroy(dx12_vertex_buffer* VertexBuffer);
internal void DX12VertexBufferSendData(dx12_vertex_buffer* VertexBuffer, ID3D12GraphicsCommandList* CommandList, const void* Data, u64 DataSize);

struct dx12_constant_buffer
{
    dx12_buffer Buffer;
    void* MappedData;
};
internal dx12_constant_buffer DX12ConstantBufferCreate(ID3D12Device* Device, u64 Size);
internal void DX12ConstantBufferDestroy(dx12_constant_buffer* ConstantBuffer);
internal void DX12ConstantBufferSetData(dx12_constant_buffer* ConstantBuffer, const void* Data, u64 Size);
