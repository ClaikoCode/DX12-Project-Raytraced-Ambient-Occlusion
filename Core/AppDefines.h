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
	DeferredGBufferPass,
	DeferredLightingPass,
	RaytracedAOPass,

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
constexpr uint32_t MiddleTextureDescriptorCount = 2;

constexpr uint32_t MaxRTVDescriptors = 20u;
// This should be below the sum of all the descriptors for this type of descriptor.
constexpr uint32_t MaxCBVSRVUAVDescriptors = 100u;

// The count of different types of descriptors.
enum CBVSRVUAVCounts : UINT
{
	CBVCountRenderInstance = MaxRenderInstances,
	SRVCountGbuffers = GBufferCount,
	SRVCountMiddleTexture = 1,
	SRVCountTLAS = 1,
	UAVCountMiddleTexture = 1
};

// Offsets for descriptors in the descriptor heap. The pattern is always adding the value before and adding its equivalent count.
// So its always: [DescriptorType]Offset[SpecificName] + [DescriptorType]Count[SpecificName].
enum CBVSRVUAVOffsets : UINT {
	CBVOffsetRenderInstance = 0,
	SRVOffsetGBuffers = CBVCountRenderInstance,
	SRVOffsetMiddleTexture = SRVOffsetGBuffers + SRVCountGbuffers,
	SRVOffsetTLAS = SRVOffsetMiddleTexture + SRVCountMiddleTexture,
	UAVOffsetMiddleTexture = SRVOffsetTLAS + SRVCountTLAS
};

enum RTVCounts : UINT
{
	RTVCountBackbuffers = BufferCount,
	RTVCountGbuffers = GBufferCount,
	RTVCountMiddleTexture = 1
};

enum RTVOffsets : UINT
{
	RTVOffsetBackBuffers = 0, // Back buffers should always come first for simplicity.
	RTVOffsetGBuffers = RTVOffsetBackBuffers + RTVCountBackbuffers,
	RTVOffsetMiddleTexture = RTVOffsetGBuffers + RTVCountGbuffers
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

namespace RasterShaderRegisters {

	enum CBVRegisters : uint32_t {
		CBMatrixConstants = 0,
		CBDescriptorTable = 1,
	};

	enum SRVRegisters : uint32_t {
		SRDescriptorTable = 0
	};

}

namespace RTShaderRegisters {

	enum SRVRegisters : uint32_t {
		SRDescriptorTable = 0
	};

	enum UAVRegisters : uint32_t {
		UAVDescriptor = 0
	};

}

enum DefaultRootParameterIdx
{
	MatrixIdx = 0,
	CBVTableIdx = 1,
	SRVTableIdx = 2,

	DefaultRootParameterCount // Keep last!
};

enum RTRayGenParameterIdx
{
	RayGenSRVTableIdx = 0,
	RayGenUAVTableIdx = 1,

	RTRayGenParameterCount // Keep last!
};

enum RTHitGroupParameterIdx
{
	HitGroupSRVTableIdx = 0,
	HitGroupUAVIdx = 1,

	RTHitGroupParameterCount // Keep last!
};
