#include "DX12SyncHandler.h"

SyncHandles::SyncHandles()
{
	for (UINT i = 0; i < NumContexts; i++)
	{
		handles[i] = CreateEvent(nullptr, FALSE, FALSE, nullptr);

		// Assert that the event was created successfully.
		assert(handles[i] != nullptr);
	}
}

SyncHandles::~SyncHandles()
{
	for (UINT i = 0; i < NumContexts; i++)
	{
		CloseHandle(handles[i]);
	}
}

HANDLE SyncHandles::Get(UINT index) const
{
	return handles[index];
}

HANDLE SyncHandles::operator[](UINT index) const
{
	return Get(index);
}

DX12SyncHandler::DX12SyncHandler() : 
	startSync(std::make_shared<SyncHandles>()), 
	endSync(std::make_shared<SyncHandles>())
{

}

void DX12SyncHandler::AddUniquePassSync(RenderPassType passType)
{
	// If the pass ID is already in the map, return.
	if (uniquePassFinish.find(passType) != uniquePassFinish.end())
	{
		return;
	}

	uniquePassFinish[passType] = std::make_shared<SyncHandles>();
}

void DX12SyncHandler::WaitStart(UINT context)
{
	WaitForSingleObject(startSync->Get(context), INFINITE);
}

void DX12SyncHandler::WaitStartAll()
{
	WaitForMultipleObjects(NumContexts, startSync->handles.data(), TRUE, INFINITE);
}

void DX12SyncHandler::WaitEnd(UINT context)
{
	WaitForSingleObject(endSync->Get(context), INFINITE);
}

void DX12SyncHandler::WaitEndAll()
{
	WaitForMultipleObjects(NumContexts, endSync->handles.data(), TRUE, INFINITE);
}

void DX12SyncHandler::WaitPass(UINT context, RenderPassType passType)
{
	WaitForSingleObject(uniquePassFinish[passType]->Get(context), INFINITE);
}

void DX12SyncHandler::WaitPassAll(RenderPassType passType)
{
	WaitForMultipleObjects(NumContexts, uniquePassFinish[passType]->handles.data(), TRUE, INFINITE);
}

void DX12SyncHandler::SetStart(UINT context)
{
	SetEvent(startSync->Get(context));
}

void DX12SyncHandler::SetEnd(UINT context)
{
	SetEvent(endSync->Get(context));
}

void DX12SyncHandler::SetPass(UINT context, RenderPassType passType)
{
	SetEvent(uniquePassFinish[passType]->Get(context));
}
