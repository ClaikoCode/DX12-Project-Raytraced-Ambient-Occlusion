#include "DX12Renderer.h"

#include <stdexcept>
#include <vector>

#include "GraphicsErrorHandling.h"
#include "DX12AbstractionUtils.h"
#include "AppDefines.h"
#include "tiny_obj_loader.h"

// TODO: Move this when more cohesive use is found.
template<> DXGI_FORMAT GetDXGIFormat<float>() { return DXGI_FORMAT_R32_FLOAT; }
template<> DXGI_FORMAT GetDXGIFormat<uint32_t>() { return DXGI_FORMAT_R32_UINT; }
template<> DXGI_FORMAT GetDXGIFormat<uint16_t>() { return DXGI_FORMAT_R16_UINT; }

using namespace DX12Abstractions;
namespace dx = DirectX;

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
	UpdateInstanceConstantBuffers();
}

void DX12Renderer::Render()
{
	static float t = 0;
	t += 1 / 144.0f;

	UINT currBackBufferIndex = m_swapChain->GetCurrentBackBufferIndex();

	// Store command lists for each render pass.
	std::vector<ComPtr<ID3D12CommandList>> commandLists;

	// Before render pass setup.
	static ComPtr<ID3D12GraphicsCommandList1> mainThreadCommandListPre = m_directCommandQueue.CreateCommandList(m_device);

	// After render pass setup.
	static ComPtr<ID3D12GraphicsCommandList1> mainThreadCommandListPost = m_directCommandQueue.CreateCommandList(m_device);

	// Fetch the current back buffer that we want to render to.
	auto& backBuffer = m_renderTargets[currBackBufferIndex];

	// Get RTV handle for the current back buffer.
	const CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(
		m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
		currBackBufferIndex,
		m_rtvDescriptorSize
	);

	// Get DSV handle.
	const CD3DX12_CPU_DESCRIPTOR_HANDLE dsv(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

	// Reset command allocator.
	m_directCommandQueue.ResetAllocator();
	
	// Pre render pass setup.
	{
		// Reset pre command list.
		m_directCommandQueue.ResetCommandList(mainThreadCommandListPre);

		// Clear back buffer and prime for rendering.
		{
			backBuffer.TransitionTo(D3D12_RESOURCE_STATE_RENDER_TARGET, mainThreadCommandListPre);

			float clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
			mainThreadCommandListPre->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
		}

		// Clear g buffers and prime for rendering to.
		{
			ClearGBuffers(mainThreadCommandListPre);

			//for (GPUResource& gBuffer : m_gBuffers)
			//{
			//	gBuffer.TransitionTo(D3D12_RESOURCE_STATE_RENDER_TARGET, mainThreadCommandListPre);
			//}
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

	//std::vector<RenderPassType> renderPassOrder = { IndexedPass, NonIndexedPass, GBufferPass };
	std::vector<RenderPassType> renderPassOrder = { DeferredGBufferPass };

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

	// Update camera before rendering.
	m_activeCamera->UpdateViewMatrix();
	m_activeCamera->UpdateViewProjectionMatrix();

	// This is supposed to be ran by different threads.
	for (UINT context = 0; context < NumContexts; context++)
	{
		// Wait for start sync.
		m_syncHandler.WaitStart(context);

		for (RenderPassType renderPassType : renderPassOrder)
		{
			DX12RenderPass& renderPass = *m_renderPasses[renderPassType];

			// Common args for all passes.
			CommonRenderPassArgs commonArgs = {
				dsv,
				m_rootSignature,
				m_viewport,
				m_scissorRect,
				m_cbvSrvUavHeap,
				m_cbvSrvUavDescriptorSize,
				t,
				m_activeCamera->GetViewProjectionMatrix()
			};

			const std::vector<RenderObjectID>& passObjectIDs = m_renderObjectIDsByRenderPassType[renderPassType];

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
			if (renderPackages.size() > 0)
			{
				RenderPassArgsVariant renderPassArgs;

				if (renderPassType == NonIndexedPass)
				{
					NonIndexedRenderPassArgs args;

					// Add state args.
					args.commonArgs = commonArgs;
					args.RTV = rtv;

					renderPassArgs = args;
				}
				else if (renderPassType == IndexedPass)
				{
					IndexedRenderPassArgs args;

					// Add state args.
					args.commonArgs = commonArgs;
					args.RTV = rtv;

					renderPassArgs = args;
				}
				else if (renderPassType == DeferredGBufferPass)
				{
					DeferredGBufferRenderPassArgs args;

					// Get RTV handle for the first GBuffer.
					const CD3DX12_CPU_DESCRIPTOR_HANDLE firstGBufferRTVHandle(
						m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
						RTVOffsetGBuffers,
						m_rtvDescriptorSize
					);

					// Add state args.
					args.commonArgs = commonArgs;
					args.firstGBufferRTVHandle = firstGBufferRTVHandle;

					renderPassArgs = std::move(args);
				}
				else if(renderPassType == DeferredLightingPass)
				{
					
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

	// Reset post command list with allocator used on command list recently closed.
	m_directCommandQueue.ResetCommandList(mainThreadCommandListPost);

	// Prepare for present
	backBuffer.TransitionTo(D3D12_RESOURCE_STATE_PRESENT, mainThreadCommandListPost);

	//for (GPUResource& gBuffer : m_gBuffers)
	//{
	//	gBuffer.TransitionTo(D3D12_RESOURCE_STATE_RENDER_TARGET, mainThreadCommandListPost);
	//}

	// Close post command list.
	mainThreadCommandListPost->Close() >> CHK_HR;

	// Add post command list to list of command lists.
	commandLists.push_back(mainThreadCommandListPost);

	// Execute all command lists.
	{
		ID3D12CommandList* const* commandListsRaw = commandLists[0].GetAddressOf();

		m_directCommandQueue.commandQueue->ExecuteCommandLists((UINT)commandLists.size(), commandListsRaw);
	}

	// Insert fence that signifies command list completion.
	m_directCommandQueue.Signal();

	// Present
	m_swapChain->Present(1, 0) >> CHK_HR;

	// Wait for the command queue to finish.
	m_directCommandQueue.Wait(nullptr);

}

DX12Renderer::~DX12Renderer()
{
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
}


void DX12Renderer::InitPipeline()
{
	CreateDeviceAndSwapChain();
	CreateGBuffers();
	CreateRTVHeap();
	CreateRTVs();
	CreateDepthBuffer();
	CreateDSVHeap();
	CreateDSV();

	// TODO: Check if this makes sense to be in "init pipeline".
	CreateConstantBuffers();
	CreateCBVSRVUAVHeap();
	CreateCBV();
	CreateSRV();
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
		m_directCommandQueue = CommandQueueHandler(m_device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
		m_copyCommandQueue = CommandQueueHandler(m_device.Get(), D3D12_COMMAND_LIST_TYPE_COPY);
	}

	// Create swap chain.
	{
		const DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {
			.Width = m_width,
			.Height = m_height,
			.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
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
			m_directCommandQueue.Get(), // Implicit synchronization with the command queue.
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
	DXGI_FORMAT dxgiFormat = DXGI_FORMAT_UNKNOWN;
	CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		dxgiFormat,
		m_width,
		m_height
	);
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	// TODO: This is ignored for now. Check if it is important.
	D3D12_CLEAR_VALUE optimizedClearValue = {
		.Format = dxgiFormat,
		.Color = { 0.0f, 1.0f, 0.0f, 1.0f }
	};

	// Diffuse gbuffer.
	{
		dxgiFormat = GBufferFormats[GBufferDiffuse];
		resourceDesc.Format = dxgiFormat;
		optimizedClearValue.Format = dxgiFormat;

		m_gBuffers[GBufferDiffuse] = CreateResource(
			m_device,
			resourceDesc, 
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_HEAP_TYPE_DEFAULT
		);
	}
	
	// Surface normal gbuffer.
	{
		dxgiFormat = GBufferFormats[GBufferNormal];
		resourceDesc.Format = dxgiFormat;
		optimizedClearValue.Format = dxgiFormat;
		
		m_gBuffers[GBufferNormal] = CreateResource(
			m_device,
			resourceDesc,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_HEAP_TYPE_DEFAULT
		);
	}

	// World position gbuffer.
	{
		dxgiFormat = GBufferFormats[GBufferWorldPos];
		resourceDesc.Format = dxgiFormat;
		optimizedClearValue.Format = dxgiFormat;

		m_gBuffers[GBufferWorldPos] = CreateResource(
			m_device,
			resourceDesc,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_HEAP_TYPE_DEFAULT
		);
	}
}

void DX12Renderer::CreateRTVHeap()
{
	constexpr UINT totalRTVs = BufferCount + GBufferCount;
	const D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptorHeapDesc = {
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
			.NumDescriptors = totalRTVs,
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
		m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])) >> CHK_HR;

		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
		rtvHandle.Offset(i, m_rtvDescriptorSize);

		m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);

		NAME_D3D12_OBJECT_MEMBER_INDEXED(m_renderTargets, i, DX12Renderer);
	}

	for (UINT i = 0; i < GBufferCount; i++)
	{
		UINT rtvHandleIndex = i + RTVOffsetGBuffers;

		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
		rtvHandle.Offset(rtvHandleIndex, m_rtvDescriptorSize);

		m_device->CreateRenderTargetView(m_gBuffers[i].Get(), nullptr, rtvHandle);

		NAME_D3D12_OBJECT_MEMBER_INDEXED(m_gBuffers, i, DX12Renderer);
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

}

void DX12Renderer::CreateCBVSRVUAVHeap()
{
	const D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDescCBVSRVUAV =
	{
		.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		.NumDescriptors = NumCBVSRVUAVDescriptors,
		.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
		.NodeMask = 0
	};

	m_device->CreateDescriptorHeap(&descriptorHeapDescCBVSRVUAV, IID_PPV_ARGS(&m_cbvSrvUavHeap)) >> CHK_HR;
	m_cbvSrvUavDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	NAME_D3D12_OBJECT_MEMBER(m_cbvSrvUavHeap, DX12Renderer);
}

void DX12Renderer::CreateCBV()
{
	constexpr UINT instanceDataSize = DX12Abstractions::CalculateConstantBufferByteSize(sizeof(InstanceConstants));

	// Create all CBV descriptors for render instances.
	for (UINT i = CBVSRVUAVOffsets::CBVOffsetRenderInstance; i < MaxRenderInstances; i++)
	{
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
		instanceCBVHandle.Offset(i, m_cbvSrvUavDescriptorSize);

		m_device->CreateConstantBufferView(&cbvDesc, instanceCBVHandle);
	} 
}

void DX12Renderer::CreateSRV()
{
	// SRVs for gbuffers.
	for (UINT i = 0; i < GBufferCount; i++)
	{
		UINT SRVIndex = i + CBVSRVUAVOffsets::SRVOffsetGBuffers;

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = GBufferFormats[i];
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D = {
			.MostDetailedMip = 0,
			.MipLevels = -1u,
			.PlaneSlice = 0,
			.ResourceMinLODClamp = 0.0f
		};
		
		CD3DX12_CPU_DESCRIPTOR_HANDLE gbufferSRVHandle(m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
		gbufferSRVHandle.Offset(SRVIndex, m_cbvSrvUavDescriptorSize);

		m_device->CreateShaderResourceView(m_gBuffers[i].Get(), &srvDesc, gbufferSRVHandle);
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
	std::array<CD3DX12_ROOT_PARAMETER, 3> rootParameters = {};
	// Root parameter setup.
	{
		// Add a matrix to the root signature where each element is stored as a constant.
		rootParameters[0].InitAsConstants(
			sizeof(dx::XMMATRIX) / 4,
			RootSigRegisters::CBVRegisters::CBMatrixConstants,
			0,
			D3D12_SHADER_VISIBILITY_VERTEX
		);

		// Add descriptor table for instance specific constants.
		CD3DX12_DESCRIPTOR_RANGE cbvTable;
		cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, RootSigRegisters::CBVRegisters::CBDescriptorTable);
		rootParameters[1].InitAsDescriptorTable(1, &cbvTable, D3D12_SHADER_VISIBILITY_ALL);

		// Add descriptor table for shader resources.
		CD3DX12_DESCRIPTOR_RANGE srvTable;
		srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, GBufferCount, RootSigRegisters::SRVRegisters::SRDescriptorTable);
		rootParameters[2].InitAsDescriptorTable(1, &srvTable, D3D12_SHADER_VISIBILITY_PIXEL);
	}

	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init(
		(UINT)rootParameters.size(),
		rootParameters.data(),
		0,
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	);

	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;
	{
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

		// Create the root sig.
		m_device->CreateRootSignature(
			0,
			signature->GetBufferPointer(),
			signature->GetBufferSize(),
			IID_PPV_ARGS(&m_rootSignature)
		) >> CHK_HR;

		NAME_D3D12_OBJECT_MEMBER(m_rootSignature, DX12Renderer);
	}
}

void DX12Renderer::CreatePSOs()
{
	struct PipelineStateStream
	{
		CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
		CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
		CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimtiveTopology;
		CD3DX12_PIPELINE_STATE_STREAM_VS VS;
		CD3DX12_PIPELINE_STATE_STREAM_PS PS;
		CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL DepthStencil;
		CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
		CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
	} pipelineStateStream;

	const D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = {
			.SizeInBytes = sizeof(PipelineStateStream),
			.pPipelineStateSubobjectStream = &pipelineStateStream
	};

	const D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	// Simple rendering pass.
	{
		ComPtr<ID3DBlob> vsBlob;
		D3DReadFileToBlob(L"../VertexShader.cso", &vsBlob) >> CHK_HR;

		ComPtr<ID3DBlob> psBlob;
		D3DReadFileToBlob(L"../PixelShader.cso", &psBlob) >> CHK_HR;

		pipelineStateStream.RootSignature = m_rootSignature.Get();
		pipelineStateStream.InputLayout = { inputLayout, (UINT)std::size(inputLayout) };
		pipelineStateStream.PrimtiveTopology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vsBlob.Get());
		pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(psBlob.Get());
		// TODO: REMOVE LATER! THIS IS ONLY FOR TEMPORARY TESTING WITH GBUFFERS.
		{
			CD3DX12_DEPTH_STENCIL_DESC DepthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(CD3DX12_DEFAULT());
			DepthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
			pipelineStateStream.DepthStencil = DepthStencilDesc;
		}
		pipelineStateStream.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		pipelineStateStream.RTVFormats = {
			.RTFormats = { DXGI_FORMAT_R8G8B8A8_UNORM },
			.NumRenderTargets = 1
		};

		ComPtr<ID3D12PipelineState> defaultPipelineState;
		m_device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&defaultPipelineState)) >> CHK_HR;

		NAME_D3D12_OBJECT(defaultPipelineState);

		RegisterRenderPass(NonIndexedPass, defaultPipelineState);
		RegisterRenderPass(IndexedPass, defaultPipelineState);
	}
	

	// Deferred render gbuffer pass settings.
	{
		ComPtr<ID3DBlob> vsBlob;
		D3DReadFileToBlob(L"../VertexShader.cso", &vsBlob) >> CHK_HR;

		ComPtr<ID3DBlob> psBlob;
		D3DReadFileToBlob(L"../DeferredPixelShader.cso", &psBlob) >> CHK_HR;

		D3D12_RT_FORMAT_ARRAY formatArr;
		memset(&formatArr, 0, sizeof(formatArr)); // Initialize memory with 0.

		// Set the format of each gbuffer.
		for (UINT i = 0; i < GBufferCount; i++)
		{
			formatArr.RTFormats[i] = GBufferFormats[i];
			formatArr.NumRenderTargets++;
		}
		
		// Set the correct array of RTV formats expected.
		pipelineStateStream.RTVFormats = formatArr;

		pipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vsBlob.Get());
		pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(psBlob.Get());

		ComPtr<ID3D12PipelineState> deferredGBufferPipelineState;
		m_device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&deferredGBufferPipelineState)) >> CHK_HR;

		NAME_D3D12_OBJECT(deferredGBufferPipelineState);

		RegisterRenderPass(DeferredGBufferPass, deferredGBufferPipelineState);
	}

	// Deferred render lighting pass settings.
	{
		ComPtr<ID3DBlob> vsBlob;
		D3DReadFileToBlob(L"../FSQVS.cso", &vsBlob) >> CHK_HR;

		ComPtr<ID3DBlob> psBlob;
		D3DReadFileToBlob(L"../DeferredLightingPS.cso", &psBlob) >> CHK_HR;

		const D3D12_INPUT_ELEMENT_DESC inputLayoutSpecial[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		struct PipelineStateStreamSpecial
		{
			CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
			CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
			CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimtiveTopology;
			CD3DX12_PIPELINE_STATE_STREAM_VS VS;
			CD3DX12_PIPELINE_STATE_STREAM_PS PS;
			CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
		} pipelineStateStreamSpecial;

		pipelineStateStreamSpecial.RootSignature = m_rootSignature.Get();
		pipelineStateStreamSpecial.InputLayout = { inputLayoutSpecial, (UINT)std::size(inputLayoutSpecial) };
		pipelineStateStreamSpecial.PrimtiveTopology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vsBlob.Get());
		pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(psBlob.Get());
		pipelineStateStream.RTVFormats = { { DXGI_FORMAT_R8G8B8A8_UNORM }, 1 };

		ComPtr<ID3D12PipelineState> deferredLightingPipelineState;
		m_device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&deferredLightingPipelineState)) >> CHK_HR;

		NAME_D3D12_OBJECT_FUNC(deferredLightingPipelineState, CreatePSOs);

		RegisterRenderPass(DeferredLightingPass, deferredLightingPipelineState);
	}

}


void DX12Renderer::CreateRenderObjects()
{
	// Register what render objects are going to be allowed to render on specific pipelines.
	{
		m_renderObjectIDsByRenderPassType[NonIndexedPass].push_back(RenderObjectID::Triangle);

		m_renderObjectIDsByRenderPassType[IndexedPass].push_back(RenderObjectID::Cube);
		m_renderObjectIDsByRenderPassType[IndexedPass].push_back(RenderObjectID::OBJModel1);

		//m_renderObjectIDsByRenderPassType[GBufferPass].push_back(RenderObjectID::Cube);
		m_renderObjectIDsByRenderPassType[DeferredGBufferPass].push_back(RenderObjectID::OBJModel1);
	}
	
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
		std::vector<Vertex> cubeData = { {
			{ { -1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f, 0.0f } }, // 0
			{ { -1.0f,  1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } }, // 1
			{ {  1.0f,  1.0f, -1.0f }, { 1.0f, 1.0f, 0.0f } }, // 2
			{ {  1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f } }, // 3
			{ { -1.0f, -1.0f,  1.0f }, { 0.0f, 0.0f, 1.0f } }, // 4
			{ { -1.0f,  1.0f,  1.0f }, { 0.0f, 1.0f, 1.0f } }, // 5
			{ {  1.0f,  1.0f,  1.0f }, { 1.0f, 1.0f, 1.0f } }, // 6
			{ {  1.0f, -1.0f,  1.0f }, { 1.0f, 0.0f, 1.0f } }  // 7
		} };

		std::vector<uint32_t> cubeIndices = {
				0, 1, 2, 0, 2, 3, // front face
				4, 6, 5, 4, 7, 6, // back face
				4, 5, 1, 4, 1, 0, // left face
				3, 2, 6, 3, 6, 7, // right face
				1, 5, 6, 1, 6, 2, // top face
				4, 0, 3, 4, 3, 7  // bottom face
		};

		m_renderObjectsByID[RenderObjectID::Cube] = CreateRenderObject(&cubeData, &cubeIndices, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
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
		std::vector<RenderInstance>& renderInstances = m_renderInstancesByID[RenderObjectID::Cube];

		RenderInstance renderInstance = {};

		renderInstance.CBIndex = renderInstanceCount++;
		dx::XMStoreFloat4x4(&renderInstance.instanceData.modelMatrix, dx::XMMatrixTranslation(3.0f, 3.0f, 0.0f));
		renderInstances.push_back(renderInstance);

		renderInstance.CBIndex = renderInstanceCount++;
		dx::XMStoreFloat4x4(&renderInstance.instanceData.modelMatrix, dx::XMMatrixTranslation(-3.0f, 3.0f, 0.0f));
		renderInstances.push_back(renderInstance);

		renderInstance.CBIndex = renderInstanceCount++;
		dx::XMStoreFloat4x4(&renderInstance.instanceData.modelMatrix, dx::XMMatrixTranslation(-6.0f, 3.0f, 0.0f));
		renderInstances.push_back(renderInstance);

		renderInstance.CBIndex = renderInstanceCount++;
		dx::XMStoreFloat4x4(&renderInstance.instanceData.modelMatrix, dx::XMMatrixRotationX(dx::XM_PIDIV2 * 0.34f) * dx::XMMatrixTranslation(6.0f, 3.0f, 0.0f));
		renderInstances.push_back(renderInstance);

		renderInstance.CBIndex = renderInstanceCount++;
		dx::XMStoreFloat4x4(&renderInstance.instanceData.modelMatrix, dx::XMMatrixTranslation(0.0f, 3.0f, 0.0f));
		renderInstances.push_back(renderInstance);
	}

	// OBJ models.
	{
		std::vector<RenderInstance>& renderInstances = m_renderInstancesByID[RenderObjectID::OBJModel1];

		RenderInstance renderInstance = {};

		renderInstance.CBIndex = renderInstanceCount++;
		dx::XMStoreFloat4x4(&renderInstance.instanceData.modelMatrix, dx::XMMatrixRotationX(dx::XM_PIDIV2 * 0.34f) * dx::XMMatrixTranslation(0.0f, 0.0f, 0.0f));
		renderInstances.push_back(renderInstance);
	}
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

// Macro for reducing code duplication in render pass registration.
#define CaseRegisterRenderPass(renderpasstype, renderclass) \
case renderpasstype: \
	m_renderPasses[renderpasstype] = std::make_unique<renderclass>(m_device.Get(), pipelineState); \
	m_syncHandler.AddUniquePassSync(renderpasstype); \
	break

void DX12Renderer::RegisterRenderPass(const RenderPassType renderPassType, ComPtr<ID3D12PipelineState> pipelineState)
{
	switch (renderPassType)
	{
		CaseRegisterRenderPass(DeferredGBufferPass, DeferredGBufferRenderPass);

		CaseRegisterRenderPass(NonIndexedPass, NonIndexedRenderPass);

		CaseRegisterRenderPass(IndexedPass, IndexedRenderPass);

		CaseRegisterRenderPass(DeferredLightingPass, DeferredLightingRenderPass);

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
			RTVOffsetGBuffers + i,
			m_rtvDescriptorSize
		);

		commandList->ClearRenderTargetView(gBufferRTVHandle, OptimizedClearColor, 0, nullptr);
	}
}

RenderObject DX12Renderer::CreateRenderObject(const std::vector<Vertex>* vertices, const std::vector<uint32_t>* indices, D3D12_PRIMITIVE_TOPOLOGY topology)
{
	RenderObject renderObject;

	// Temp command list for setting up render objects.
	// Using command list '1' closes the command list immediately and 
	// doesn't require a command allocator as input, which usually is replaced either way.
	ComPtr<ID3D12GraphicsCommandList1> copyCommandList = m_copyCommandQueue.CreateCommandList(m_device);
	m_copyCommandQueue.ResetAllocator();
	m_copyCommandQueue.ResetCommandList(copyCommandList);

	ComPtr<ID3D12GraphicsCommandList1> directCommandList = m_directCommandQueue.CreateCommandList(m_device);
	m_directCommandQueue.ResetAllocator();
	m_directCommandQueue.ResetCommandList(directCommandList);

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

		UploadResource<uint32_t>(
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
			ibView.Format = GetDXGIFormat<uint32_t>();
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
		m_copyCommandQueue.commandQueue->ExecuteCommandLists((UINT)commandLists.size(), commandLists.data());

		m_copyCommandQueue.SignalAndWait(nullptr);
	}
	
	// Execute direct commands.
	{
		std::array<ID3D12CommandList* const, 1> directCommandLists = { directCommandList.Get() };
		m_directCommandQueue.commandQueue->ExecuteCommandLists((UINT)directCommandLists.size(), directCommandLists.data());

		m_directCommandQueue.SignalAndWait(nullptr);
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

	std::vector<uint32_t> indices;
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

	return CreateRenderObject(&vertices, &indices, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

CommandQueueHandler::CommandQueueHandler(ComPtr<ID3D12Device5> device, D3D12_COMMAND_LIST_TYPE type)
	: m_type(type), m_fenceValue(0)
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

ComPtr<ID3D12GraphicsCommandList1> CommandQueueHandler::CreateCommandList(ComPtr<ID3D12Device5> device, D3D12_COMMAND_LIST_FLAGS flags /*= D3D12_COMMAND_LIST_FLAG_NONE*/)
{
	ComPtr<ID3D12GraphicsCommandList1> commandList;
	device->CreateCommandList1(
		0, 
		m_type, 
		flags, 
		IID_PPV_ARGS(&commandList)
	) >> CHK_HR;

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

void CommandQueueHandler::Wait(HANDLE event)
{
	m_fence->SetEventOnCompletion(m_fenceValue, event) >> CHK_HR;
}

void CommandQueueHandler::SignalAndWait(HANDLE event)
{
	Signal();
	Wait(event);
}
