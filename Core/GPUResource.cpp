#include "GPUResource.h"

#include <array>

#include "GraphicsErrorHandling.h"
#include "DX12AbstractionUtils.h"
#include "AppDefines.h"

namespace DX12Abstractions
{

	GPUResource::GPUResource() 
		: GPUResource(D3D12_RESOURCE_STATE_COMMON) {}

	GPUResource::GPUResource(D3D12_RESOURCE_STATES initState) 
		: GPUResource(nullptr, initState) {}

	GPUResource::GPUResource(ComPtr<ID3D12Resource> _resource, D3D12_RESOURCE_STATES initState) 
		: resource(_resource), currentState(initState) {}

	GPUResource::operator ComPtr<ID3D12Resource>() const
	{
		return resource;
	}

	ResourceComPtrRef GPUResource::operator&()
	{
		return &resource;
	}

	void GPUResource::TransitionTo(D3D12_RESOURCE_STATES newState, ComPtr<ID3D12GraphicsCommandList> commandList)
	{  
		if (newState == currentState) {
			return;
		}

		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			resource.Get(),
			currentState,
			newState
		);

		commandList->ResourceBarrier(1, &barrier);

		currentState = newState;
	}

	ID3D12Resource* GPUResource::Get() const
	{
		return resource.Get();
	}

	GPUResource CreateResource(ComPtr<ID3D12Device4> device, CD3DX12_RESOURCE_DESC resourceDesc, D3D12_RESOURCE_STATES resourceState, D3D12_HEAP_TYPE heapType)
	{
		GPUResource resource(resourceState);

		D3D12_CLEAR_VALUE* optimizedClearValPtr = nullptr;
		D3D12_CLEAR_VALUE optimizedClearVal;

		bool canUseOptimizedClearVal =
			resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && // If it is a texture.
			resourceDesc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL); // If it has the right flags.
		
		if (canUseOptimizedClearVal)
		{
			optimizedClearVal = {
				.Format = resourceDesc.Format,
				.Color = { OptimizedClearColor[0], OptimizedClearColor[1],  OptimizedClearColor[2],  OptimizedClearColor[3] }
			};

			optimizedClearValPtr = &optimizedClearVal;
		}

		const CD3DX12_HEAP_PROPERTIES heapProps{ heapType };
		device->CreateCommittedResource1(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			resourceState,
			optimizedClearValPtr,
			nullptr,
			IID_PPV_ARGS(&resource)
		) >> CHK_HR;

		return resource;
	}

	GPUResource CreateUploadResource(ComPtr<ID3D12Device4> device, CD3DX12_RESOURCE_DESC resourceDesc)
	{
		return CreateResource(device, resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_HEAP_TYPE_UPLOAD);
	}

	GPUResource CreateDefaultResource(ComPtr<ID3D12Device4> device, CD3DX12_RESOURCE_DESC resourceDesc)
	{
		return CreateResource(device, resourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_HEAP_TYPE_DEFAULT);
	}

}
  