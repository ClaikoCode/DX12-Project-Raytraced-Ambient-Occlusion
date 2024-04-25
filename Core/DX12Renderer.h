#pragma once

#include "DirectXIncludes.h"

#include <array>
#include <unordered_map>

#include "GPUResource.h"

using Microsoft::WRL::ComPtr;

constexpr UINT BufferCount = 2;

typedef UINT ShaderPipelineStateType;

struct ShaderPipeline
{
	ComPtr<ID3D12PipelineState> pipelineState;
};

// Singleton class designed with a public constructor that only is allowed to be called once.
class DX12Renderer
{

public:
	static void Init(UINT width, UINT height, HWND windowHandle);
	static DX12Renderer& Get();

	void Render();

private:

	DX12Renderer(UINT width, UINT height, HWND windowHandle);
	~DX12Renderer();

	// Remove copy constructor and copy assignment operator
	DX12Renderer(const DX12Renderer& rhs) = delete;
	DX12Renderer& operator=(const DX12Renderer& rhs) = delete;

	void InitPipeline();
	void InitAssets();

private:

	UINT m_width;
	UINT m_height;
	HWND m_windowHandle;

	// App resources.
	CD3DX12_RECT m_scissorRect;
	CD3DX12_VIEWPORT m_viewport;
	ComPtr<IDXGISwapChain3> m_swapChain;
	ComPtr<ID3D12Device5> m_device;
	// Command queue only to be used by the main thread.
	ComPtr<ID3D12CommandQueue> m_commandQueue;
	// This command allocator is only used for work that the main thread will do.
	// It is supposed to only be used with temporary command lists.
	ComPtr<ID3D12CommandAllocator> m_commandAllocator;

	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	UINT m_rtvDescriptorSize;
	std::array<DX12Abstractions::GPUResource, BufferCount> m_renderTargets;

	std::unordered_map<ShaderPipelineStateType, ShaderPipeline> m_pipelineStates;
	ComPtr<ID3D12RootSignature> m_rootSignature;

	// Synchronization objects.
	ComPtr<ID3D12Fence> m_fence;
	UINT m_fenceValue;
	
	// Resources.
	DX12Abstractions::GPUResource m_vertexBuffer;
	UINT m_vertexBufferSize;
	UINT vertexCount;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;

	static DX12Renderer* s_instance;
};