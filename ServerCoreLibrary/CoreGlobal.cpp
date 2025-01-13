#include "pch.h"
#include "CoreGlobal.h"
#include "SendBuffer.h"
#include "ThreadManager.h"

ThreadManager* GThreadManager = nullptr;
SendBufferManager* GSendBufferManager = nullptr;

CoreGlobal::CoreGlobal()
{
	GThreadManager = new ThreadManager();
	GSendBufferManager = new SendBufferManager();
}

CoreGlobal::~CoreGlobal()
{
	delete GThreadManager;
	delete GSendBufferManager;
}