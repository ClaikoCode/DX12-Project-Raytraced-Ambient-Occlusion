#pragma once

#include "DirectXIncludes.h"
#include <vector>

using Microsoft::WRL::ComPtr;

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
	ComPtr<ID3D12Resource> vertexBuffer = nullptr;
	ComPtr<ID3D12Resource> indexBuffer = nullptr;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
	D3D12_INDEX_BUFFER_VIEW indexBufferView = {};

	std::vector<DrawArgs> drawArgs;
};