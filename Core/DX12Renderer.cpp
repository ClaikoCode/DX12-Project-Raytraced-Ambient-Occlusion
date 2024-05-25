#include "DX12Renderer.h"

#include <stdexcept>
#include <vector>

#include "GraphicsErrorHandling.h"
#include "DX12AbstractionUtils.h"
#include "AppDefines.h"
#include "tiny_obj_loader.h"

using namespace DX12Abstractions;
namespace dx = DirectX;

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

	UpdateCamera();
	UpdateGlobalFrameDataBuffer();
	UpdateInstanceConstantBuffers();
}

void DX12Renderer::Render()
{
	

	UINT currBackBufferIndex = m_swapChain->GetCurrentBackBufferIndex();

	// Store command lists for each render pass.
	// TODO: Make as CommandListVector for consistency.
	std::vector<ComPtr<ID3D12CommandList>> commandLists;

	// Before render pass setup.
	static ComPtr<ID3D12GraphicsCommandList1> mainThreadCommandListPre = nullptr;
	static ComPtr<ID3D12CommandAllocator> mainThreadCommandAllocatorPre = nullptr;
	if (mainThreadCommandListPre == nullptr)
	{
		m_device->CreateCommandList1(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			D3D12_COMMAND_LIST_FLAG_NONE,
			IID_PPV_ARGS(&mainThreadCommandListPre)
		) >> CHK_HR;

		m_device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(&mainThreadCommandAllocatorPre)
		) >> CHK_HR;
	}
	

	// After render pass setup.
	static ComPtr<ID3D12GraphicsCommandList1> mainThreadCommandListPost = nullptr;
	static ComPtr<ID3D12CommandAllocator> mainThreadCommandAllocatorPost = nullptr;
	if (mainThreadCommandListPost == nullptr)
	{
		m_device->CreateCommandList1(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			D3D12_COMMAND_LIST_FLAG_NONE,
			IID_PPV_ARGS(&mainThreadCommandListPost)
		) >> CHK_HR;

		m_device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(&mainThreadCommandAllocatorPost)
		) >> CHK_HR;
	}

	// Fetch the current back buffer that we want to render to.
	GPUResource& currentBackBuffer = m_backBuffers[currBackBufferIndex];

	// Get RTV handle for the current back buffer.
	const CD3DX12_CPU_DESCRIPTOR_HANDLE bbRTV (
		m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
		currBackBufferIndex,
		m_rtvDescriptorSize
	);

	const CD3DX12_CPU_DESCRIPTOR_HANDLE middleTextureRTV(
		m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
		RTVOffsets::RTVOffsetMiddleTexture,
		m_rtvDescriptorSize
	);

	// Get DSV handle.
	const CD3DX12_CPU_DESCRIPTOR_HANDLE dsv(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

	// Reset command allocators
	mainThreadCommandAllocatorPre->Reset();
	mainThreadCommandAllocatorPost->Reset();

	// Reset pre command list.
	mainThreadCommandListPre->Reset(mainThreadCommandAllocatorPre.Get(), nullptr);
	// Reset post command list.
	mainThreadCommandListPost->Reset(mainThreadCommandAllocatorPost.Get(), nullptr);
	
	std::vector<RenderPassType> renderPassOrder = { DeferredGBufferPass, DeferredLightingPass, RaytracedAOPass, AccumulationPass };

	// Pre render pass setup.
	{
		// Clear back buffer and prime for rendering.
		{
			currentBackBuffer.TransitionTo(D3D12_RESOURCE_STATE_RENDER_TARGET, mainThreadCommandListPre);

			float clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
			mainThreadCommandListPre->ClearRenderTargetView(bbRTV, clearColor, 0, nullptr);
		}

		// Clear middle texture.
		{
			m_middleTexture.TransitionTo(D3D12_RESOURCE_STATE_RENDER_TARGET, mainThreadCommandListPre);

			mainThreadCommandListPre->ClearRenderTargetView(middleTextureRTV, OptimizedClearColor, 0, nullptr);
		}

		// Setup gbuffers.
		if (HasRenderPass(renderPassOrder, RenderPassType::DeferredGBufferPass))
		{
			for (GPUResource& gBuffer : m_gBuffers)
			{
				gBuffer.TransitionTo(D3D12_RESOURCE_STATE_RENDER_TARGET, mainThreadCommandListPre);
			}

			ClearGBuffers(mainThreadCommandListPre);
		}
		

		// Clear depth buffer.
		mainThreadCommandListPre->ClearDepthStencilView(
			dsv,
			D3D12_CLEAR_FLAG_DEPTH,
			1.0f,
			0,
			0,
			nullptr
		);

		// Close pre command list.
		mainThreadCommandListPre->Close();

		// Add command list to list of command lists.
		commandLists.push_back(mainThreadCommandListPre);
	}

	//std::vector<RenderPassType> renderPassOrder = { IndexedPass, NonIndexedPass };
	

	// Initialize all render passes.
	for (auto& renderPass : renderPassOrder)
	{
		m_renderPasses[renderPass]->Init();
	}

	// Set start sync.
	for (UINT context = 0; context < NumContexts; context++)
	{
		m_syncHandler.SetStart(context);
	}

	// Common args for all passes.
	CommonRenderPassArgs commonArgs = {
		dsv,
		m_rasterRootSignature,
		m_viewport,
		m_scissorRect,
		m_cbvSrvUavHeap,
		m_cbvSrvUavDescriptorSize,
		m_globalFrameDataCB,
		m_activeCamera->GetViewProjectionMatrix()
	};

	CommonRaytracingRenderPassArgs commonRTArgs = {
		.cbvSrvUavHeap = m_cbvSrvUavHeap,
		.cbvSrvUavDescSize = m_cbvSrvUavDescriptorSize,

		.globalRootSig = m_RTGlobalRootSignature,

		.rayGenShaderTable = &m_rayGenShaderTable,
		.hitGroupShaderTable = &m_hitGroupShaderTable,
		.missShaderTable = &m_missShaderTable
	};

	// This is supposed to be ran by different threads.
	for (UINT context = 0; context < NumContexts; context++)
	{
		// Wait for start sync.
		m_syncHandler.WaitStart(context);

		for (RenderPassType renderPassType : renderPassOrder)
		{
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
			// If 
			if (renderPackages.size() > 0 || passObjectIDs.size() == 0)
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
						RTVOffsetGBuffers,
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
						auto commandList = renderPass.commandLists[context];

						for (GPUResource& gBuffer : m_gBuffers)
						{
							gBuffer.TransitionTo(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, commandList);
						}

						m_middleTexture.TransitionTo(D3D12_RESOURCE_STATE_RENDER_TARGET, commandList);

						// Put uav barrier before use.
						CD3DX12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_accumulationTexture.Get());
						commandList->ResourceBarrier(1, &uavBarrier);
					}

					renderPassArgs = DeferredLightingRenderPassArgs {
						.commonArgs = commonArgs,
						.RTV = middleTextureRTV
					};
				}
				else if (renderPassType == RaytracedAOPass)
				{
					if (context == 0)
					{
						auto commandList = renderPass.commandLists[context];

						for (GPUResource& gBuffer : m_gBuffers)
						{
							gBuffer.TransitionTo(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, commandList);
						}

						m_middleTexture.TransitionTo(D3D12_RESOURCE_STATE_UNORDERED_ACCESS, commandList);

						for (const RenderObjectID renderObjectID : passObjectIDs)
						{
							UpdateTopLevelAccelerationStructure(renderObjectID, commandList);
						}
					}

					renderPassArgs = RaytracedAORenderPassArgs {
						.commonRTArgs = commonRTArgs,
						.stateObject = m_RTPipelineState,
						.frameCount = m_frameCount,
						.screenWidth = m_width,
						.screenHeight = m_height,
					};
				}
				else if (renderPassType == AccumulationPass)
				{
					if (context == 0)
					{
						auto commandList = renderPass.commandLists[context];

						m_middleTexture.TransitionTo(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, commandList);
					}

					renderPassArgs = AccumulationRenderPassArgs {
						.commonArgs = commonArgs,
						.RTVTargetFrame = bbRTV
					};

					if (context == 0)
					{
						static const float frequency = 1.0f;
						m_accumulatedFrames = (1u * (UINT)(m_time * frequency));
					}
				}
				else
				{
					throw std::runtime_error("Unknown render pass type.");
				}


				// Render all render packages.
				renderPass.Render(renderPackages, context, &renderPassArgs);
			}

			// Signal that pass is done.
			m_syncHandler.SetPass(context, renderPassType);
			m_renderPasses[renderPassType]->Close(context);
		}

		// Signal end sync.
		m_syncHandler.SetEnd(context);
	}

	// Wait for all passes to finish.
	m_syncHandler.WaitEndAll();

	// Add all command lists to the main command list.
	for (RenderPassType renderPass : renderPassOrder)
	{
		for (UINT context = 0; context < NumContexts; context++)
		{
			commandLists.push_back(m_renderPasses[renderPass]->commandLists[context]);
		}
	}

	// Coppy middle texture to backbuffer.
	//m_middleTexture.TransitionTo(D3D12_RESOURCE_STATE_COPY_SOURCE, mainThreadCommandListPost);
	//currentBackBuffer.TransitionTo(D3D12_RESOURCE_STATE_COPY_DEST, mainThreadCommandListPost);
	//mainThreadCommandListPost->CopyResource(currentBackBuffer.Get(), m_middleTexture.Get());

	// Prepare backbuffer for present
	currentBackBuffer.TransitionTo(D3D12_RESOURCE_STATE_PRESENT, mainThreadCommandListPost);

	// Close post command list.
	mainThreadCommandListPost->Close() >> CHK_HR;

	// Add post command list to list of command lists.
	commandLists.push_back(mainThreadCommandListPost);

	// Execute all command lists.
	{
		//for (UINT i = 0; i < commandLists.size(); i++)
		//{
		//	ID3D12CommandList* const* commandListsRaw = commandLists[i].GetAddressOf();
		//	m_directCommandQueue->commandQueue->ExecuteCommandLists(1, commandListsRaw);
		//	m_directCommandQueue->SignalAndWait();
		//}

		ID3D12CommandList* const* commandListsRaw = commandLists[0].GetAddressOf();
		m_directCommandQueue->commandQueue->ExecuteCommandLists((UINT)commandLists.size(), commandListsRaw);
	}

	// Insert fence that signifies command list completion.
	m_directCommandQueue->Signal();

	// Present
	m_swapChain->Present(1, 0) >> CHK_HR;

	// Wait for the command queue to finish.
	m_directCommandQueue->Wait();

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
}


void DX12Renderer::InitPipeline()
{
	CreateDeviceAndSwapChain();
	CreateGBuffers();
	CreateMiddleTexture();
	CreateAccumulationTexture();
	CreateRTVHeap();
	CreateRTVs();
	CreateDepthBuffer();
	CreateDSVHeap();
	CreateDSV();

	// TODO: Check if this makes sense to be in "init pipeline".
	CreateConstantBuffers();
	CreateCBVSRVUAVHeap();
	CreateCBVs();
	CreateSRVs();
	CreateUAVs();
}

void DX12Renderer::CreateDeviceAndSwapChain()
{
	ComPtr<IDXGIFactory4> factory;
	{
		UINT dxgiFactoryFlags = 0;
//TODO: Add back that this only happens on debug mode.
//#if defined(_DEBUG) 
		// Enables debug layer.
		ComPtr<ID3D12Debug1> debugController;
		D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)) >> CHK_HR;
		debugController->EnableDebugLayer();
		debugController->SetEnableGPUBasedValidation(true);

		// Additional debug layer options.
		dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
//#endif

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
		//m_directCommandQueue = CommandQueueHandler(m_device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
		//m_copyCommandQueue = CommandQueueHandler(m_device.Get(), D3D12_COMMAND_LIST_TYPE_COPY);

		m_directCommandQueue = std::make_unique<CommandQueueHandler>(m_device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
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
			.BufferCount = BufferCount,
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

void DX12Renderer::CreateGBuffers()
{
	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_UNKNOWN,
		m_width,
		m_height
	);
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	// Diffuse gbuffer.
	{
		resourceDesc.Format = GBufferFormats[GBufferDiffuse];

		m_gBuffers[GBufferDiffuse] = CreateResource(
			m_device,
			resourceDesc, 
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_HEAP_TYPE_DEFAULT
		);

		NAME_D3D12_OBJECT_MEMBER_INDEXED(m_gBuffers, GBufferDiffuse, DX12Renderer);
	}
	
	// Surface normal gbuffer.
	{
		resourceDesc.Format = GBufferFormats[GBufferNormal];
		
		m_gBuffers[GBufferNormal] = CreateResource(
			m_device,
			resourceDesc,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_HEAP_TYPE_DEFAULT
		);

		NAME_D3D12_OBJECT_MEMBER_INDEXED(m_gBuffers, GBufferNormal, DX12Renderer);
	}

	// World position gbuffer.
	{
		resourceDesc.Format = GBufferFormats[GBufferWorldPos];

		m_gBuffers[GBufferWorldPos] = CreateResource(
			m_device,
			resourceDesc,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_HEAP_TYPE_DEFAULT
		);

		NAME_D3D12_OBJECT_MEMBER_INDEXED(m_gBuffers, GBufferWorldPos, DX12Renderer);
	}

	
}

CD3DX12_RESOURCE_DESC DX12Renderer::CreateBackbufferResourceDesc() const
{
	CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		BackBufferFormat,
		m_width,
		m_height
	);
	resourceDesc.MipLevels = 1; // Match back buffer.

	return resourceDesc;
}

void DX12Renderer::CreateMiddleTexture()
{
	CD3DX12_RESOURCE_DESC resourceDesc = CreateBackbufferResourceDesc();

	// Render target when writing gbuffer info to it and unordered access in ray tracing shader when writing and reading to it.
	resourceDesc.Flags = 
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | 
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	m_middleTexture = CreateResource(
		m_device,
		resourceDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_HEAP_TYPE_DEFAULT
	);
}

void DX12Renderer::CreateAccumulationTexture()
{
	CD3DX12_RESOURCE_DESC resourceDesc = CreateBackbufferResourceDesc();
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	m_accumulationTexture = CreateResource(
		m_device,
		resourceDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_HEAP_TYPE_DEFAULT
	);
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

void DX12Renderer::CreateRTVs()
{
	for (UINT i = 0; i < BufferCount; i++)
	{
		m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i])) >> CHK_HR;

		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
		rtvHandle.Offset(RTVOffsets::RTVOffsetBackBuffers + i, m_rtvDescriptorSize);

		m_device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, rtvHandle);

		NAME_D3D12_OBJECT_MEMBER_INDEXED(m_backBuffers, i, DX12Renderer);
	}

	for (UINT i = 0; i < GBufferCount; i++)
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
		rtvHandle.Offset(RTVOffsets::RTVOffsetGBuffers + i, m_rtvDescriptorSize);

		m_device->CreateRenderTargetView(m_gBuffers[i].Get(), nullptr, rtvHandle);
	}

	// Create middletexture rtv.
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
		rtvHandle.Offset(RTVOffsets::RTVOffsetMiddleTexture, m_rtvDescriptorSize);

		m_device->CreateRenderTargetView(m_middleTexture.Get(), nullptr, rtvHandle);
	}
}

void DX12Renderer::CreateDepthBuffer()
{
	const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

	const CD3DX12_RESOURCE_DESC depthBufferDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_D32_FLOAT,
		m_width,
		m_height,
		1, 0, 1, 0,
		D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
	);

	const D3D12_CLEAR_VALUE depthOptimizedClearValue = {
		.Format = DXGI_FORMAT_D32_FLOAT,
		.DepthStencil = { 1.0f, 0 }
	};

	m_device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&depthBufferDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthOptimizedClearValue,
		IID_PPV_ARGS(&m_depthBuffer)
	) >> CHK_HR;

	NAME_D3D12_OBJECT_MEMBER(m_depthBuffer, DX12Renderer);
}

void DX12Renderer::CreateDSVHeap()
{
	const D3D12_DESCRIPTOR_HEAP_DESC dsvDescriptorHeapDesc = {
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
			.NumDescriptors = 1,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
			.NodeMask = 0
	};

	m_device->CreateDescriptorHeap(&dsvDescriptorHeapDesc, IID_PPV_ARGS(&m_dsvHeap)) >> CHK_HR;


	NAME_D3D12_OBJECT_MEMBER(m_dsvHeap, DX12Renderer);
}

void DX12Renderer::CreateDSV()
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

	m_device->CreateDepthStencilView(m_depthBuffer.Get(), nullptr, dsvHandle);
}

void DX12Renderer::CreateConstantBuffers()
{
	// Create instances CBs.
	{
		constexpr UINT instanceElementSize = DX12Abstractions::CalculateConstantBufferByteSize(sizeof(InstanceConstants));
		const UINT instanceBufferSize = instanceElementSize * MaxRenderInstances;

		const CD3DX12_RESOURCE_DESC constantBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(instanceBufferSize);

		m_perInstanceCB = CreateUploadResource(m_device, constantBufferDesc);
		NAME_D3D12_OBJECT_MEMBER(m_perInstanceCB, DX12Renderer);

		// Map and initialize the constant buffer.
		{
			InstanceConstants* pConstantBufferData;
			m_perInstanceCB.resource->Map(0, nullptr, reinterpret_cast<void**>(&pConstantBufferData)) >> CHK_HR;

			dx::XMMATRIX modelMatrix = dx::XMMatrixIdentity();

			for (UINT i = 0; i < MaxRenderInstances; i++)
			{
				// No need to transpose the matrix since it's identity.
				dx::XMStoreFloat4x4(&pConstantBufferData[i].modelMatrix, modelMatrix);
			}

			m_perInstanceCB.resource->Unmap(0, nullptr);
		}
	}

	// Create global frame data CB.
	{
		constexpr UINT globalFrameDataSize = DX12Abstractions::CalculateConstantBufferByteSize(sizeof(GlobalFrameData));
		
		const CD3DX12_RESOURCE_DESC constantBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(globalFrameDataSize * 1); // Only one instance.

		m_globalFrameDataCB = CreateUploadResource(m_device, constantBufferDesc);
		NAME_D3D12_OBJECT_MEMBER(m_globalFrameDataCB, DX12Renderer);

		// Initialize some values.
		GlobalFrameData globalFrameData = {
			.frameCount = 0,
			.accumulatedFrames = 0,
			.time = 0.0f
		};
		MapDataToBuffer(m_globalFrameDataCB, &globalFrameData, sizeof(GlobalFrameData));
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

	NAME_D3D12_OBJECT_MEMBER(m_cbvSrvUavHeap, DX12Renderer);
}

void DX12Renderer::CreateCBVs()
{
	// Create all CBV descriptors for render instances.
	for (UINT i = 0; i < MaxRenderInstances; i++)
	{
		constexpr UINT instanceDataSize = DX12Abstractions::CalculateConstantBufferByteSize(sizeof(InstanceConstants));

		D3D12_GPU_VIRTUAL_ADDRESS instanceCBAddress = m_perInstanceCB.resource->GetGPUVirtualAddress();
		// Set the offset for the data location inside the upload buffer.
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {
			.BufferLocation = instanceCBAddress + i * instanceDataSize,
			.SizeInBytes = instanceDataSize
		};

		// Set the offset for the handle location inside the descriptor heap.
		// This was noticed to be needed inside the loop to function without getting a memory exception.
		// My theory is that there is no guarantee that the start of the memory wont change as the heap fills up dynamically, because of reallocation.
		// The start of the actual memory therefore needs to be fetched each loop to get the proper memory location.
		CD3DX12_CPU_DESCRIPTOR_HANDLE instanceCBVHandle(m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		instanceCBVHandle.Offset(CBVSRVUAVOffsets::CBVOffsetRenderInstance + i, m_cbvSrvUavDescriptorSize);

		m_device->CreateConstantBufferView(&cbvDesc, instanceCBVHandle);
	} 

	// For global data.
	{
		constexpr UINT globalFrameDataSize = DX12Abstractions::CalculateConstantBufferByteSize(sizeof(GlobalFrameData));

		D3D12_GPU_VIRTUAL_ADDRESS frameDataCBAddress = m_perInstanceCB.resource->GetGPUVirtualAddress();
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {
			.BufferLocation = frameDataCBAddress,
			.SizeInBytes = globalFrameDataSize
		};

		// Calculate offset of CBV.
		CD3DX12_CPU_DESCRIPTOR_HANDLE globalFrameDataCBVHandle(m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		globalFrameDataCBVHandle.Offset(CBVSRVUAVOffsets::CBVOffsetGlobalFrameData, m_cbvSrvUavDescriptorSize);

		m_device->CreateConstantBufferView(&cbvDesc, globalFrameDataCBVHandle);
	}
}


D3D12_SHADER_RESOURCE_VIEW_DESC DX12Renderer::CreateBackbufferSRVDesc() const
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

void DX12Renderer::CreateSRVs()
{
	// SRVs for gbuffers.
	for (UINT i = 0; i < CBVSRVUAVCounts::SRVCountGbuffers; i++)
	{
		UINT SRVIndex = i + CBVSRVUAVOffsets::SRVOffsetGBuffers;

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
		
		CD3DX12_CPU_DESCRIPTOR_HANDLE gbufferSRVHandle(m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		gbufferSRVHandle.Offset(SRVIndex, m_cbvSrvUavDescriptorSize);

		m_device->CreateShaderResourceView(m_gBuffers[i].Get(), &srvDesc, gbufferSRVHandle);
	}

	// SRV for middle texture
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = CreateBackbufferSRVDesc();

		CD3DX12_CPU_DESCRIPTOR_HANDLE middleTextureSRVHandle(m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		middleTextureSRVHandle.Offset(CBVSRVUAVOffsets::SRVOffsetMiddleTexture, m_cbvSrvUavDescriptorSize);

		m_device->CreateShaderResourceView(m_middleTexture.Get(), &srvDesc, middleTextureSRVHandle);
	}

	// SRV for accumulation texture
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = CreateBackbufferSRVDesc();

		CD3DX12_CPU_DESCRIPTOR_HANDLE accumilationTextureSRVHandle(m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		accumilationTextureSRVHandle.Offset(CBVSRVUAVOffsets::SRVOffsetAccumulationTexture, m_cbvSrvUavDescriptorSize);

		m_device->CreateShaderResourceView(m_accumulationTexture.Get(), &srvDesc, accumilationTextureSRVHandle);
	}
}

D3D12_UNORDERED_ACCESS_VIEW_DESC DX12Renderer::CreateBackbufferUAVDesc() const
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

void DX12Renderer::CreateUAVs()
{
	// UAV for middle texture.
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = CreateBackbufferUAVDesc();

		CD3DX12_CPU_DESCRIPTOR_HANDLE middleTextureUAVHandle(m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		middleTextureUAVHandle.Offset(CBVSRVUAVOffsets::UAVOffsetMiddleTexture, m_rtvDescriptorSize);

		m_device->CreateUnorderedAccessView(m_middleTexture.Get(), nullptr, &uavDesc, middleTextureUAVHandle);
	}


	// UAV for accumulation texture.
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = CreateBackbufferUAVDesc();

		CD3DX12_CPU_DESCRIPTOR_HANDLE accumulationTextureUAVHandle(m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		accumulationTextureUAVHandle.Offset(CBVSRVUAVOffsets::UAVOffsetAccumulationTexture, m_rtvDescriptorSize);

		m_device->CreateUnorderedAccessView(m_accumulationTexture.Get(), nullptr, &uavDesc, accumulationTextureUAVHandle);
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
			CBVSRVUAVCounts::SRVCountGbuffers, 
			RasterShaderRegisters::SRVRegisters::SRVDescriptorRange
		);

		// Descriptor range for middle texture SRV.
		CD3DX12_DESCRIPTOR_RANGE middleTextureSRVRange;
		middleTextureSRVRange.Init(
			D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
			CBVSRVUAVCounts::SRVCountMiddleTexture,
			RasterShaderRegisters::SRVRegisters::SRVDescriptorRange + CBVSRVUAVCounts::SRVCountGbuffers,
			0,
			CBVSRVUAVOffsets::SRVOffsetMiddleTexture - CBVSRVUAVOffsets::SRVOffsetGBuffers
		);

		// Descriptor range for accumulation UAV.
		CD3DX12_DESCRIPTOR_RANGE accumulationUAVRange;
		accumulationUAVRange.Init(
			D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
			CBVSRVUAVCounts::UAVCountAccumulationTexture,
			RasterShaderRegisters::UAVRegisters::UAVDescriptorRange,
			0,
			CBVSRVUAVOffsets::UAVOffsetAccumulationTexture - CBVSRVUAVOffsets::SRVOffsetGBuffers
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
	RegisterRenderPass(RenderPassType::DeferredGBufferPass);
	RegisterRenderPass(RenderPassType::DeferredLightingPass);
	RegisterRenderPass(RenderPassType::AccumulationPass);
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

		//renderInstance.CBIndex = renderInstanceCount++;
		//dx::XMStoreFloat4x4(&renderInstance.instanceData.modelMatrix, dx::XMMatrixRotationX(dx::XM_PIDIV2 * 0.34f) * dx::XMMatrixTranslation(0.0f, 0.0f, 0.0f));
		//renderInstances.push_back(renderInstance);

		int offset = 2;
		float scale = 3.f;
		for (int x = 0; x < 5; x++)
		{
			for (int y = 0; y < 5; y++)
			{
				
				float xPos = (x - offset) * scale;
				float yPos = (y - offset) * scale;
				renderInstance.CBIndex = renderInstanceCount++;
				dx::XMStoreFloat4x4(&renderInstance.instanceData.modelMatrix, dx::XMMatrixTranslation(xPos, yPos, 0.0f));
				renderInstances.push_back(renderInstance);
			}
		}

		for (int x = 0; x < 5; x++)
		{
			for (int y = 0; y < 5; y++)
			{
				float xPos = (x - offset) * scale;
				float yPos = (y - offset) * scale;
				renderInstance.CBIndex = renderInstanceCount++;
				dx::XMStoreFloat4x4(&renderInstance.instanceData.modelMatrix, dx::XMMatrixTranslation(xPos, yPos, -5.0f));
				renderInstances.push_back(renderInstance);
			}
		}
	}
}

void DX12Renderer::InitRaytracing()
{
	CreateAccelerationStructures();
	CreateRaytracingPipelineState();
	CreateShaderTables();
	CreateTopLevelASDescriptors();

	// Wait for all work to be done.
	m_directCommandQueue->SignalAndWait();
}

void DX12Renderer::CreateAccelerationStructures()
{
	m_directCommandQueue->ResetAllocator();
	auto commandList = m_directCommandQueue->CreateCommandList(m_device);

	CreateBottomLevelASs(commandList);
	CreateTopLevelASs(commandList);

	commandList->Close();

	DX12Abstractions::CommandListVector commandLists = { commandList };
	m_directCommandQueue->ExecuteCommandLists(commandLists);

	m_directCommandQueue->SignalAndWait();
}

void DX12Renderer::CreateBottomLevelASs(ComPtr<ID3D12GraphicsCommandList4> commandList)
{
	// Cube
	{
		RenderObjectID objectID = RenderObjectID::Cube;
		CreateBottomLevelAccelerationStructure(objectID, commandList);
	}

	// OBJ Model
	{
		RenderObjectID objectID = RenderObjectID::OBJModel1;
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

void DX12Renderer::CreateTopLevelASs(ComPtr<ID3D12GraphicsCommandList4> commandList)
{
	// Cube
	{
		RenderObjectID objectID = RenderObjectID::Cube;
		CreateTopLevelAccelerationStructure(objectID, commandList);
	}

	// OBJ Model
	{
		RenderObjectID objectID = RenderObjectID::OBJModel1;
		CreateTopLevelAccelerationStructure(objectID, commandList);
	}
}

void DX12Renderer::CreateTopLevelAccelerationStructure(RenderObjectID objectID, ComPtr<ID3D12GraphicsCommandList4> commandList)
{
	AccelerationStructureBuffers& bottomAccStruct = m_bottomAccStructByID[objectID];

	// First, get the size of the TLAS buffers and create them
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
	inputs.NumDescs = (UINT)m_renderInstancesByID[objectID].size();
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

	AccelerationStructureBuffers& topAccStruct = m_topAccStructByID[objectID];

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
	m_device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

	CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	topAccStruct.scratch = CreateResource(
		m_device,
		resourceDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_HEAP_TYPE_DEFAULT
	);

	resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	topAccStruct.result = CreateResource(
		m_device,
		resourceDesc,
		D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
		D3D12_HEAP_TYPE_DEFAULT
	);

	resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * inputs.NumDescs);
	topAccStruct.instanceDesc = CreateUploadResource(m_device, resourceDesc);

	UpdateTopLevelAccelerationStructure(objectID, commandList);
}

void DX12Renderer::CreateRaytracingPipelineState()
{
	constexpr UINT MaxSubObjects = 100u;
	std::array<D3D12_STATE_SUBOBJECT, 100u> soMemory{};
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

	// TODO: Make this a more correct way of registering passes.
	RegisterRenderPass(RaytracedAOPass);
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
			CBVSRVUAVCounts::SRVCountTLAS,
			RTShaderRegisters::SRVRegistersRayGen::SRVDescriptorTableTLASRegister
		);
		rootParameters[RTRayGenParameterIdx::RayGenSRVTableTLASIdx].InitAsDescriptorTable(1, &srvRangeTLAS, D3D12_SHADER_VISIBILITY_ALL);
		
		// Add root descriptor table for shader resources (gbuffers).
		srvRangeGbuffers.Init(
			D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
			CBVSRVUAVCounts::SRVCountGbuffers,
			RTShaderRegisters::SRVRegistersRayGen::SRVDescriptorTableGbuffersRegister
		);
		rootParameters[RTRayGenParameterIdx::RayGenSRVTableGbuffersIdx].InitAsDescriptorTable(1, &srvRangeGbuffers, D3D12_SHADER_VISIBILITY_ALL);

		// Add root descriptor for UAV that is going to be written to.
		uavRange.Init(
			D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 
			CBVSRVUAVCounts::SRVCountMiddleTexture, 
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

void DX12Renderer::CreateShaderTables()
{
	ComPtr<ID3D12StateObjectProperties> RTStateObjectProps = nullptr;
	m_RTPipelineState->QueryInterface(IID_PPV_ARGS(&RTStateObjectProps)) >> CHK_HR;

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
			CD3DX12_GPU_DESCRIPTOR_HANDLE uavHandle(m_cbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart());
			uavHandle.Offset(CBVSRVUAVOffsets::UAVOffsetMiddleTexture, m_cbvSrvUavDescriptorSize);
		
			tableData.UAVDescriptorTableMiddleTexture = uavHandle.ptr;
		}

		// Set TLAS SRV.
		{
			CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(m_cbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart());
			srvHandle.Offset(CBVSRVUAVOffsets::SRVOffsetTLAS, m_cbvSrvUavDescriptorSize);

			tableData.SRVDescriptorTableTopLevelAS = srvHandle.ptr;
		}
		
		// Set descriptor table for gbuffer start.
		{
			auto descHeapHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_cbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart());
			descHeapHandle.Offset(CBVSRVUAVOffsets::SRVOffsetGBuffers, m_cbvSrvUavDescriptorSize);

			tableData.SRVDescriptorTableGbuffers = descHeapHandle.ptr;
		}

		void* dest = tableData.ShaderIdentifier;
		void* src = RTStateObjectProps->GetShaderIdentifier(RayGenShaderName);
		memcpy(dest, src, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

		union MaxSizeStruct
		{
			RAY_GEN_SHADER_TABLE_DATA tabledata0;
		};

		m_rayGenShaderTable.strideInBytes = sizeof(MaxSizeStruct);
		m_rayGenShaderTable.sizeInBytes = m_rayGenShaderTable.strideInBytes * 1; // A single ray gen table for now.
		{
			CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(m_rayGenShaderTable.sizeInBytes);
			m_rayGenShaderTable.tableResource = CreateUploadResource(
				m_device,
				resourceDesc
			);
		}

		MapDataToBuffer(m_rayGenShaderTable.tableResource, &tableData, sizeof(tableData));
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

		m_missShaderTable.strideInBytes = sizeof(MaxSizeStruct);
		m_missShaderTable.sizeInBytes = m_missShaderTable.strideInBytes * 1; // A single miss table for now.
		{
			CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(m_missShaderTable.sizeInBytes);
			m_missShaderTable.tableResource = CreateUploadResource(
				m_device,
				resourceDesc
			);
		}

		MapDataToBuffer(m_missShaderTable.tableResource, &tableData, sizeof(tableData));
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

		m_hitGroupShaderTable.strideInBytes = sizeof(MaxSizeStruct);
		m_hitGroupShaderTable.sizeInBytes = m_hitGroupShaderTable.strideInBytes * 1; // A single hit group for now.
		{
			CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(m_hitGroupShaderTable.sizeInBytes);
			m_hitGroupShaderTable.tableResource = CreateUploadResource(
				m_device,
				resourceDesc
			);
		}

		MapDataToBuffer(m_hitGroupShaderTable.tableResource, &tableData, sizeof(tableData));
	}
}

void DX12Renderer::CreateTopLevelASDescriptors()
{
	CreateTopLevelASDescriptor(RenderObjectID::Cube);
}

void DX12Renderer::CreateTopLevelASDescriptor(RenderObjectID objectID)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.RaytracingAccelerationStructure.Location = m_topAccStructByID[objectID].result.resource->GetGPUVirtualAddress();

	CD3DX12_CPU_DESCRIPTOR_HANDLE tlasSRVHandle(m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
	tlasSRVHandle.Offset(CBVSRVUAVOffsets::SRVOffsetTLAS, m_cbvSrvUavDescriptorSize);

	// Use nullptr because the resource is already referenced in description of the view.
	m_device->CreateShaderResourceView(nullptr, &srvDesc, tlasSRVHandle);
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
	//float angle = m_time * dx::XM_2PI / 20.0f;
	float angle = 0.0f;
	
	dx::XMMATRIX rotationMatrix = dx::XMMatrixRotationNormal(dx::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), angle);
	dx::XMVECTOR newPos = dx::XMVector3Transform(startPos, rotationMatrix);

	m_activeCamera->SetPosAndLookAt(newPos, dx::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f));
	m_activeCamera->UpdateViewMatrix();
	m_activeCamera->UpdateViewProjectionMatrix();
}

void DX12Renderer::UpdateInstanceConstantBuffers()
{
	// Copy through byte offsets.
	uint8_t* instanceDataStart = nullptr;
	m_perInstanceCB.resource->Map(0, nullptr, reinterpret_cast<void**>(&instanceDataStart)) >> CHK_HR;
	constexpr UINT perInstanceSize = DX12Abstractions::CalculateConstantBufferByteSize(sizeof(InstanceConstants));

	for (auto& it : m_renderInstancesByID)
	{
		RenderObjectID renderObjectID = it.first;
		std::vector<RenderInstance>& renderInstances = it.second;

		for (RenderInstance& renderInstance : renderInstances)
		{
			uint8_t* instanceData = instanceDataStart + renderInstance.CBIndex * perInstanceSize;
			memcpy(instanceData, &renderInstance.instanceData, sizeof(InstanceConstants));
		}
	}

	m_perInstanceCB.resource->Unmap(0, nullptr);
}

void DX12Renderer::UpdateGlobalFrameDataBuffer()
{
	GlobalFrameData globalFrameData = {
		.frameCount = m_frameCount,
		.accumulatedFrames = m_accumulatedFrames,
		.time = m_time
	};

	MapDataToBuffer<GlobalFrameData>(m_globalFrameDataCB, &globalFrameData, sizeof(GlobalFrameData));
}

void DX12Renderer::UpdateTopLevelAccelerationStructure(RenderObjectID objectID, ComPtr<ID3D12GraphicsCommandList4> commandList)
{
	AccelerationStructureBuffers& topAccStruct = m_topAccStructByID[objectID];
	const D3D12_GPU_VIRTUAL_ADDRESS bottomLevelAddress = m_bottomAccStructByID[objectID].result.resource->GetGPUVirtualAddress();
	const UINT instanceCount = (UINT)m_renderInstancesByID[objectID].size();

	D3D12_RAYTRACING_INSTANCE_DESC* instanceDesc = nullptr;
	topAccStruct.instanceDesc.resource->Map(0, nullptr, reinterpret_cast<void**>(&instanceDesc)) >> CHK_HR;

	for (UINT i = 0; i < instanceCount; i++)
	{
		const RenderInstance& renderInstance = m_renderInstancesByID[objectID][i];
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

	// TODO: Make this input shared between the initial creation and now.
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
	inputs.NumDescs = instanceCount;
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	
	// Create the TLAS
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
	asDesc.Inputs = inputs;
	asDesc.Inputs.InstanceDescs = topAccStruct.instanceDesc.resource->GetGPUVirtualAddress();
	asDesc.DestAccelerationStructureData = topAccStruct.result.resource->GetGPUVirtualAddress();
	asDesc.ScratchAccelerationStructureData = topAccStruct.scratch.resource->GetGPUVirtualAddress();

	commandList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);

	// UAV barrier needed before using the acceleration structures in a raytracing operation
	CD3DX12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(topAccStruct.result.Get());
	commandList->ResourceBarrier(1, &uavBarrier);
}



// Macro for reducing code duplication in render pass registration.
// What this macro does is adds it to the render pass map and also registers it for the sync handler.
#define CaseRegisterRenderPass(renderpasstype, renderclass) \
case renderpasstype: \
	m_renderPasses[renderpasstype] = std::make_unique<renderclass>(m_device.Get(), m_rasterRootSignature); \
	m_syncHandler.AddUniquePassSync(renderpasstype); \
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
	for (UINT i = 0; i < GBufferCount; i++)
	{
		// Get RTV handle for GBuffer.
		const CD3DX12_CPU_DESCRIPTOR_HANDLE gBufferRTVHandle(
			m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
			RTVOffsets::RTVOffsetGBuffers + i,
			m_rtvDescriptorSize
		);

		commandList->ClearRenderTargetView(gBufferRTVHandle, OptimizedClearColor, 0, nullptr);
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
	if(m_eventHandle)
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

void CommandQueueHandler::ResetAllocator()
{
	commandAllocator->Reset() >> CHK_HR;
}

void CommandQueueHandler::ResetCommandList(ComPtr<ID3D12GraphicsCommandList1> commandList)
{
	commandList->Reset(commandAllocator.Get(), nullptr) >> CHK_HR;
}

void CommandQueueHandler::Signal()
{
	commandQueue->Signal(m_fence.Get(), ++m_fenceValue) >> CHK_HR;
}

void CommandQueueHandler::Wait()
{
	// If the latest completed value is already at the fence value or larger,
	// there is no need to set and wait for any event as it would pass immediately.
	if (m_fence->GetCompletedValue() < m_fenceValue)
	{
		m_fence->SetEventOnCompletion(m_fenceValue, m_eventHandle) >> CHK_HR;
		WaitForSingleObject(m_eventHandle, INFINITE);
	}
}

void CommandQueueHandler::SignalAndWait()
{
	Signal();
	Wait();
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
