#pragma once

#include "DirectXIncludes.h"

#include <array>
#include <unordered_map>
#include <memory>
#include <thread>

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
 
typedef std::unordered_map<RenderObjectID, DX12Abstractions::AccelerationStructureBuffers> AccelerationStructureMap;
typedef std::unordered_map<RenderObjectID, std::vector<RenderInstance>> RenderInstanceMap;
typedef std::unordered_map<RenderPassType, std::unique_ptr<DX12RenderPass>> RenderPassMap;

struct FrameResource; // Forward declaration.

class CommandQueueHandler
{
public:

	// Static variable describing max wait time for command queue.
	static const UINT MaxWaitTimeMS = 20000u;

	ComPtr<ID3D12CommandQueue> commandQueue;
	ComPtr<ID3D12CommandAllocator> commandAllocator;

public:
	CommandQueueHandler();
	~CommandQueueHandler();
	CommandQueueHandler(ComPtr<ID3D12Device5> device, D3D12_COMMAND_LIST_TYPE type);
	CommandQueueHandler(const CommandQueueHandler& other) = delete;
	CommandQueueHandler& operator= (const CommandQueueHandler& other) = delete;

	// Automatically resets the command list by default. If the usage pattern involves manual resetting in a loop for example, set argument to false.
	ComPtr<ID3D12GraphicsCommandList4> CreateCommandList(ComPtr<ID3D12Device5> device, bool autoReset = true, D3D12_COMMAND_LIST_FLAGS flags = D3D12_COMMAND_LIST_FLAG_NONE);

	ID3D12CommandQueue* Get() const;
	ComPtr<ID3D12Fence> GetFence() const;
	UINT64 GetCompletedFenceValue();

	void ResetAllocator();
	// Assumes that the command list used is a command list previously created by this class.
	void ResetCommandList(ComPtr<ID3D12GraphicsCommandList1> commandList);

	UINT64 Signal();
	// If nullptr is passed, the function will wait indefinitely.
	void WaitForLatestSignal();
	void WaitForFence(UINT64 fenceValue);
	// If nullptr is passed, the function will wait indefinitely.
	void SignalAndWait();

	void GPUWait(ComPtr<ID3D12Fence> fence, UINT64 fenceValue);
	void GPUWaitForOtherQueue(CommandQueueHandler& otherQueue);

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

	// Clears relevant buffers for each frame.
	void ClearBuffers(GPUResource& currentBackBuffer, ComPtr<ID3D12GraphicsCommandList4> preCommandList, const CD3DX12_CPU_DESCRIPTOR_HANDLE bbRTV, const CD3DX12_CPU_DESCRIPTOR_HANDLE middleTextureRTV, CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle);

private:

	// Private constructor as this is a singleton. The Get() function is used to get the instance.
	DX12Renderer(UINT width, UINT height, HWND windowHandle);
	~DX12Renderer();

	// Remove copy constructor and copy assignment operator
	DX12Renderer(const DX12Renderer& rhs) = delete;
	DX12Renderer& operator=(const DX12Renderer& rhs) = delete;

	void InitPipeline();
	void CreateDeviceAndSwapChain();
	void CreateAccumulationTexture();
	void CreateBackBuffers();
	void CreateDepthBuffer();
	void CreateGBuffers();
	void CreateMiddleTexture();

	void CreateRTVHeap();
	void CreateDSVHeap();
	void CreateCBVSRVUAVHeapGlobal();

	void CreateRTVs();
	void CreateDSV();
	void CreateSRVs();
	void CreateUAVs();

	void InitAssets();
	void CreateRootSignatures();
	void RegisterRenderPasses();
	void CreateRenderObjects();
	void CreateCamera();
	void CreateRenderInstances();

	void InitRaytracing();
	void CreateAccelerationStructures();
	void CreateBottomLevelASs(ComPtr<ID3D12GraphicsCommandList4> commandList);
	void CreateBottomLevelAccelerationStructure(RenderObjectID objectID, ComPtr<ID3D12GraphicsCommandList4> commandList);
	void CreateRaytracingPipelineState();
	void CreateRayGenLocalRootSignature(ComPtr<ID3D12RootSignature>& rootSig);
	void CreateHitGroupLocalRootSignature(ComPtr<ID3D12RootSignature>& rootSig);
	void CreateMissLocalRootSignature(ComPtr<ID3D12RootSignature>& rootSig);
	void CreateGlobalRootSignature(ComPtr<ID3D12RootSignature>& rootSig);

	void InitFrameResources();

	void RegisterRenderPass(const RenderPassType renderPassType);
	void InitThreads();

	void UpdateCamera();

	void BuildRenderPass(UINT context);

	void ClearGBuffers(ComPtr<ID3D12GraphicsCommandList> commandList);
	void TransitionGBuffers(ComPtr<ID3D12GraphicsCommandList> commandList, D3D12_RESOURCE_STATES newResourceState);

	RenderObject CreateRenderObject(const std::vector<Vertex>* vertices, const std::vector<VertexIndex>* indices, D3D12_PRIMITIVE_TOPOLOGY topology);
	RenderObject CreateRenderObjectFromOBJ(const std::string& objPath, D3D12_PRIMITIVE_TOPOLOGY topology);
	void SerializeAndCreateRootSig(CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc, ComPtr<ID3D12RootSignature>& rootSig);

	CD3DX12_CPU_DESCRIPTOR_HANDLE GetGlobalRTVHandle(GlobalDescriptorNames globalRTVDescriptorName, UINT offset = 0);
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetGlobalDSVHandle(GlobalDescriptorNames globalDSVDescriptorName, UINT offset = 0);
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetGlobalCBVSRVUAVHandle(GlobalDescriptorNames globalUAVDescriptorName, UINT offset = 0);
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetGlobalHandleFromHeap(ComPtr<ID3D12DescriptorHeap> heap, const UINT descriptorSize, const GlobalDescriptorNames globalUAVDescriptorName, const UINT offset = 0);

private:

	UINT m_width;
	UINT m_height;
	HWND m_windowHandle;

	// App resources.
	CD3DX12_RECT m_scissorRect;
	CD3DX12_VIEWPORT m_viewport;
	ComPtr<IDXGISwapChain3> m_swapChain;
	ComPtr<ID3D12Device5> m_device;

	// Descriptor heap for pointing at resources that are global or shared between frames.
	ComPtr<ID3D12DescriptorHeap> m_rtvHeapGlobal;
	UINT m_rtvDescriptorSize;
	ComPtr<ID3D12DescriptorHeap> m_dsvHeapGlobal;
	UINT m_dsvDescriptorSize;
	ComPtr<ID3D12DescriptorHeap> m_cbvSrvUavHeapGlobal;
	UINT m_cbvSrvUavDescriptorSize;
	
	// Command queues that are to be used by the main thread.
	std::unique_ptr<CommandQueueHandler> m_directCommandQueue;
	std::unique_ptr<CommandQueueHandler> m_computeCommandQueue;
	std::unique_ptr<CommandQueueHandler> m_copyCommandQueue;

	std::array<DX12Abstractions::GPUResource, BackBufferCount> m_backBuffers;
	DX12Abstractions::GPUResource m_accumulationTexture;
	std::array<DX12Abstractions::GPUResource, GBufferIDCount> m_gBuffers;
	DX12Abstractions::GPUResource m_middleTexture;
	ComPtr<ID3D12Resource> m_depthBuffer;

	RenderPassMap m_renderPasses;
	ComPtr<ID3D12RootSignature> m_rasterRootSignature;

	ComPtr<ID3D12RootSignature> m_RTGlobalRootSignature;
	ComPtr<ID3D12StateObject> m_RTPipelineState;

	std::unordered_map<RenderObjectID, RenderObject> m_renderObjectsByID;
	RenderInstanceMap m_renderInstancesByID;

	AccelerationStructureMap m_bottomAccStructByID;

	std::array<std::unique_ptr<FrameResource>, BackBufferCount> m_frameResources;
	FrameResource* m_currentFrameResource;

	std::array<std::thread, NumContexts> m_threadWorkers;
	DX12SyncHandler m_syncHandler;
	bool m_forceExitThread; // Used to make a thread jump out of its loop.

	Camera* m_activeCamera;
	std::vector<Camera> m_cameras;

	UINT m_frameCount;
	UINT m_accumulatedFrames;
	float m_time;

	static DX12Renderer* s_instance;
};

struct FrameResource
{
	struct FrameResourceInputs
	{
		ComPtr<ID3D12Device5> device;
		CD3DX12_VIEWPORT viewPort;
		ComPtr<ID3D12DescriptorHeap> dsvHeap;
		ComPtr<ID3D12DescriptorHeap> cbvSrvUavHeapGlobal;
		UINT cbvSrvUavDescriptorSize;
		ComPtr<ID3D12DescriptorHeap> rtvHeap;
		ComPtr<ID3D12StateObject> rtPipelineStateObject;
	};

	struct FrameResourceUpdateInputs
	{
		const Camera* camera;
		const RenderInstanceMap& renderInstancesByID;
		const AccelerationStructureMap& bottomAccStructByID;
		GlobalFrameData globalFrameData;
	};

	FrameResource(UINT frameIndex, ComPtr<ID3D12Resource> backBuffer, FrameResourceInputs inputs);
	~FrameResource() = default;

	// Resets allocators and command lists.
	void Init();
	UINT GetFrameIndex() const;

public:
	void CreateCommandResources(ComPtr<ID3D12Device5> device);

private:
	
	void CreateFrameCBVs(ComPtr<ID3D12Device5> device, ComPtr<ID3D12DescriptorHeap> cbvSrvUavHeap, UINT cbvSrvUavDescriptorSize);
	void CreateConstantBuffers(ComPtr<ID3D12Device5> device);

	void CreateTopLevelASs(ComPtr<ID3D12Device5> device);
	void CreateTopLevelAS(ComPtr<ID3D12Device5> device, RenderObjectID renderObjectID);
	void CreateTopLevelASDescriptors(ComPtr<ID3D12Device5> device, ComPtr<ID3D12DescriptorHeap> cbvSrvUavHeap, UINT cbvSrvUavDescriptorSize);
	void CreateTopLevelASDescriptor(ComPtr<ID3D12Device5> device, RenderObjectID objectID, ComPtr<ID3D12DescriptorHeap> cbvSrvUavHeap, UINT cbvSrvUavDescriptorSize);
	void CreateShaderTables(FrameResourceInputs inputs);

public:
	void UpdateFrameResources(const FrameResourceUpdateInputs inputs);

private: 
	void UpdateInstanceConstantBuffers(const FrameResourceUpdateInputs& inputs);
	void UpdateGlobalFrameDataBuffer(const FrameResourceUpdateInputs& inputs);
	void UpdateTopLevelAccelerationStructure(const FrameResourceUpdateInputs& inputs, RenderObjectID objectID);
	
public:
	GPUResource perInstanceCB; 
	GPUResource globalFrameDataCB;

	AccelerationStructureMap topAccStructByID;

	DX12Abstractions::ShaderTableData rayGenShaderTable;
	DX12Abstractions::ShaderTableData hitGroupShaderTable;
	DX12Abstractions::ShaderTableData missShaderTable;

	ComPtr<ID3D12CommandAllocator> generalCommandAllocator;
	ComPtr<ID3D12GraphicsCommandList4> generalCommandList;

	std::array<ComPtr<ID3D12CommandAllocator>, CommandListIdentifier::NumCommandLists> commandAllocators;
	std::array<ComPtr<ID3D12GraphicsCommandList4>, CommandListIdentifier::NumCommandLists> commandLists;

	UINT64 fenceValue;

private:
	UINT m_frameIndex;
};