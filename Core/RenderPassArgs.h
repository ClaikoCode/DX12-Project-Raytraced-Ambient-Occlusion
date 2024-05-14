#pragma once

#include "DirectXIncludes.h"
#include <variant>

struct CommonRenderPassArgs
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE depthStencilView;
	ComPtr<ID3D12RootSignature> rootSignature;
	CD3DX12_VIEWPORT viewport;
	CD3DX12_RECT scissorRect;

	ComPtr<ID3D12DescriptorHeap> cbvSrvUavHeap;
	UINT perInstanceCBVDescSize;

	float time;

	DirectX::XMMATRIX viewProjectionMatrix;
};

struct NonIndexedRenderPassArgs
{
	CommonRenderPassArgs commonArgs;

	CD3DX12_CPU_DESCRIPTOR_HANDLE RTV;
};

struct IndexedRenderPassArgs
{
	CommonRenderPassArgs commonArgs;

	CD3DX12_CPU_DESCRIPTOR_HANDLE RTV;
};

struct DeferredGBufferRenderPassArgs
{
	CommonRenderPassArgs commonArgs;

	CD3DX12_CPU_DESCRIPTOR_HANDLE firstGBufferRTVHandle;
};

struct DeferredLightingRenderPassArgs
{
	int a;
};

using RenderPassArgsVariant = std::variant
<
	NonIndexedRenderPassArgs, 
	IndexedRenderPassArgs, 
	DeferredGBufferRenderPassArgs, 
	DeferredLightingRenderPassArgs
>;