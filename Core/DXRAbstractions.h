#pragma once

#include "DirectXIncludes.h"
#include "GPUResource.h"

namespace DX12Abstractions 
{
	struct AccelerationStructureBuffers
	{
		GPUResource scratch;
		GPUResource result;
		GPUResource instanceDesc;
	};

	struct ShaderTableData
	{
		uint64_t sizeInBytes;
		uint32_t strideInBytes;
		GPUResource tableResource;

		D3D12_GPU_VIRTUAL_ADDRESS GetResourceGPUVirtualAddress()
		{
			return tableResource.resource->GetGPUVirtualAddress();
		}
	};

}