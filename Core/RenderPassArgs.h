#pragma once

#include "DirectXIncludes.h"
#include "DXRAbstractions.h"
#include <variant>

struct CommonRenderPassArgs
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE depthStencilView;
	ComPtr<ID3D12RootSignature> rootSignature;
	CD3DX12_VIEWPORT viewport;
	CD3DX12_RECT scissorRect;

	ComPtr<ID3D12DescriptorHeap> cbvSrvUavHeap;
	UINT cbvSrvUavDescSize;

	float time;

	DirectX::XMMATRIX viewProjectionMatrix;
};

struct CommonRaytracingRenderPassArgs
{
	ComPtr<ID3D12DescriptorHeap> cbvSrvUavHeap;
	UINT cbvSrvUavDescSize;

	ComPtr<ID3D12RootSignature> globalRootSig;

	DX12Abstractions::ShaderTableData* rayGenShaderTable;
	DX12Abstractions::ShaderTableData* hitGroupShaderTable;
	DX12Abstractions::ShaderTableData* missShaderTable;
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
	CommonRenderPassArgs commonArgs;

	CD3DX12_CPU_DESCRIPTOR_HANDLE RTV;
};

struct RaytracedAORenderPassArgs
{
	CommonRaytracingRenderPassArgs commonRTArgs;

	ComPtr<ID3D12StateObject> stateObject;
	UINT frameCount;
	UINT screenWidth;
	UINT screenHeight;
};

struct AccumilationRenderPassArgs
{
	CommonRenderPassArgs commonArgs;

	CD3DX12_CPU_DESCRIPTOR_HANDLE RTVTargetFrame;
	CD3DX12_CPU_DESCRIPTOR_HANDLE SRVCurrentFrame;
	CD3DX12_CPU_DESCRIPTOR_HANDLE SRVPrevFrame;
};

// This acts as a union of sorts but is safer in the way that
// if a certain type is trying to be fetched from the variant is not the same as the one that was previously written 
// then an exception is thrown. For my app, this only gives me upsides as there is no need for any other niche usage pattern.
// A union of structs would not work as the structs themselves hold complex objects with non-trivial destructors. 
using RenderPassArgs = std::variant
<
	NonIndexedRenderPassArgs, 
	IndexedRenderPassArgs, 
	DeferredGBufferRenderPassArgs, 
	DeferredLightingRenderPassArgs,
	RaytracedAORenderPassArgs,
	AccumilationRenderPassArgs
>;