#include "DX12RenderPass.h"

#include "GraphicsErrorHandling.h"
#include "DX12AbstractionUtils.h"


DX12RenderPass::DX12RenderPass(ComPtr<ID3D12Device5> device, D3D12_COMMAND_LIST_TYPE commandType, bool parallelizable) 
	: m_pipelineState(nullptr), m_renderableObjects({}), m_parallelizable(parallelizable)
{
	for (UINT bb = 0; bb < commandLists.size(); bb++)
	{
		for (UINT i = 0; i < NumContexts; i++)
		{
			device->CreateCommandAllocator(
				commandType,
				IID_PPV_ARGS(&commandAllocators[bb][i])
			) >> CHK_HR;

			NAME_D3D12_OBJECT_MEMBER_INDEXED(commandAllocators[bb], i, DX12RenderPass);

			device->CreateCommandList(
				0,
				commandType,
				commandAllocators[bb][i].Get(),
				m_pipelineState.Get(),
				IID_PPV_ARGS(&commandLists[bb][i])
			) >> CHK_HR;

			commandLists[bb][i]->Close() >> CHK_HR;

			NAME_D3D12_OBJECT_MEMBER_INDEXED(commandLists[bb], i, DX12RenderPass);
		}
	}
}

void DX12RenderPass::Init(UINT frameIndex)
{
	// Reset the command lists and allocators.
	for (UINT i = 0; i < NumContexts; i++)
	{
		commandAllocators[frameIndex][i]->Reset() >> CHK_HR;
		commandLists[frameIndex][i]->Reset(commandAllocators[frameIndex][i].Get(), m_pipelineState.Get()) >> CHK_HR;
	}
}

void DX12RenderPass::Close(UINT frameIndex, UINT context)
{
	commandLists[frameIndex][context]->Close() >> CHK_HR;
}

bool DX12RenderPass::IsContextAllowedToBuild(const UINT context) const
{
	return m_parallelizable || context == 0;
}

const std::vector<RenderObjectID>& DX12RenderPass::GetRenderableObjects() const
{
	return m_renderableObjects;
}

ComPtr<ID3D12GraphicsCommandList4> DX12RenderPass::GetCommandList(UINT context, UINT frameIndex)
{
	return commandLists[frameIndex][context];
}

ComPtr<ID3D12GraphicsCommandList4> DX12RenderPass::GetFirstCommandList(UINT frameIndex)
{
	return commandLists[frameIndex][0];
}

ComPtr<ID3D12GraphicsCommandList4> DX12RenderPass::GetLastCommandList(UINT frameIndex)
{
	return commandLists[frameIndex][NumContexts - 1];
}

void SetCommonStates(CommonRenderPassArgs commonArgs, ComPtr<ID3D12PipelineState> pipelineState, ComPtr<ID3D12GraphicsCommandList4> commandList)
{
	// Set root signature and pipeline state.
	commandList->SetGraphicsRootSignature(commonArgs.rootSignature.Get());
	commandList->SetPipelineState(pipelineState.Get());

	// Configure RS.
	commandList->RSSetViewports(1, &commonArgs.viewport);
	commandList->RSSetScissorRects(1, &commonArgs.scissorRect);

	// Set descriptor heap.
	std::array<ID3D12DescriptorHeap*, 1> descriptorHeaps = {
		commonArgs.cbvSrvUavHeapGlobal.Get()
	};
	commandList->SetDescriptorHeaps((UINT)descriptorHeaps.size(), descriptorHeaps.data());

	const auto vpMatrix = commonArgs.viewProjectionMatrix;
	commandList->SetGraphicsRoot32BitConstants(
		DefaultRootParameterIdx::MatrixIdx,
		sizeof(vpMatrix) / sizeof(float),
		&vpMatrix,
		0
	);

	commandList->SetGraphicsRootConstantBufferView(
		DefaultRootParameterIdx::CBVGlobalFrameDataIdx,
		commonArgs.globalFrameDataResource->GetGPUVirtualAddress()
	);
}

void DrawInstanceIndexed(UINT context, const std::vector<DrawArgs>& drawArgs, ComPtr<ID3D12GraphicsCommandList> commandList)
{
	for (UINT i = 0; i < drawArgs.size(); i++)
	{
		const DrawArgs& drawArg = drawArgs[i];

		commandList->DrawIndexedInstanced(
			drawArg.indexCount,
			1,
			drawArg.startIndex,
			drawArg.baseVertex,
			drawArg.startInstance
		);
	}
}

void SetInstanceCB(CommonRenderPassArgs& args, const UINT frameIndex, const RenderInstance& renderInstance, ComPtr<ID3D12GraphicsCommandList> commandList)
{
	auto cbvHeapHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(
		args.cbvSrvUavHeapGlobal->GetGPUDescriptorHandleForHeapStart(),
		FrameDescriptors::GetDescriptorOffsetCBVSRVUAV(CBVRenderInstance, frameIndex),
		args.cbvSrvUavDescSize
	);
	cbvHeapHandle.Offset(renderInstance.CBIndex, args.cbvSrvUavDescSize);

	commandList->SetGraphicsRootDescriptorTable(
		DefaultRootParameterIdx::CBVTableIdx,
		cbvHeapHandle
	);
}
