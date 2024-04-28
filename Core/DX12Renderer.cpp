#include "DX12Renderer.h"

#include <stdexcept>
#include <vector>

#include "GraphicsErrorHandling.h"
#include "DX12AbstractionUtils.h"
#include "tiny_obj_loader.h"

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

void DX12Renderer::Render()
{
	UINT currBackBufferIndex = m_swapChain->GetCurrentBackBufferIndex();

	// Store command lists for each render pass.
	std::vector<ComPtr<ID3D12CommandList>> commandLists;

	static ComPtr<ID3D12GraphicsCommandList1> mainThreadCommandListPre;
	if (mainThreadCommandListPre == nullptr)
	{
		m_device->CreateCommandList1(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			D3D12_COMMAND_LIST_FLAG_NONE,
			IID_PPV_ARGS(&mainThreadCommandListPre)
		) >> CHK_HR;
	}

	static ComPtr<ID3D12GraphicsCommandList1> mainThreadCommandListPost;
	if (mainThreadCommandListPost == nullptr)
	{
		m_device->CreateCommandList1(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			D3D12_COMMAND_LIST_FLAG_NONE,
			IID_PPV_ARGS(&mainThreadCommandListPost)
		) >> CHK_HR;
	}

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

	// Reset command list and command allocator.
	m_commandAllocator->Reset() >> CHK_HR;
	mainThreadCommandListPre->Reset(m_commandAllocator.Get(), nullptr) >> CHK_HR;
	
	
	// Pre render pass setup.
	{
		// Clear render target.
		{
			backBuffer.TransitionTo(D3D12_RESOURCE_STATE_RENDER_TARGET, mainThreadCommandListPre);

			float clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
			mainThreadCommandListPre->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
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

		mainThreadCommandListPre->Close();

		// Add command list to list of command lists.
		commandLists.push_back(mainThreadCommandListPre);
	}

	std::vector<RenderPassType> renderPassOrder = { IndexedPass, NonIndexedPass };

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

		for (RenderPassType renderPass : renderPassOrder)
		{
			if (renderPass == NonIndexedPass)
			{
				for(auto& renderObject : m_renderObjectsByPipelineState[NonIndexedPass])
				{
					NonIndexedRenderPass& nonIndexedRenderPass = static_cast<NonIndexedRenderPass&>(*m_renderPasses[renderPass]);
					NonIndexedRenderPass::NonIndexedRenderPassArgs args;

					// Add state args.
					args.renderObject = renderObject;
					args.renderTargetView = rtv;
					args.depthStencilView = dsv;
					args.rootSignature = m_rootSignature;
					args.viewport = m_viewport;
					args.scissorRect = m_scissorRect;

					// Add view projection matrix.
					args.viewProjectionMatrix = m_activeCamera->GetViewProjectionMatrix();

					nonIndexedRenderPass.Render(context, m_device, args);

					// Signal that pass is done.
					m_syncHandler.SetPass(context, renderPass);
				}
			}
			else if (renderPass == IndexedPass)
			{
				for(auto& renderObject : m_renderObjectsByPipelineState[IndexedPass])
				{
					IndexedRenderPass& indexedRenderPass = static_cast<IndexedRenderPass&>(*m_renderPasses[renderPass]);
					IndexedRenderPass::IndexedRenderPassArgs args;

					// Add state args.
					args.renderObject = renderObject;
					args.renderTargetView = rtv;
					args.depthStencilView = dsv;
					args.rootSignature = m_rootSignature;
					args.viewport = m_viewport;
					args.scissorRect = m_scissorRect;

					// Add view projection matrix.
					args.viewProjectionMatrix = m_activeCamera->GetViewProjectionMatrix();

					indexedRenderPass.Render(context, m_device, args);

					// Signal that pass is done.
					m_syncHandler.SetPass(context, renderPass);
				}
			}
			else
			{
				throw std::runtime_error("Unknown render pass type.");
			}

			m_renderPasses[renderPass]->Close(context);
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
	mainThreadCommandListPost->Reset(m_commandAllocator.Get(), nullptr) >> CHK_HR;

	// Prepare for present
	backBuffer.TransitionTo(D3D12_RESOURCE_STATE_PRESENT, mainThreadCommandListPost);

	// Close post command list.
	mainThreadCommandListPost->Close() >> CHK_HR;

	// Add post command list to list of command lists.
	commandLists.push_back(mainThreadCommandListPost);

	// Execute all command lists.
	{
		ID3D12CommandList* const* commandListsRaw = commandLists[0].GetAddressOf();

		m_commandQueue->ExecuteCommandLists((UINT)commandLists.size(), commandListsRaw);
	}

	// Insert fence that signifies command list completion.
	m_commandQueue->Signal(m_fence.Get(), ++m_fenceValue) >> CHK_HR;

	// Present
	m_swapChain->Present(1, 0) >> CHK_HR;

	// Wait for signal
	//CHECK_HR(fence->SetEventOnCompletion(fenceValue, fenceEvent));
	//if (::WaitForSingleObject(fenceEvent, INFINITE) == WAIT_FAILED)
	//{
	//	CHECK_HR(GetLastError());
	//}

	// Alternative signal wait without requiring event handle.
	// According to the docs for SetEventOnCompletion(), this will not return until the fence value has been reached.
	m_fence->SetEventOnCompletion(m_fenceValue, nullptr) >> CHK_HR;
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
	m_rtvDescriptorSize(0),
	m_fenceValue(0)
{
	s_instance = this;

	InitPipeline();
	InitAssets();
}


void DX12Renderer::InitPipeline()
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

	NAME_D3D12_OBJECT(m_device);

	// Create command queue.
	{
		const D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {
			.Type = D3D12_COMMAND_LIST_TYPE_DIRECT, // Direct queue is a 3D queue, which has highest level of feature support.
			.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
			.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
			.NodeMask = 0
		};

		m_device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&m_commandQueue)) >> CHK_HR;

		NAME_D3D12_OBJECT(m_commandQueue);
	}

	// Create command allocator.
	{
		m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)) >> CHK_HR;

		NAME_D3D12_OBJECT(m_commandAllocator);
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
			m_commandQueue.Get(), // Implicit synchronization with the command queue.
			m_windowHandle,
			&swapChainDesc,
			nullptr,
			nullptr,
			&swapChainTemp
		) >> CHK_HR;

		swapChainTemp.As(&m_swapChain) >> CHK_HR; // Upgrade from Version 1 to Version 4.
	}

	// Create RTV descriptor heap.
	{
		const D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptorHeapDesc = {
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
			.NumDescriptors = BufferCount,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
			.NodeMask = 0
		};

		m_device->CreateDescriptorHeap(&rtvDescriptorHeapDesc, IID_PPV_ARGS(&m_rtvHeap)) >> CHK_HR;
		m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		NAME_D3D12_OBJECT(m_rtvHeap);
	}

	// Create render target views.
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

		for (UINT i = 0; i < BufferCount; i++)
		{
			m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])) >> CHK_HR;
			m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
			rtvHandle.Offset(1, m_rtvDescriptorSize);

			NAME_D3D12_OBJECT_INDEXED(m_renderTargets, i);
		}
	}

	// Create depth buffer.
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
	
		NAME_D3D12_OBJECT(m_depthBuffer);
	}

	// Create DSV descriptor heap.
	{
		const D3D12_DESCRIPTOR_HEAP_DESC dsvDescriptorHeapDesc = {
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
			.NumDescriptors = 1,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
			.NodeMask = 0
		};

		m_device->CreateDescriptorHeap(&dsvDescriptorHeapDesc, IID_PPV_ARGS(&m_dsvHeap)) >> CHK_HR;

		NAME_D3D12_OBJECT(m_dsvHeap);
	}

	// Create DSV.
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

		m_device->CreateDepthStencilView(m_depthBuffer.Get(), nullptr, dsvHandle);
	}
}

void DX12Renderer::InitAssets()
{
	// Create root signature.
	{
		std::array<CD3DX12_ROOT_PARAMETER, 1> rootParameters = {};
		// Add a matrix to the root signature where each element is stored as a constant.
		rootParameters[0].InitAsConstants(sizeof(dx::XMMATRIX) / 4, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

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

			NAME_D3D12_OBJECT(m_rootSignature);
		}
	}


	// Create pipeline state object.
	{
		struct PipelineStateStream
		{
			CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
			CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
			CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimtiveTopology;
			CD3DX12_PIPELINE_STATE_STREAM_VS VS;
			CD3DX12_PIPELINE_STATE_STREAM_PS PS;
			CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
			CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
		} pipelineStateStream;

		const D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};

		ComPtr<ID3DBlob> vsBlob;
		D3DReadFileToBlob(L"../VertexShader.cso", &vsBlob) >> CHK_HR;

		ComPtr<ID3DBlob> psBlob;
		D3DReadFileToBlob(L"../PixelShader.cso", &psBlob) >> CHK_HR;


		pipelineStateStream.RootSignature = m_rootSignature.Get();
		pipelineStateStream.InputLayout = { inputLayout, (UINT)std::size(inputLayout) };
		pipelineStateStream.PrimtiveTopology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vsBlob.Get());
		pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(psBlob.Get());
		pipelineStateStream.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		pipelineStateStream.RTVFormats = {
			.RTFormats = { DXGI_FORMAT_R8G8B8A8_UNORM },
			.NumRenderTargets = 1
		};

		const D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = {
			.SizeInBytes = sizeof(PipelineStateStream),
			.pPipelineStateSubobjectStream = &pipelineStateStream
		};


		ComPtr<ID3D12PipelineState> pipelineState;
		m_device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&pipelineState)) >> CHK_HR;

		NAME_D3D12_OBJECT(pipelineState);

		m_renderPasses[NonIndexedPass] = std::make_unique<NonIndexedRenderPass>(m_device.Get(), pipelineState);
		m_syncHandler.AddUniquePassSync(NonIndexedPass);

		m_renderPasses[IndexedPass] = std::make_unique<IndexedRenderPass>(m_device.Get(), pipelineState);
		m_syncHandler.AddUniquePassSync(IndexedPass);
	}




	// Temp command list for setting up the assets.
	// Using command list '1' closes the command list immediately and 
	// doesn't require a command allocator as input, which usually is replaced either way.
	ComPtr<ID3D12GraphicsCommandList1> commandList;
	m_device->CreateCommandList1(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		D3D12_COMMAND_LIST_FLAG_NONE,
		IID_PPV_ARGS(&commandList)
	) >> CHK_HR;

	// Create fence.
	m_device->CreateFence(m_fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)) >> CHK_HR;

	NAME_D3D12_OBJECT(m_fence);

	{
		std::vector<Vertex> triangleData{ {
			{ { 0.0f, 0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, // top
			{ { 0.43f, -0.25f, 0.0f }, { 0.0f, 0.0f, 1.0f } }, // right
			{ { -0.43f, -0.25f, 0.0f }, { 0.0f, 1.0f, 0.0f } } // left
		} };

		RenderObject nonIndexedObject = CreateRenderObject(&triangleData, nullptr);
		m_renderObjectsByPipelineState[NonIndexedPass].push_back(nonIndexedObject);
	}

	{
		std::vector<Vertex> cubeData{ {
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

		RenderObject cube =	CreateRenderObject(&cubeData, &cubeIndices);
		m_renderObjectsByPipelineState[IndexedPass].push_back(cube);
	}

	
	{
		std::string modelPath = std::string(AssetsPath) + "pumpkin.obj";
		tinyobj::ObjReaderConfig readerConfig = {};
		tinyobj::ObjReader reader;

		ReadObjFile(modelPath, reader, readerConfig);

		auto& attrib = reader.GetAttrib();
		auto& shapes = reader.GetShapes();
		auto& materials = reader.GetMaterials();

		std::vector<uint32_t> indices;
		GetObjVertexIndices(indices, reader);

		std::vector<Vertex> vertices;
		for (UINT vertexStart = 0; vertexStart < attrib.vertices.size(); vertexStart += 3)
		{
			Vertex vertex = {};
			vertex.position = {
				attrib.vertices[vertexStart + 0],
				attrib.vertices[vertexStart + 1],
				attrib.vertices[vertexStart + 2]
			};

			vertex.color = { 1.0f, 0.0f, 0.0f };

			vertices.push_back(vertex);
		}

		RenderObject pumpkin = CreateRenderObject(&vertices, &indices);
		m_renderObjectsByPipelineState[IndexedPass].push_back(pumpkin);
	}

	// Initialize camera.
	{
		const float aspectRatio = static_cast<float>(m_width) / static_cast<float>(m_height);
		const float nearZ = 0.01f;
		const float farZ = 1000.0f;
		const float fov = XMConvertToRadians(90.0f);

		m_cameras.push_back(Camera(fov, aspectRatio, nearZ, farZ));
		m_activeCamera = &m_cameras[0];

		m_activeCamera->SetPosAndDir({ 0.0f, 0.0f, -30.0f }, { 0.0f, 0.0f, 1.0f });
	}

}

RenderObject DX12Renderer::CreateRenderObject(const std::vector<Vertex>* vertices, const std::vector<uint32_t>* indices)
{
	RenderObject renderObject;

	ComPtr<ID3D12GraphicsCommandList1> commandList;
	m_device->CreateCommandList1(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		D3D12_COMMAND_LIST_FLAG_NONE,
		IID_PPV_ARGS(&commandList)
	) >> CHK_HR;

	// Reset command list and allocator.
	m_commandAllocator->Reset() >> CHK_HR;
	commandList->Reset(m_commandAllocator.Get(), nullptr) >> CHK_HR;

	UINT vertexCount = 0;
	UINT vertexBufferSize = 0;
	GPUResource vertexUploadBuffer;
	if(vertices != nullptr)
	{
		vertexCount = (UINT)vertices->size();
		vertexBufferSize = sizeof(vertices->at(0)) * vertexCount;

		UploadResource<Vertex>(
			m_device,
			commandList,
			renderObject.vertexBuffer,
			vertexUploadBuffer,
			vertices->data(),
			vertexBufferSize
		);

		// Create vertex buffer view.
		auto& vbView = renderObject.vertexBufferView;
		{
			vbView.BufferLocation = renderObject.vertexBuffer.resource->GetGPUVirtualAddress();
			vbView.StrideInBytes = sizeof(vertices->at(0));
			vbView.SizeInBytes = vertexBufferSize;
		}

		renderObject.vertexBuffer.TransitionTo(D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, commandList);
	}

	UINT indexCount = 0;
	UINT indexBufferSize = 0;
	GPUResource indexUploadBuffer;
	if(indices != nullptr)
	{
		indexCount = (UINT)indices->size();
		indexBufferSize = sizeof(indices->at(0)) * indexCount;

		UploadResource<uint32_t>(
			m_device,
			commandList,
			renderObject.indexBuffer,
			indexUploadBuffer,
			indices->data(),
			indexBufferSize
		);

		// Create index buffer view.
		auto& ibView = renderObject.indexBufferView;
		{
			ibView.BufferLocation = renderObject.indexBuffer.resource->GetGPUVirtualAddress();
			ibView.Format = DXGI_FORMAT_R32_UINT;
			ibView.SizeInBytes = indexBufferSize;
		}

		renderObject.indexBuffer.TransitionTo(D3D12_RESOURCE_STATE_INDEX_BUFFER, commandList);
	}

	// Close when done.
	commandList->Close() >> CHK_HR;

	std::array<ID3D12CommandList* const, 1> commandLists = { commandList.Get() };
	m_commandQueue->ExecuteCommandLists((UINT)commandLists.size(), commandLists.data());

	m_commandQueue->Signal(m_fence.Get(), ++m_fenceValue) >> CHK_HR;

	// Wait until the command queue is done.
	m_fence->SetEventOnCompletion(m_fenceValue, nullptr) >> CHK_HR;
	

	// Add to render objects.
	DrawArgs drawArgs = {
		.vertexCount = vertexCount,
		.startVertex = 0,
		.indexCount = indexCount,
		.startIndex = 0
	};

	renderObject.drawArgs.push_back(drawArgs);

	return renderObject;
}

