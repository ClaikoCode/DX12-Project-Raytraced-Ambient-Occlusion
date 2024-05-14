#pragma once 

#include "DirectXIncludes.h"
#include <array>
#include <vector>

#include "GPUResource.h"
#include "AppDefines.h"
#include "RenderObject.h"
#include "RenderPassArgs.h"

using namespace Microsoft::WRL;
using namespace DirectX;
using DX12Abstractions::GPUResource;

typedef std::array<ComPtr<ID3D12CommandAllocator>, NumContexts> CommandAllocators;
typedef std::array<ComPtr<ID3D12GraphicsCommandList>, NumContexts> CommandLists;

void SetCommonStates(CommonRenderPassArgs commonArgs, ComPtr<ID3D12PipelineState> pipelineState, ComPtr<ID3D12GraphicsCommandList> commandList);

// Assumes that the void* is not null. This assertion should happen before usage.
template<typename T>
T& ToSpecificArgs(RenderPassArgsVariant* pipelineSpecificArgs)
{
	// TODO: Handle this error better.
	assert(pipelineSpecificArgs != nullptr);

	return std::get<T>(*pipelineSpecificArgs);
}

// Abstract class for a direct render pass.
class DX12RenderPass
{
public:
	DX12RenderPass(ID3D12Device* device, ComPtr<ID3D12PipelineState> pipelineState);
	~DX12RenderPass() = default;

	void Init();
	void Close(UINT context);

	virtual void Render(const std::vector<RenderPackage>& renderPackages, UINT context, RenderPassArgsVariant* pipelineArgs) = 0;
	virtual void PerRenderObject(const RenderObject& renderObject, RenderPassArgsVariant* pipelineArgs, UINT context) = 0;
	virtual void PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgsVariant* pipelineArgs, UINT context) = 0;

public:

	CommandAllocators commandAllocators;
	CommandLists commandLists;
	
protected:
	ComPtr<ID3D12PipelineState> m_pipelineState;

};

class NonIndexedRenderPass : public DX12RenderPass
{
public:
	NonIndexedRenderPass(ID3D12Device* device, ComPtr<ID3D12PipelineState> pipelineState) 
		: DX12RenderPass(device, pipelineState) {}

	void Render(const std::vector<RenderPackage>& renderPackages, UINT context, RenderPassArgsVariant* pipelineArgs) override final;
	void PerRenderObject(const RenderObject& renderObject, RenderPassArgsVariant* pipelineArgs, UINT context) override final;
	void PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgsVariant* pipelineArgs, UINT context) override final;
	
};

class IndexedRenderPass : public DX12RenderPass
{
public:
	IndexedRenderPass(ID3D12Device* device, ComPtr<ID3D12PipelineState> pipelineState)
		: DX12RenderPass(device, pipelineState) {}

	void Render(const std::vector<RenderPackage>& renderPackages, UINT context, RenderPassArgsVariant* pipelineSpecificArgs) override final;
	void PerRenderObject(const RenderObject& renderObject, RenderPassArgsVariant* pipelineArgs, UINT context) override final;
	void PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgsVariant* pipelineArgs, UINT context) override final;
};

class DeferredGBufferRenderPass : public DX12RenderPass
{
public:
	DeferredGBufferRenderPass(ID3D12Device* device, ComPtr<ID3D12PipelineState> pipelineState)
		: DX12RenderPass(device, pipelineState) {}

	void Render(const std::vector<RenderPackage>& renderPackages, UINT context, RenderPassArgsVariant* pipelineSpecificArgs) override final;
	void PerRenderObject(const RenderObject& renderObject, RenderPassArgsVariant* pipelineArgs, UINT context) override final;
	void PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgsVariant* pipelineArgs, UINT context) override final;
};

class DeferredLightingRenderPass : public DX12RenderPass
{
public:
	DeferredLightingRenderPass(ID3D12Device* device, ComPtr<ID3D12PipelineState> pipelineState)
		: DX12RenderPass(device, pipelineState) {}

	void Render(const std::vector<RenderPackage>& renderPackages, UINT context, RenderPassArgsVariant* pipelineSpecificArgs) override final;
	void PerRenderObject(const RenderObject& renderObject, RenderPassArgsVariant* pipelineArgs, UINT context) override final;
	void PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgsVariant* pipelineArgs, UINT context) override final;
};

