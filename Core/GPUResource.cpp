#include "GPUResource.h"
#include "GraphicsErrorHandling.h"
#include "DX12AbstractionUtils.h"

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
		const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
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

		const CD3DX12_HEAP_PROPERTIES heapProps(heapType);
		device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			resourceState,
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
		return CreateResource(device, resourceDesc, D3D12_RESOURCE_STATE_COMMON, D3D12_HEAP_TYPE_DEFAULT);
	}
}
