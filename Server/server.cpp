#include "pch.h"

#include "Session.h"
#include "ThreadManager.h"
#include "CorePch.h"

CoreGlobal Core;

void ThreadMain()
{
	while (true)
	{
		std::cout << "Hello ! I am thread... " << LThreadId << std::endl;
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}
}


int main() {
	for (__int32 i = 0; i < 5; i++)
	{
		GThreadManager->Launch(ThreadMain);
	}

	GThreadManager->Join();
}