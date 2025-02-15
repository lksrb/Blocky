#pragma once

struct dx12_buffer
{
    ID3D12Resource* Resource;
    u64 Size;
};

struct dx12_vertex_buffer
{
    dx12_buffer IntermediateBuffer;
    dx12_buffer Buffer;
    void* MappedIntermediateData = nullptr;
};

static dx12_buffer DX12BufferCreate(ID3D12Device* Device, D3D12_RESOURCE_STATES ResourceState, D3D12_HEAP_TYPE HeapType, u64 Size);
static void DX12BufferDestroy(dx12_buffer* Buffer);

static dx12_vertex_buffer DX12VertexBufferCreate(ID3D12Device* Device, u64 Size);
static void DX12VertexBufferDestroy(dx12_vertex_buffer* VertexBuffer);
static void DX12VertexBufferSendData(dx12_vertex_buffer* VertexBuffer, ID3D12GraphicsCommandList* CommandList, void* Data, u64 DataSize);
