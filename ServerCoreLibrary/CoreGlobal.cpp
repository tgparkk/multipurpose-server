#include "pch.h"
#include "CoreGlobal.h"
#include "SendBuffer.h"
#include "ThreadManager.h"
#include "MemoryPool.h"

ThreadManager* GThreadManager = nullptr;
SendBufferManager* GSendBufferManager = nullptr;
MemoryPoolManager* GMemoryManager = nullptr;
CoreGlobal::CoreGlobal()
{
	GThreadManager = new ThreadManager();
	GSendBufferManager = new SendBufferManager();
	GMemoryManager = new MemoryPoolManager();
}

CoreGlobal::~CoreGlobal()
{
	delete GThreadManager;
	delete GSendBufferManager;
	delete GMemoryManager;
}