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
typedef std::array<ComPtr<ID3D12GraphicsCommandList4>, NumContexts> CommandLists;

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
	DX12RenderPass(ComPtr<ID3D12Device5> device);
	~DX12RenderPass() = default;

	void Init();
	void Close(UINT context);
	const std::vector<RenderObjectID>& GetRenderableObjects() const;

	virtual void Render(const std::vector<RenderPackage>& renderPackages, UINT context, RenderPassArgs* pipelineArgs) = 0;

protected:
	//virtual void BuildPipelineState() = 0;

	virtual void PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context) = 0;
	virtual void PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context) = 0;

public:
	CommandAllocators commandAllocators;
	CommandLists commandLists;
	
protected:
	ComPtr<ID3D12PipelineState> m_pipelineState;
	std::vector<RenderObjectID> m_renderableObjects;
};

class NonIndexedRenderPass : public DX12RenderPass
{
public:
	NonIndexedRenderPass(ComPtr<ID3D12Device5> device, ComPtr<ID3D12RootSignature> rootSig)
		: DX12RenderPass(device) {}

	void Render(const std::vector<RenderPackage>& renderPackages, UINT context, RenderPassArgs* pipelineArgs) override final;

protected:
	void PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context) override final;
	void PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context) override final;
};

class IndexedRenderPass : public DX12RenderPass
{
public:
	IndexedRenderPass(ComPtr<ID3D12Device5> device, ComPtr<ID3D12RootSignature> rootSig)
		: DX12RenderPass(device) {}

	void Render(const std::vector<RenderPackage>& renderPackages, UINT context, RenderPassArgs* pipelineSpecificArgs) override final;

protected:
	void PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context) override final;
	void PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context) override final;
};

class DeferredGBufferRenderPass : public DX12RenderPass
{
public:
	DeferredGBufferRenderPass(ComPtr<ID3D12Device5> device, ComPtr<ID3D12RootSignature> rootSig);

	void Render(const std::vector<RenderPackage>& renderPackages, UINT context, RenderPassArgs* pipelineSpecificArgs) override final;

protected:
	void PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context) override final;
	void PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context) override final;
};

class DeferredLightingRenderPass : public DX12RenderPass
{
public:
	DeferredLightingRenderPass(ComPtr<ID3D12Device5> device, ComPtr<ID3D12RootSignature> rootSig);

	void Render(const std::vector<RenderPackage>& renderPackages, UINT context, RenderPassArgs* pipelineSpecificArgs) override final;

protected:
	void PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context) override final;
	void PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context) override final;
};

class RaytracedAORenderPass : public DX12RenderPass
{
public:
	RaytracedAORenderPass(ComPtr<ID3D12Device5> device, ComPtr<ID3D12RootSignature> rootSig);

	void Render(const std::vector<RenderPackage>& renderPackages, UINT context, RenderPassArgs* pipelineSpecificArgs) override final;

protected:
	void PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context) override final;
	void PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context) override final;
};

class AccumilationRenderPass : public DX12RenderPass
{
public:
	AccumilationRenderPass(ComPtr<ID3D12Device5> device, ComPtr<ID3D12RootSignature> rootSig);

	void Render(const std::vector<RenderPackage>& renderPackages, UINT context, RenderPassArgs* pipelineSpecificArgs) override final;

protected:
	void PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context) override final;
	void PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context) override final;
};

