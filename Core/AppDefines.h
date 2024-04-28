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

// A unique identifier for each type of render pass.
enum RenderPassType : uint32_t
{
	NonIndexedPass = 0u,
	IndexedPass,

	NumRenderPasses // Keep this last!
};