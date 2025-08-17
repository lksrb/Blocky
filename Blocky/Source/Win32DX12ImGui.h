#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx12.h>

#include "ImGuiCustomWidgets.h"

struct win32_dx12_imgui_context
{
    ImDrawData* CurrentDrawData = nullptr;
};

internal win32_dx12_imgui_context* win32_dx12_imgui_create(arena* Arena, win32_context* Win32Context, struct dx12_render_backend* DX12Backend);
internal void win32_dx12_imgui_destroy(win32_dx12_imgui_context* Context);

internal void win32_dx12_imgui_begin_frame(win32_dx12_imgui_context* Context);
internal void win32_dx12_imgui_end_frame(win32_dx12_imgui_context* Context);
