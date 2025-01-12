#pragma once

#include "CorePch.h"

#ifdef _DEBUG
#pragma comment(lib, "ServerCoreLibrary\\Debug\\ServerCoreLibrary.lib")
//#pragma comment(lib, "Protobuf\\Debug\\libprotobufd.lib")
#else
#pragma comment(lib, "ServerCore\\Release\\ServerCoreLibrary.lib")
//#pragma comment(lib, "Protobuf\\Release\\libprotobuf.lib")
#endif
