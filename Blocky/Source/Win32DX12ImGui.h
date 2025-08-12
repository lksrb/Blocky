#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx12.h>

#include "ImGuiCustomWidgets.h"

// Simple free list based allocator
struct example_descriptor_heap_allocator
{
    ID3D12DescriptorHeap* m_Heap = nullptr;
    D3D12_DESCRIPTOR_HEAP_TYPE  m_HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
    D3D12_CPU_DESCRIPTOR_HANDLE m_HeapStartCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_HeapStartGpu = {};
    u32 m_HeapHandleIncrement = 0;

    i32* m_FreeIndices = nullptr;
    i32 m_FreeIndicesSize = 0;

    void Create(arena* Arena, ID3D12Device* Device, ID3D12DescriptorHeap* Heap)
    {
        m_Heap = Heap;
        D3D12_DESCRIPTOR_HEAP_DESC Desc = Heap->GetDesc();
        m_HeapType = Desc.Type;
        m_HeapStartCpu = m_Heap->GetCPUDescriptorHandleForHeapStart();
        m_HeapStartGpu = m_Heap->GetGPUDescriptorHandleForHeapStart();
        m_HeapHandleIncrement = Device->GetDescriptorHandleIncrementSize(m_HeapType);
        m_FreeIndices = arena_new_array(Arena, i32, Desc.NumDescriptors);
        m_FreeIndicesSize = Desc.NumDescriptors;

        i32 Index = 0;
        for (i32 N = Desc.NumDescriptors; N > 0; N--)
            m_FreeIndices[Index++] = N - 1;
    }
    void Destroy()
    {
        *this = example_descriptor_heap_allocator();
    }

    void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle)
    {
         IM_ASSERT(m_FreeIndicesSize > 0);
         i32 Index = m_FreeIndices[m_FreeIndicesSize - 1];
         //m_FreeIndices.back();
         //m_FreeIndices.pop_back();
         --m_FreeIndicesSize;
         out_cpu_desc_handle->ptr = m_HeapStartCpu.ptr + (Index * m_HeapHandleIncrement);
         out_gpu_desc_handle->ptr = m_HeapStartGpu.ptr + (Index * m_HeapHandleIncrement);
    }
    void Free(D3D12_CPU_DESCRIPTOR_HANDLE out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE out_gpu_desc_handle)
    {
        i32 CPUIndex = (i32)((out_cpu_desc_handle.ptr - m_HeapStartCpu.ptr) / m_HeapHandleIncrement);
        i32 GPUIndex = (i32)((out_gpu_desc_handle.ptr - m_HeapStartGpu.ptr) / m_HeapHandleIncrement);
        IM_ASSERT(CPUIndex == GPUIndex);
        m_FreeIndices[m_FreeIndicesSize++] = CPUIndex;
        //m_FreeIndices.push_back(cpu_idx);
    }
};

struct win32_dx12_imgui_context
{
    ID3D12DescriptorHeap* SrvDescHeap = nullptr;
    ImDrawData* CurrentDrawData = nullptr;
    example_descriptor_heap_allocator DescriptorHeapAllocator;
};

internal win32_dx12_imgui_context* win32_dx12_imgui_create(arena* Arena, win32_context* Win32Context, struct dx12_render_backend* DX12Backend);
internal void win32_dx12_imgui_destroy(win32_dx12_imgui_context* Context);

internal void win32_dx12_imgui_begin_frame(win32_dx12_imgui_context* Context);
internal void win32_dx12_imgui_end_frame(win32_dx12_imgui_context* Context);
