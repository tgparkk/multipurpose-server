#pragma once
#include "CorePch.h"

// 전방 선언
class SendBufferChunk;

/*----------------
    MemoryHeader
-----------------*/
struct MemoryHeader
{
    // [MemoryHeader][Data]
    MemoryHeader(uint32 size) : allocSize(size) {}

    static void* AttachHeader(MemoryHeader* header, uint32 size)
    {
        new(header)MemoryHeader(size); // placement new
        return reinterpret_cast<void*>(++header);
    }

    static MemoryHeader* DetachHeader(void* ptr)
    {
        MemoryHeader* header = reinterpret_cast<MemoryHeader*>(ptr) - 1;
        return header;
    }

    uint32 allocSize;
};

/*-----------------
    MemoryPool
------------------*/
class MemoryPool
{
public:
    MemoryPool(uint32 allocSize) : _allocSize(allocSize) {}

    ~MemoryPool()
    {
        std::lock_guard<std::mutex> guard(_lock);
        for (MemoryHeader* ptr : _queue)
            free(ptr);
    }

    MemoryHeader* Pop()
    {
        std::lock_guard<std::mutex> guard(_lock);
        if (_queue.empty())
        {
            // 새로 할당
            MemoryHeader* ptr = reinterpret_cast<MemoryHeader*>(malloc(_allocSize + sizeof(MemoryHeader)));
            return ptr;
        }

        MemoryHeader* ptr = _queue.back();
        _queue.pop_back();
        return ptr;
    }

    void Push(MemoryHeader* ptr)
    {
        std::lock_guard<std::mutex> guard(_lock);
        _queue.push_back(ptr);
    }

private:
    uint32 _allocSize = 0;
    std::mutex _lock;
    std::vector<MemoryHeader*> _queue;
};

/*-----------------
    MemoryPoolManager
------------------*/
class MemoryPoolManager
{
public:
    MemoryPoolManager();
    ~MemoryPoolManager();

    void* Allocate(uint32 size);
    void Release(void* ptr);

private:
    std::map<uint32, MemoryPool*> _pools;
};