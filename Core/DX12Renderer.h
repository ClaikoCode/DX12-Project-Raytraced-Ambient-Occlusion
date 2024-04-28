#pragma once

#include "DirectXIncludes.h"

#include <array>
#include <unordered_map>

#include "GPUResource.h"
#include "RenderObject.h"
#include "DX12SyncHandler.h"
#include "DX12RenderPass.h"
#include "AppDefines.h"
#include "Camera.h"

using Microsoft::WRL::ComPtr;

constexpr UINT BufferCount = 2;

typedef UINT ShaderPipelineStateType;

template<typename T> DXGI_FORMAT GetDXGIFormat() { throw std::exception("Unsupported type"); }

// Create vertex buffers.
struct Vertex
{
	DirectX::XMFLOAT3 position;
	DirectX::XMFLOAT3 color;
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

	RenderObject CreateRenderObject(const std::vector<Vertex>* vertices, const std::vector<uint32_t>* indices, D3D12_PRIMITIVE_TOPOLOGY topology);

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

	ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
	ComPtr<ID3D12Resource> m_depthBuffer;
	

	std::unordered_map<RenderPassType, std::unique_ptr<DX12RenderPass>> m_renderPasses;
	ComPtr<ID3D12RootSignature> m_rootSignature;

	// Synchronization objects.
	ComPtr<ID3D12Fence> m_fence;
	UINT m_fenceValue;
	DX12SyncHandler m_syncHandler;

	std::unordered_map<ShaderPipelineStateType, std::vector<RenderObject>> m_renderObjectsByPipelineState;

	Camera* m_activeCamera;
	std::vector<Camera> m_cameras;

	static DX12Renderer* s_instance;
};