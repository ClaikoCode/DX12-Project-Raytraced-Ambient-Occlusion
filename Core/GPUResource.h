#pragma once


#include <vector>

#include "DirectXIncludes.h"
#include "GraphicsErrorHandling.h"

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
	
	template <typename T>
	void UploadResource(ComPtr<ID3D12Device5> device, ComPtr<ID3D12GraphicsCommandList> commandList, GPUResource& destBuffer, GPUResource& uploadBuffer, const T* data, UINT size)
	{
		CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(size);
		uploadBuffer = CreateUploadResource(device, bufferDesc);
		MapDataToBuffer(uploadBuffer, data, size);
		
		destBuffer = CreateDefaultResource(device, bufferDesc);

		commandList->CopyResource(destBuffer.Get(), uploadBuffer.Get());
	}

	template <typename T>
	void MapDataToBuffer(ComPtr<ID3D12Resource> uploadBuffer, const T* data, UINT size)
	{
		T* mappedData;
		uploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedData)) >> CHK_HR;
		memcpy(mappedData, static_cast<const void*>(data), size);
		uploadBuffer->Unmap(0, nullptr);
	}

}