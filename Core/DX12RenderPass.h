#pragma once 

#include "DirectXIncludes.h"
#include <array>

constexpr UINT NumContexts = 3;

using namespace Microsoft::WRL;
using namespace DirectX;

typedef std::array<ComPtr<ID3D12CommandAllocator>, NumContexts> CommandAllocators;
typedef std::array<ComPtr<ID3D12GraphicsCommandList>, NumContexts> CommandLists;

class DX12RenderPass
{
public:
	DX12RenderPass(ID3D12Device* device, ComPtr<ID3D12PipelineState> pipelineState);
	~DX12RenderPass();

public:

	CommandAllocators commandAllocators;
	CommandLists commandLists;
	
private:
	ComPtr<ID3D12PipelineState> m_pipelineState;

};

class TriangleRenderPass : public DX12RenderPass
{
public:


	TriangleRenderPass(ID3D12Device* device, ComPtr<ID3D12PipelineState> pipelineState);
	~TriangleRenderPass();

	void Init();
	void Render(ComPtr<ID3D12GraphicsCommandList> commandList, ComPtr<ID3D12DescriptorHeap> rtvHeap, CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle);

};