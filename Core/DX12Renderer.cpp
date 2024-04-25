#include "DX12Renderer.h"

#include <stdexcept>

DX12Renderer* DX12Renderer::instance = nullptr;

DX12Renderer& DX12Renderer::Get()
{
	if (instance == nullptr)
	{
		throw std::runtime_error("DX12Renderer::Get() called before DX12Renderer::DX12Renderer()");
	}

	return *instance;
}

DX12Renderer::~DX12Renderer()
{
	// Not implemented
}

DX12Renderer::DX12Renderer(UINT width, UINT height)
{
	if (instance != nullptr)
	{
		throw std::runtime_error("DX12Renderer::DX12Renderer() called more than once");
	}

	instance = this;

	// Not implemented
}

