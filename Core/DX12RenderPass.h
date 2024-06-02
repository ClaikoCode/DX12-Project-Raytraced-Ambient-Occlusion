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

typedef std::array<ComPtr<ID3D12CommandAllocator>, NumContexts> CommandAllocatorArray;
typedef std::array<ComPtr<ID3D12GraphicsCommandList4>, NumContexts> CommandListArray;

void SetCommonStates(CommonRenderPassArgs commonArgs, ComPtr<ID3D12PipelineState> pipelineState, ComPtr<ID3D12GraphicsCommandList4> commandList);

// Assumes that the void* is not null. This assertion should happen before usage.
template<typename T>
T& ToSpecificArgs(RenderPassArgs* pipelineSpecificArgs)
{
	// TODO: Handle this error better.
	assert(pipelineSpecificArgs != nullptr);

	return std::get<T>(*pipelineSpecificArgs);
}

// Abstract class for a direct render pass.
class DX12RenderPass
{
public:
	DX12RenderPass(ComPtr<ID3D12Device5> device, D3D12_COMMAND_LIST_TYPE commandType, bool parallelizable);
	~DX12RenderPass() = default;

	void Init(UINT frameIndex);
	void Close(UINT frameIndex, UINT context);

	// Returns if a given context is allowed to build the render pass or not.
	// If the render pass is parallelizable, then it can be built in any context.
	// Context 0 is always allowed to build.
	bool IsContextAllowedToBuild(const UINT context) const;

	const std::vector<RenderObjectID>& GetRenderableObjects() const;

	ComPtr<ID3D12GraphicsCommandList4> GetCommandList(UINT context, UINT frameIndex);
	ComPtr<ID3D12GraphicsCommandList4> GetFirstCommandList(UINT frameIndex);
	ComPtr<ID3D12GraphicsCommandList4> GetLastCommandList(UINT frameIndex);

	virtual void BuildRenderPass(const std::vector<RenderPackage>& renderPackages, UINT context, UINT frameIndex, RenderPassArgs* pipelineArgs) = 0;

protected:

	virtual void PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex) = 0;
	virtual void PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex) = 0;

public:
	std::array<CommandAllocatorArray, BackBufferCount> commandAllocators;
	std::array<CommandListArray, BackBufferCount> commandLists;
	
protected:
	ComPtr<ID3D12PipelineState> m_pipelineState;
	std::vector<RenderObjectID> m_renderableObjects;

	// Set to true if the render pass can have its work parallelized.
	bool m_parallelizable;
};

class NonIndexedRenderPass : public DX12RenderPass
{
public:
	NonIndexedRenderPass(ComPtr<ID3D12Device5> device, ComPtr<ID3D12RootSignature> rootSig)
		: DX12RenderPass(device, D3D12_COMMAND_LIST_TYPE_DIRECT, true) {}

	void BuildRenderPass(const std::vector<RenderPackage>& renderPackages, UINT context, UINT frameIndex, RenderPassArgs* pipelineArgs) override final;

protected:
	void PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex) override final;
	void PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex) override final;
};

class IndexedRenderPass : public DX12RenderPass
{
public:
	IndexedRenderPass(ComPtr<ID3D12Device5> device, ComPtr<ID3D12RootSignature> rootSig)
		: DX12RenderPass(device, D3D12_COMMAND_LIST_TYPE_DIRECT, true) {}

	void BuildRenderPass(const std::vector<RenderPackage>& renderPackages, UINT context, UINT frameIndex, RenderPassArgs* pipelineArgs) override final;

protected:
	void PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex) override final;
	void PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex) override final;
};

class DeferredGBufferRenderPass : public DX12RenderPass
{
public:
	DeferredGBufferRenderPass(ComPtr<ID3D12Device5> device, ComPtr<ID3D12RootSignature> rootSig);

	void BuildRenderPass(const std::vector<RenderPackage>& renderPackages, UINT context, UINT frameIndex, RenderPassArgs* pipelineArgs) override final;

protected:
	void PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex) override final;
	void PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex) override final;
};

class DeferredLightingRenderPass : public DX12RenderPass
{
public:
	DeferredLightingRenderPass(ComPtr<ID3D12Device5> device, ComPtr<ID3D12RootSignature> rootSig);

	void BuildRenderPass(const std::vector<RenderPackage>& renderPackages, UINT context, UINT frameIndex, RenderPassArgs* pipelineArgs) override final;

protected:
	void PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex) override final;
	void PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex) override final;
};

class RaytracedAORenderPass : public DX12RenderPass
{
public:
	RaytracedAORenderPass(ComPtr<ID3D12Device5> device, ComPtr<ID3D12RootSignature> rootSig);

	void BuildRenderPass(const std::vector<RenderPackage>& renderPackages, UINT context, UINT frameIndex, RenderPassArgs* pipelineArgs) override final;

protected:
	void PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex) override final;
	void PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex) override final;
};

class AccumilationRenderPass : public DX12RenderPass
{
public:
	AccumilationRenderPass(ComPtr<ID3D12Device5> device, ComPtr<ID3D12RootSignature> rootSig);

	void BuildRenderPass(const std::vector<RenderPackage>& renderPackages, UINT context, UINT frameIndex, RenderPassArgs* pipelineArgs) override final;

protected:
	void PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex) override final;
	void PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex) override final;
};

