#pragma once

#include "DirectXIncludes.h"
#include <thread>
#include <unordered_map>
#include <array>
#include <memory>

#include "AppDefines.h"

typedef std::array<HANDLE, NumContexts> SyncHandleArray;

struct SyncHandles
{
	SyncHandles();
	~SyncHandles();

	HANDLE Get(UINT index) const;
	HANDLE operator[](UINT index) const;

	SyncHandleArray handles;
};

// Contains sync handles for start and end sync as well as unique sync handles for each pass type.
// Sync handles are stored as smart pointers to ensure that handles are properly copied and destroyed efficiently.
class DX12SyncHandler
{
public:

	DX12SyncHandler();

	std::shared_ptr<SyncHandles> startSync;
	std::shared_ptr<SyncHandles> endSync;

	void AddUniquePassSync(RenderPassType passType);

	void WaitStart(UINT context);
	void WaitStartAll();
	void WaitEnd(UINT context);
	void WaitEndAll();
	void WaitPass(UINT context, RenderPassType passType);
	void WaitPassAll(RenderPassType passType);

	void SetStart(UINT context);
	void SetEnd(UINT context);
	void SetPass(UINT context, RenderPassType passType);

private:
	// This maps a certain pass type to the sync handles that are used to synchronize the work of that specific pass.
	std::unordered_map<RenderPassType, std::shared_ptr<SyncHandles>> uniquePassFinish;
};