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

// Assumes that the void* is not null. This assertion should happen before usage.
template<typename T>
T ToSpecificArgs(void* pipelineSpecificArgs)
{
	return *reinterpret_cast<T*>(pipelineSpecificArgs);
}

// Abstract class for a direct render pass.
class DX12RenderPass
{
public:
	DX12RenderPass(ID3D12Device* device, ComPtr<ID3D12PipelineState> pipelineState);
	~DX12RenderPass() = default;

	void Init();
	void Close(UINT context);

	virtual void Render(const std::vector<RenderPackage>& renderPackages, UINT context, void* pipelineSpecificArgs) = 0;
	virtual void PerRenderObject(const RenderObject& renderObject, void* pipelineSpecificArgs, UINT context) = 0;
	virtual void PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, void* pipelineSpecificArgs, UINT context) = 0;

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
		
		CD3DX12_CPU_DESCRIPTOR_HANDLE RTV;
	};

	NonIndexedRenderPass(ID3D12Device* device, ComPtr<ID3D12PipelineState> pipelineState) 
		: DX12RenderPass(device, pipelineState) {}

	void Render(const std::vector<RenderPackage>& renderPackages, UINT context, void* pipelineSpecificArgs) override final;
	void PerRenderObject(const RenderObject& renderObject, void* pipelineSpecificArgs, UINT context) override final;
	void PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, void* pipelineSpecificArgs, UINT context) override final;
	
};

class IndexedRenderPass : public DX12RenderPass
{
public:
	struct IndexedRenderPassArgs
	{
		CommonRenderPassArgs commonArgs;

		CD3DX12_CPU_DESCRIPTOR_HANDLE RTV;
	};

	IndexedRenderPass(ID3D12Device* device, ComPtr<ID3D12PipelineState> pipelineState)
		: DX12RenderPass(device, pipelineState) {}

	void Render(const std::vector<RenderPackage>& renderPackages, UINT context, void* pipelineSpecificArgs) override final;
	void PerRenderObject(const RenderObject& renderObject, void* pipelineSpecificArgs, UINT context) override final;
	void PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, void* pipelineSpecificArgs, UINT context) override final;
};

class DeferredGBufferRenderPass : public DX12RenderPass
{
public:
	struct GBufferRenderPassArgs
	{
		CommonRenderPassArgs commonArgs;

		CD3DX12_CPU_DESCRIPTOR_HANDLE firstGBufferRTVHandle;
	};

	DeferredGBufferRenderPass(ID3D12Device* device, ComPtr<ID3D12PipelineState> pipelineState)
		: DX12RenderPass(device, pipelineState) {}

	void Render(const std::vector<RenderPackage>& renderPackages, UINT context, void* pipelineSpecificArgs) override final;
	void PerRenderObject(const RenderObject& renderObject, void* pipelineSpecificArgs, UINT context) override final;
	void PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, void* pipelineSpecificArgs, UINT context) override final;
};

class DeferredLightingRenderPass : public DX12RenderPass
{
public:
	struct GBufferLightingPassArgs
	{
		CommonRenderPassArgs commonArgs;

		CD3DX12_CPU_DESCRIPTOR_HANDLE firstGBufferRTVHandle;
	};

	DeferredLightingRenderPass(ID3D12Device* device, ComPtr<ID3D12PipelineState> pipelineState)
		: DX12RenderPass(device, pipelineState) {}

	void Render(const std::vector<RenderPackage>& renderPackages, UINT context, void* pipelineSpecificArgs) override final;
	void PerRenderObject(const RenderObject& renderObject, void* pipelineSpecificArgs, UINT context) override final;
	void PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, void* pipelineSpecificArgs, UINT context) override final;
};