#pragma once

struct dx12_pipeline
{
    ID3D12PipelineState* Handle;
};

internal dx12_pipeline DX12PipelineCreate(ID3D12Device* Device, ID3D12RootSignature* RootSignature)
{
    dx12_pipeline Pipeline = {};
#if defined(BK_DEBUG)
    // Enable better shader debugging with the graphics debugging tools.
    const UINT CompileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    const UINT CompileFlags = 0;
#endif

    ID3DBlob* VertexShader;
    ID3DBlob* PixelShader;

    ID3DBlob* ErrorMessage;

    if (FAILED(D3DCompileFromFile(L"Resources/Shader.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "VSMain", "vs_5_1", CompileFlags, 0, &VertexShader, &ErrorMessage)))
    {
        Err("%s", (const char*)ErrorMessage->GetBufferPointer());
        Assert(false, "");
    }

    if (FAILED(D3DCompileFromFile(L"Resources/Shader.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "PSMain", "ps_5_1", CompileFlags, 0, &PixelShader, &ErrorMessage)))
    {
        Err("%s", (const char*)ErrorMessage->GetBufferPointer());
        Assert(false, "");
    }

    // Define the vertex input layout.
    D3D12_INPUT_ELEMENT_DESC InputElementDescs[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXINDEX", 0, DXGI_FORMAT_R32_UINT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC PipelineDesc = {};

    // Rasterizer state
    PipelineDesc.RasterizerState = {};
    PipelineDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    PipelineDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
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
    const D3D12_RENDER_TARGET_BLEND_DESC DefaultRenderTargetBlendDesc =
    {
        FALSE,FALSE,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_LOGIC_OP_NOOP,
        D3D12_COLOR_WRITE_ENABLE_ALL,
    };
    for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        PipelineDesc.BlendState.RenderTarget[i] = DefaultRenderTargetBlendDesc;

    PipelineDesc.InputLayout = { InputElementDescs, CountOf(InputElementDescs) };
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

