#include "DX12RenderPass.h"

#include "GraphicsErrorHandling.h"
#include "DX12AbstractionUtils.h"

void SetCommonStates(CommonRenderPassArgs commonArgs, ComPtr<ID3D12PipelineState> pipelineState, ComPtr<ID3D12GraphicsCommandList> commandList)
{
	// Set root signature and pipeline state.
	commandList->SetGraphicsRootSignature(commonArgs.rootSignature.Get());
	commandList->SetPipelineState(pipelineState.Get());

	// Configure RS.
	commandList->RSSetViewports(1, &commonArgs.viewport);
	commandList->RSSetScissorRects(1, &commonArgs.scissorRect);

	// Set descriptor heap.
	std::array<ID3D12DescriptorHeap*, 1> descriptorHeaps = { commonArgs.cbvSrvUavHeap.Get() };
	commandList->SetDescriptorHeaps((UINT)descriptorHeaps.size(), descriptorHeaps.data());

	const auto vpMatrix = commonArgs.viewProjectionMatrix;
	commandList->SetGraphicsRoot32BitConstants(
		RootSigRegisters::CBRegisters::MatrixConstants,
		sizeof(vpMatrix) / sizeof(float),
		&vpMatrix,
		0
	);
}

void DrawInstanceIndexed(UINT context, const std::vector<DrawArgs>& drawArgs, ComPtr<ID3D12GraphicsCommandList> commandList)
{
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

void SetInstanceCB(CommonRenderPassArgs& args, const RenderInstance& renderInstance, ComPtr<ID3D12GraphicsCommandList> commandList)
{
	auto instanceCBVHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(args.cbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart());
	instanceCBVHandle.Offset(renderInstance.CBIndex, args.perInstanceCBVDescSize);
	commandList->SetGraphicsRootDescriptorTable(
		RootSigRegisters::CBRegisters::CBDescriptorTable,
		instanceCBVHandle
	);
}

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

void NonIndexedRenderPass::Render(const std::vector<RenderPackage>& renderPackages, UINT context, void* pipelineSpecificArgs)
{
	// TODO: Handle this error better.
	assert(pipelineSpecificArgs != nullptr);

	NonIndexedRenderPassArgs args = ToSpecificArgs<NonIndexedRenderPassArgs>(pipelineSpecificArgs);
	
	auto commandList = commandLists[context];
	SetCommonStates(args.commonArgs, m_pipelineState, commandList);

	// Set render target and depth stencil.
	commandList->OMSetRenderTargets(1, &args.RTV, TRUE, &args.commonArgs.depthStencilView);

	// Draw.
	for (const RenderPackage& renderPackage : renderPackages)
	{
		if (renderPackage.renderObject)
		{
			const RenderObject& renderObject = *renderPackage.renderObject;

			PerRenderObject(renderObject, pipelineSpecificArgs, context);

			const std::vector<DrawArgs>& drawArgs = renderObject.drawArgs;

			if (renderPackage.renderInstances)
			{
				const std::vector<RenderInstance>& renderInstances = *renderPackage.renderInstances;
			
				for (const RenderInstance& renderInstance : renderInstances)
				{
					PerRenderInstance(renderInstance, drawArgs, pipelineSpecificArgs, context);
				}
			}
		}
	}
}

void NonIndexedRenderPass::PerRenderObject(const RenderObject& renderObject, void* pipelineSpecificArgs, UINT context)
{
	auto commandList = commandLists[context];

	commandList->IASetPrimitiveTopology(renderObject.topology);
	commandList->IASetVertexBuffers(0, 1, &renderObject.vertexBufferView);
}

void NonIndexedRenderPass::PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, void* pipelineSpecificArgs, UINT context)
{
	NonIndexedRenderPassArgs args = ToSpecificArgs<NonIndexedRenderPassArgs>(pipelineSpecificArgs);
	auto commandList = commandLists[context];

	SetInstanceCB(args.commonArgs, renderInstance, commandList);

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

void IndexedRenderPass::Render(const std::vector<RenderPackage>& renderPackages, UINT context, void* pipelineSpecificArgs)
{
	// TODO: Handle this error better.
	assert(pipelineSpecificArgs != nullptr);
	IndexedRenderPassArgs& args = *reinterpret_cast<IndexedRenderPassArgs*>(pipelineSpecificArgs);

	auto commandList = commandLists[context];
	SetCommonStates(args.commonArgs, m_pipelineState, commandList);

	// Set render target and depth stencil.
	commandList->OMSetRenderTargets(1, &args.RTV, TRUE, &args.commonArgs.depthStencilView);

	// Draw.
	for (const RenderPackage& renderPackage : renderPackages)
	{
		if (renderPackage.renderObject)
		{
			const RenderObject& renderObject = *renderPackage.renderObject;

			PerRenderObject(renderObject, pipelineSpecificArgs, context);

			const std::vector<DrawArgs>& drawArgs = renderObject.drawArgs;

			if (renderPackage.renderInstances)
			{
				const std::vector<RenderInstance>& renderInstances = *renderPackage.renderInstances;

				for (const RenderInstance& renderInstance : renderInstances)
				{
					PerRenderInstance(renderInstance, drawArgs, pipelineSpecificArgs, context);
				}
			}
		}
	}
}

void IndexedRenderPass::PerRenderObject(const RenderObject& renderObject, void* pipelineSpecificArgs, UINT context)
{
	auto commandList = commandLists[context];

	commandList->IASetPrimitiveTopology(renderObject.topology);
	commandList->IASetVertexBuffers(0, 1, &renderObject.vertexBufferView);
	commandList->IASetIndexBuffer(&renderObject.indexBufferView);
}

void IndexedRenderPass::PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, void* pipelineSpecificArgs, UINT context)
{
	IndexedRenderPassArgs args = ToSpecificArgs<IndexedRenderPassArgs>(pipelineSpecificArgs);
	auto commandList = commandLists[context];

	SetInstanceCB(args.commonArgs, renderInstance, commandList);

	DrawInstanceIndexed(context, drawArgs, commandList);
}

void GBufferRenderPass::Render(const std::vector<RenderPackage>& renderPackages, UINT context, void* pipelineSpecificArgs)
{
	// TODO: Handle this error better.
	assert(pipelineSpecificArgs != nullptr);
	GBufferRenderPassArgs& args = *reinterpret_cast<GBufferRenderPassArgs*>(pipelineSpecificArgs);

	auto commandList = commandLists[context];
	SetCommonStates(args.commonArgs, m_pipelineState, commandList);

	// Set render targets and depth stencil. The RTVs are assumed to be contiguous in memory, 
	// which is why the handle to the first GBuffer RTV is required.
	commandList->OMSetRenderTargets(GBufferCount, &args.firstGBufferRTVHandle, TRUE, &args.commonArgs.depthStencilView);

	// Draw.
	for (const RenderPackage& renderPackage : renderPackages)
	{
		if (renderPackage.renderObject)
		{
			const RenderObject& renderObject = *renderPackage.renderObject;

			PerRenderObject(renderObject, pipelineSpecificArgs, context);

			const std::vector<DrawArgs>& drawArgs = renderObject.drawArgs;

			if (renderPackage.renderInstances)
			{
				const std::vector<RenderInstance>& renderInstances = *renderPackage.renderInstances;

				for (const RenderInstance& renderInstance : renderInstances)
				{
					PerRenderInstance(renderInstance, drawArgs, pipelineSpecificArgs, context);
				}
			}
		}
	}
}

void GBufferRenderPass::PerRenderObject(const RenderObject& renderObject, void* pipelineSpecificArgs, UINT context)
{
	auto commandList = commandLists[context];

	commandList->IASetPrimitiveTopology(renderObject.topology);
	commandList->IASetVertexBuffers(0, 1, &renderObject.vertexBufferView);
	commandList->IASetIndexBuffer(&renderObject.indexBufferView);
}

void GBufferRenderPass::PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, void* pipelineSpecificArgs, UINT context)
{
	GBufferRenderPassArgs args = ToSpecificArgs<GBufferRenderPassArgs>(pipelineSpecificArgs);
	auto commandList = commandLists[context];

	SetInstanceCB(args.commonArgs, renderInstance, commandList);

	DrawInstanceIndexed(context, drawArgs, commandList);
}


