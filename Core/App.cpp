#include <stdexcept>
#include <array>
#include <ranges>

#include "DirectXIncludes.h"
#include "App.h"
#include "GraphicsErrorHandling.h"

using Microsoft::WRL::ComPtr;

ErrorHandling::HrPasserTag hrPasserTag;
#define CHK_HR hrPasserTag

// Abstraction of a general GPU resource.
struct GPUResource 
{
	typedef Microsoft::WRL::Details::ComPtrRef<ComPtr<ID3D12Resource>> ResourceComPtrRef;

	GPUResource()
		: GPUResource(D3D12_RESOURCE_STATE_COMMON) {}

	GPUResource(D3D12_RESOURCE_STATES initState)
		: GPUResource(nullptr, initState) {}

	GPUResource(ComPtr<ID3D12Resource> _resource, D3D12_RESOURCE_STATES initState)
		: resource(_resource), currentState(initState) {}

	// Allows the resource to be cast to its ComPtr implicitly.
	operator ComPtr<ID3D12Resource>() const { return resource; }

	// This enables the GPU resource to be used in usual D3D patterns of casting to void** for address instantiation. 
	ResourceComPtrRef operator&() { return &resource; }

	void TransitionTo(D3D12_RESOURCE_STATES newState, ComPtr<ID3D12GraphicsCommandList> commandList)
	{
		const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			resource.Get(),
			currentState,
			newState
		);

		commandList->ResourceBarrier(1, &barrier);

		currentState = newState;
	}

	ID3D12Resource* Get() const
	{
		return resource.Get();
	}
	
	ComPtr<ID3D12Resource> resource;
	D3D12_RESOURCE_STATES currentState;
};

void EnableD3D12DebugLayer()
{
	ComPtr<ID3D12Debug> debugController;
	D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)) >> CHK_HR;
	debugController->EnableDebugLayer();
}

GPUResource CreateResource(ComPtr<ID3D12Device4> device, CD3DX12_RESOURCE_DESC resourceDesc, D3D12_RESOURCE_STATES resourceState, D3D12_HEAP_TYPE heapType)
{
	GPUResource resource(resourceState);

	const CD3DX12_HEAP_PROPERTIES heapProps(heapType);
	device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		resourceState,
		nullptr,
		IID_PPV_ARGS(&resource)
	) >> CHK_HR;

	return resource;
}

GPUResource CreateUploadResource(ComPtr<ID3D12Device4> device, CD3DX12_RESOURCE_DESC resourceDesc)
{
	return CreateResource(device, resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_HEAP_TYPE_UPLOAD);
}


GPUResource CreateDefaultResource(ComPtr<ID3D12Device4> device, CD3DX12_RESOURCE_DESC resourceDesc)
{
	return CreateResource(device, resourceDesc, D3D12_RESOURCE_STATE_COMMON, D3D12_HEAP_TYPE_DEFAULT);
}

bool RunApp(Core::Window& window)
{
	const UINT width = window.Width();
	const UINT height = window.Height();
	const UINT bufferCount = 2;

	EnableD3D12DebugLayer();

	// DXGI factory (with debugging).
	ComPtr<IDXGIFactory4> factory;
	CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory)) >> CHK_HR;
	
	// Device
	ComPtr<ID3D12Device4> device;
	D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)) >> CHK_HR;

	// Command queue
	ComPtr<ID3D12CommandQueue> commandQueue;
	{
		const D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {
			.Type = D3D12_COMMAND_LIST_TYPE_DIRECT, // Direct queue is a 3D queue, which has highest level of feature support.
			.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
			.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
			.NodeMask = 0
		};

		device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue)) >> CHK_HR;
	}
	

	// Swap chain
	ComPtr<IDXGISwapChain4> swapChain;
	{
		const DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {
			.Width = width,
			.Height = height,
			.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
			.Stereo = FALSE,
			.SampleDesc = { 
				.Count = 1, 
				.Quality = 0
			},
			.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
			.BufferCount = bufferCount,
			.Scaling = DXGI_SCALING_STRETCH,
			.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
			.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
			.Flags = 0
		};

		ComPtr<IDXGISwapChain1> swapChainTemp;
		factory->CreateSwapChainForHwnd(
			commandQueue.Get(), // Implicit synchronization with the command queue.
			window.Handle(),
			&swapChainDesc,
			nullptr,
			nullptr,
			&swapChainTemp
		) >> CHK_HR;

		swapChainTemp.As(&swapChain) >> CHK_HR; // Upgrade from Version 1 to Version 4.
	}

	// RTV descriptor heap
	ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap;
	{
		const D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptorHeapDesc = {
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
			.NumDescriptors = bufferCount,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
			.NodeMask = 0
		};
		device->CreateDescriptorHeap(&rtvDescriptorHeapDesc, IID_PPV_ARGS(&rtvDescriptorHeap)) >> CHK_HR;
	}
	// RTV descriptor size needed for properly indexing inside of the heap.
	const UINT rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// RTV Descriptors and buffer references
	std::array<GPUResource, bufferCount> backBuffers;
	{
		// Use CD3DX12 structure for easier usage
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

		for (int i = 0; i < bufferCount; i++)
		{
			swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffers[i])) >> CHK_HR;
			device->CreateRenderTargetView(backBuffers[i].Get(), nullptr, rtvHandle);
			
			rtvHandle.Offset(rtvDescriptorSize);
		}
	}

	// Command allocator
	const D3D12_COMMAND_LIST_TYPE commandListType = D3D12_COMMAND_LIST_TYPE_DIRECT;
	ComPtr<ID3D12CommandAllocator> commandAllocator;
	device->CreateCommandAllocator(commandListType, IID_PPV_ARGS(&commandAllocator)) >> CHK_HR;
	
	// Command list
	// Using command list '1' closes the command list immediately and 
	// doesn't require a command allocator as input, which usually is replaced either way.
	ComPtr<ID3D12GraphicsCommandList1> commandList; 
	device->CreateCommandList1(
		0, 
		commandListType,
		D3D12_COMMAND_LIST_FLAG_NONE,
		IID_PPV_ARGS(&commandList)
	) >> CHK_HR;


	// Fence
	ComPtr<ID3D12Fence> fence;
	UINT64 fenceValue = 0;
	device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)) >> CHK_HR;

	struct Vertex
	{
		std::array<float, 3> position;
		std::array<float, 3> color;
	};

	GPUResource vertexBuffer;
	UINT vertexCount;
	UINT bufferSize;
	{
		std::array<Vertex, 3> triangleData{{
			{ { 0.0f, 0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, // top
			{ { 0.43f, -0.25f, 0.0f }, { 0.0f, 0.0f, 1.0f } }, // right
			{ { -0.43f, -0.25f, 0.0f }, { 0.0f, 1.0f, 0.0f } } // left
		}};

		vertexCount = (UINT)triangleData.size();
		bufferSize = sizeof(triangleData);

		GPUResource vertexUploadBuffer;
		{
			CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

			vertexBuffer = CreateDefaultResource(device, bufferDesc);
			vertexUploadBuffer = CreateUploadResource(device, bufferDesc);
		}
		

		// Copy the data onto GPU memory.
		{
			Vertex* mappedVertexData = nullptr;
			vertexUploadBuffer.resource->Map(0, nullptr, reinterpret_cast<void**>(&mappedVertexData)) >> CHK_HR;
			std::ranges::copy(triangleData, mappedVertexData);
			vertexUploadBuffer.resource->Unmap(0, nullptr);
		}

		// Reset command list and allocator.
		commandAllocator->Reset() >> CHK_HR;
		commandList->Reset(commandAllocator.Get(), nullptr) >> CHK_HR;

		// Copy data from upload buffer into vertex buffer.
		commandList->CopyResource(vertexBuffer.Get(), vertexUploadBuffer.Get());

		// Close when done.
		commandList->Close() >> CHK_HR;

		std::array<ID3D12CommandList* const, 1> commandLists = { commandList.Get() };
		commandQueue->ExecuteCommandLists((UINT)commandLists.size(), commandLists.data());

		commandQueue->Signal(fence.Get(), ++fenceValue) >> CHK_HR;
		fence->SetEventOnCompletion(fenceValue, nullptr) >> CHK_HR;
	}

	// Create a vertex buffer view.
	const D3D12_VERTEX_BUFFER_VIEW vertexBufferView{
		.BufferLocation = vertexBuffer.resource->GetGPUVirtualAddress(),
		.SizeInBytes = bufferSize,
		.StrideInBytes = sizeof(Vertex)
	};

	// Define root signature.
	ComPtr<ID3D12RootSignature> rootSig;
	{
		CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc;
		rootSigDesc.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		// Serialize root sig.
		ComPtr<ID3DBlob> signatureBlob;
		ComPtr<ID3DBlob> errorBlob;
		{
			const HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
			if (FAILED(hr))
			{
				// If there is an error blob, first print out the problem reported from the blob.
				if (errorBlob)
				{
					const char* errorBuffer = static_cast<const char*>(errorBlob->GetBufferPointer());
					std::string errorString = std::string("Serialize ERROR: ") + std::string(errorBuffer);
					OutputDebugStringA(errorString.c_str());
				}

				// Properly handle the HR value.
				hr >> CHK_HR;
			}

			// Create the root sig.
			device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSig)) >> CHK_HR;
		}
	}

	// Create pipeline state object.
	ComPtr<ID3D12PipelineState> pipelineState;
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

		pipelineStateStream.RootSignature = rootSig.Get();
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
		device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&pipelineState)) >> CHK_HR;
	}

	// Define scissor rect
	const CD3DX12_RECT scissorRect(0, 0, width, height);

	// Define viewport
	const CD3DX12_VIEWPORT viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));

	// Not needed as current solution does not require fence event handles.
	//HANDLE fenceEvent = CreateEventW(nullptr, 0, 0, nullptr);
	//if (!fenceEvent)
	//{
	//	GetLastError() >> CHK_HR;
	//	throw std::runtime_error("Failed to create fence event with CreateEventW().");
	//}

	while (!window.Closed())
	{
		window.ProcessMessages();

		UINT currBackBufferIndex = swapChain->GetCurrentBackBufferIndex();
		
		// Fetch the current back buffer that we want to render to.
		auto& backBuffer = backBuffers[currBackBufferIndex];

		// Reset command list and command allocator.
		commandAllocator->Reset() >> CHK_HR;
		commandList->Reset(commandAllocator.Get(), nullptr) >> CHK_HR;

		// Get RTV handle for the current back buffer.
		const CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(
			rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			currBackBufferIndex,
			rtvDescriptorSize
		);

		// Clear render target.
		{
			backBuffer.TransitionTo(D3D12_RESOURCE_STATE_RENDER_TARGET, commandList);

			float clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
			commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
		}

		// Set pipeline state.
		commandList->SetGraphicsRootSignature(rootSig.Get());
		commandList->SetPipelineState(pipelineState.Get());
		
		// Configure IA.
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->IASetVertexBuffers(0, 1, &vertexBufferView);

		// Configure RS.
		commandList->RSSetViewports(1, &viewport);
		commandList->RSSetScissorRects(1, &scissorRect);

		// Bind render target.
		commandList->OMSetRenderTargets(1, &rtv, TRUE, nullptr);

		// Draw triangle.
		commandList->DrawInstanced(vertexCount, 1, 0, 0);

		// Prepare for present
		backBuffer.TransitionTo(D3D12_RESOURCE_STATE_PRESENT, commandList);

		// Close command list and execute.
		{
			commandList->Close() >> CHK_HR;
			ID3D12CommandList* const commandLists[] = { commandList.Get() };
			commandQueue->ExecuteCommandLists((UINT)std::size(commandLists), commandLists);
		}

		// Insert fence that signifies command list completion.
		commandQueue->Signal(fence.Get(), ++fenceValue) >> CHK_HR;

		// Present
		swapChain->Present(1, 0) >> CHK_HR;
	
		// Wait for signal
		//CHECK_HR(fence->SetEventOnCompletion(fenceValue, fenceEvent));
		//if (::WaitForSingleObject(fenceEvent, INFINITE) == WAIT_FAILED)
		//{
		//	CHECK_HR(GetLastError());
		//}
		
		// Alternative signal wait without requiring event handle.
		// According to the docs for SetEventOnCompletion(), this will not return until the fence value has been reached.
		fence->SetEventOnCompletion(fenceValue, nullptr) >> CHK_HR;
		
	}

	return true;
}
