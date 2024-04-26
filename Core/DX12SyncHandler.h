#pragma once

#include "DirectXIncludes.h"
#include <thread>
#include <unordered_map>
#include <array>

constexpr UINT NumContexts = 3;

typedef std::array<HANDLE, NumContexts> SyncHandles;
typedef UINT PassID;

class DX12SyncHandler
{
	SyncHandles startSync;
	// This maps a certain pass ID (like shadow pass, lighting pass, etc) 
	// to the sync handles that are used to synchronize the work of that pass.
	std::unordered_map<PassID, SyncHandles> uniquePassSync;
	SyncHandles endSync;
};