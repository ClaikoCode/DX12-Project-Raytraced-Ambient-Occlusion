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

void DX12RenderPass::Close(UINT context)
{
	commandLists[context]->Close() >> CHK_HR;
}

void NonIndexedRenderPass::Render(const std::vector<RenderObject>& renderObjects, UINT context, ComPtr<ID3D12Device> device, NonIndexedRenderPassArgs args)
{
	auto& commandList = commandLists[context];
	
	// Set root signature and pipeline state.
	commandList->SetGraphicsRootSignature(args.rootSignature.Get());
	commandList->SetPipelineState(m_pipelineState.Get());
	
	// Set vertex buffer as triangle list.
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	

	// Configure RS.
	commandList->RSSetViewports(1, &args.viewport);
	commandList->RSSetScissorRects(1, &args.scissorRect);

	// Set render target and depth stencil.
	commandList->OMSetRenderTargets(1, &args.renderTargetView, TRUE, &args.depthStencilView);
	
	// Draw.
	for(const auto& renderObject : renderObjects)
	{
		float angle = args.time * XM_2PI;
		const auto rotationMatrix = XMMatrixRotationZ(angle);
		const auto translationMatrix = XMMatrixTranslation(0.0f, 1.0f, 0.0f);
		const auto combinedMatrix = rotationMatrix * translationMatrix;

		const auto modelViewProjectionMatrix = XMMatrixTranspose(combinedMatrix * args.viewProjectionMatrix);

		commandList->SetGraphicsRoot32BitConstants(0, sizeof(modelViewProjectionMatrix) / sizeof(float), &modelViewProjectionMatrix, 0);

		commandList->IASetVertexBuffers(0, 1, &renderObject.vertexBufferView);

		const std::vector<DrawArgs>& drawArgs = renderObject.drawArgs;
		for (UINT i = context; i < drawArgs.size(); i += NumContexts)
		{
			const DrawArgs& drawArg = drawArgs[i];

			commandList->DrawInstanced(
				drawArg.vertexCount,
				1,
				drawArg.startVertex,
				drawArg.startInstance
			);
		}
	}
}

void IndexedRenderPass::Render(const std::vector<RenderObject>& renderObjects, UINT context, ComPtr<ID3D12Device> device, IndexedRenderPassArgs args)
{
	auto& commandList = commandLists[context];

	// Set root signature and pipeline state.
	commandList->SetGraphicsRootSignature(args.rootSignature.Get());
	commandList->SetPipelineState(m_pipelineState.Get());

	// Set vertex buffer as triangle list.
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Configure RS.
	commandList->RSSetViewports(1, &args.viewport);
	commandList->RSSetScissorRects(1, &args.scissorRect);

	// Set render target and depth stencil.
	commandList->OMSetRenderTargets(1, &args.renderTargetView, TRUE, &args.depthStencilView);

	for(const auto& renderObject : renderObjects)
	{
		float angle = args.time * XM_2PI;
		const auto rotationMatrix = XMMatrixRotationX(angle * 1.2f + 1.0f) * XMMatrixRotationY(angle * 0.8f + 1.3f) * XMMatrixRotationZ(angle * 1.0f + 3.0f);
		const auto translationMatrix = XMMatrixTranslation(1.0f, 0.0f, 0.0f);
		const auto scaleMatrix = XMMatrixScaling(0.1f, 0.1f, 0.1f);
		const auto combinedMatrix = scaleMatrix * rotationMatrix * translationMatrix;

		const auto modelViewProjectionMatrix = XMMatrixTranspose(combinedMatrix * args.viewProjectionMatrix);

		commandList->SetGraphicsRoot32BitConstants(0, sizeof(modelViewProjectionMatrix) / sizeof(float), &modelViewProjectionMatrix, 0);
		commandList->IASetVertexBuffers(0, 1, &renderObject.vertexBufferView);
		commandList->IASetIndexBuffer(&renderObject.indexBufferView);

		// Draw.
		const std::vector<DrawArgs>& drawArgs = renderObject.drawArgs;
		for (UINT i = context; i < drawArgs.size(); i += NumContexts)
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
}
