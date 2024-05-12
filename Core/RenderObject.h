#pragma once

#include <vector>

#include "DirectXIncludes.h"
#include "GPUResource.h"
#include "AppDefines.h"

using Microsoft::WRL::ComPtr;
using DX12Abstractions::GPUResource;

struct DrawArgs
{
	UINT vertexCount = UINT_MAX;
	UINT startVertex = UINT_MAX;

	UINT indexCount = UINT_MAX;
	UINT startIndex = UINT_MAX;
	UINT baseVertex = 0;

	UINT startInstance = 0;
};

struct RenderObject
{
	GPUResource vertexBuffer;
	GPUResource indexBuffer;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
	D3D12_INDEX_BUFFER_VIEW indexBufferView = {};

	std::vector<DrawArgs> drawArgs;
	D3D12_PRIMITIVE_TOPOLOGY topology;
};

struct RenderInstance
{
	UINT CBIndex;
	InstanceConstants instanceData;
};

// Contains information about the render object and also each instance of that render object.
struct RenderPackage
{
	RenderObject* renderObject;
	std::vector<RenderInstance>* renderInstances;
};