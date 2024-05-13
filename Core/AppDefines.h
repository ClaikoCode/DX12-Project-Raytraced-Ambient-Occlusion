#pragma once

#include <cstdint>

/*
* This file is used to define the common types and constants that are used throughout the application.
* 
*/

// Common paths
constexpr const char* AssetsPath = "../../../../assets/";

// The number of contexts that the program uses.
constexpr uint32_t NumContexts = 1u;

// How many back sbuffers the program uses.
constexpr UINT BufferCount = 2;
constexpr FLOAT OptimizedClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

// A unique identifier for each type of render pass.
enum RenderPassType : uint32_t
{
	NonIndexedPass = 0u,
	IndexedPass,
	GBufferPass,

	NumRenderPasses // Keep this last!
};

// A unique identifier for each type of GBuffer.
enum GBufferID : UINT
{
	GBufferDiffuse = 0,
	GBufferNormal,
	GBufferWorldPos,

	GBufferCount // Keep last!
};

// The maximum number of instances that can be rendered in a single draw call.
constexpr uint32_t MaxRenderInstances = 32u;

// This should be a sum of all the descriptors for this type of descriptor.
constexpr uint32_t NumCBVSRVUAVDescriptors = MaxRenderInstances;


enum CBVSRVUAVOffsets : UINT {
	RenderInstanceOffset = 0
};

struct InstanceConstants
{
	DirectX::XMFLOAT4X4 modelMatrix;
};

enum class RenderObjectID : uint32_t
{
	Triangle = 0u,
	Cube,
	OBJModel1
};

namespace RootSigRegisters {

	enum CBRegisters : uint32_t {
		MatrixConstants = 0,
		CBDescriptorTable
	};

}