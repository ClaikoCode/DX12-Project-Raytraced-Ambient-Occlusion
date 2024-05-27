#pragma once

#include <cstdint>
#include <array>
#include <unordered_map>

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
constexpr uint32_t MaxRTInstancesPerTopLevel = 100u;

constexpr uint32_t MaxRTVDescriptors = (20u) * BackBufferCount;
// This should be below the sum of all the descriptors for this type of descriptor.
constexpr uint32_t MaxCBVSRVUAVDescriptors = (MaxRenderInstances * 2) * BackBufferCount;

// Macros to define indexing inside of offsets in descriptor heaps.
#define DESCRIPTOR_COUNT(BaseName) BaseName##Count
#define DESCRIPTOR_OFFSET(BaseName) BaseName##Offset
#define FRAME_DESCRIPTOR_OFFSET(BaseName, FrameIndex) (DESCRIPTOR_OFFSET(BaseName) * BackBufferCount + DESCRIPTOR_COUNT(BaseName) * FrameIndex)




// A enum with all unique global descriptor names.
enum GlobalDescriptorNames
{
	SRVGBuffers,
	SRVMiddleTexture,
	UAVMiddleTexture,
	UAVAccumulationTexture,
	RTVGBuffers,
	RTVMiddleTexture,
	RTVBackBuffers,
	DSVScene
};

namespace GlobalDescriptors
{
	constexpr uint32_t MaxGlobalCBVSRVUAVDescriptors = 128u;
	constexpr uint32_t MaxGlobalRTVDescriptors = 32u;
	constexpr uint32_t MaxGlobalDSVDescriptors = 8u;

	enum CBVSRVUAVCounts : uint32_t
	{
		SRVGBuffersCount			= GBufferIDCount,
		SRVMiddleTextureCount		= 1,
		UAVMiddleTextureCount		= 1,
		UAVAccumulationTextureCount = 1
	};

	enum CBVSRVUAVOffsets : uint32_t
	{
		SRVGBuffersOffset				= 0,
		SRVMiddleTextureOffset			= SRVGBuffersOffset				+ SRVGBuffersCount,
		UAVMiddleTextureOffset			= SRVMiddleTextureOffset		+ SRVMiddleTextureCount,
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
		RTVMiddleTextureOffset	= RTVGBuffersOffset			+ RTVGBuffersCount,
		RTVBackBuffersOffset	= RTVMiddleTextureOffset	+ RTVMiddleTextureCount
	};

	enum DSVCounts : UINT
	{
		DSVSceneCount = 1
	};

	enum DSVOffsets : UINT
	{
		DSVSceneOffset = 0
	};

	// A map that maps the descriptor names to the count of descriptors.
	static const std::unordered_map<GlobalDescriptorNames, uint32_t> DescriptorCountMap = {

		// SRVs
		{ SRVGBuffers,				SRVGBuffersCount			},
		{ SRVMiddleTexture,			SRVMiddleTextureCount		},

		// UAVs
		{ UAVMiddleTexture,			UAVMiddleTextureCount		},
		{ UAVAccumulationTexture,	UAVAccumulationTextureCount	},

		// RTVs
		{ RTVGBuffers,				RTVGBuffersCount			},
		{ RTVMiddleTexture,			RTVMiddleTextureCount		},
		{ RTVBackBuffers,			RTVBackBuffersCount			},

		// DSVs
		{ DSVScene,					DSVSceneCount				}
	};

	// A map that maps the descriptor names to the offset of the descriptors.
	static const std::unordered_map<GlobalDescriptorNames, uint32_t> DescriptorOffsetMap = {

		// SRVs
		{ SRVGBuffers,				SRVGBuffersOffset				},
		{ SRVMiddleTexture,			SRVMiddleTextureOffset			},
		
		// UAVs
		{ UAVMiddleTexture,			UAVMiddleTextureOffset			},
		{ UAVAccumulationTexture,	UAVAccumulationTextureOffset	},

		// RTVs
		{ RTVGBuffers,				RTVGBuffersOffset				},
		{ RTVMiddleTexture,			RTVMiddleTextureOffset			},
		{ RTVBackBuffers,			RTVBackBuffersOffset			},

		// DSVs
		{ DSVScene,					DSVSceneOffset					}
	};

	static uint32_t GetDescriptorCount(GlobalDescriptorNames descriptorName)
	{
		return DescriptorCountMap.at(descriptorName);
	}

	static uint32_t GetDescriptorOffset(GlobalDescriptorNames descriptorName)
	{
		return DescriptorOffsetMap.at(descriptorName);
	}

	static uint32_t GetDescriptorRelativeOffset(GlobalDescriptorNames from, GlobalDescriptorNames to)
	{
		int fromOffset = (int)GetDescriptorOffset(from);
		int toOffset = (int)GetDescriptorOffset(to);

		int offset = toOffset - fromOffset;

		return (uint32_t)abs(offset);
	}
}

enum FrameDescriptorNames
{
	CBVRenderInstance,
	CBVFrameData,
	SRVTopLevelAS
};

namespace FrameDescriptors
{
	constexpr uint32_t MaxFrameCBVSRVUAVDescriptors = 256u;

	enum CBVSRVUAVCounts : uint32_t
	{
		CBVRenderInstanceCount	= MaxRenderInstances,
		CBVFrameDataCount		= 1,
		SRVTLASCount			= 1,
	};

	enum CBVSRVUAVOffsets : uint32_t
	{
		CBVRenderInstanceOffset = 0,
		CBVFrameDataOffset = CBVRenderInstanceOffset + CBVRenderInstanceCount,
		SRVTLASOffset = CBVFrameDataOffset + CBVFrameDataCount
	};


	// A map that maps the descriptor names to the count of descriptors.
	static const std::unordered_map<FrameDescriptorNames, uint32_t> DescriptorCountMap = {

		// CBVs
		{ CBVRenderInstance,	CBVRenderInstanceCount	},
		{ CBVFrameData,			CBVFrameDataCount		},

		// SRVs
		{ SRVTopLevelAS,				SRVTLASCount	}
	};


	// A map that maps the descriptor names to the offset of the descriptors.
	static const std::unordered_map<FrameDescriptorNames, uint32_t> DescriptorOffsetMap = {

		// CBVs
		{ CBVRenderInstance,	CBVRenderInstanceOffset	},
		{ CBVFrameData,			CBVFrameDataOffset		},

		// SRVs
		{ SRVTopLevelAS,				SRVTLASOffset			}
	};

	static uint32_t GetDescriptorCount(FrameDescriptorNames descriptorName)
	{
		return DescriptorCountMap.at(descriptorName);
	}

	static uint32_t GetDescriptorOffsetCBVSRVUAV(FrameDescriptorNames descriptorName, UINT frameIndex)
	{
		UINT internalOffset = 
			GlobalDescriptors::MaxGlobalCBVSRVUAVDescriptors + 
			FrameDescriptors::MaxFrameCBVSRVUAVDescriptors * frameIndex;

		return DescriptorOffsetMap.at(descriptorName) + internalOffset;
	}
}

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
