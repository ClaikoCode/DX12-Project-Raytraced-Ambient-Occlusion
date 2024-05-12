#pragma once 

#include "DirectXIncludes.h"
#include <array>
#include <vector>

#include "GPUResource.h"
#include "AppDefines.h"
#include "RenderObject.h"

using namespace Microsoft::WRL;
using namespace DirectX;
using DX12Abstractions::GPUResource;

typedef std::array<ComPtr<ID3D12CommandAllocator>, NumContexts> CommandAllocators;
typedef std::array<ComPtr<ID3D12GraphicsCommandList>, NumContexts> CommandLists;

struct CommonRenderPassArgs 
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE renderTargetView;
	CD3DX12_CPU_DESCRIPTOR_HANDLE depthStencilView;
	ComPtr<ID3D12RootSignature> rootSignature;
	CD3DX12_VIEWPORT viewport;
	CD3DX12_RECT scissorRect;

	ComPtr<ID3D12DescriptorHeap> cbvSrvUavHeap;
	UINT perInstanceCBVDescSize;

	float time;

	XMMATRIX viewProjectionMatrix;
};

void SetCommonStates(CommonRenderPassArgs commonArgs, ComPtr<ID3D12PipelineState> pipelineState, ComPtr<ID3D12GraphicsCommandList> commandList);

class DX12RenderPass
{
public:
	DX12RenderPass(ID3D12Device* device, ComPtr<ID3D12PipelineState> pipelineState);
	~DX12RenderPass() = default;

	void Init();
	void Close(UINT context);

public:

	CommandAllocators commandAllocators;
	CommandLists commandLists;
	
protected:
	ComPtr<ID3D12PipelineState> m_pipelineState;

};

class NonIndexedRenderPass : public DX12RenderPass
{
public:
	struct NonIndexedRenderPassArgs
	{
		CommonRenderPassArgs commonArgs;
	};

	NonIndexedRenderPass(ID3D12Device* device, ComPtr<ID3D12PipelineState> pipelineState) 
		: DX12RenderPass(device, pipelineState) {}


	void Render(const std::vector<RenderPackage>& renderPackages, UINT context, ComPtr<ID3D12Device> device, NonIndexedRenderPassArgs args);

};

class IndexedRenderPass : public DX12RenderPass
{
public:
	struct IndexedRenderPassArgs
	{
		CommonRenderPassArgs commonArgs;
	};

	IndexedRenderPass(ID3D12Device* device, ComPtr<ID3D12PipelineState> pipelineState)
		: DX12RenderPass(device, pipelineState) {}

	void Render(const std::vector<RenderPackage>& renderPackages, UINT context, ComPtr<ID3D12Device> device, IndexedRenderPassArgs args);
};