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

DX12RenderPass::DX12RenderPass(ComPtr<ID3D12Device5> device, D3D12_COMMAND_LIST_TYPE commandType) 
	: m_pipelineState(nullptr), m_renderableObjects({}), m_enabled(true)
{
	for (UINT bb = 0; bb < commandLists.size(); bb++)
	{
		for (UINT i = 0; i < NumContexts; i++)
		{
			device->CreateCommandAllocator(
				commandType,
				IID_PPV_ARGS(&commandAllocators[bb][i])
			) >> CHK_HR;

			device->CreateCommandList(
				0,
				commandType,
				commandAllocators[bb][i].Get(),
				m_pipelineState.Get(),
				IID_PPV_ARGS(&commandLists[bb][i])
			) >> CHK_HR;

			commandLists[bb][i]->Close() >> CHK_HR;
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

const std::vector<RenderObjectID>& DX12RenderPass::GetRenderableObjects() const
{
	return m_renderableObjects;
}

void DX12RenderPass::Enable()
{
	m_enabled = true;
}

void DX12RenderPass::Disable()
{
	m_enabled = false;
}

bool DX12RenderPass::IsEnabled()
{
	return m_enabled;
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

void NonIndexedRenderPass::Render(const std::vector<RenderPackage>& renderPackages, UINT context, UINT frameIndex, RenderPassArgs* pipelineArgs)
{
	// TODO: Handle this error better.
	assert(pipelineArgs != nullptr);

	NonIndexedRenderPassArgs& args = ToSpecificArgs<NonIndexedRenderPassArgs>(pipelineArgs);
	
	auto commandList = GetCommandList(context, frameIndex);
	SetCommonStates(args.commonArgs, m_pipelineState, commandList);

	// Set render target and depth stencil.
	commandList->OMSetRenderTargets(1, &args.RTV, TRUE, &args.commonArgs.depthStencilView);

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
			
				for (const RenderInstance& renderInstance : renderInstances)
				{
					PerRenderInstance(renderInstance, drawArgs, pipelineArgs, context, frameIndex);
				}
			}
		}
	}
}

void NonIndexedRenderPass::PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex)
{
	auto commandList = GetCommandList(context, frameIndex);

	commandList->IASetPrimitiveTopology(renderObject.topology);
	commandList->IASetVertexBuffers(0, 1, &renderObject.vertexBufferView);
}

void NonIndexedRenderPass::PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex)
{
	NonIndexedRenderPassArgs& args = ToSpecificArgs<NonIndexedRenderPassArgs>(pipelineArgs);
	auto commandList = GetCommandList(context, frameIndex);

	SetInstanceCB(args.commonArgs, frameIndex, renderInstance, commandList);

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

void IndexedRenderPass::Render(const std::vector<RenderPackage>& renderPackages, UINT context, UINT frameIndex, RenderPassArgs* pipelineArgs)
{
	// TODO: Handle this error better.
	assert(pipelineArgs != nullptr);
	IndexedRenderPassArgs& args = *reinterpret_cast<IndexedRenderPassArgs*>(pipelineArgs);

	auto commandList = GetCommandList(context, frameIndex);
	SetCommonStates(args.commonArgs, m_pipelineState, commandList);

	// Set render target and depth stencil.
	commandList->OMSetRenderTargets(1, &args.RTV, TRUE, &args.commonArgs.depthStencilView);

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

				for (const RenderInstance& renderInstance : renderInstances)
				{
					PerRenderInstance(renderInstance, drawArgs, pipelineArgs, context, frameIndex);
				}
			}
		}
	}
}

void IndexedRenderPass::PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex)
{
	auto commandList = GetCommandList(context, frameIndex);

	commandList->IASetPrimitiveTopology(renderObject.topology);
	commandList->IASetVertexBuffers(0, 1, &renderObject.vertexBufferView);
	commandList->IASetIndexBuffer(&renderObject.indexBufferView);
}

void IndexedRenderPass::PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex)
{
	IndexedRenderPassArgs& args = ToSpecificArgs<IndexedRenderPassArgs>(pipelineArgs);
	auto commandList = GetCommandList(context, frameIndex);

	SetInstanceCB(args.commonArgs, frameIndex, renderInstance, commandList);

	DrawInstanceIndexed(context, drawArgs, commandList);
}

DeferredGBufferRenderPass::DeferredGBufferRenderPass(ComPtr<ID3D12Device5> device, ComPtr<ID3D12RootSignature> rootSig) 
	: DX12RenderPass(device, D3D12_COMMAND_LIST_TYPE_DIRECT)
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
	D3DReadFileToBlob(L"../VertexShader.cso", &vsBlob) >> CHK_HR;

	ComPtr<ID3DBlob> psBlob;
	D3DReadFileToBlob(L"../DeferredPixelShader.cso", &psBlob) >> CHK_HR;

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

void DeferredGBufferRenderPass::Render(const std::vector<RenderPackage>& renderPackages, UINT context, UINT frameIndex, RenderPassArgs* pipelineArgs)
{
	// TODO: Handle this error better.
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

				for (const RenderInstance& renderInstance : renderInstances)
				{
					PerRenderInstance(renderInstance, drawArgs, pipelineArgs, context, frameIndex);
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


DeferredLightingRenderPass::DeferredLightingRenderPass(ComPtr<ID3D12Device5> device, ComPtr<ID3D12RootSignature> rootSig) 
	: DX12RenderPass(device, D3D12_COMMAND_LIST_TYPE_DIRECT)
{
	struct DeferredLightingStateStream
	{
		CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
		CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
		CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimtiveTopology;
		CD3DX12_PIPELINE_STATE_STREAM_VS VS;
		CD3DX12_PIPELINE_STATE_STREAM_PS PS;
		CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
		CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
	} deferredLightingStateStream;

	// TODO: Check if this is even needed. No actual data for input is used as this is a FSQ drawn from the VS.
	const D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	ComPtr<ID3DBlob> vsBlob;
	D3DReadFileToBlob(L"../FSQVS.cso", &vsBlob) >> CHK_HR;

	ComPtr<ID3DBlob> psBlob;
	D3DReadFileToBlob(L"../DeferredLightingPS.cso", &psBlob) >> CHK_HR;

	deferredLightingStateStream.RootSignature = rootSig.Get();
	deferredLightingStateStream.InputLayout = { inputLayout, (UINT)std::size(inputLayout) };
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

void DeferredLightingRenderPass::Render(const std::vector<RenderPackage>& renderPackages, UINT context, UINT frameIndex, RenderPassArgs* pipelineArgs)
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

RaytracedAORenderPass::RaytracedAORenderPass(ComPtr<ID3D12Device5> device, ComPtr<ID3D12RootSignature> rootSig) 
	: DX12RenderPass(device, D3D12_COMMAND_LIST_TYPE_COMPUTE)
{	
	// Add specifically allowed rt render object.
	m_renderableObjects.push_back(RTRenderObjectID);
}

void RaytracedAORenderPass::Render(const std::vector<RenderPackage>& renderPackages, UINT context, UINT frameIndex, RenderPassArgs* pipelineArgs)
{
	// TODO: This feels like a hack. 
	// Maybe there is a more official way to handle this (single thread flag?).
	if (context != 0)  
		return;

	assert(pipelineArgs != nullptr);
	const RaytracedAORenderPassArgs& args = ToSpecificArgs<RaytracedAORenderPassArgs>(pipelineArgs);

	auto commandList = GetCommandList(context, frameIndex);

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

	std::vector<CD3DX12_RESOURCE_BARRIER> barriers = {};
	for (const RayTracingRenderPackage& rtRenderPackage : args.renderPackages)
	{
		// TODO: Make this input shared between the initial creation and now.
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS rtInputs = {};
		rtInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		rtInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
		rtInputs.NumDescs = rtRenderPackage.instanceCount;
		rtInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

		DX12Abstractions::AccelerationStructureBuffers* topAccStruct = rtRenderPackage.topLevelASBuffers;

		// Create the TLAS
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
		asDesc.Inputs = rtInputs;
		asDesc.Inputs.InstanceDescs = topAccStruct->instanceDesc.resource->GetGPUVirtualAddress();
		asDesc.DestAccelerationStructureData = topAccStruct->result.resource->GetGPUVirtualAddress();
		asDesc.ScratchAccelerationStructureData = topAccStruct->scratch.resource->GetGPUVirtualAddress();

		commandList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);

		// UAV barrier needed before using the acceleration structures in a ray tracing operation
		CD3DX12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(topAccStruct->result.Get());
		barriers.push_back(uavBarrier);
	}

	// Put UAV barriers for all the acceleration structures
	commandList->ResourceBarrier((UINT)barriers.size(), barriers.data());

	// Dispatch
	commandList->SetPipelineState1(args.stateObject.Get());
	commandList->DispatchRays(&raytraceDesc);
}

void RaytracedAORenderPass::PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex)
{
	// NO OP
}

void RaytracedAORenderPass::PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex)
{
	// NO OP
}

AccumilationRenderPass::AccumilationRenderPass(ComPtr<ID3D12Device5> device, ComPtr<ID3D12RootSignature> rootSig) 
	: DX12RenderPass(device, D3D12_COMMAND_LIST_TYPE_DIRECT)
{
	struct AccumilationPipelineStateStream
	{
		CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
		CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
		CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimtiveTopology;
		CD3DX12_PIPELINE_STATE_STREAM_VS VS;
		CD3DX12_PIPELINE_STATE_STREAM_PS PS;
		CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
		CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
	} accumilationPipelineStateStream;

	// TODO: Check if this is even needed. No actual data for input is used as this is a FSQ drawn from the VS.
	const D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	ComPtr<ID3DBlob> vsBlob;
	D3DReadFileToBlob(L"../FSQVS.cso", &vsBlob) >> CHK_HR;

	ComPtr<ID3DBlob> psBlob;
	D3DReadFileToBlob(L"../AccumilationPS.cso", &psBlob) >> CHK_HR;

	accumilationPipelineStateStream.RootSignature = rootSig.Get();
	accumilationPipelineStateStream.InputLayout = { inputLayout, (UINT)std::size(inputLayout) };
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

void AccumilationRenderPass::Render(const std::vector<RenderPackage>& renderPackages, UINT context, UINT frameIndex, RenderPassArgs* pipelineArgs)
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
