#include "DeferredLightingRenderPass.h"

DeferredLightingRenderPass::DeferredLightingRenderPass(ComPtr<ID3D12Device5> device, ComPtr<ID3D12RootSignature> rootSig)
	: DX12RenderPass(device, D3D12_COMMAND_LIST_TYPE_DIRECT, false)
{
	struct DeferredLightingStateStream
	{
		CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
		CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimtiveTopology;
		CD3DX12_PIPELINE_STATE_STREAM_VS VS;
		CD3DX12_PIPELINE_STATE_STREAM_PS PS;
		CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
		CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
	} deferredLightingStateStream;

	ComPtr<ID3DBlob> vsBlob;
	D3DReadFileToBlob(L"../FullScreenQuadVS.cso", &vsBlob) >> CHK_HR;

	ComPtr<ID3DBlob> psBlob;
	D3DReadFileToBlob(L"../DeferredLightingPS.cso", &psBlob) >> CHK_HR;

	deferredLightingStateStream.RootSignature = rootSig.Get();
	deferredLightingStateStream.PrimtiveTopology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	deferredLightingStateStream.VS = CD3DX12_SHADER_BYTECODE(vsBlob.Get());
	deferredLightingStateStream.PS = CD3DX12_SHADER_BYTECODE(psBlob.Get());
	deferredLightingStateStream.RTVFormats = { { BackBufferFormat }, 1 };
	deferredLightingStateStream.DSVFormat = DXGI_FORMAT_UNKNOWN;

	const D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = {
			.SizeInBytes = sizeof(DeferredLightingStateStream),
			.pPipelineStateSubobjectStream = &deferredLightingStateStream
	};

	device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&m_pipelineState)) >> CHK_HR;

	NAME_D3D12_OBJECT_MEMBER(m_pipelineState, DeferredLightingStateStream);
}

void DeferredLightingRenderPass::BuildRenderPass(const std::vector<RenderPackage>& renderPackages, UINT context, UINT frameIndex, RenderPassArgs* pipelineArgs)
{
	assert(pipelineArgs != nullptr);
	DeferredLightingRenderPassArgs& args = ToSpecificArgs<DeferredLightingRenderPassArgs>(pipelineArgs);

	auto commandList = GetCommandList(context, frameIndex);

	SetCommonStates(args.commonArgs, m_pipelineState, commandList);

	// Set default primitive topology for full screen quad.
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Set the descriptor table for gbuffer srvs.
	auto descHeapHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(args.commonArgs.cbvSrvUavHeapGlobal->GetGPUDescriptorHandleForHeapStart());
	descHeapHandle.Offset(GlobalDescriptors::GetDescriptorOffset(SRVGBuffers), args.commonArgs.cbvSrvUavDescSize);
	commandList->SetGraphicsRootDescriptorTable(DefaultRootParameterIdx::UAVSRVTableIdx, descHeapHandle);

	commandList->OMSetRenderTargets(1, &args.RTV, TRUE, nullptr);

	commandList->DrawInstanced(6, 1, 0, 0);
}

void DeferredLightingRenderPass::PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex)
{
	// No OP.
}

void DeferredLightingRenderPass::PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex)
{
	// No OP.
}