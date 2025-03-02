#pragma once
#include "CorePch.h"

// Àü¹æ ¼±¾ð
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
    MemoryPool(uint32 allocSize);
    ~MemoryPool();

    void          Push(MemoryHeader* ptr);
    MemoryHeader* Pop();

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

// °´Ã¼ Ç® ÅÛÇÃ¸´
template<typename Type>
class ObjectPool
{
public:
    template<typename... Args>
    static Type* Pop(Args&&... args)
    {
        Type* memory = static_cast<Type*>(GMemoryManager->Allocate(sizeof(Type)));
        new(memory)Type(std::forward<Args>(args)...); // placement new
        return memory;
    }

    static void Push(Type* obj)
    {
        obj->~Type();
        GMemoryManager->Release(obj);
    }

    template<typename... Args>
    static std::shared_ptr<Type> MakeShared(Args&&... args)
    {
        std::shared_ptr<Type> ptr = { Pop(std::forward<Args>(args)...), Push };
        return ptr;
    }
};