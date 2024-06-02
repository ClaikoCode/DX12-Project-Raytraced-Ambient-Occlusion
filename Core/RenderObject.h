#pragma once

#include <vector>

#include "DirectXIncludes.h"
#include "GPUResource.h"
#include "AppDefines.h"
#include "DXRAbstractions.h"

using Microsoft::WRL::ComPtr;
using DX12Abstractions::GPUResource;

// The vertex structure contains the position, normal, and color of a vertex.
struct Vertex
{
	DirectX::XMFLOAT3 position;
	DirectX::XMFLOAT3 normal;
	DirectX::XMFLOAT3 color;

	static const DXGI_FORMAT sVertexFormat = DXGI_FORMAT_R32G32B32_FLOAT; // Position format.
};

// Typedef to have one definition for the vertex index.
typedef uint32_t VertexIndex;

// The draw arguments are used to specify how many vertices and indices to draw.
struct DrawArgs
{
	UINT vertexCount = UINT_MAX;
	UINT startVertex = UINT_MAX;

	UINT indexCount = UINT_MAX;
	UINT startIndex = UINT_MAX;
	UINT baseVertex = 0;

	UINT startInstance = 0;
};

// A render object is a unique object that can be rendered in the scene.
// It contains the vertex and index buffers, as well as the draw arguments.
// Each render object can have multiple instances.
struct RenderObject
{
	GPUResource vertexBuffer;
	GPUResource indexBuffer;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
	D3D12_INDEX_BUFFER_VIEW indexBufferView = {};

	std::vector<DrawArgs> drawArgs;
	D3D12_PRIMITIVE_TOPOLOGY topology;
};

// Each instance contains a set of constants and an index to a descriptor heap where its CBV is stored.
struct RenderInstance
{
	UINT CBIndex;
	InstanceConstants instanceData;
};

// Render packages are sent to render passes so they can render multiple render objects with several instances.
struct RenderPackage
{
	RenderObject* renderObject;
	std::vector<RenderInstance>* renderInstances;
};

// The ray tracing equivalent of the RenderObject.
struct RayTracingRenderPackage
{
	DX12Abstractions::AccelerationStructureBuffers* topLevelASBuffers;
	UINT instanceCount;
};