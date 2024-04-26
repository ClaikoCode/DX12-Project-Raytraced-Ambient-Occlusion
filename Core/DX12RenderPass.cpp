#include "DX12RenderPass.h"

#include "GraphicsErrorHandling.h"

DX12RenderPass::DX12RenderPass(ID3D12Device* device, ComPtr<ID3D12PipelineState> pipelineState) :
	m_pipelineState(pipelineState) 
{
	for (UINT i = 0; i < NumContexts; i++)
	{
		device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(&commandAllocators[i])
		) >> CHK_HR;

		device->CreateCommandList(
			0, 
			D3D12_COMMAND_LIST_TYPE_DIRECT, 
			commandAllocators[i].Get(), 
			m_pipelineState.Get(), 
			IID_PPV_ARGS(&commandLists[i])
		) >> CHK_HR;

		commandLists[i]->Close();
	}
}

DX12RenderPass::~DX12RenderPass()
{
	// Not implemented
}

void TriangleRend







erPass::Init()
{


}
