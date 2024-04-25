#include <stdexcept>
#include <array>
#include <ranges>

#include "DirectXIncludes.h"
#include "App.h"
#include "GraphicsErrorHandling.h"
#include "GPUResource.h"
#include "DX12AbstractionUtils.h"
#include "DX12Renderer.h"

using Microsoft::WRL::ComPtr;
using namespace DX12Abstractions;

bool RunApp(Core::Window& window)
{
	// Initialize the renderer.
	DX12Renderer::Init(window.Width(), window.Height(), window.Handle());
	DX12Renderer& renderer = DX12Renderer::Get();

	// Main loop.
	while (!window.Closed())
	{
		window.ProcessMessages();

		renderer.Render();
	}

	return true;
}
