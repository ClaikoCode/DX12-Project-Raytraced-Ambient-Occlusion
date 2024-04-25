#pragma once

#include "DirectXIncludes.h"

using Microsoft::WRL::ComPtr;

// Singleton class designed with a public constructor that only is allowed to be called once.
class DX12Renderer
{

public:

	DX12Renderer(UINT width, UINT height);
	static DX12Renderer& Get();

private:
	
	// Private destructor
	
	~DX12Renderer();

	// Remove copy constructor and copy assignment operator
	DX12Renderer(const DX12Renderer& rhs) = delete;
	DX12Renderer& operator=(const DX12Renderer& rhs) = delete;

private:

	static DX12Renderer* instance;

};