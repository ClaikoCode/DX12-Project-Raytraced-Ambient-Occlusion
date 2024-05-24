#include "DX12RenderPass.h"

#include "GraphicsErrorHandling.h"
#include "DX12AbstractionUtils.h"

/*
	HUGE TODO: Split these up into their own header files and source files so it is easier to read.

*/

void SetCommonStates(CommonRenderPassArgs commonArgs, ComPtr<ID3D12PipelineState> pipelineState, ComPtr<ID3D12GraphicsCommandList4> commandList)
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
		DefaultRootParameterIdx::MatrixIdx,
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
	auto cbvHeapHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(args.cbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart());
	cbvHeapHandle.Offset(renderInstance.CBIndex, args.cbvSrvUavDescSize);
	commandList->SetGraphicsRootDescriptorTable(
		DefaultRootParameterIdx::CBVTableIdx,
		cbvHeapHandle
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

void NonIndexedRenderPass::Render(const std::vector<RenderPackage>& renderPackages, UINT context, RenderPassArgs* pipelineArgs)
{
	// TODO: Handle this error better.
	assert(pipelineArgs != nullptr);

	NonIndexedRenderPassArgs& args = ToSpecificArgs<NonIndexedRenderPassArgs>(pipelineArgs);
	
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

			PerRenderObject(renderObject, pipelineArgs, context);

			const std::vector<DrawArgs>& drawArgs = renderObject.drawArgs;

			if (renderPackage.renderInstances)
			{
				const std::vector<RenderInstance>& renderInstances = *renderPackage.renderInstances;
			
				for (const RenderInstance& renderInstance : renderInstances)
				{
					PerRenderInstance(renderInstance, drawArgs, pipelineArgs, context);
				}
			}
		}
	}
}

void NonIndexedRenderPass::PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context)
{
	auto commandList = commandLists[context];

	commandList->IASetPrimitiveTopology(renderObject.topology);
	commandList->IASetVertexBuffers(0, 1, &renderObject.vertexBufferView);
}

void NonIndexedRenderPass::PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context)
{
	NonIndexedRenderPassArgs& args = ToSpecificArgs<NonIndexedRenderPassArgs>(pipelineArgs);
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

void IndexedRenderPass::Render(const std::vector<RenderPackage>& renderPackages, UINT context, RenderPassArgs* pipelineSpecificArgs)
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

void IndexedRenderPass::PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context)
{
	auto commandList = commandLists[context];

	commandList->IASetPrimitiveTopology(renderObject.topology);
	commandList->IASetVertexBuffers(0, 1, &renderObject.vertexBufferView);
	commandList->IASetIndexBuffer(&renderObject.indexBufferView);
}

void IndexedRenderPass::PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context)
{
	IndexedRenderPassArgs& args = ToSpecificArgs<IndexedRenderPassArgs>(pipelineArgs);
	auto commandList = commandLists[context];

	SetInstanceCB(args.commonArgs, renderInstance, commandList);

	DrawInstanceIndexed(context, drawArgs, commandList);
}

void DeferredGBufferRenderPass::Render(const std::vector<RenderPackage>& renderPackages, UINT context, RenderPassArgs* pipelineSpecificArgs)
{
	// TODO: Handle this error better.
	assert(pipelineSpecificArgs != nullptr);
	DeferredGBufferRenderPassArgs& args = ToSpecificArgs<DeferredGBufferRenderPassArgs>(pipelineSpecificArgs);

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

void DeferredGBufferRenderPass::PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context)
{
	auto commandList = commandLists[context];

	commandList->IASetPrimitiveTopology(renderObject.topology);
	commandList->IASetVertexBuffers(0, 1, &renderObject.vertexBufferView);
	commandList->IASetIndexBuffer(&renderObject.indexBufferView);
}

void DeferredGBufferRenderPass::PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context)
{
	DeferredGBufferRenderPassArgs& args = ToSpecificArgs<DeferredGBufferRenderPassArgs>(pipelineArgs);
	auto commandList = commandLists[context];

	SetInstanceCB(args.commonArgs, renderInstance, commandList);

	DrawInstanceIndexed(context, drawArgs, commandList);
}


void DeferredLightingRenderPass::Render(const std::vector<RenderPackage>& renderPackages, UINT context, RenderPassArgs* pipelineSpecificArgs)
{
	assert(pipelineSpecificArgs != nullptr);
	DeferredLightingRenderPassArgs& args = ToSpecificArgs<DeferredLightingRenderPassArgs>(pipelineSpecificArgs);

	auto commandList = commandLists[context];

	SetCommonStates(args.commonArgs, m_pipelineState, commandList);

	// Set default primitive topology for full screen quad.
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	
	// Set the descriptor table for gbuffer srvs.
	auto descHeapHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(args.commonArgs.cbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart());
	descHeapHandle.Offset(CBVSRVUAVOffsets::SRVOffsetGBuffers, args.commonArgs.cbvSrvUavDescSize);
	commandList->SetGraphicsRootDescriptorTable(DefaultRootParameterIdx::SRVTableIdx, descHeapHandle);

	commandList->OMSetRenderTargets(1, &args.RTV, TRUE, nullptr);

	commandList->DrawInstanced(6, 1, 0, 0);
}

void DeferredLightingRenderPass::PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context)
{
	// No OP.
}

void DeferredLightingRenderPass::PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context)
{
	// No OP.
}

void RaytracedAORenderPass::Render(const std::vector<RenderPackage>& renderPackages, UINT context, RenderPassArgs* pipelineSpecificArgs)
{
	// TODO: This is a temporary fix to not forget that this will not work for more contexts.
	assert(context == 0);
	if (context != 0)
		return;

	assert(pipelineSpecificArgs != nullptr);
	const RaytracedAORenderPassArgs& args = ToSpecificArgs<RaytracedAORenderPassArgs>(pipelineSpecificArgs);

	auto commandList = commandLists[context];

	// Set descriptor heap.
	std::array<ID3D12DescriptorHeap*, 1> descriptorHeaps = { args.commonRTArgs.cbvSrvUavHeap.Get() };
	commandList->SetDescriptorHeaps((UINT)descriptorHeaps.size(), descriptorHeaps.data());
	
	D3D12_DISPATCH_RAYS_DESC raytraceDesc = {};
	raytraceDesc.Width = args.screenWidth;
	raytraceDesc.Height = args.screenHeight;
	raytraceDesc.Depth = 1;

	//set shader tables
	DX12Abstractions::ShaderTableData* rayGenShaderTableData = args.commonRTArgs.rayGenShaderTable;
	assert(rayGenShaderTableData != nullptr);
	raytraceDesc.RayGenerationShaderRecord.StartAddress = rayGenShaderTableData->GetResourceGPUVirtualAddress();
	raytraceDesc.RayGenerationShaderRecord.SizeInBytes = rayGenShaderTableData->sizeInBytes;


	DX12Abstractions::ShaderTableData* missShaderTableData = args.commonRTArgs.missShaderTable;
	assert(missShaderTableData != nullptr);
	raytraceDesc.MissShaderTable.StartAddress = missShaderTableData->GetResourceGPUVirtualAddress();
	raytraceDesc.MissShaderTable.StrideInBytes = missShaderTableData->strideInBytes;
	raytraceDesc.MissShaderTable.SizeInBytes = missShaderTableData->sizeInBytes;


	DX12Abstractions::ShaderTableData* hitGroupShaderTableData = args.commonRTArgs.hitGroupShaderTable;
	assert(missShaderTableData != nullptr);
	raytraceDesc.HitGroupTable.StartAddress = hitGroupShaderTableData->GetResourceGPUVirtualAddress();
	raytraceDesc.HitGroupTable.StrideInBytes = hitGroupShaderTableData->strideInBytes;
	raytraceDesc.HitGroupTable.SizeInBytes = hitGroupShaderTableData->sizeInBytes;

	// Bind the empty root signature
	commandList->SetComputeRootSignature(args.commonRTArgs.globalRootSig.Get());
	commandList->SetComputeRoot32BitConstant(
		RTShaderRegisters::ConstantRegistersGlobal::ConstantRegister,
		args.frameCount,
		0
	);

	// Dispatch
	commandList->SetPipelineState1(args.stateObject.Get());
	commandList->DispatchRays(&raytraceDesc);
}

void RaytracedAORenderPass::PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context)
{

}

void RaytracedAORenderPass::PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context)
{

}
