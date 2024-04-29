//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

// TODO: Add a proper license for this file.
// NOTE: The code for SetName and SetNameIndexed is taken from the Microsoft DirectX-Graphics-Samples repository.

#pragma once

#include "DirectXIncludes.h"

namespace DX12Abstractions
{
// Assign a name to the object to aid with debugging.
#if defined(_DEBUG)
	inline void SetName(ID3D12Object* pObject, LPCWSTR name)
	{
		pObject->SetName(name);
	}
	inline void SetNameIndexed(ID3D12Object* pObject, LPCWSTR name, UINT index)
	{
		WCHAR fullName[50];
		if (swprintf_s(fullName, L"%s[%u]", name, index) > 0)
		{
			pObject->SetName(fullName);
		}
	}
#else
	inline void SetName(ID3D12Object*, LPCWSTR)
	{
	}
	inline void SetNameIndexed(ID3D12Object*, LPCWSTR, UINT)
	{
	}
#endif

}

// Naming helper for ComPtr<T>.
// Assigns the name of the variable as the name of the object.
// The indexed variant will include the index in the name of the object.
#define NAME_D3D12_OBJECT(x) DX12Abstractions::SetName((x).Get(), L#x)
#define NAME_D3D12_OBJECT_MEMBER(x, className) DX12Abstractions::SetName((x).Get(), L#className L"::" L#x)
#define NAME_D3D12_OBJECT_INDEXED(x, n) DX12Abstractions::SetNameIndexed((x)[n].Get(), L#x, n)
#define NAME_D3D12_OBJECT_MEMBER_INDEXED(x, n, className) DX12Abstractions::SetNameIndexed((x)[n].Get(), L#className L"::" L#x, n)