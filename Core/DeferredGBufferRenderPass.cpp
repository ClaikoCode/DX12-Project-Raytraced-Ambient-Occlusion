#include "DeferredGBufferRenderPass.h"

DeferredGBufferRenderPass::DeferredGBufferRenderPass(ComPtr<ID3D12Device5> device, ComPtr<ID3D12RootSignature> rootSig)
	: DX12RenderPass(device, D3D12_COMMAND_LIST_TYPE_DIRECT, true)
{
	// White list render objects.
	{
		m_renderableObjects.push_back(RTRenderObjectID);
	}

	struct PipelineStateStream
	{
		CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
		CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
		CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimtiveTopology;
		CD3DX12_PIPELINE_STATE_STREAM_VS VS;
		CD3DX12_PIPELINE_STATE_STREAM_PS PS;
		CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL DepthStencil;
		CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
		CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
	} pipelineStateStream;

	const D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	ComPtr<ID3DBlob> vsBlob;
	D3DReadFileToBlob(L"../DeferredRenderVS.cso", &vsBlob) >> CHK_HR;

	ComPtr<ID3DBlob> psBlob;
	D3DReadFileToBlob(L"../DeferredRenderPS.cso", &psBlob) >> CHK_HR;

	pipelineStateStream.RootSignature = rootSig.Get();
	pipelineStateStream.InputLayout = { inputLayout, (UINT)std::size(inputLayout) };
	pipelineStateStream.PrimtiveTopology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vsBlob.Get());
	pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(psBlob.Get());
	pipelineStateStream.DepthStencil = CD3DX12_DEPTH_STENCIL_DESC(CD3DX12_DEFAULT());
	pipelineStateStream.DSVFormat = DXGI_FORMAT_D32_FLOAT;

	D3D12_RT_FORMAT_ARRAY formatArr;
	memset(&formatArr, 0, sizeof(formatArr)); // Initialize memory with 0.

	// Set the format of each gbuffer.
	for (UINT i = 0; i < GBufferIDCount; i++)
	{
		formatArr.RTFormats[i] = GBufferFormats[i];
		formatArr.NumRenderTargets++;
	}

	// Set the correct array of RTV formats expected.
	pipelineStateStream.RTVFormats = formatArr;

	const D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = {
		.SizeInBytes = sizeof(PipelineStateStream),
		.pPipelineStateSubobjectStream = &pipelineStateStream
	};

	device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&m_pipelineState)) >> CHK_HR;

	NAME_D3D12_OBJECT_MEMBER(m_pipelineState, DeferredGBufferRenderPass);
}

void DeferredGBufferRenderPass::BuildRenderPass(const std::vector<RenderPackage>& renderPackages, UINT context, UINT frameIndex, RenderPassArgs* pipelineArgs)
{
	assert(pipelineArgs != nullptr);
	DeferredGBufferRenderPassArgs& args = ToSpecificArgs<DeferredGBufferRenderPassArgs>(pipelineArgs);

	auto commandList = GetCommandList(context, frameIndex);
	SetCommonStates(args.commonArgs, m_pipelineState, commandList);

	// Set render targets and depth stencil. The RTVs are assumed to be contiguous in memory, 
	// which is why the handle to the first GBuffer RTV is required.
	commandList->OMSetRenderTargets(GBufferIDCount, &args.firstGBufferRTVHandle, TRUE, &args.commonArgs.depthStencilView);

	// Draw.
	for (const RenderPackage& renderPackage : renderPackages)
	{
		if (renderPackage.renderObject)
		{
			const RenderObject& renderObject = *renderPackage.renderObject;

			PerRenderObject(renderObject, pipelineArgs, context, frameIndex);

			const std::vector<DrawArgs>& drawArgs = renderObject.drawArgs;

			if (renderPackage.renderInstances)
			{
				const std::vector<RenderInstance>& renderInstances = *renderPackage.renderInstances;

				for (UINT i = context; i < renderInstances.size(); i += NumContexts)
				{
					PerRenderInstance(renderInstances[i], drawArgs, pipelineArgs, context, frameIndex);
				}
			}
		}
	}
}

void DeferredGBufferRenderPass::PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex)
{
	auto commandList = GetCommandList(context, frameIndex);

	commandList->IASetPrimitiveTopology(renderObject.topology);
	commandList->IASetVertexBuffers(0, 1, &renderObject.vertexBufferView);
	commandList->IASetIndexBuffer(&renderObject.indexBufferView);
}

void DeferredGBufferRenderPass::PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex)
{
	DeferredGBufferRenderPassArgs& args = ToSpecificArgs<DeferredGBufferRenderPassArgs>(pipelineArgs);
	auto commandList = GetCommandList(context, frameIndex);

	SetInstanceCB(args.commonArgs, frameIndex, renderInstance, commandList);
	DrawInstanceIndexed(context, drawArgs, commandList);
}