#pragma once 

#include "DirectXIncludes.h"
#include <array>

#include "GPUResource.h"
#include "AppDefines.h"

using namespace Microsoft::WRL;
using namespace DirectX;
using DX12Abstractions::GPUResource;

typedef std::array<ComPtr<ID3D12CommandAllocator>, NumContexts> CommandAllocators;
typedef std::array<ComPtr<ID3D12GraphicsCommandList>, NumContexts> CommandLists;

struct DrawArgs
{
	UINT vertexCount;
	UINT startVertex;
	UINT startInstance;
};

class DX12RenderPass
{
public:
	DX12RenderPass(ID3D12Device* device, ComPtr<ID3D12PipelineState> pipelineState);
	~DX12RenderPass() = default;

	void Init();

public:

	CommandAllocators commandAllocators;
	CommandLists commandLists;
	
protected:
	ComPtr<ID3D12PipelineState> m_pipelineState;

};

class TriangleRenderPass : public DX12RenderPass
{
public:
	struct TriangleRenderPassArgs
	{
		D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
		CD3DX12_CPU_DESCRIPTOR_HANDLE renderTargetView;
		ComPtr<ID3D12RootSignature> rootSignature;
		CD3DX12_VIEWPORT viewport;
		CD3DX12_RECT scissorRect;

		XMMATRIX viewProjectionMatrix;

		std::vector<DrawArgs> drawArgs;
	};

	TriangleRenderPass(ID3D12Device* device, ComPtr<ID3D12PipelineState> pipelineState);
	~TriangleRenderPass() = default;

	void Render(UINT context, ComPtr<ID3D12Device> device, TriangleRenderPassArgs args);

};