#pragma once



#ifdef _DEBUG
//#pragma comment(lib, "ServerCore\\Debug\\ServerCore.lib")
//#pragma comment(lib, "Protobuf\\Debug\\libprotobufd.lib")
#else
#pragma comment(lib, "ServerCore\\Release\\ServerCore.lib")
#pragma comment(lib, "Protobuf\\Release\\libprotobuf.lib")
#endif
