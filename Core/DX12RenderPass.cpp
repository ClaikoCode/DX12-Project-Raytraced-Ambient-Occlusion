#include "DX12RenderPass.h"

#include "GraphicsErrorHandling.h"
#include "DX12AbstractionUtils.h"

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

		commandLists[i]->Close() >> CHK_HR;
	}
}

void DX12RenderPass::Init()
{
	// Reset the command lists and allocators.

	for (UINT i = 0; i < NumContexts; i++)
	{
		commandAllocators[i]->Reset() >> CHK_HR;
		commandLists[i]->Reset(commandAllocators[i].Get(), m_pipelineState.Get()) >> CHK_HR;
	}

}

TriangleRenderPass::TriangleRenderPass(ID3D12Device* device, ComPtr<ID3D12PipelineState> pipelineState) 
	: DX12RenderPass(device, pipelineState) {}

void TriangleRenderPass::Render(UINT context, ComPtr<ID3D12Device> device, TriangleRenderPassArgs args)
{
	auto& commandList = commandLists[context];
	
	// Set root signature and pipeline state.
	commandList->SetGraphicsRootSignature(args.rootSignature.Get());
	commandList->SetPipelineState(m_pipelineState.Get());
	
	// Set vertex buffer as triangle list.
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->IASetVertexBuffers(0, 1, &args.vertexBufferView);

	// Configure RS.
	commandList->RSSetViewports(1, &args.viewport);
	commandList->RSSetScissorRects(1, &args.scissorRect);

	// Set render target.
	commandList->OMSetRenderTargets(1, &args.renderTargetView, TRUE, nullptr);

	static float t = 0;
	t += 1 / 144.0f;
	float angle = t * XM_2PI;
	const auto rotationMatrix = XMMatrixRotationZ(angle);

	const auto modelViewProjectionMatrix = XMMatrixTranspose(rotationMatrix * args.viewProjectionMatrix);

	commandList->SetGraphicsRoot32BitConstants(0, sizeof(modelViewProjectionMatrix) / sizeof(float), &modelViewProjectionMatrix, 0);
	
	// Draw.
	for (UINT i = context; i < args.drawArgs.size(); i += NumContexts)
	{
		const DrawArgs& drawArg = args.drawArgs[i];

		commandList->DrawInstanced(
			drawArg.vertexCount,
			1,
			drawArg.startVertex,
			drawArg.startInstance
		);
	}

	// Finish off by closing the command list.
	commandList->Close() >> CHK_HR;
}
