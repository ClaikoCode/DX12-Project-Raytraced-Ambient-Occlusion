#include "AccumilationRenderPass.h"

AccumilationRenderPass::AccumilationRenderPass(ComPtr<ID3D12Device5> device, ComPtr<ID3D12RootSignature> rootSig)
	: DX12RenderPass(device, D3D12_COMMAND_LIST_TYPE_DIRECT, false)
{
	struct AccumilationPipelineStateStream
	{
		CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
		CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimtiveTopology;
		CD3DX12_PIPELINE_STATE_STREAM_VS VS;
		CD3DX12_PIPELINE_STATE_STREAM_PS PS;
		CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
		CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
	} accumilationPipelineStateStream;

	ComPtr<ID3DBlob> vsBlob;
	D3DReadFileToBlob(L"../FullScreenQuadVS.cso", &vsBlob) >> CHK_HR;

	ComPtr<ID3DBlob> psBlob;
	D3DReadFileToBlob(L"../AccumulationPS.cso", &psBlob) >> CHK_HR;

	accumilationPipelineStateStream.RootSignature = rootSig.Get();
	accumilationPipelineStateStream.PrimtiveTopology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	accumilationPipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vsBlob.Get());
	accumilationPipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(psBlob.Get());
	accumilationPipelineStateStream.RTVFormats = { { BackBufferFormat }, 1 };
	accumilationPipelineStateStream.DSVFormat = DXGI_FORMAT_UNKNOWN;

	const D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = {
			.SizeInBytes = sizeof(AccumilationPipelineStateStream),
			.pPipelineStateSubobjectStream = &accumilationPipelineStateStream
	};

	device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&m_pipelineState)) >> CHK_HR;

	NAME_D3D12_OBJECT_MEMBER(m_pipelineState, AccumilationRenderPass);
}

void AccumilationRenderPass::BuildRenderPass(const std::vector<RenderPackage>& renderPackages, UINT context, UINT frameIndex, RenderPassArgs* pipelineArgs)
{
	assert(pipelineArgs != nullptr);
	const AccumulationRenderPassArgs& args = ToSpecificArgs<AccumulationRenderPassArgs>(pipelineArgs);

	auto commandList = GetCommandList(context, frameIndex);

	SetCommonStates(args.commonArgs, m_pipelineState, commandList);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->OMSetRenderTargets(1, &args.RTVTargetFrame, TRUE, nullptr);

	// Set the descriptor table for SRVs and UAVs
	auto descHeapHandleBase = CD3DX12_GPU_DESCRIPTOR_HANDLE(
		args.commonArgs.cbvSrvUavHeapGlobal->GetGPUDescriptorHandleForHeapStart(),
		GlobalDescriptors::GetDescriptorOffset(SRVGBuffers),
		args.commonArgs.cbvSrvUavDescSize
	);

	commandList->SetGraphicsRootDescriptorTable(DefaultRootParameterIdx::UAVSRVTableIdx, descHeapHandleBase);

	commandList->DrawInstanced(6, 1, 0, 0);
}

void AccumilationRenderPass::PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex)
{
	// NO OP
}

void AccumilationRenderPass::PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex)
{
	// NO OP
}
