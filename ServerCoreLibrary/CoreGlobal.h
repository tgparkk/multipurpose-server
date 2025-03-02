#pragma once

extern class ThreadManager* GThreadManager;
extern class SendBufferManager* GSendBufferManager;
extern class MemoryPoolManager* GMemoryManager;

class CoreGlobal
{
public:
	CoreGlobal();
	~CoreGlobal();
};

