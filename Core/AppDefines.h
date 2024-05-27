#pragma once

#include <cstdint>
#include <array>

#include "DirectXIncludes.h"

/*
	This file is used to define the common types and constants that are used throughout the application.
*/

// Common paths
constexpr const char* AssetsPath = "../../../../assets/";

// The number of contexts that the program uses.
constexpr uint32_t NumContexts = 1u;

// How many back back buffers the program uses.
constexpr UINT BackBufferCount = 2;
constexpr FLOAT OptimizedClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

// Reference to the back buffer format.
//constexpr DXGI_FORMAT BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT BackBufferFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

enum CommandListIdentifier
{
	PreCommandList = 0,
	PostCommandList,

	NumCommandLists // Keep this last!
};

// A unique identifier for each type of render pass.
enum RenderPassType : uint32_t
{
	NonIndexedPass = 0u,
	IndexedPass,
	DeferredGBufferPass,
	DeferredLightingPass,
	RaytracedAOPass,
	AccumulationPass,

	NumRenderPasses // Keep this last!
};

// A unique identifier for each type of GBuffer.
enum GBufferID : UINT
{
	GBufferDiffuse = 0,
	GBufferNormal,
	GBufferWorldPos,

	GBufferIDCount // Keep last!
};

// Array of formats for each gbuffer texture.
constexpr std::array<DXGI_FORMAT, GBufferIDCount> GBufferFormats = { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT };

// The maximum number of instances that can be rendered in a single draw call.
constexpr uint32_t MaxRenderInstances = 100u;
constexpr uint32_t MaxRTInstancesPerTopLevel = 50u;

constexpr uint32_t MaxRTVDescriptors = (20u) * BackBufferCount;
// This should be below the sum of all the descriptors for this type of descriptor.
constexpr uint32_t MaxCBVSRVUAVDescriptors = (MaxRenderInstances * 2) * BackBufferCount;

// Macros to define indexing inside of offsets in descriptor heaps.
#define DESCRIPTOR_COUNT(BaseName) BaseName##Count
#define DESCRIPTOR_OFFSET(BaseName) BaseName##Offset
#define FRAME_DESCRIPTOR_OFFSET(BaseName, FrameIndex) (DESCRIPTOR_OFFSET(BaseName) * BackBufferCount + DESCRIPTOR_COUNT(BaseName) * FrameIndex)


// The count of different types of descriptors.
enum CBVSRVUAVCounts : UINT
{
	CBVRenderInstanceCount				= MaxRenderInstances,
	CBVGlobalFrameDataCount				= 1,
	SRVGBuffersCount					= GBufferIDCount,
	SRVMiddleTextureCount				= 1,
	SRVAccumulationTextureCount			= 1,
	SRVTLASCount						= 1,
	UAVMiddleTextureCount				= 1,
	UAVAccumulationTextureCount			= 1
};

// Offsets for descriptors in the descriptor heap. The pattern is always adding the value before and adding its equivalent count.
// So its always: [DescriptorType]Offset[SpecificName] + [DescriptorType]Count[SpecificName].

enum CBVSRVUAVOffsets : UINT {
	CBVRenderInstanceOffset			= 0,
	CBVGlobalFrameDataOffset		= CBVRenderInstanceOffset		+ CBVRenderInstanceCount,
	SRVGBuffersOffset				= CBVGlobalFrameDataOffset		+ CBVGlobalFrameDataCount,
	SRVMiddleTextureOffset			= SRVGBuffersOffset				+ SRVGBuffersCount,
	SRVAccumulationTextureOffset	= SRVMiddleTextureOffset		+ SRVMiddleTextureCount,
	SRVTLASOffset					= SRVAccumulationTextureOffset	+ SRVAccumulationTextureCount,
	UAVMiddleTextureOffset			= SRVTLASOffset					+ SRVTLASCount,
	UAVAccumulationTextureOffset	= UAVMiddleTextureOffset		+ UAVMiddleTextureCount
};

enum RTVCounts : UINT
{
	RTVGBuffersCount		= GBufferIDCount,
	RTVMiddleTextureCount	= 1,
	RTVBackBuffersCount		= BackBufferCount
};

enum RTVOffsets : UINT
{
	RTVGBuffersOffset		= 0,
	RTVMiddleTextureOffset	= RTVGBuffersOffset + RTVGBuffersCount,
	RTVBackBuffersOffset	= RTVMiddleTextureOffset + RTVMiddleTextureCount // Back buffers should always come last for simplicity.
};

struct InstanceConstants
{
	DirectX::XMFLOAT4X4 modelMatrix;
};

struct GlobalFrameData
{
	UINT frameCount;
	UINT accumulatedFrames;
	float time;
};

enum class RenderObjectID : uint32_t
{
	Triangle = 0u,
	Cube,
	OBJModel1
};

namespace RasterShaderRegisters {

	enum CBVRegisters : uint32_t {
		CBMatrixConstants		= 0,
		CBVDescriptorGlobals	= 1,
		CBVDescriptorRange		= 2
	};

	enum SRVRegisters : uint32_t {
		SRVDescriptorRange = 0
	};

	enum UAVRegisters : uint32_t {
		UAVDescriptorRange = 0
	};

}

namespace RTShaderRegisters {

	enum SRVRegistersRayGen : uint32_t {
		SRVDescriptorTableTLASRegister		= 0,
		SRVDescriptorTableGbuffersRegister	= 1
	};

	enum UAVRegistersRayGen : uint32_t {
		UAVDescriptorRegister = 0
	};

	enum ConstantRegistersGlobal : uint32_t {
		ConstantRegister = 0
	};

}

enum DefaultRootParameterIdx
{
	MatrixIdx = 0,
	CBVGlobalFrameDataIdx,
	CBVTableIdx,
	UAVSRVTableIdx,

	DefaultRootParameterCount // Keep last!
};

enum RTRayGenParameterIdx
{
	RayGenSRVTableTLASIdx = 0,
	RayGenSRVTableGbuffersIdx,
	RayGenUAVTableIdx,

	RTRayGenParameterCount // Keep last!
};

enum RTGlobalParameterIdx
{
	Global32BitConstantIdx = 0,

	RTGlobalParameterCount
};

enum RTHitGroupParameterIdx
{
	HitGroupSRVTableIdx		= 0,
	HitGroupUAVIdx			= 1,

	RTHitGroupParameterCount // Keep last!
};
