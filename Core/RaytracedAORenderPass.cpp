#include "RaytracedAORenderPass.h"

RaytracedAORenderPass::RaytracedAORenderPass(ComPtr<ID3D12Device5> device, ComPtr<ID3D12RootSignature> rootSig)
	: DX12RenderPass(device, D3D12_COMMAND_LIST_TYPE_COMPUTE, false)
{
	// Add specifically allowed rt render object.
	m_renderableObjects.push_back(RTRenderObjectID);
}

void RaytracedAORenderPass::BuildRenderPass(const std::vector<RenderPackage>& renderPackages, UINT context, UINT frameIndex, RenderPassArgs* pipelineArgs)
{
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