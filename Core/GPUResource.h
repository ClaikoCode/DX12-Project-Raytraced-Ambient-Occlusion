#pragma once

#include "DirectXIncludes.h"

using Microsoft::WRL::ComPtr;

namespace DX12Abstractions
{
	typedef Microsoft::WRL::Details::ComPtrRef<ComPtr<ID3D12Resource>> ResourceComPtrRef;

	// Abstraction of a general GPU resource.
	struct GPUResource
	{
		GPUResource();
		GPUResource(D3D12_RESOURCE_STATES initState);
		GPUResource(ComPtr<ID3D12Resource> _resource, D3D12_RESOURCE_STATES initState);

		// Allows the resource to be cast to its ComPtr implicitly.
		operator ComPtr<ID3D12Resource>() const;
		// This enables the GPU resource to be used in usual D3D patterns of casting to void** for address instantiation. 
		ResourceComPtrRef operator&();
		
		// Puts a transition resource barrier into the command list to transition the resource to the new state.
		void TransitionTo(D3D12_RESOURCE_STATES newState, ComPtr<ID3D12GraphicsCommandList> commandList);
		ID3D12Resource* Get() const;

		ComPtr<ID3D12Resource> resource;
		D3D12_RESOURCE_STATES currentState;
	};

	GPUResource CreateResource(ComPtr<ID3D12Device4> device, CD3DX12_RESOURCE_DESC resourceDesc, D3D12_RESOURCE_STATES resourceState, D3D12_HEAP_TYPE heapType);
	GPUResource CreateUploadResource(ComPtr<ID3D12Device4> device, CD3DX12_RESOURCE_DESC resourceDesc);
	GPUResource CreateDefaultResource(ComPtr<ID3D12Device4> device, CD3DX12_RESOURCE_DESC resourceDesc);
}