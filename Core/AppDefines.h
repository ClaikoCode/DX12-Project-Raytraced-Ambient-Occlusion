#pragma once

#include <cstdint>

// This file is used to define the common types and constants that are used throughout the application.

// The number of contexts that the program uses.
constexpr uint32_t NumContexts = 2u;

// A unique identifier for each type of render pass.
enum RenderPassType : uint32_t
{
	TrianglePass = 0u,

	NumRenderPasses // Keep this last!
};