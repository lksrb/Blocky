#pragma once

struct dx12_pipeline
{
    ID3D12PipelineState* Handle;
};

// TODO: Make more generic 
internal dx12_pipeline DX12PipelineCreate(ID3D12Device* Device, ID3D12RootSignature* RootSignature, D3D12_INPUT_ELEMENT_DESC Inputs[], u32 InputsCount, const wchar_t* ShaderPath)
{
    dx12_pipeline Pipeline = {};
#if defined(BK_DEBUG)
    // Enable better shader debugging with the graphics debugging tools.
    const UINT CompileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    const UINT CompileFlags = 0;
#endif

    ID3DBlob* VertexShader = nullptr;
    ID3DBlob* PixelShader = nullptr;

    ID3DBlob* ErrorMessage = nullptr;

    // Really? Why isn't there some ANSI version?
    if (FAILED(D3DCompileFromFile(ShaderPath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "VSMain", "vs_5_1", CompileFlags, 0, &VertexShader, &ErrorMessage)))
    {
        if (ErrorMessage)
            Err("%s", (const char*)ErrorMessage->GetBufferPointer());
        Assert(false, "");
    }

    if (FAILED(D3DCompileFromFile(ShaderPath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "PSMain", "ps_5_1", CompileFlags, 0, &PixelShader, &ErrorMessage)))
    {
        if (ErrorMessage)
            Err("%s", (const char*)ErrorMessage->GetBufferPointer());
        Assert(false, "");
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC PipelineDesc = {};

    // Rasterizer state
    PipelineDesc.RasterizerState = {};
    PipelineDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    PipelineDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    PipelineDesc.RasterizerState.FrontCounterClockwise = TRUE;
    PipelineDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    PipelineDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    PipelineDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    PipelineDesc.RasterizerState.DepthClipEnable = TRUE;
    PipelineDesc.RasterizerState.MultisampleEnable = FALSE;
    PipelineDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    PipelineDesc.RasterizerState.ForcedSampleCount = 0;
    PipelineDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    // Depth and stencil state
    PipelineDesc.DepthStencilState.DepthEnable = TRUE;
    PipelineDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    PipelineDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;  // Closer pixels are drawn
    PipelineDesc.DepthStencilState.StencilEnable = FALSE;  // Stencil disabled for now
    PipelineDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;  // Must match depth buffer format

    // Blend state
    PipelineDesc.BlendState = {};
    PipelineDesc.BlendState.AlphaToCoverageEnable = FALSE;
    PipelineDesc.BlendState.IndependentBlendEnable = FALSE;
    for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        auto& Desc = PipelineDesc.BlendState.RenderTarget[i];
        Desc.BlendEnable = TRUE;
        Desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;       // Use source alpha
        Desc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;  // 1 - source alpha
        Desc.BlendOp = D3D12_BLEND_OP_ADD;
        Desc.SrcBlendAlpha = D3D12_BLEND_ONE;
        Desc.DestBlendAlpha = D3D12_BLEND_ZERO;
        Desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        Desc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }

    PipelineDesc.InputLayout = { Inputs, InputsCount };
    PipelineDesc.pRootSignature = RootSignature;
    PipelineDesc.VS = CD3DX12_SHADER_BYTECODE(VertexShader);
    PipelineDesc.PS = CD3DX12_SHADER_BYTECODE(PixelShader);
    PipelineDesc.SampleMask = UINT_MAX;
    PipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    PipelineDesc.NumRenderTargets = 1;
    PipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    PipelineDesc.SampleDesc.Count = 1;
    DxAssert(Device->CreateGraphicsPipelineState(&PipelineDesc, IID_PPV_ARGS(&Pipeline.Handle)));

    return Pipeline;
}

internal void DX12PipelineDestroy(dx12_pipeline* Pipeline)
{
    Pipeline->Handle->Release();
}

