#pragma once

#include "DirectXIncludes.h"

#include <array>
#include <unordered_map>
#include <memory>

#include "GPUResource.h"
#include "RenderObject.h"
#include "DX12SyncHandler.h"
#include "DX12RenderPass.h"
#include "AppDefines.h"
#include "Camera.h"
#include "DX12AbstractionUtils.h"
#include "DXRAbstractions.h"

using Microsoft::WRL::ComPtr;

constexpr LPCWSTR MissShaderName = L"miss";
constexpr LPCWSTR RayGenShaderName = L"raygen";
constexpr LPCWSTR AnyHitShaderName = L"anyhit";
constexpr LPCWSTR HitGroupName = L"HitGroup";
 

class CommandQueueHandler
{
public:
	CommandQueueHandler();
	~CommandQueueHandler();
	CommandQueueHandler(ComPtr<ID3D12Device5> device, D3D12_COMMAND_LIST_TYPE type);
	CommandQueueHandler(const CommandQueueHandler& other) = delete;
	CommandQueueHandler& operator= (const CommandQueueHandler& other) = delete;

	ComPtr<ID3D12CommandQueue> commandQueue;
	ComPtr<ID3D12CommandAllocator> commandAllocator;

	// Automatically resets the command list by default. If the usage pattern involves manual resetting in a loop for example, set argument to false.
	ComPtr<ID3D12GraphicsCommandList4> CreateCommandList(ComPtr<ID3D12Device5> device, bool autoReset = true, D3D12_COMMAND_LIST_FLAGS flags = D3D12_COMMAND_LIST_FLAG_NONE);

	ID3D12CommandQueue* Get() const;

	void ResetAllocator();
	// Assumes that the command list used is a command list previously created by this class.
	void ResetCommandList(ComPtr<ID3D12GraphicsCommandList1> commandList);

	void Signal();
	// If nullptr is passed, the function will wait indefinitely.
	void Wait();
	// If nullptr is passed, the function will wait indefinitely.
	void SignalAndWait();

	void ExecuteCommandLists(DX12Abstractions::CommandListVector& commandLists, UINT count = 0, const UINT offset = 0);

private:
	ComPtr<ID3D12Fence> m_fence;
	HANDLE m_eventHandle; // TODO: Properly add event handle.
	UINT64 m_fenceValue;
	D3D12_COMMAND_LIST_TYPE m_type;
};

// Singleton class designed with a public constructor that only is allowed to be called once.
class DX12Renderer
{
public:
	static ComPtr<ID3D12InfoQueue1> GetInfoQueue();

public:
	static void Init(UINT width, UINT height, HWND windowHandle);
	static DX12Renderer& Get();

	void Update();
	void Render();

	

private:

	DX12Renderer(UINT width, UINT height, HWND windowHandle);
	~DX12Renderer();

	// Remove copy constructor and copy assignment operator
	DX12Renderer(const DX12Renderer& rhs) = delete;
	DX12Renderer& operator=(const DX12Renderer& rhs) = delete;

	void InitPipeline();
	void CreateDeviceAndSwapChain();
	void CreateGBuffers();
	void CreateMiddleTexture();
	void CreateAccumulationTexture();
	void CreateRTVHeap();
	void CreateRTVs();
	void CreateDepthBuffer();
	void CreateDSVHeap();
	void CreateDSV();
	void CreateConstantBuffers();
	void CreateCBVSRVUAVHeap();
	void CreateCBVs();
	void CreateSRVs();
	void CreateUAVs();

	void InitAssets();
	void CreateRootSignatures();
	void CreatePSOs();
	void CreateRenderObjects();
	void CreateCamera();
	void CreateRenderInstances();

	void InitRaytracing();
	void CreateAccelerationStructures();
	void CreateBottomLevelASs(ComPtr<ID3D12GraphicsCommandList4> commandList);
	void CreateBottomLevelAccelerationStructure(RenderObjectID objectID, ComPtr<ID3D12GraphicsCommandList4> commandList);
	void CreateTopLevelASs(ComPtr<ID3D12GraphicsCommandList4> commandList);
	void CreateTopLevelAccelerationStructure(RenderObjectID objectID, ComPtr<ID3D12GraphicsCommandList4> commandList);
	void CreateRaytracingPipelineState();
	void CreateRayGenLocalRootSignature(ComPtr<ID3D12RootSignature>& rootSig);
	void CreateHitGroupLocalRootSignature(ComPtr<ID3D12RootSignature>& rootSig);
	void CreateMissLocalRootSignature(ComPtr<ID3D12RootSignature>& rootSig);
	void CreateGlobalRootSignature(ComPtr<ID3D12RootSignature>& rootSig);
	void CreateShaderTables();
	void CreateTopLevelASDescriptors();
	void CreateTopLevelASDescriptor(RenderObjectID objectID);

	void SerializeAndCreateRootSig(CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc, ComPtr<ID3D12RootSignature>& rootSig);

	void UpdateCamera();

	void UpdateInstanceConstantBuffers();
	void UpdateGlobalFrameDataBuffer();
	void UpdateTopLevelAccelerationStructure(RenderObjectID objectID, ComPtr<ID3D12GraphicsCommandList4> commandList);

	void ClearGBuffers(ComPtr<ID3D12GraphicsCommandList> commandList);

	void RegisterRenderPass(const RenderPassType renderPassType);

	RenderObject CreateRenderObject(const std::vector<Vertex>* vertices, const std::vector<VertexIndex>* indices, D3D12_PRIMITIVE_TOPOLOGY topology);
	RenderObject CreateRenderObjectFromOBJ(const std::string& objPath, D3D12_PRIMITIVE_TOPOLOGY topology);

	CD3DX12_RESOURCE_DESC CreateBackbufferResourceDesc() const;
	D3D12_SHADER_RESOURCE_VIEW_DESC CreateBackbufferSRVDesc() const;
	D3D12_UNORDERED_ACCESS_VIEW_DESC CreateBackbufferUAVDesc() const;

private:

	UINT m_width;
	UINT m_height;
	HWND m_windowHandle;

	// App resources.
	CD3DX12_RECT m_scissorRect;
	CD3DX12_VIEWPORT m_viewport;
	ComPtr<IDXGISwapChain3> m_swapChain;
	ComPtr<ID3D12Device5> m_device;
	
	// Command queues that are to be used by the main thread.
	std::unique_ptr<CommandQueueHandler> m_directCommandQueue;
	std::unique_ptr<CommandQueueHandler> m_copyCommandQueue;

	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	UINT m_rtvDescriptorSize;

	std::array<DX12Abstractions::GPUResource, GBufferCount> m_gBuffers;
	std::array<DX12Abstractions::GPUResource, BufferCount> m_backBuffers;
	DX12Abstractions::GPUResource m_middleTexture;
	DX12Abstractions::GPUResource m_accumulationTexture;

	ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
	ComPtr<ID3D12Resource> m_depthBuffer;

	ComPtr<ID3D12DescriptorHeap> m_cbvSrvUavHeap;
	UINT m_cbvSrvUavDescriptorSize;
	GPUResource m_perInstanceCB;
	GPUResource m_globalFrameDataCB;

	std::unordered_map<RenderPassType, std::unique_ptr<DX12RenderPass>> m_renderPasses;
	ComPtr<ID3D12RootSignature> m_rasterRootSignature;

	ComPtr<ID3D12RootSignature> m_RTGlobalRootSignature;
	ComPtr<ID3D12StateObject> m_RTPipelineState;

	// Synchronization objects.
	DX12SyncHandler m_syncHandler;

	std::unordered_map<RenderObjectID, RenderObject> m_renderObjectsByID;
	std::unordered_map<RenderObjectID, std::vector<RenderInstance>> m_renderInstancesByID;

	std::unordered_map<RenderObjectID, DX12Abstractions::AccelerationStructureBuffers> m_bottomAccStructByID;
	std::unordered_map<RenderObjectID, DX12Abstractions::AccelerationStructureBuffers> m_topAccStructByID;

	DX12Abstractions::ShaderTableData m_rayGenShaderTable;
	DX12Abstractions::ShaderTableData m_hitGroupShaderTable;
	DX12Abstractions::ShaderTableData m_missShaderTable;

	Camera* m_activeCamera;
	std::vector<Camera> m_cameras;

	UINT m_frameCount;
	UINT m_accumulatedFrames;
	float m_time;


	static DX12Renderer* s_instance;
};