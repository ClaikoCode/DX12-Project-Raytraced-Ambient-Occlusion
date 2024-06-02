#include "DX12AbstractionUtils.h"

namespace DX12Abstractions
{
	ID3D12CommandList* const* GetCommandListPtr(CommandListVector& commandListVec, const UINT offset)
	{
		const bool isValidSize = !commandListVec.empty() && commandListVec.size() - 1 >= offset;
		return isValidSize ? commandListVec[offset].GetAddressOf() : nullptr;
	}
}
