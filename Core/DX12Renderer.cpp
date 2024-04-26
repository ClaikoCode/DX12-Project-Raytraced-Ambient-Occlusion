#include "DX12Renderer.h"

#include <stdexcept>
#include <vector>

#include "GraphicsErrorHandling.h"
#include "DX12AbstractionUtils.h"

using namespace DX12Abstractions;
namespace dx = DirectX;

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

		mainThreadCommandListPre->Close();

		// Add command list to list of command lists.
		commandLists.push_back(mainThreadCommandListPre);
	}

	// Initialize all render passes.
	for (auto& renderPass : m_renderPasses)
	{
		renderPass.second->Init();
	}

	// Set start sync.
	for (UINT context = 0; context < NumContexts; context++)
	{
		m_syncHandler.SetStart(context);
	}

	// This is supposed to be ran by different threads.
	for (UINT context = 0; context < NumContexts; context++)
	{
		// Wait for start sync.
		m_syncHandler.WaitStart(context);

		RenderPassType currentPass;

		// Triangle pass.
		{
			currentPass = RenderPassType::TrianglePass;

			TriangleRenderPass& triangleRenderPass = static_cast<TriangleRenderPass&>(*m_renderPasses[currentPass]);

			TriangleRenderPass::TriangleRenderPassArgs args;
			
			// Add state args.
			args.vertexBufferView = m_vertexBufferView;
			args.renderTargetView = rtv;
			args.rootSignature = m_rootSignature;
			args.viewport = m_viewport;
			args.scissorRect = m_scissorRect;
			
			// Add draw args.
			args.drawArgs.push_back({ vertexCount, 0, 0 });

			triangleRenderPass.Render(context, m_device, args);

			// Signal that pass is done.
			m_syncHandler.SetPass(context, currentPass);
		}
		
		{
			// Any future passes can be added here.
		}

		// Signal end sync.
		m_syncHandler.SetEnd(context);
	}

	// Wait for all passes to finish.
	m_syncHandler.WaitEndAll();

	// Add all command lists to the main command list.
	for (auto& renderPass : m_renderPasses)
	{
		for (UINT context = 0; context < NumContexts; context++)
		{
			commandLists.push_back(renderPass.second->commandLists[context]);
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
		ComPtr<ID3D12Debug> debugController;
		D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)) >> CHK_HR;
		debugController->EnableDebugLayer();

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
}

void DX12Renderer::InitAssets()
{
	// Create root signature.
	{
		CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
		// Empty root sig for now.
		rootSignatureDesc.Init(
			0,
			nullptr,
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

		m_renderPasses[TrianglePass] = std::make_unique<TriangleRenderPass>(m_device.Get(), pipelineState);
		m_syncHandler.AddUniquePassSync(TrianglePass);
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

	// Create vertex buffers.
	struct Vertex
	{
		dx::XMFLOAT3 position;
		dx::XMFLOAT3 color;
	};
	{
		std::array<Vertex, 3> triangleData{ {
			{ { 0.0f, 0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, // top
			{ { 0.43f, -0.25f, 0.0f }, { 0.0f, 0.0f, 1.0f } }, // right
			{ { -0.43f, -0.25f, 0.0f }, { 0.0f, 1.0f, 0.0f } } // left
		} };

		vertexCount = (UINT)triangleData.size();
		m_vertexBufferSize = sizeof(triangleData);

		GPUResource vertexUploadBuffer;
		{
			CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(m_vertexBufferSize);

			m_vertexBuffer = CreateDefaultResource(m_device, bufferDesc);
			vertexUploadBuffer = CreateUploadResource(m_device, bufferDesc);

			NAME_D3D12_OBJECT(m_vertexBuffer);
		}

		// Copy the data onto GPU memory.
		{
			Vertex* mappedVertexData = nullptr;
			vertexUploadBuffer.resource->Map(0, nullptr, reinterpret_cast<void**>(&mappedVertexData)) >> CHK_HR;
			std::ranges::copy(triangleData, mappedVertexData);
			vertexUploadBuffer.resource->Unmap(0, nullptr);
		}

		// Reset command list and allocator.
		m_commandAllocator->Reset() >> CHK_HR;
		commandList->Reset(m_commandAllocator.Get(), nullptr) >> CHK_HR;

		// Copy data from upload buffer into vertex buffer.
		commandList->CopyResource(m_vertexBuffer.Get(), vertexUploadBuffer.Get());

		// Close when done.
		commandList->Close() >> CHK_HR;

		std::array<ID3D12CommandList* const, 1> commandLists = { commandList.Get() };
		m_commandQueue->ExecuteCommandLists((UINT)commandLists.size(), commandLists.data());

		m_commandQueue->Signal(m_fence.Get(), ++m_fenceValue) >> CHK_HR;
		// Wait until the command queue is done.
		m_fence->SetEventOnCompletion(m_fenceValue, nullptr) >> CHK_HR;

		// Create vertex buffer view.
		{
			m_vertexBufferView.BufferLocation = m_vertexBuffer.resource->GetGPUVirtualAddress();
			m_vertexBufferView.StrideInBytes = sizeof(Vertex);
			m_vertexBufferView.SizeInBytes = m_vertexBufferSize;
		}
	}

}

