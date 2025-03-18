#pragma once

struct dx12_pipeline
{
    ID3D12PipelineState* Handle;
};

// TODO: Make more generic 
internal dx12_pipeline DX12PipelineCreate(ID3D12Device* Device, ID3D12RootSignature* RootSignature, D3D12_INPUT_ELEMENT_DESC Inputs[], u32 InputsCount, const wchar_t* ShaderPath)
{
    dx12_pipeline Pipeline = {};

    IDxcBlob* VertexShader = nullptr;
    IDxcBlob* PixelShader = nullptr;

    // Initialize DXC
    IDxcCompiler3* Compiler;
    IDxcLibrary* Library;
    DxAssert(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&Compiler)));
    DxAssert(DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&Library)));

    IDxcBlobEncoding* SourceShader;
    DxAssert(Library->CreateBlobFromFile(ShaderPath, nullptr, &SourceShader));

    // VERTEX
    {
#if defined(BK_DEBUG)
        LPCWSTR Arguments[] = {
            L"-T", L"vs_6_0",  // Shader profile
            L"-E", L"VSMain", // Entry point
            L"-Zi",            // Debug info
            L"-Qembed_debug",  // Embed debug info
        };
#else
        LPCWSTR Arguments[] = {
            L"-T", L"vs_6_0",  // Shader profile
            L"-E", L"VSMain", // Entry point
            //L"-Zi",            // Debug info
            //L"-Qembed_debug",  // Embed debug info
        };
#endif

        DxcBuffer Buffer = {};

        BOOL Known;
        SourceShader->GetEncoding(&Known, &Buffer.Encoding);
        Buffer.Ptr = SourceShader->GetBufferPointer();
        Buffer.Size = SourceShader->GetBufferSize();

        IDxcResult* Result;
        DxAssert(Compiler->Compile(&Buffer, Arguments, CountOf(Arguments), nullptr, IID_PPV_ARGS(&Result)));

        HRESULT ErrorCode;
        Result->GetStatus(&ErrorCode);

        if (FAILED(ErrorCode))
        {
            IDxcBlobEncoding* ErrorMessage = nullptr;

            Result->GetErrorBuffer(&ErrorMessage);

            Err("%s", (const char*)ErrorMessage->GetBufferPointer());
            Assert(false, "");
        }
        else
        {
            Result->GetResult(&VertexShader);
        }
    }

    // FRAGMENT
    {
#if defined(BK_DEBUG)
        LPCWSTR Arguments[] = {
           L"-T", L"ps_6_0",  // Shader profile
           L"-E", L"PSMain", // Entry point
           L"-Zi",            // Debug info
           L"-Qembed_debug",  // Embed debug info
        };
#else
        LPCWSTR Arguments[] = {
           L"-T", L"ps_6_0",  // Shader profile
           L"-E", L"PSMain", // Entry point
           //L"-Zi",            // Debug info
           //L"-Qembed_debug",  // Embed debug info
        };
#endif

        DxcBuffer Buffer = {};

        BOOL Known;
        SourceShader->GetEncoding(&Known, &Buffer.Encoding);
        Buffer.Ptr = SourceShader->GetBufferPointer();
        Buffer.Size = SourceShader->GetBufferSize();

        IDxcResult* Result;
        DxAssert(Compiler->Compile(&Buffer, Arguments, CountOf(Arguments), nullptr, IID_PPV_ARGS(&Result)));

        HRESULT ErrorCode;
        Result->GetStatus(&ErrorCode);

        if (FAILED(ErrorCode))
        {
            IDxcBlobEncoding* ErrorMessage = nullptr;

            Result->GetErrorBuffer(&ErrorMessage);

            Err("%s", (const char*)ErrorMessage->GetBufferPointer());
            Assert(false, "");
        }
        else
        {
            Result->GetResult(&PixelShader);
        }
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
    PipelineDesc.VS = { VertexShader->GetBufferPointer(), VertexShader->GetBufferSize() };
    PipelineDesc.PS = { PixelShader->GetBufferPointer(), PixelShader->GetBufferSize() };
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

