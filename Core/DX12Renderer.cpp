#include "DX12Renderer.h"

#include <stdexcept>
#include <vector>

#include "GraphicsErrorHandling.h"
#include "DX12AbstractionUtils.h"
#include "AppDefines.h"
#include "tiny_obj_loader.h"

using namespace DX12Abstractions;
namespace dx = DirectX;

static std::vector<RenderPassType> sPassesToRegister = {
	RenderPassType::DeferredGBufferPass,
	RenderPassType::DeferredLightingPass,
	RenderPassType::RaytracedAOPass,
	RenderPassType::AccumulationPass
};

static 	std::vector<RenderObjectID> sRTRenderObjectIDs = { RenderObjectID::Cube };

bool HasRenderPass(std::vector<RenderPassType>& renderPassOrder, const RenderPassType pass)
{
	return std::find(renderPassOrder.begin(), renderPassOrder.end(), pass) != renderPassOrder.end();
}

void ReadObjFile(std::string modelPath, tinyobj::ObjReader& reader, tinyobj::ObjReaderConfig config)
{
	if (!reader.ParseFromFile(modelPath, config))
	{
		if (!reader.Error().empty())
		{
			throw std::runtime_error(reader.Error());
		}
		else
		{
			throw std::runtime_error("Failed to load model");
		}
	}

	if (!reader.Warning().empty())
	{
		OutputDebugStringA(reader.Warning().c_str());
	}
}

template <typename T>
void GetObjVertexIndices(std::vector<T>& vertexIndices, tinyobj::ObjReader& reader)
{
	auto& attrib = reader.GetAttrib();
	auto& shapes = reader.GetShapes();
	auto& materials = reader.GetMaterials();

	for (const auto& shape : shapes)
	{
		for (const auto& index : shape.mesh.indices)
		{
			vertexIndices.push_back((T)index.vertex_index);
		}
	}
}

D3D12_UNORDERED_ACCESS_VIEW_DESC CreateBackbufferUAVDesc()
{
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc;
	uavDesc.Format = BackBufferFormat;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D = {
		.MipSlice = 0,
		.PlaneSlice = 0
	};

	return uavDesc;
}

D3D12_SHADER_RESOURCE_VIEW_DESC CreateBackbufferSRVDesc()
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.Format = BackBufferFormat;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D = {
		.MostDetailedMip = 0,
		.MipLevels = (UINT)(-1),
		.PlaneSlice = 0,
		.ResourceMinLODClamp = 0.0f
	};

	return srvDesc;
}

CD3DX12_RESOURCE_DESC CreateBackbufferResourceDesc(const UINT width, const UINT height)
{
	CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		BackBufferFormat,
		width,
		height
	);
	resourceDesc.MipLevels = 1; // Match back buffer.

	return resourceDesc;
}

DX12Renderer* DX12Renderer::s_instance = nullptr;

ComPtr<ID3D12InfoQueue1> DX12Renderer::GetInfoQueue()
{
	if (s_instance != nullptr)
	{
		ComPtr<ID3D12InfoQueue1> infoQueue;
		s_instance->m_device->QueryInterface(IID_PPV_ARGS(&infoQueue));

		return infoQueue;
	}

	return nullptr;
}

void DX12Renderer::Init(UINT width, UINT height, HWND windowHandle)
{
	// Will save the instance in the classes static variable.
	static DX12Renderer instance(width, height, windowHandle);
}

DX12Renderer& DX12Renderer::Get()
{
	if (s_instance == nullptr)
	{
		throw std::runtime_error("DX12Renderer::Get() called before DX12Renderer::Init().");
	}

	return *s_instance;
}

void DX12Renderer::Update()
{
	m_time += 1 / 60.0f;

	UINT currentBackBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
	m_currentFrameResource = m_frameResources[currentBackBufferIndex].get();

	// Wait for the frame to finish if its still in flight.
	m_directCommandQueue->WaitForFence(m_currentFrameResource->fenceValue);

	UpdateCamera();

	FrameResource::FrameResourceUpdateInputs inputs = {
		.camera = m_activeCamera,
		.renderInstancesByID = m_renderInstancesByID,
		.bottomAccStructByID = m_bottomAccStructByID,

		.globalFrameData = {
			.frameCount = m_frameCount,
			.accumulatedFrames = m_accumulatedFrames,
			.time = m_time
		}
	};


	m_currentFrameResource->UpdateFrameResources(inputs);
}

void DX12Renderer::Render()
{
	UINT currentFrameIndex = m_currentFrameResource->GetFrameIndex();

	// Store command lists for each render pass.
	CommandListVector commandLists;

	m_currentFrameResource->Init();
	ComPtr<ID3D12GraphicsCommandList4> preCommandList = m_currentFrameResource->commandLists[PreCommandList];
	ComPtr<ID3D12GraphicsCommandList4> postCommandList = m_currentFrameResource->commandLists[PostCommandList];

	// Fetch the current back buffer that we want to render to.
	GPUResource& currentBackBuffer = m_backBuffers[currentFrameIndex];

	// Get RTV handle for the current back buffer.
	const CD3DX12_CPU_DESCRIPTOR_HANDLE bbRTV (
		m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
		FRAME_DESCRIPTOR_OFFSET(RTVBackBuffers, 0) + currentFrameIndex,
		m_rtvDescriptorSize
	);

	const CD3DX12_CPU_DESCRIPTOR_HANDLE middleTextureRTV(
		m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
		FRAME_DESCRIPTOR_OFFSET(RTVMiddleTexture, currentFrameIndex),
		m_rtvDescriptorSize
	);

	// Get DSV handle.
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
	dsvHandle.Offset(currentFrameIndex, m_dsvDescriptorSize); //TODONOW: Fix proper offset.

	//std::vector<RenderPassType> renderPassOrder = { DeferredGBufferPass, DeferredLightingPass, RaytracedAOPass, AccumulationPass };
	std::vector<RenderPassType> renderPassOrder = { DeferredGBufferPass, DeferredLightingPass, RaytracedAOPass };

	// Pre render pass setup.
	{
		// Clear back buffer and prime for rendering.
		{
			currentBackBuffer.TransitionTo(D3D12_RESOURCE_STATE_RENDER_TARGET, preCommandList);

			float clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
			preCommandList->ClearRenderTargetView(bbRTV, clearColor, 0, nullptr);
		}

		// Clear middle texture.
		{
			m_currentFrameResource->middleTexture.TransitionTo(D3D12_RESOURCE_STATE_RENDER_TARGET, preCommandList);

			preCommandList->ClearRenderTargetView(middleTextureRTV, OptimizedClearColor, 0, nullptr);
		}

		// Setup gbuffers.
		if (HasRenderPass(renderPassOrder, RenderPassType::DeferredGBufferPass))
		{
			TransitionGBuffers(preCommandList, D3D12_RESOURCE_STATE_RENDER_TARGET);
			ClearGBuffers(preCommandList);
		}
		

		// Clear depth buffer.
		preCommandList->ClearDepthStencilView(
			dsvHandle,
			D3D12_CLEAR_FLAG_DEPTH,
			1.0f,
			0,
			0,
			nullptr
		);

		// Close pre-command list.
		preCommandList->Close();

		// Add command list to list of command lists.
		commandLists.push_back(preCommandList);
	}

	// Initialize all render passes (resetting).
	for (auto& renderPass : renderPassOrder)
	{
		m_renderPasses[renderPass]->Init(currentFrameIndex);
	}

	// Set start sync.
	for (UINT context = 0; context < NumContexts; context++)
	{
		m_currentFrameResource->syncHandler.SetStart(context);
	}

	// Common args for all passes.
	CommonRenderPassArgs commonArgs = {
		dsvHandle,
		m_rasterRootSignature,
		m_viewport,
		m_scissorRect,
		m_cbvSrvUavHeap,
		m_cbvSrvUavDescriptorSize,
		m_currentFrameResource->globalFrameDataCB,
		m_activeCamera->GetViewProjectionMatrix()
	};

	CommonRaytracingRenderPassArgs commonRTArgs = {
		.cbvSrvUavHeap = m_cbvSrvUavHeap,
		.cbvSrvUavDescSize = m_cbvSrvUavDescriptorSize,

		.globalRootSig = m_RTGlobalRootSignature,

		.rayGenShaderTable = &m_currentFrameResource->rayGenShaderTable,
		.hitGroupShaderTable = &m_currentFrameResource->hitGroupShaderTable,
		.missShaderTable = &m_currentFrameResource->missShaderTable
	};

	// This is supposed to be ran by different threads.
	for (UINT context = 0; context < NumContexts; context++)
	{
		// Wait for start sync.
		m_currentFrameResource->syncHandler.WaitStart(context);

		for (UINT passIndex = 0; passIndex < renderPassOrder.size(); passIndex++)
		{
			bool isLastRenderPass = passIndex == (renderPassOrder.size() - 1);

			RenderPassType renderPassType = renderPassOrder[passIndex];
			DX12RenderPass& renderPass = *m_renderPasses[renderPassType];

			const std::vector<RenderObjectID>& passObjectIDs = renderPass.GetRenderableObjects();

			// Build render packages to send to render.
			std::vector<RenderPackage> renderPackages;
			for (RenderObjectID renderID : passObjectIDs)
			{
				RenderObject& renderObject = m_renderObjectsByID[renderID];
				std::vector<RenderInstance>& instances = m_renderInstancesByID[renderID];

				RenderPackage renderPackage = {
					.renderObject = &renderObject,
					.renderInstances = &instances
				};

				renderPackages.push_back(std::move(renderPackage));
			}

			// Only try to render if there actually is anything to render.
			// If the render pass does not have any objects at all then it is assumed it doesn't need them to fulfill its task.
			// Dont render if its disabled.
			if ((renderPackages.size() > 0 || passObjectIDs.size() == 0) && renderPass.IsEnabled())
			{
				RenderPassArgs renderPassArgs;

				if (renderPassType == NonIndexedPass)
				{
					renderPassArgs = NonIndexedRenderPassArgs{
						.commonArgs = commonArgs,
						.RTV = bbRTV
					};
				}
				else if (renderPassType == IndexedPass)
				{
					renderPassArgs = IndexedRenderPassArgs{
						.commonArgs = commonArgs,
						.RTV = bbRTV
					};
				}
				else if (renderPassType == DeferredGBufferPass)
				{
					// Get RTV handle for the first GBuffer.
					const CD3DX12_CPU_DESCRIPTOR_HANDLE firstGBufferRTVHandle(
						m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
						FRAME_DESCRIPTOR_OFFSET(RTVGBuffers, currentFrameIndex),
						m_rtvDescriptorSize
					);

					renderPassArgs = DeferredGBufferRenderPassArgs {
						.commonArgs = commonArgs,
						.firstGBufferRTVHandle = firstGBufferRTVHandle
					};
				}
				else if(renderPassType == DeferredLightingPass)
				{
					if (context == 0)
					{
						auto commandList = renderPass.GetFirstCommandList(currentFrameIndex);

						D3D12_RESOURCE_STATES gBufferResourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
						// Using the concept of better resource transitions depending on what type of render pipeline we have.
						// If we know that a raytraced AO pass will come, we make sure that it also can be used outside of a pixel shader.
						if (HasRenderPass(renderPassOrder, RaytracedAOPass))
						{
							gBufferResourceState |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
						}

						// Resource barrier for g buffers.
						TransitionGBuffers(commandList, gBufferResourceState);

						// If this is the last render pass we make the middle texture as final output instead.
						if(isLastRenderPass)
							m_currentFrameResource->middleTexture.TransitionTo(D3D12_RESOURCE_STATE_RENDER_TARGET, commandList);
					}

					renderPassArgs = DeferredLightingRenderPassArgs {
						.commonArgs = commonArgs,
						.RTV = isLastRenderPass ? bbRTV : middleTextureRTV
					};
				}
				else if (renderPassType == RaytracedAOPass)
				{
					if (context == 0)
					{
						auto commandList = renderPass.GetFirstCommandList(currentFrameIndex);

						m_currentFrameResource->middleTexture.TransitionTo(D3D12_RESOURCE_STATE_UNORDERED_ACCESS, commandList);
						
						// Put resource barrier.
						CD3DX12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_currentFrameResource->middleTexture.Get());
						commandList->ResourceBarrier(1, &uavBarrier);
					}

					std::vector<RayTracingRenderPackage> rayTracingRenderPackages;
					for (RenderObjectID renderObjectID : passObjectIDs)
					{
						RayTracingRenderPackage rtRenderPackage;

						rtRenderPackage.topLevelASBuffers = &m_currentFrameResource->topAccStructByID[renderObjectID];
						rtRenderPackage.instanceCount = m_renderInstancesByID[renderObjectID].size();

						rayTracingRenderPackages.push_back(rtRenderPackage);
					}

					
					renderPassArgs = RaytracedAORenderPassArgs{
						.commonRTArgs = commonRTArgs,
						.stateObject = m_RTPipelineState,
						.frameCount = m_frameCount,
						.screenWidth = m_width,
						.screenHeight = m_height,
						.renderPackages = rayTracingRenderPackages
					};

					if (isLastRenderPass && context == 0)
					{
						// Copy middle texture to back buffer.
						m_currentFrameResource->middleTexture.TransitionTo(D3D12_RESOURCE_STATE_COPY_SOURCE, postCommandList);
						currentBackBuffer.TransitionTo(D3D12_RESOURCE_STATE_COPY_DEST, postCommandList);
						postCommandList->CopyResource(currentBackBuffer.Get(), m_currentFrameResource->middleTexture.Get());
					}
				}
				else if (renderPassType == AccumulationPass)
				{
					if (context == 0)
					{
						auto commandList = renderPass.GetFirstCommandList(currentFrameIndex);

						m_currentFrameResource->middleTexture.TransitionTo(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, commandList);

						// Put uav barrier before use.
						CD3DX12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_accumulationTexture.Get());
						commandList->ResourceBarrier(1, &uavBarrier);
					}

					renderPassArgs = AccumulationRenderPassArgs {
						.commonArgs = commonArgs,
						.RTVTargetFrame = bbRTV
					};

					if (context == 0)
					{
						static const float frequency = 1.0f;
						//m_accumulatedFrames = (1u * (UINT)(m_time * frequency));
						m_accumulatedFrames += 1;
					}
				}
				else
				{
					throw std::runtime_error("Unknown render pass type.");
				}


				// Render all render packages.
				renderPass.Render(renderPackages, context, currentFrameIndex, &renderPassArgs);
			}

			// Signal that pass is done.
			m_currentFrameResource->syncHandler.SetPass(context, renderPassType);
			m_renderPasses[renderPassType]->Close(currentFrameIndex, context);
		}

		// Signal end sync.
		m_currentFrameResource->syncHandler.SetEnd(context);
	}

	// Wait for all passes to finish on the CPU.
	m_currentFrameResource->syncHandler.WaitEndAll();

	// Prepare back buffer for present.
	currentBackBuffer.TransitionTo(D3D12_RESOURCE_STATE_PRESENT, postCommandList);

	// Close post command list.
	postCommandList->Close() >> CHK_HR;

	// Add all command lists to the main command list.
	for (RenderPassType renderPass : renderPassOrder)
	{
		for (UINT context = 0; context < NumContexts; context++)
		{
			if (m_renderPasses[renderPass]->IsEnabled())
			{
				commandLists.push_back(m_renderPasses[renderPass]->GetCommandList(context, currentFrameIndex));
			}
		}
	}

	// Add post command list to list of command lists.
	commandLists.push_back(postCommandList);

	// Execute all command lists.
	{
		ID3D12CommandList* const* commandListsRaw = commandLists[0].GetAddressOf();
		m_directCommandQueue->commandQueue->ExecuteCommandLists((UINT)commandLists.size(), commandListsRaw);
	}

	// Present
	m_swapChain->Present(0, 0) >> CHK_HR;

	UINT fenceVal = m_directCommandQueue->Signal();
	m_currentFrameResource->fenceValue = fenceVal; // Save the fence val for this frame.

	// Increment frame count.
	m_frameCount++;
}

DX12Renderer::~DX12Renderer()
{
	// Wait for GPU commands to finish executing before destroying.
	m_directCommandQueue->SignalAndWait();
	m_copyCommandQueue->SignalAndWait();

	s_instance = nullptr;
}


DX12Renderer::DX12Renderer(UINT width, UINT height, HWND windowHandle) : 
	m_width(width), 
	m_height(height), 
	m_windowHandle(windowHandle),
	m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
	m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
	m_rtvDescriptorSize(0)
{
	s_instance = this;

	InitPipeline();
	InitAssets();
	InitRaytracing();
	InitFrameResources();
}


void DX12Renderer::InitPipeline()
{
	CreateDeviceAndSwapChain();
	CreateAccumulationTexture();
	CreateBackBuffers();

	CreateDSVHeap();
	CreateRTVHeap();
	CreateCBVSRVUAVHeap();

	CreateBackBufferRTVs();
}

void DX12Renderer::CreateDeviceAndSwapChain()
{
	ComPtr<IDXGIFactory4> factory;
	{
		UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG) 
		// Enables debug layer.
		ComPtr<ID3D12Debug1> debugController;
		D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)) >> CHK_HR;
		debugController->EnableDebugLayer();
		debugController->SetEnableGPUBasedValidation(true);

		// Additional debug layer options.
		dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

		CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)) >> CHK_HR;
	}

	ComPtr<IDXGIAdapter1> hardwareAdapter;
	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_12_0;
	{
		ComPtr<IDXGIAdapter1> adapter;
		for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(adapterIndex, &adapter); ++adapterIndex)
		{
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc) >> CHK_HR;

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				// Don't select the Basic Render Driver adapter.
				continue;
			}

			// Check to see if the adapter supports the feature level, but don't create the actual device yet.
			if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), featureLevel, _uuidof(ID3D12Device5), nullptr)))
			{
				break;
			}
		}

		hardwareAdapter = adapter;
	}

	if (hardwareAdapter)
	{
		// Create the device.
		D3D12CreateDevice(nullptr, featureLevel, IID_PPV_ARGS(&m_device)) >> CHK_HR;
	}
	else
	{
		// If no adapter could be found, create WARP device.
		ComPtr<IDXGIAdapter> warpAdapter;
		factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)) >> CHK_HR;

		D3D12CreateDevice(warpAdapter.Get(), featureLevel, IID_PPV_ARGS(&m_device)) >> CHK_HR;
	}

	NAME_D3D12_OBJECT_MEMBER(m_device, DX12Renderer);

	// Create command queues.
	{
		m_directCommandQueue = std::make_unique<CommandQueueHandler>(m_device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
		m_computeCommandQueue = std::make_unique<CommandQueueHandler>(m_device.Get(), D3D12_COMMAND_LIST_TYPE_COMPUTE);
		m_copyCommandQueue = std::make_unique<CommandQueueHandler>(m_device.Get(), D3D12_COMMAND_LIST_TYPE_COPY);
	}

	// Create swap chain.
	{
		const DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {
			.Width = m_width,
			.Height = m_height,
			.Format = BackBufferFormat,
			.Stereo = FALSE,
			.SampleDesc = {
				.Count = 1,
				.Quality = 0
			},
			.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
			.BufferCount = BackBufferCount,
			.Scaling = DXGI_SCALING_STRETCH,
			.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
			.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
			.Flags = 0
		};

		ComPtr<IDXGISwapChain1> swapChainTemp;
		factory->CreateSwapChainForHwnd(
			m_directCommandQueue->Get(), // Implicit synchronization with the command queue.
			m_windowHandle,
			&swapChainDesc,
			nullptr,
			nullptr,
			&swapChainTemp
		) >> CHK_HR;

		swapChainTemp.As(&m_swapChain) >> CHK_HR; // Upgrade from Version 1 to Version 4.
	}
}

void FrameResource::CreateGBuffers(ComPtr<ID3D12Device5> device, const UINT width, const UINT height)
{
	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_UNKNOWN,
		width,
		height
	);
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	// Diffuse gbuffer.
	{
		resourceDesc.Format = GBufferFormats[GBufferDiffuse];

		gBuffers[GBufferDiffuse] = CreateResource(
			device,
			resourceDesc, 
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_HEAP_TYPE_DEFAULT
		);

		NAME_D3D12_OBJECT_MEMBER_INDEXED(gBuffers, GBufferDiffuse, DX12Renderer);
	}
	
	// Surface normal gbuffer.
	{
		resourceDesc.Format = GBufferFormats[GBufferNormal];
		
		gBuffers[GBufferNormal] = CreateResource(
			device,
			resourceDesc,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_HEAP_TYPE_DEFAULT
		);

		NAME_D3D12_OBJECT_MEMBER_INDEXED(gBuffers, GBufferNormal, DX12Renderer);
	}

	// World position gbuffer.
	{
		resourceDesc.Format = GBufferFormats[GBufferWorldPos];

		gBuffers[GBufferWorldPos] = CreateResource(
			device,
			resourceDesc,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_HEAP_TYPE_DEFAULT
		);

		NAME_D3D12_OBJECT_MEMBER_INDEXED(gBuffers, GBufferWorldPos, DX12Renderer);
	}

	
}

void FrameResource::CreateMiddleTexture(ComPtr<ID3D12Device5> device, const UINT width, const UINT height)
{
	CD3DX12_RESOURCE_DESC resourceDesc = CreateBackbufferResourceDesc(width, height);

	// Render target when writing gbuffer info to it and unordered access in ray tracing shader when writing and reading to it.
	resourceDesc.Flags = 
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | 
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	middleTexture = CreateResource(
		device,
		resourceDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_HEAP_TYPE_DEFAULT
	);
}

void FrameResource::CreateTopLevelASs(ComPtr<ID3D12Device5> device)
{
	for (const RenderObjectID renderObjectID : sRTRenderObjectIDs)
	{
		CreateTopLevelAS(device, renderObjectID);
	}
}

void DX12Renderer::CreateAccumulationTexture()
{
	CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		BackBufferFormat,
		m_width,
		m_height
	);
	resourceDesc.MipLevels = 1; // Match back buffer.
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	m_accumulationTexture = CreateResource(
		m_device,
		resourceDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_HEAP_TYPE_DEFAULT
	);
}

void DX12Renderer::CreateBackBuffers()
{
	for (UINT i = 0; i < BackBufferCount; i++)
	{
		m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i])) >> CHK_HR;

		NAME_D3D12_OBJECT_MEMBER_INDEXED(m_backBuffers, i, DX12Renderer);
	}
}

void DX12Renderer::CreateRTVHeap()
{
	const D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptorHeapDesc = {
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
			.NumDescriptors = MaxRTVDescriptors,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
			.NodeMask = 0
	};

	m_device->CreateDescriptorHeap(&rtvDescriptorHeapDesc, IID_PPV_ARGS(&m_rtvHeap)) >> CHK_HR;
	m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	NAME_D3D12_OBJECT_MEMBER(m_rtvHeap, DX12Renderer);
}

void DX12Renderer::CreateBackBufferRTVs()
{
	// TODONOW: Make sure this works properly.

	for (UINT i = 0; i < BackBufferCount; i++)
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
		// TODONOW: Make this index better implemented.
		rtvHandle.Offset(FRAME_DESCRIPTOR_OFFSET(RTVBackBuffers, 0) + i, m_rtvDescriptorSize);

		m_device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, rtvHandle);
	}
}

void FrameResource::CreateCommandResources(ComPtr<ID3D12Device5> device)
{
	// General.
	{
		device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&generalCommandAllocator));
		NAME_D3D12_OBJECT_MEMBER(generalCommandAllocator, FrameResource);

		device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&generalCommandList));
		NAME_D3D12_OBJECT_MEMBER(generalCommandList, FrameResource);
	}

	for (UINT i = 0; i < CommandListIdentifier::NumCommandLists; i++)
	{
		device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocators[i]));
		NAME_D3D12_OBJECT_MEMBER_INDEXED(commandAllocators, i, FrameResource);

		device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&commandLists[i]));
		NAME_D3D12_OBJECT_MEMBER_INDEXED(commandLists, i, FrameResource);
	}
}

void FrameResource::CreateRTVs(ComPtr<ID3D12Device5> device, ComPtr<ID3D12DescriptorHeap> rtvHeap, ComPtr<ID3D12Resource> backBuffer)
{
	const UINT rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// RTV for gbuffers.
	for (UINT i = 0; i < GBufferIDCount; i++)
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());
		rtvHandle.Offset(FRAME_DESCRIPTOR_OFFSET(RTVGBuffers, m_frameIndex) + i, rtvDescriptorSize);

		device->CreateRenderTargetView(gBuffers[i].Get(), nullptr, rtvHandle);
	}

	// Create middle texture RTV.
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());
		rtvHandle.Offset(FRAME_DESCRIPTOR_OFFSET(RTVMiddleTexture, m_frameIndex), rtvDescriptorSize);

		device->CreateRenderTargetView(middleTexture.Get(), nullptr, rtvHandle);
	}
}

void FrameResource::CreateDepthBuffer(ComPtr<ID3D12Device5> device, const UINT width, const UINT height)
{
	const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

	const CD3DX12_RESOURCE_DESC depthBufferDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_D32_FLOAT,
		width,
		height,
		1, 0, 1, 0,
		D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
	);

	const D3D12_CLEAR_VALUE depthOptimizedClearValue = {
		.Format = DXGI_FORMAT_D32_FLOAT,
		.DepthStencil = { 1.0f, 0 }
	};

	device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&depthBufferDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthOptimizedClearValue,
		IID_PPV_ARGS(&depthBuffer)
	) >> CHK_HR;

	NAME_D3D12_OBJECT_MEMBER(depthBuffer, DX12Renderer);
}

void DX12Renderer::CreateDSVHeap()
{
	const D3D12_DESCRIPTOR_HEAP_DESC dsvDescriptorHeapDesc = {
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
			.NumDescriptors = BackBufferCount, // TODONOW: Make this proper count and future offsets.
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
			.NodeMask = 0
	};

	m_device->CreateDescriptorHeap(&dsvDescriptorHeapDesc, IID_PPV_ARGS(&m_dsvHeap)) >> CHK_HR;
	m_dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	NAME_D3D12_OBJECT_MEMBER(m_dsvHeap, DX12Renderer);
}

void FrameResource::CreateDSV(ComPtr<ID3D12Device5> device, ComPtr<ID3D12DescriptorHeap> dsvHeap)
{
	const UINT dsvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(dsvHeap->GetCPUDescriptorHandleForHeapStart());
	dsvHandle.Offset(m_frameIndex, dsvDescriptorSize); // TODONOW: Fix proper offset.

	device->CreateDepthStencilView(depthBuffer.Get(), nullptr, dsvHandle);
}

void FrameResource::CreateConstantBuffers(ComPtr<ID3D12Device5> device)
{
	// Create instances CBs.
	{
		constexpr UINT instanceElementSize = DX12Abstractions::CalculateConstantBufferByteSize(sizeof(InstanceConstants));
		const UINT instanceBufferSize = instanceElementSize * MaxRenderInstances;

		const CD3DX12_RESOURCE_DESC constantBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(instanceBufferSize);

		perInstanceCB = CreateUploadResource(device, constantBufferDesc);
		NAME_D3D12_OBJECT_MEMBER(perInstanceCB, FrameResource);

		// Map and initialize the constant buffer.
		{
			InstanceConstants* pConstantBufferData;
			perInstanceCB.resource->Map(0, nullptr, reinterpret_cast<void**>(&pConstantBufferData)) >> CHK_HR;

			dx::XMMATRIX modelMatrix = dx::XMMatrixIdentity();

			for (UINT i = 0; i < MaxRenderInstances; i++)
			{
				// No need to transpose the matrix since it's identity.
				dx::XMStoreFloat4x4(&pConstantBufferData[i].modelMatrix, modelMatrix);
			}

			perInstanceCB.resource->Unmap(0, nullptr);
		}
	}

	// Create global frame data CB.
	{
		constexpr UINT globalFrameDataSize = DX12Abstractions::CalculateConstantBufferByteSize(sizeof(GlobalFrameData));
		
		const CD3DX12_RESOURCE_DESC constantBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(globalFrameDataSize * 1); // Only one instance.

		globalFrameDataCB = CreateUploadResource(device, constantBufferDesc);
		NAME_D3D12_OBJECT_MEMBER(globalFrameDataCB, FrameResource);

		// Initialize some values.
		GlobalFrameData globalFrameData = {
			.frameCount = 0,
			.accumulatedFrames = 0,
			.time = 0.0f
		};
		MapDataToBuffer(globalFrameDataCB, &globalFrameData, sizeof(GlobalFrameData));
	}

}

void DX12Renderer::CreateCBVSRVUAVHeap()
{
	const D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDescCBVSRVUAV =
	{
		.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		.NumDescriptors = MaxCBVSRVUAVDescriptors,
		.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
		.NodeMask = 0
	};

	m_device->CreateDescriptorHeap(&descriptorHeapDescCBVSRVUAV, IID_PPV_ARGS(&m_cbvSrvUavHeap)) >> CHK_HR;
	m_cbvSrvUavDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	NAME_D3D12_OBJECT_MEMBER(m_cbvSrvUavHeap, FrameResource);
}

void FrameResource::CreateCBVs(ComPtr<ID3D12Device5> device, ComPtr<ID3D12DescriptorHeap> cbvSrvUavHeap, UINT cbvSrvUavDescriptorSize)
{
	// Create all CBV descriptors for render instances.
	for (UINT i = 0; i < MaxRenderInstances; i++)
	{
		constexpr UINT instanceDataSize = DX12Abstractions::CalculateConstantBufferByteSize(sizeof(InstanceConstants));

		D3D12_GPU_VIRTUAL_ADDRESS instanceCBAddress = perInstanceCB.resource->GetGPUVirtualAddress();
		// Set the offset for the data location inside the upload buffer.
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {
			.BufferLocation = instanceCBAddress + i * instanceDataSize,
			.SizeInBytes = instanceDataSize
		};

		// Set the offset for the handle location inside the descriptor heap.
		// This was noticed to be needed inside the loop to function without getting a memory exception.
		// My theory is that there is no guarantee that the start of the memory wont change as the heap fills up dynamically, because of reallocation.
		// The start of the actual memory therefore needs to be fetched each loop to get the proper memory location.
		CD3DX12_CPU_DESCRIPTOR_HANDLE instanceCBVHandle(cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		instanceCBVHandle.Offset(FRAME_DESCRIPTOR_OFFSET(CBVRenderInstance, m_frameIndex) + i, cbvSrvUavDescriptorSize);

		device->CreateConstantBufferView(&cbvDesc, instanceCBVHandle);
	} 

	// For global data.
	{
		constexpr UINT globalFrameDataSize = DX12Abstractions::CalculateConstantBufferByteSize(sizeof(GlobalFrameData));

		D3D12_GPU_VIRTUAL_ADDRESS frameDataCBAddress = perInstanceCB.resource->GetGPUVirtualAddress();
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {
			.BufferLocation = frameDataCBAddress,
			.SizeInBytes = globalFrameDataSize
		};

		// Calculate offset of CBV.
		CD3DX12_CPU_DESCRIPTOR_HANDLE globalFrameDataCBVHandle(cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		globalFrameDataCBVHandle.Offset(FRAME_DESCRIPTOR_OFFSET(CBVGlobalFrameData, m_frameIndex), cbvSrvUavDescriptorSize);

		device->CreateConstantBufferView(&cbvDesc, globalFrameDataCBVHandle);
	}
}

void FrameResource::CreateSRVs(ComPtr<ID3D12Device5> device, ComPtr<ID3D12DescriptorHeap> cbvSrvUavHeap, UINT cbvSrvUavDescriptorSize)
{
	// SRVs for gbuffers.
	for (UINT i = 0; i < CBVSRVUAVCounts::SRVGBuffersCount; i++)
	{
		UINT SRVIndex = FRAME_DESCRIPTOR_OFFSET(SRVGBuffers, m_frameIndex) + i;

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = GBufferFormats[i];
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D = {
			.MostDetailedMip = 0,
			.MipLevels = (UINT)(-1),
			.PlaneSlice = 0,
			.ResourceMinLODClamp = 0.0f
		};
		
		CD3DX12_CPU_DESCRIPTOR_HANDLE gbufferSRVHandle(cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		gbufferSRVHandle.Offset(SRVIndex, cbvSrvUavDescriptorSize);

		device->CreateShaderResourceView(gBuffers[i].Get(), &srvDesc, gbufferSRVHandle);
	}

	// SRV for middle texture
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = CreateBackbufferSRVDesc();

		CD3DX12_CPU_DESCRIPTOR_HANDLE middleTextureSRVHandle(cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		middleTextureSRVHandle.Offset(FRAME_DESCRIPTOR_OFFSET(SRVMiddleTexture, m_frameIndex), cbvSrvUavDescriptorSize);

		device->CreateShaderResourceView(middleTexture.Get(), &srvDesc, middleTextureSRVHandle);
	}

	// SRV for accumulation texture
	// TODONOW: fix accumilation texture.
	{
		//D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = CreateBackbufferSRVDesc();
		//
		//CD3DX12_CPU_DESCRIPTOR_HANDLE accumilationTextureSRVHandle(cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		//accumilationTextureSRVHandle.Offset(FRAME_DESCRIPTOR_OFFSET(SRVAccumulationTexture, m_frameIndex), cbvSrvUavDescriptorSize);
		//
		//device->CreateShaderResourceView(m_accumulationTexture.Get(), &srvDesc, accumilationTextureSRVHandle);
	}
}



FrameResource::FrameResource(UINT frameIndex, ComPtr<ID3D12Resource> backBuffer, FrameResourceInputs inputs)
	: syncHandler(), fenceValue(0), m_frameIndex(frameIndex)
{
	const UINT width = (UINT)inputs.viewPort.Width;
	const UINT height = (UINT)inputs.viewPort.Height;

	CreateCommandResources(inputs.device);
	CreateDepthBuffer(inputs.device, width, height);
	CreateGBuffers(inputs.device, width, height);
	CreateMiddleTexture(inputs.device, width, height);
	CreateTopLevelASs(inputs.device);
	CreateConstantBuffers(inputs.device);

	CreateRTVs(inputs.device, inputs.rtvHeap, backBuffer);
	CreateDSV(inputs.device, inputs.dsvHeap);
	CreateCBVs(inputs.device, inputs.cbvSrvUavHeap, inputs.cbvSrvUavDescriptorSize);
	CreateSRVs(inputs.device, inputs.cbvSrvUavHeap, inputs.cbvSrvUavDescriptorSize);
	CreateUAVs(inputs.device, inputs.cbvSrvUavHeap, inputs.cbvSrvUavDescriptorSize);

	CreateShaderTables(inputs.device, inputs.cbvSrvUavHeap, inputs.cbvSrvUavDescriptorSize, inputs.rtPipelineStateObject);
	CreateTopLevelASDescriptors(inputs.device, inputs.cbvSrvUavHeap, inputs.cbvSrvUavDescriptorSize);

	
}

void FrameResource::CreateUAVs(ComPtr<ID3D12Device5> device, ComPtr<ID3D12DescriptorHeap> cbvSrvUavHeap, UINT cbvSrvUavDescriptorSize)
{
	// UAV for middle texture.
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = CreateBackbufferUAVDesc();

		CD3DX12_CPU_DESCRIPTOR_HANDLE middleTextureUAVHandle(cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		middleTextureUAVHandle.Offset(FRAME_DESCRIPTOR_OFFSET(UAVMiddleTexture, m_frameIndex), cbvSrvUavDescriptorSize);

		device->CreateUnorderedAccessView(middleTexture.Get(), nullptr, &uavDesc, middleTextureUAVHandle);
	}


	// UAV for accumulation texture.
	// TODONOW: fix accumilation texture.
	{
		//D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = CreateBackbufferUAVDesc();
		//
		//CD3DX12_CPU_DESCRIPTOR_HANDLE accumulationTextureUAVHandle(cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		//accumulationTextureUAVHandle.Offset(CBVSRVUAVOffsets::UAVAccumulationTextureOffset, cbvSrvUavDescriptorSize);
		//
		//device->CreateUnorderedAccessView(m_accumulationTexture.Get(), nullptr, &uavDesc, accumulationTextureUAVHandle);
	}
}

void DX12Renderer::InitAssets()
{
	CreateRootSignatures();
	CreatePSOs();
	CreateRenderObjects();
	CreateCamera();
	CreateRenderInstances();
}

void DX12Renderer::CreateRootSignatures()
{
	std::array<CD3DX12_ROOT_PARAMETER, DefaultRootParameterIdx::DefaultRootParameterCount> rootParameters = {};
	// Default root parameter setup.
	{
		// Add a matrix to the root signature where each element is stored as a constant.
		rootParameters[DefaultRootParameterIdx::MatrixIdx].InitAsConstants(
			sizeof(dx::XMMATRIX) / 4,
			RasterShaderRegisters::CBVRegisters::CBMatrixConstants,
			0,
			D3D12_SHADER_VISIBILITY_VERTEX
		);

		rootParameters[DefaultRootParameterIdx::CBVGlobalFrameDataIdx].InitAsConstantBufferView(RasterShaderRegisters::CBVRegisters::CBVDescriptorGlobals);

		// Add descriptor table for instance specific constants.
		CD3DX12_DESCRIPTOR_RANGE instanceCBVRange;
		instanceCBVRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, RasterShaderRegisters::CBVRegisters::CBVDescriptorRange);
		rootParameters[DefaultRootParameterIdx::CBVTableIdx].InitAsDescriptorTable(1, &instanceCBVRange, D3D12_SHADER_VISIBILITY_VERTEX);

		// Add descriptor range for gbuffers.
		CD3DX12_DESCRIPTOR_RANGE gBufferSRVRange;
		gBufferSRVRange.Init(
			D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 
			CBVSRVUAVCounts::SRVGBuffersCount, 
			RasterShaderRegisters::SRVRegisters::SRVDescriptorRange
		);

		// Descriptor range for middle texture SRV.
		CD3DX12_DESCRIPTOR_RANGE middleTextureSRVRange;
		middleTextureSRVRange.Init(
			D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
			CBVSRVUAVCounts::SRVMiddleTextureCount,
			RasterShaderRegisters::SRVRegisters::SRVDescriptorRange + CBVSRVUAVCounts::SRVGBuffersCount,
			0,
			CBVSRVUAVOffsets::SRVMiddleTextureOffset - CBVSRVUAVOffsets::SRVGBuffersOffset
		);

		// Descriptor range for accumulation UAV.
		CD3DX12_DESCRIPTOR_RANGE accumulationUAVRange;
		accumulationUAVRange.Init(
			D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
			CBVSRVUAVCounts::UAVAccumulationTextureCount,
			RasterShaderRegisters::UAVRegisters::UAVDescriptorRange,
			0,
			CBVSRVUAVOffsets::UAVAccumulationTextureOffset - CBVSRVUAVOffsets::SRVGBuffersOffset
		);

		std::array<CD3DX12_DESCRIPTOR_RANGE, 3> UAVSRVTable = { { gBufferSRVRange, middleTextureSRVRange, accumulationUAVRange } };
		rootParameters[DefaultRootParameterIdx::UAVSRVTableIdx].InitAsDescriptorTable(
			(UINT)UAVSRVTable.size(), 
			UAVSRVTable.data(), 
			D3D12_SHADER_VISIBILITY_PIXEL
		);
	}

	// Static general sampler for all shaders.
	CD3DX12_STATIC_SAMPLER_DESC staticSampler(0);

	// Define rootsig description.
	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init(
		(UINT)rootParameters.size(),
		rootParameters.data(),
		1, &staticSampler,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	);

	SerializeAndCreateRootSig(rootSignatureDesc, m_rasterRootSignature);
	NAME_D3D12_OBJECT_MEMBER(m_rasterRootSignature, DX12Renderer);
}

void DX12Renderer::CreatePSOs()
{
	for (RenderPassType passType : sPassesToRegister)
	{
		RegisterRenderPass(passType);
	}
}


void DX12Renderer::CreateRenderObjects()
{
	// Create render objects.
	{
		std::vector<Vertex> triangleData = { {
			{ { 0.0f, 0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, // top
			{ { 0.43f, -0.25f, 0.0f }, { 0.0f, 0.0f, 1.0f } }, // right
			{ { -0.43f, -0.25f, 0.0f }, { 0.0f, 1.0f, 0.0f } } // left
		} };

		m_renderObjectsByID[RenderObjectID::Triangle] = CreateRenderObject(&triangleData, nullptr, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}

	{
		//std::vector<Vertex> cubeData = { {
		//	{ { -1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f, 0.0f } }, // 0
		//	{ { -1.0f,  1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } }, // 1
		//	{ {  1.0f,  1.0f, -1.0f }, { 1.0f, 1.0f, 0.0f } }, // 2
		//	{ {  1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f } }, // 3
		//	{ { -1.0f, -1.0f,  1.0f }, { 0.0f, 0.0f, 1.0f } }, // 4
		//	{ { -1.0f,  1.0f,  1.0f }, { 0.0f, 1.0f, 1.0f } }, // 5
		//	{ {  1.0f,  1.0f,  1.0f }, { 1.0f, 1.0f, 1.0f } }, // 6
		//	{ {  1.0f, -1.0f,  1.0f }, { 1.0f, 0.0f, 1.0f } }  // 7
		//} };
		//
		//std::vector<VertexIndex> cubeIndices = {
		//		0, 1, 2, 0, 2, 3, // front face
		//		4, 6, 5, 4, 7, 6, // back face
		//		4, 5, 1, 4, 1, 0, // left face
		//		3, 2, 6, 3, 6, 7, // right face
		//		1, 5, 6, 1, 6, 2, // top face
		//		4, 0, 3, 4, 3, 7  // bottom face
		//};

		std::string modelPath = std::string(AssetsPath) + "cube.obj";
		m_renderObjectsByID[RenderObjectID::Cube] = CreateRenderObjectFromOBJ(modelPath, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}

	// Add obj model.
	{
		std::string modelPath = std::string(AssetsPath) + "Sphere.obj";
		m_renderObjectsByID[RenderObjectID::OBJModel1] = CreateRenderObjectFromOBJ(modelPath, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}
}

void DX12Renderer::CreateCamera()
{
	const float aspectRatio = static_cast<float>(m_width) / static_cast<float>(m_height);
	constexpr float nearZ = 0.01f;
	constexpr float farZ = 1000.0f;
	constexpr float fov = XMConvertToRadians(90.0f);

	m_cameras.push_back(Camera(fov, aspectRatio, nearZ, farZ));
	m_activeCamera = &m_cameras[0];

	m_activeCamera->SetPosAndDir({ 0.0f, 0.0f, -10.0f }, { 0.0f, 0.0f, 1.0f });
}

void DX12Renderer::CreateRenderInstances()
{
	static UINT renderInstanceCount = 0;

	// Triangles.
	{
		std::vector<RenderInstance>& renderInstances = m_renderInstancesByID[RenderObjectID::Triangle];

		RenderInstance renderInstance = {};

		renderInstance.CBIndex = renderInstanceCount++;
		dx::XMStoreFloat4x4(&renderInstance.instanceData.modelMatrix, dx::XMMatrixTranslation(3.0f, 0.0f, 0.0f));
		renderInstances.push_back(renderInstance);

		renderInstance.CBIndex = renderInstanceCount++;
		dx::XMStoreFloat4x4(&renderInstance.instanceData.modelMatrix, dx::XMMatrixTranslation(-3.0f, 0.0f, 0.0f));
		renderInstances.push_back(renderInstance);

		renderInstance.CBIndex = renderInstanceCount++;
		dx::XMStoreFloat4x4(&renderInstance.instanceData.modelMatrix, dx::XMMatrixTranslation(-6.0f, 0.0f, 0.0f));
		renderInstances.push_back(renderInstance);

		renderInstance.CBIndex = renderInstanceCount++;
		dx::XMStoreFloat4x4(&renderInstance.instanceData.modelMatrix, dx::XMMatrixTranslation(6.0f, 0.0f, 0.0f));
		renderInstances.push_back(renderInstance);

		renderInstance.CBIndex = renderInstanceCount++;
		dx::XMStoreFloat4x4(&renderInstance.instanceData.modelMatrix, dx::XMMatrixTranslation(0.0f, 0.0f, 0.0f));
		renderInstances.push_back(renderInstance);
	}

	// Cubes.
	{
		std::vector<RenderInstance>& renderInstances = m_renderInstancesByID[RenderObjectID::OBJModel1];

		RenderInstance renderInstance = {};

		renderInstance.CBIndex = renderInstanceCount++;
		dx::XMStoreFloat4x4(&renderInstance.instanceData.modelMatrix, dx::XMMatrixTranslation(0.0f, 0.0f, -5.0f));
		renderInstances.push_back(renderInstance);
	}

	{
		std::vector<RenderInstance>& renderInstances = m_renderInstancesByID[RenderObjectID::Cube];

		RenderInstance renderInstance = {};

		int offset = 2;
		float scale = 3.0f;
		
		int maxZ = 3;
		int maxYX = 5;
		for (int z = 0; z < maxZ; z++)
		{
			float zPos = (z - (maxZ / 2)) * scale;
			for (int x = 0; x < maxYX; x++)
			{
				float xPos = (x - (maxYX / 2)) * scale;
				for (int y = 0; y < maxYX; y++)
				{
					float yPos = (y - (maxYX / 2)) * scale;
					
					renderInstance.CBIndex = renderInstanceCount++;
					dx::XMStoreFloat4x4(&renderInstance.instanceData.modelMatrix, dx::XMMatrixTranslation(xPos, yPos, zPos));
					renderInstances.push_back(renderInstance);
				}
			}
		}
	}
}

void DX12Renderer::InitRaytracing()
{
	CreateAccelerationStructures();
	CreateRaytracingPipelineState();

	// Wait for all work to be done.
	m_directCommandQueue->SignalAndWait();
}

void DX12Renderer::CreateAccelerationStructures()
{
	m_directCommandQueue->ResetAllocator();
	auto commandList = m_directCommandQueue->CreateCommandList(m_device);

	CreateBottomLevelASs(commandList);

	commandList->Close();

	DX12Abstractions::CommandListVector commandLists = { commandList };
	m_directCommandQueue->ExecuteCommandLists(commandLists);

	m_directCommandQueue->SignalAndWait();
}

void DX12Renderer::CreateBottomLevelASs(ComPtr<ID3D12GraphicsCommandList4> commandList)
{
	for (const RenderObjectID objectID : sRTRenderObjectIDs)
	{
		CreateBottomLevelAccelerationStructure(objectID, commandList);
	}
}

void DX12Renderer::CreateBottomLevelAccelerationStructure(RenderObjectID objectID, ComPtr<ID3D12GraphicsCommandList4> commandList)
{
	RenderObject& renderObject = m_renderObjectsByID[objectID];

	D3D12_RAYTRACING_GEOMETRY_DESC geomDesc[1] = {};
	geomDesc[0].Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	geomDesc[0].Triangles.VertexBuffer.StartAddress = renderObject.vertexBuffer.resource->GetGPUVirtualAddress();
	geomDesc[0].Triangles.VertexBuffer.StrideInBytes = renderObject.vertexBufferView.StrideInBytes;
	geomDesc[0].Triangles.VertexCount = renderObject.vertexBufferView.SizeInBytes / renderObject.vertexBufferView.StrideInBytes;
	geomDesc[0].Triangles.VertexFormat = Vertex::sVertexFormat;
	geomDesc[0].Triangles.IndexBuffer = renderObject.indexBuffer.resource->GetGPUVirtualAddress();
	geomDesc[0].Triangles.IndexCount = renderObject.indexBufferView.SizeInBytes / sizeof(VertexIndex);
	geomDesc[0].Triangles.IndexFormat = renderObject.indexBufferView.Format;
	geomDesc[0].Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

	// Get the size requirements for the scratch and AS buffers
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
	inputs.NumDescs = ARRAYSIZE(geomDesc);
	inputs.pGeometryDescs = geomDesc;
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
	m_device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

	AccelerationStructureBuffers& bottomLevelAccStruct = m_bottomAccStructByID[objectID];

	// Create scratch resource.
	{
		CD3DX12_RESOURCE_DESC bottomScratchResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(
			info.ScratchDataSizeInBytes,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		);

		bottomLevelAccStruct.scratch = CreateResource(
			m_device,
			bottomScratchResourceDesc,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_HEAP_TYPE_DEFAULT
		);
	}

	// Create result resource.
	{
		CD3DX12_RESOURCE_DESC bottomResultResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(
			info.ResultDataMaxSizeInBytes,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		);

		bottomLevelAccStruct.result = CreateResource(
			m_device,
			bottomResultResourceDesc,
			D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
			D3D12_HEAP_TYPE_DEFAULT
		);
	}

	// Create the bottom-level AS
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
	asDesc.Inputs = inputs;
	asDesc.DestAccelerationStructureData = bottomLevelAccStruct.result.resource->GetGPUVirtualAddress();
	asDesc.ScratchAccelerationStructureData = bottomLevelAccStruct.scratch.resource->GetGPUVirtualAddress();

	commandList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
}

void FrameResource::CreateTopLevelAS(ComPtr<ID3D12Device5> device, RenderObjectID renderObjectID)
{
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
	inputs.NumDescs = MaxRTInstancesPerTopLevel;
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

	AccelerationStructureBuffers topAccStruct = {};

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
	device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

	CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	topAccStruct.scratch = CreateResource(
		device,
		resourceDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_HEAP_TYPE_DEFAULT
	);

	resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	topAccStruct.result = CreateResource(
		device,
		resourceDesc,
		D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
		D3D12_HEAP_TYPE_DEFAULT
	);

	resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * inputs.NumDescs);
	topAccStruct.instanceDesc = CreateUploadResource(device, resourceDesc);

	topAccStructByID[renderObjectID] = topAccStruct;
}

void DX12Renderer::CreateRaytracingPipelineState()
{
	constexpr UINT MaxSubObjects = 100u;
	std::array<D3D12_STATE_SUBOBJECT, MaxSubObjects> soMemory{};
	UINT totalSubobjects = 0u;

	auto GetNextSubObject = [&]()
	{
		return &soMemory[totalSubobjects++];
	};

	ComPtr<ID3DBlob> rtShaderBlob;
	D3D12_DXIL_LIBRARY_DESC dxilLibraryDesc;
	// Shader exports.
	std::array<D3D12_EXPORT_DESC, 3> dxilExports = { {
		{ RayGenShaderName, nullptr, D3D12_EXPORT_FLAG_NONE },
		{ AnyHitShaderName, nullptr, D3D12_EXPORT_FLAG_NONE },
		{ MissShaderName, nullptr, D3D12_EXPORT_FLAG_NONE }
	} };
	{
		D3DReadFileToBlob(L"../RTShader.dxil", &rtShaderBlob) >> CHK_HR;
		CD3DX12_SHADER_BYTECODE anyHitBytecode(rtShaderBlob.Get());

		
		dxilLibraryDesc.DXILLibrary = anyHitBytecode;
		dxilLibraryDesc.pExports = dxilExports.data();
		dxilLibraryDesc.NumExports = (UINT)dxilExports.size();

		D3D12_STATE_SUBOBJECT* so = GetNextSubObject();
		so->Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
		so->pDesc = &dxilLibraryDesc;
	}

	// Init hit group.
	D3D12_HIT_GROUP_DESC hitGroupDesc;
	{
		hitGroupDesc.HitGroupExport = HitGroupName;

		hitGroupDesc.AnyHitShaderImport = AnyHitShaderName;
		hitGroupDesc.ClosestHitShaderImport = nullptr;
		hitGroupDesc.IntersectionShaderImport = nullptr;
		hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;

		D3D12_STATE_SUBOBJECT* so = GetNextSubObject();
		so->Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
		so->pDesc = &hitGroupDesc;
	}

	// Init raygen local root signature and bind to raygen shader.
	ComPtr<ID3D12RootSignature> rayGenLocalRootSig;
	D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION rayGenLocalRootAssociation;
	std::array<LPCWSTR, 1> rayGenShaderNames = { RayGenShaderName };
	{
		CreateRayGenLocalRootSignature(rayGenLocalRootSig);
		D3D12_STATE_SUBOBJECT* soRayGenLocalRootSig = GetNextSubObject();
		soRayGenLocalRootSig->Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
		soRayGenLocalRootSig->pDesc = rayGenLocalRootSig.GetAddressOf();

		// Bind to shader.
		{
			rayGenLocalRootAssociation.pExports = rayGenShaderNames.data();
			rayGenLocalRootAssociation.NumExports = (UINT)rayGenShaderNames.size();
			rayGenLocalRootAssociation.pSubobjectToAssociate = soRayGenLocalRootSig;

			D3D12_STATE_SUBOBJECT* so = GetNextSubObject();
			so->Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
			so->pDesc = &rayGenLocalRootAssociation;
		}
	}

	
	// Init hit group local root signature and bind to hit group shaders.
	ComPtr<ID3D12RootSignature> hitGroupLocalRootSig;
	D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION hitGroupLocalRootAssociation;
	std::array<LPCWSTR, 1> hitGroupShaderNames = { AnyHitShaderName };
	{
		CreateHitGroupLocalRootSignature(hitGroupLocalRootSig);
		D3D12_STATE_SUBOBJECT* soHitGroupLocalRootSig = GetNextSubObject();
		soHitGroupLocalRootSig->Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
		soHitGroupLocalRootSig->pDesc = hitGroupLocalRootSig.GetAddressOf();

		// Bind to shader.
		{
			hitGroupLocalRootAssociation.pExports = hitGroupShaderNames.data();
			hitGroupLocalRootAssociation.NumExports = (UINT)hitGroupShaderNames.size();
			hitGroupLocalRootAssociation.pSubobjectToAssociate = soHitGroupLocalRootSig;

			D3D12_STATE_SUBOBJECT* so = GetNextSubObject();
			so->Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
			so->pDesc = &hitGroupLocalRootAssociation;
		}
	}

	// Init miss local root signature and bind to miss shaders
	ComPtr<ID3D12RootSignature> missLocalRootSig;
	D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION missLocalRootAssociation;
	std::array<LPCWSTR, 1> missShaderNames = { MissShaderName };
	{
		CreateMissLocalRootSignature(missLocalRootSig);
		D3D12_STATE_SUBOBJECT* soMissLocalRootSig = GetNextSubObject();
		soMissLocalRootSig->Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
		soMissLocalRootSig->pDesc = missLocalRootSig.GetAddressOf();

		// Bind to shader.
		{
			missLocalRootAssociation.pExports = missShaderNames.data();
			missLocalRootAssociation.NumExports = (UINT)missShaderNames.size();
			missLocalRootAssociation.pSubobjectToAssociate = soMissLocalRootSig;

			D3D12_STATE_SUBOBJECT* so = GetNextSubObject();
			so->Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
			so->pDesc = &missLocalRootAssociation;
		}
	}

	// Init shader config and bind it to programs.
	D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
	D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shaderConfigAssociation;
	std::array<LPCWSTR, 3> shaderNamesForConfig = { MissShaderName, AnyHitShaderName, RayGenShaderName };
	{
		// TODO: Make this match shader.
		shaderConfig.MaxAttributeSizeInBytes = sizeof(float) * 2;
		shaderConfig.MaxPayloadSizeInBytes = sizeof(float) * 1;

		D3D12_STATE_SUBOBJECT* soShaderConfig = GetNextSubObject();
		soShaderConfig->Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
		soShaderConfig->pDesc = &shaderConfig;
		
		//Bind the payload size to the programs
		{
			// TODO: Check if it needs to be const pointers inside of array.	
			shaderConfigAssociation.pExports = shaderNamesForConfig.data();
			shaderConfigAssociation.NumExports = (UINT)shaderNamesForConfig.size();
			shaderConfigAssociation.pSubobjectToAssociate = soShaderConfig;

			D3D12_STATE_SUBOBJECT* soShaderConfigAssociation = GetNextSubObject();
			soShaderConfigAssociation->Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
			soShaderConfigAssociation->pDesc = &shaderConfigAssociation;
		}
		
	}

	// Init pipeline config
	D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig;
	{
		pipelineConfig.MaxTraceRecursionDepth = 1;

		D3D12_STATE_SUBOBJECT* so = GetNextSubObject();
		so->Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
		so->pDesc = &pipelineConfig;
	}

	// Create the global root sig and save it.
	{
		CreateGlobalRootSignature(m_RTGlobalRootSignature);

		D3D12_STATE_SUBOBJECT* so = GetNextSubObject();
		so->Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
		so->pDesc = m_RTGlobalRootSignature.GetAddressOf();
	}

	// Create the final state.
	D3D12_STATE_OBJECT_DESC desc;
	desc.NumSubobjects = totalSubobjects;
	desc.pSubobjects = soMemory.data();
	desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;

	m_device->CreateStateObject(&desc, IID_PPV_ARGS(&m_RTPipelineState)) >> CHK_HR;
}


void DX12Renderer::CreateRayGenLocalRootSignature(ComPtr<ID3D12RootSignature>& rootSig)
{
	std::array<CD3DX12_ROOT_PARAMETER, RTRayGenParameterIdx::RTRayGenParameterCount> rootParameters = {};
	CD3DX12_DESCRIPTOR_RANGE srvRangeGbuffers;
	CD3DX12_DESCRIPTOR_RANGE srvRangeTLAS;
	CD3DX12_DESCRIPTOR_RANGE uavRange;
	{
		// Add root descriptor table for TLAS shader resource.
		srvRangeTLAS.Init(
			D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
			CBVSRVUAVCounts::SRVTLASCount,
			RTShaderRegisters::SRVRegistersRayGen::SRVDescriptorTableTLASRegister
		);
		rootParameters[RTRayGenParameterIdx::RayGenSRVTableTLASIdx].InitAsDescriptorTable(1, &srvRangeTLAS, D3D12_SHADER_VISIBILITY_ALL);
		
		// Add root descriptor table for shader resources (gbuffers).
		srvRangeGbuffers.Init(
			D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
			CBVSRVUAVCounts::SRVGBuffersCount,
			RTShaderRegisters::SRVRegistersRayGen::SRVDescriptorTableGbuffersRegister
		);
		rootParameters[RTRayGenParameterIdx::RayGenSRVTableGbuffersIdx].InitAsDescriptorTable(1, &srvRangeGbuffers, D3D12_SHADER_VISIBILITY_ALL);

		// Add root descriptor for UAV that is going to be written to.
		uavRange.Init(
			D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 
			CBVSRVUAVCounts::SRVMiddleTextureCount, 
			RTShaderRegisters::UAVRegistersRayGen::UAVDescriptorRegister
		);
		rootParameters[RTRayGenParameterIdx::RayGenUAVTableIdx].InitAsDescriptorTable(1, &uavRange, D3D12_SHADER_VISIBILITY_ALL);
	}

	// Create the desc
	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init(
		(UINT)rootParameters.size(),
		rootParameters.data(),
		0,
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE
	);

	SerializeAndCreateRootSig(rootSignatureDesc, rootSig);
}

void DX12Renderer::CreateHitGroupLocalRootSignature(ComPtr<ID3D12RootSignature>& rootSig)
{
	// Empty root sig because we don't need any local data if we hit.
	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init(0, nullptr);
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

	SerializeAndCreateRootSig(rootSignatureDesc, rootSig);
}

void DX12Renderer::CreateMissLocalRootSignature(ComPtr<ID3D12RootSignature>& rootSig)
{
	// Empty root sig because we don't need any local data if we miss.
	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init(0, nullptr);
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

	SerializeAndCreateRootSig(rootSignatureDesc, rootSig);
}

void DX12Renderer::CreateGlobalRootSignature(ComPtr<ID3D12RootSignature>& rootSig)
{
	std::array<CD3DX12_ROOT_PARAMETER, RTGlobalParameterIdx::RTGlobalParameterCount> rootParameters = {};
	{
		rootParameters[RTGlobalParameterIdx::Global32BitConstantIdx].InitAsConstants(
			1,
			RTShaderRegisters::ConstantRegistersGlobal::ConstantRegister
		);
	}

	// No flag needed for global root sig.
	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init(
		(UINT)rootParameters.size(),
		rootParameters.data()
	);

	SerializeAndCreateRootSig(rootSignatureDesc, rootSig);
}

void DX12Renderer::InitFrameResources()
{
	FrameResource::FrameResourceInputs inputs = {
		.device = m_device,
		.viewPort = m_viewport,
		.dsvHeap = m_dsvHeap,
		.cbvSrvUavHeap = m_cbvSrvUavHeap,
		.cbvSrvUavDescriptorSize = m_cbvSrvUavDescriptorSize,
		.rtvHeap = m_rtvHeap,
		.rtPipelineStateObject = m_RTPipelineState,
	};

	for (UINT frameIndex = 0; frameIndex < BackBufferCount; frameIndex++)
	{
		m_frameResources[frameIndex] = std::make_unique<FrameResource>(frameIndex, m_backBuffers[frameIndex], inputs);
	}

	for (RenderPassType passType : sPassesToRegister)
	{
		// Register render passes to all sync handlers.
		for (UINT i = 0; i < m_frameResources.size(); i++)
		{
			FrameResource* frameResource = m_frameResources[i].get();

			frameResource->syncHandler.AddUniquePassSync(passType);
		}
	}

	m_currentFrameResource = m_frameResources[0].get();
}

void FrameResource::CreateShaderTables(ComPtr<ID3D12Device5> device, ComPtr<ID3D12DescriptorHeap> cbvSrvUavHeap, UINT cbvSrvUavDescriptorSize, ComPtr<ID3D12StateObject> rtPipelineStateObject)
{
	ComPtr<ID3D12StateObjectProperties> RTStateObjectProps = nullptr;
	rtPipelineStateObject->QueryInterface(IID_PPV_ARGS(&RTStateObjectProps)) >> CHK_HR;

	// Raygen table.
	{
		// Shader record 1.
		struct alignas(D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT) RAY_GEN_SHADER_TABLE_DATA
		{
			unsigned char ShaderIdentifier[D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES];
			UINT64 SRVDescriptorTableTopLevelAS;
			UINT64 SRVDescriptorTableGbuffers;
			UINT64 UAVDescriptorTableMiddleTexture;
		} tableData;

		// Set the descriptor table start for middle texture.
		{
			CD3DX12_GPU_DESCRIPTOR_HANDLE uavHandle(cbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart());
			uavHandle.Offset(FRAME_DESCRIPTOR_OFFSET(UAVMiddleTexture, m_frameIndex), cbvSrvUavDescriptorSize);
		
			tableData.UAVDescriptorTableMiddleTexture = uavHandle.ptr;
		}

		// Set TLAS SRV.
		{
			CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(cbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart());
			srvHandle.Offset(FRAME_DESCRIPTOR_OFFSET(SRVTLAS, m_frameIndex), cbvSrvUavDescriptorSize);

			tableData.SRVDescriptorTableTopLevelAS = srvHandle.ptr;
		}
		
		// Set descriptor table for gbuffer start.
		{
			auto descHeapHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(cbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart());
			descHeapHandle.Offset(FRAME_DESCRIPTOR_OFFSET(SRVGBuffers, m_frameIndex), cbvSrvUavDescriptorSize);

			tableData.SRVDescriptorTableGbuffers = descHeapHandle.ptr;
		}

		void* dest = tableData.ShaderIdentifier;
		void* src = RTStateObjectProps->GetShaderIdentifier(RayGenShaderName);
		memcpy(dest, src, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

		union MaxSizeStruct
		{
			RAY_GEN_SHADER_TABLE_DATA tabledata0;
		};

		rayGenShaderTable.strideInBytes = sizeof(MaxSizeStruct);
		rayGenShaderTable.sizeInBytes = rayGenShaderTable.strideInBytes * 1; // A single ray gen table for now.
		{
			CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(rayGenShaderTable.sizeInBytes);
			rayGenShaderTable.tableResource = CreateUploadResource(
				device,
				resourceDesc
			);
		}

		MapDataToBuffer(rayGenShaderTable.tableResource, &tableData, sizeof(tableData));
	}

	// Miss table.
	{
		struct alignas(D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT) MISS_SHADER_TABLE_DATA
		{
			unsigned char ShaderIdentifier[D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES];
		} tableData;

		void* dest = tableData.ShaderIdentifier;
		void* src = RTStateObjectProps->GetShaderIdentifier(MissShaderName);
		memcpy(dest, src, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

		union MaxSizeStruct
		{
			MISS_SHADER_TABLE_DATA tabledata0;
		};

		missShaderTable.strideInBytes = sizeof(MaxSizeStruct);
		missShaderTable.sizeInBytes = missShaderTable.strideInBytes * 1; // A single miss table for now.
		{
			CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(missShaderTable.sizeInBytes);
			missShaderTable.tableResource = CreateUploadResource(
				device,
				resourceDesc
			);
		}

		MapDataToBuffer(missShaderTable.tableResource, &tableData, sizeof(tableData));
	}

	// Hit program table.
	{
		struct alignas(D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT) HIT_GROUP_SHADER_TABLE_DATA
		{
			unsigned char ShaderIdentifier[D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES];
		} tableData;

		void* dest = tableData.ShaderIdentifier;
		void* src = RTStateObjectProps->GetShaderIdentifier(HitGroupName);
		memcpy(dest, src, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

		union MaxSizeStruct
		{
			HIT_GROUP_SHADER_TABLE_DATA tabledata0;
		};

		hitGroupShaderTable.strideInBytes = sizeof(MaxSizeStruct);
		hitGroupShaderTable.sizeInBytes = hitGroupShaderTable.strideInBytes * 1; // A single hit group for now.
		{
			CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(hitGroupShaderTable.sizeInBytes);
			hitGroupShaderTable.tableResource = CreateUploadResource(
				device,
				resourceDesc
			);
		}

		MapDataToBuffer(hitGroupShaderTable.tableResource, &tableData, sizeof(tableData));
	}
}

void FrameResource::UpdateFrameResources(const FrameResourceUpdateInputs inputs)
{
	UpdateInstanceConstantBuffers(inputs);
	UpdateGlobalFrameDataBuffer(inputs);
	UpdateTopLevelAccelerationStructure(inputs, RenderObjectID::Cube);
}

void FrameResource::CreateTopLevelASDescriptors(ComPtr<ID3D12Device5> device, ComPtr<ID3D12DescriptorHeap> cbvSrvUavHeap, UINT cbvSrvUavDescriptorSize)
{
	for (RenderObjectID objectID : sRTRenderObjectIDs)
	{
		CreateTopLevelASDescriptor(device, objectID, cbvSrvUavHeap, cbvSrvUavDescriptorSize);
	}
}

void FrameResource::CreateTopLevelASDescriptor(ComPtr<ID3D12Device5> device, RenderObjectID objectID, ComPtr<ID3D12DescriptorHeap> cbvSrvUavHeap, UINT cbvSrvUavDescriptorSize)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.RaytracingAccelerationStructure.Location = topAccStructByID[objectID].result.resource->GetGPUVirtualAddress();

	CD3DX12_CPU_DESCRIPTOR_HANDLE tlasSRVHandle(cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
	tlasSRVHandle.Offset(FRAME_DESCRIPTOR_OFFSET(SRVTLAS, m_frameIndex), cbvSrvUavDescriptorSize);

	// Use nullptr because the resource is already referenced in description of the view.
	device->CreateShaderResourceView(nullptr, &srvDesc, tlasSRVHandle);
}

void DX12Renderer::SerializeAndCreateRootSig(CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc, ComPtr<ID3D12RootSignature>& rootSig)
{
	ComPtr<ID3DBlob> signature = nullptr;
	ComPtr<ID3DBlob> error = nullptr;

	const HRESULT hr = D3D12SerializeRootSignature(
		&rootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		&signature,
		&error
	);

	if (FAILED(hr))
	{
		// If there is an error blob, first print out the problem reported from the blob.
		if (error)
		{
			const char* errorBuffer = static_cast<const char*>(error->GetBufferPointer());
			std::string errorString = std::string("Serialize ERROR: ") + std::string(errorBuffer);
			OutputDebugStringA(errorString.c_str());
		}

		// Properly handle the HR value.
		hr >> CHK_HR;
	}

	m_device->CreateRootSignature(
		0,
		signature->GetBufferPointer(),
		signature->GetBufferSize(),
		IID_PPV_ARGS(&rootSig)
	) >> CHK_HR;
}

void DX12Renderer::UpdateCamera()
{
	// Update camera before rendering.
	dx::XMVECTOR startPos = dx::XMVectorSet(0.0f, 0.0f, -13.0f, 1.0f);
	float angle = m_time * dx::XM_2PI / 20.0f;
	//float angle = 0.0f;
	
	dx::XMMATRIX rotationMatrix = dx::XMMatrixRotationNormal(dx::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), angle);
	dx::XMVECTOR newPos = dx::XMVector3Transform(startPos, rotationMatrix);

	m_activeCamera->SetPosAndLookAt(newPos, dx::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f));
	m_activeCamera->UpdateViewMatrix();
	m_activeCamera->UpdateViewProjectionMatrix();
}

void FrameResource::UpdateInstanceConstantBuffers(const FrameResourceUpdateInputs& inputs)
{
	// Copy through byte offsets.
	uint8_t* instanceDataStart = nullptr;
	perInstanceCB.resource->Map(0, nullptr, reinterpret_cast<void**>(&instanceDataStart)) >> CHK_HR;
	constexpr UINT perInstanceSize = DX12Abstractions::CalculateConstantBufferByteSize(sizeof(InstanceConstants));

	for (const auto& it : inputs.renderInstancesByID)
	{
		RenderObjectID renderObjectID = it.first;
		const std::vector<RenderInstance>& renderInstances = it.second;

		for (const RenderInstance& renderInstance : renderInstances)
		{
			uint8_t* instanceData = instanceDataStart + renderInstance.CBIndex * perInstanceSize;
			memcpy(instanceData, &renderInstance.instanceData, sizeof(InstanceConstants));
		}
	}

	perInstanceCB.resource->Unmap(0, nullptr);
}

void FrameResource::UpdateGlobalFrameDataBuffer(const FrameResourceUpdateInputs& inputs)
{
	GlobalFrameData globalFrameData = inputs.globalFrameData;
	MapDataToBuffer<GlobalFrameData>(globalFrameDataCB, &globalFrameData, sizeof(GlobalFrameData));
}

void FrameResource::UpdateTopLevelAccelerationStructure(const FrameResourceUpdateInputs& inputs, RenderObjectID objectID)
{
	AccelerationStructureBuffers& topAccStruct = topAccStructByID[objectID];
	const D3D12_GPU_VIRTUAL_ADDRESS bottomLevelAddress = inputs.bottomAccStructByID.at(objectID).result.resource->GetGPUVirtualAddress();
	const UINT instanceCount = (UINT)inputs.renderInstancesByID.at(objectID).size();

	D3D12_RAYTRACING_INSTANCE_DESC* instanceDesc = nullptr;
	topAccStruct.instanceDesc.resource->Map(0, nullptr, reinterpret_cast<void**>(&instanceDesc)) >> CHK_HR;

	for (UINT i = 0; i < instanceCount; i++)
	{
		const RenderInstance& renderInstance = inputs.renderInstancesByID.at(objectID)[i];
		instanceDesc->InstanceID = i;
		instanceDesc->InstanceContributionToHitGroupIndex = 0;
		instanceDesc->Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;

		dx::XMFLOAT3X4 transfM;
		dx::XMStoreFloat3x4(&transfM, dx::XMLoadFloat4x4(&renderInstance.instanceData.modelMatrix));
		memcpy(instanceDesc->Transform, &transfM, sizeof(instanceDesc->Transform));

		instanceDesc->AccelerationStructure = bottomLevelAddress;
		instanceDesc->InstanceMask = 0xFF; // TODO: Check if this is even needed?

		instanceDesc++;
	}

	topAccStruct.instanceDesc.resource->Unmap(0, nullptr);
}



void FrameResource::Init()
{
	for (UINT i = 0; i < CommandListIdentifier::NumCommandLists; i++)
	{
		commandAllocators[i]->Reset() >> CHK_HR;
		commandLists[i]->Reset(commandAllocators[i].Get(), nullptr) >> CHK_HR;
	}
}

UINT FrameResource::GetFrameIndex() const
{
	return m_frameIndex;
}

// Macro for reducing code duplication in render pass registration.
// What this macro does is adds it to the render pass map and also registers it for the sync handler.
#define CaseRegisterRenderPass(renderpasstype, renderclass) \
case renderpasstype: \
	m_renderPasses[renderpasstype] = std::make_unique<renderclass>(m_device.Get(), m_rasterRootSignature); \
	break

void DX12Renderer::RegisterRenderPass(const RenderPassType renderPassType)
{
	switch (renderPassType)
	{
		CaseRegisterRenderPass(DeferredGBufferPass, DeferredGBufferRenderPass);

		CaseRegisterRenderPass(NonIndexedPass, NonIndexedRenderPass);

		CaseRegisterRenderPass(IndexedPass, IndexedRenderPass);

		CaseRegisterRenderPass(DeferredLightingPass, DeferredLightingRenderPass);

		CaseRegisterRenderPass(RaytracedAOPass, RaytracedAORenderPass);

		CaseRegisterRenderPass(AccumulationPass, AccumilationRenderPass);

	default:
		// TODO: Handle this error better.
		assert(false);
		break;
	}

}

void DX12Renderer::ClearGBuffers(ComPtr<ID3D12GraphicsCommandList> commandList)
{
	for (UINT i = 0; i < GBufferIDCount; i++)
	{
		// Get RTV handle for GBuffer.
		const CD3DX12_CPU_DESCRIPTOR_HANDLE gBufferRTVHandle(
			m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
			FRAME_DESCRIPTOR_OFFSET(RTVGBuffers, m_currentFrameResource->GetFrameIndex()),
			m_rtvDescriptorSize
		);

		commandList->ClearRenderTargetView(gBufferRTVHandle, OptimizedClearColor, 0, nullptr);
	}
}

void DX12Renderer::TransitionGBuffers(ComPtr<ID3D12GraphicsCommandList> commandList, D3D12_RESOURCE_STATES newResourceState)
{
	for (GPUResource& gBuffer : m_currentFrameResource->gBuffers)
	{
		gBuffer.TransitionTo(newResourceState, commandList);
	}
}

RenderObject DX12Renderer::CreateRenderObject(const std::vector<Vertex>* vertices, const std::vector<VertexIndex>* indices, D3D12_PRIMITIVE_TOPOLOGY topology)
{
	RenderObject renderObject;

	// Temp command list for setting up render objects.
	// Using command list '1' closes the command list immediately and 
	// doesn't require a command allocator as input, which usually is replaced either way.
	m_copyCommandQueue->ResetAllocator();
	ComPtr<ID3D12GraphicsCommandList1> copyCommandList = m_copyCommandQueue->CreateCommandList(m_device);

	m_directCommandQueue->ResetAllocator();
	ComPtr<ID3D12GraphicsCommandList1> directCommandList = m_directCommandQueue->CreateCommandList(m_device);

	UINT vertexCount = 0;
	GPUResource vertexUploadBuffer;
	if(vertices != nullptr)
	{
		vertexCount = (UINT)vertices->size();
		UINT vertexSize = sizeof(vertices->at(0));
		UINT vertexBufferSize = vertexSize * vertexCount;

		UploadResource<Vertex>(
			m_device,
			copyCommandList,
			renderObject.vertexBuffer,
			vertexUploadBuffer,
			vertices->data(),
			vertexBufferSize
		);

		// Create vertex buffer view.
		auto& vbView = renderObject.vertexBufferView;
		{
			vbView.BufferLocation = renderObject.vertexBuffer.resource->GetGPUVirtualAddress();
			vbView.StrideInBytes = vertexSize;
			vbView.SizeInBytes = vertexBufferSize;
		}

		renderObject.vertexBuffer.TransitionTo(D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, directCommandList);
	}

	UINT indexCount = 0;
	GPUResource indexUploadBuffer;
	if(indices != nullptr)
	{
		indexCount = (UINT)indices->size();
		UINT indexSize = sizeof(indices->at(0));
		UINT indexBufferSize = indexSize * indexCount;

		UploadResource<VertexIndex>(
			m_device,
			copyCommandList,
			renderObject.indexBuffer,
			indexUploadBuffer,
			indices->data(),
			indexBufferSize
		);

		// Create index buffer view.
		auto& ibView = renderObject.indexBufferView;
		{
			ibView.BufferLocation = renderObject.indexBuffer.resource->GetGPUVirtualAddress();
			ibView.Format = GetDXGIFormat<VertexIndex>();
			ibView.SizeInBytes = indexBufferSize;
		}

		renderObject.indexBuffer.TransitionTo(D3D12_RESOURCE_STATE_INDEX_BUFFER, directCommandList);
	}

	// Close when done.
	copyCommandList->Close() >> CHK_HR;
	directCommandList->Close() >> CHK_HR;

	// Execute copy commands.
	{
		std::array<ID3D12CommandList* const, 1> commandLists = { copyCommandList.Get() };
		m_copyCommandQueue->commandQueue->ExecuteCommandLists((UINT)commandLists.size(), commandLists.data());

		m_copyCommandQueue->SignalAndWait();
	}
	
	// Execute direct commands.
	{
		std::array<ID3D12CommandList* const, 1> directCommandLists = { directCommandList.Get() };
		m_directCommandQueue->commandQueue->ExecuteCommandLists((UINT)directCommandLists.size(), directCommandLists.data());

		m_directCommandQueue->SignalAndWait();
	}

	// Add to render objects.
	DrawArgs drawArgs = {
		.vertexCount = vertexCount,
		.startVertex = 0,
		.indexCount = indexCount,
		.startIndex = 0
	};

	renderObject.drawArgs.push_back(drawArgs);
	renderObject.topology = topology;

	return renderObject;
}

RenderObject DX12Renderer::CreateRenderObjectFromOBJ(const std::string& objPath, D3D12_PRIMITIVE_TOPOLOGY topology)
{
	tinyobj::ObjReaderConfig readerConfig = {};
	readerConfig.triangulate = true;
	tinyobj::ObjReader reader;

	ReadObjFile(objPath, reader, readerConfig);

	auto& attrib = reader.GetAttrib();
	auto& shapes = reader.GetShapes();
	auto& materials = reader.GetMaterials();

	std::vector<VertexIndex> indices;
	GetObjVertexIndices(indices, reader);

	std::vector<Vertex> vertices;
	vertices.resize(attrib.vertices.size() / 3);
	std::set<UINT> createdVertexIndices;

	for (size_t s = 0; s < shapes.size(); s++)
	{
		for (auto& indexInfo : shapes[s].mesh.indices)
		{
			auto vertexIndex = indexInfo.vertex_index;
			auto normalIndex = indexInfo.normal_index;

			if (createdVertexIndices.find(vertexIndex) == createdVertexIndices.end())
			{
				Vertex vertex = {};
				vertex.position = {
					attrib.vertices[3 * vertexIndex + 0],
					attrib.vertices[3 * vertexIndex + 1],
					attrib.vertices[3 * vertexIndex + 2]
				};

				if (normalIndex != -1)
				{
					vertex.normal = {
						attrib.normals[3 * normalIndex + 0],
						attrib.normals[3 * normalIndex + 1],
						attrib.normals[3 * normalIndex + 2]
					};
				}

				vertex.color = { 1.0f, 1.0f, 1.0f };

				vertices[vertexIndex] = vertex;
				createdVertexIndices.insert(vertexIndex);
			}
		}

	}

	return CreateRenderObject(&vertices, &indices, topology);
}

CommandQueueHandler::CommandQueueHandler(ComPtr<ID3D12Device5> device, D3D12_COMMAND_LIST_TYPE type)
	: m_fence(nullptr), m_eventHandle(CreateEvent(nullptr, false, false, nullptr)), m_fenceValue(0), m_type(type)
{
	D3D12_COMMAND_QUEUE_DESC queueDesc = {
		.Type = m_type,
		.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
		.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
		.NodeMask = 0
	};

	device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)) >> CHK_HR;
	NAME_D3D12_OBJECT_MEMBER(commandQueue, CommandQueueHandler);

	device->CreateCommandAllocator(m_type, IID_PPV_ARGS(&commandAllocator)) >> CHK_HR;
	NAME_D3D12_OBJECT_MEMBER(commandAllocator, CommandQueueHandler);

	// Create fence.
	device->CreateFence(m_fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)) >> CHK_HR;

	NAME_D3D12_OBJECT_MEMBER(m_fence, DX12Renderer);
}

CommandQueueHandler::CommandQueueHandler()
	: m_fence(nullptr), m_eventHandle(nullptr), m_fenceValue((UINT64)(-1)), m_type(D3D12_COMMAND_LIST_TYPE_NONE)
{
	// Do nothing but set defaults.
}

CommandQueueHandler::~CommandQueueHandler()
{
	if (m_eventHandle)
		CloseHandle(m_eventHandle);
}

ComPtr<ID3D12GraphicsCommandList4> CommandQueueHandler::CreateCommandList(ComPtr<ID3D12Device5> device, bool autoReset /*= true*/, D3D12_COMMAND_LIST_FLAGS flags /*= D3D12_COMMAND_LIST_FLAG_NONE*/)
{
	ComPtr<ID3D12GraphicsCommandList4> commandList;
	device->CreateCommandList1(
		0,
		m_type,
		flags,
		IID_PPV_ARGS(&commandList)
	) >> CHK_HR;

	// Reset command list automatically so its ready for usage.
	if (autoReset)
		ResetCommandList(commandList);

	NAME_D3D12_OBJECT_FUNC(commandList, CommandQueueHandler::CreateCommandList);

	return commandList;
}

ID3D12CommandQueue* CommandQueueHandler::Get() const
{
	return commandQueue.Get();
}

UINT CommandQueueHandler::GetCompletedFenceValue()
{
	return m_fence->GetCompletedValue();
}

void CommandQueueHandler::ResetAllocator()
{
	commandAllocator->Reset() >> CHK_HR;
}

void CommandQueueHandler::ResetCommandList(ComPtr<ID3D12GraphicsCommandList1> commandList)
{
	commandList->Reset(commandAllocator.Get(), nullptr) >> CHK_HR;
}

UINT CommandQueueHandler::Signal()
{
	commandQueue->Signal(m_fence.Get(), ++m_fenceValue) >> CHK_HR;

	return m_fenceValue;
}

void CommandQueueHandler::WaitForLatestSignal()
{
	// Wait for the latest signal to complete.
	WaitForFence(m_fenceValue);
}

void CommandQueueHandler::WaitForFence(UINT64 fenceValue)
{
	// If the value is already at the fence value or larger,
	// there is no need to set and wait for any event as it would pass immediately.
	if (GetCompletedFenceValue() < fenceValue)
	{
		m_fence->SetEventOnCompletion(m_fenceValue, m_eventHandle) >> CHK_HR;
		if (WaitForSingleObject(m_eventHandle, CommandQueueHandler::MaxWaitTimeMS) != WAIT_OBJECT_0)
		{
			throw std::runtime_error("ERROR: Fence wait timed out.");
		}
	}
}

void CommandQueueHandler::SignalAndWait()
{
	Signal();
	WaitForLatestSignal();
}

void CommandQueueHandler::ExecuteCommandLists(DX12Abstractions::CommandListVector& commandLists, UINT count /*= 0*/, const UINT offset /*= 0*/)
{
	if (count == 0)
	{
		count = (UINT)commandLists.size();
	}

	ID3D12CommandList* const* listStartAddress = GetCommandListPtr(commandLists, offset);
	assert(listStartAddress != nullptr); // TODO: Handle this error better.
	commandQueue->ExecuteCommandLists(count, listStartAddress);
}
