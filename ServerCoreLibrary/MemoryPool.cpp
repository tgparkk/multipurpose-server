#include "pch.h"
#include "MemoryPool.h"
#include "SendBuffer.h" // SendBufferChunk::SEND_BUFFER_CHUNK_SIZE ����� ����


MemoryPool::MemoryPool(uint32 allocSize) : _allocSize(allocSize)
{
}

MemoryPool::~MemoryPool()
{
    std::lock_guard<std::mutex> guard(_lock);
    for (MemoryHeader* ptr : _queue)
        free(ptr);
}

void MemoryPool::Push(MemoryHeader* ptr)
{
    ptr->allocSize = 0;

    std::lock_guard<std::mutex> guard(_lock);
    _queue.push_back(ptr);
}

MemoryHeader* MemoryPool::Pop()
{
    std::lock_guard<std::mutex> guard(_lock);
    if (_queue.empty())
    {
        // ���� �Ҵ�
        MemoryHeader* header = reinterpret_cast<MemoryHeader*>(malloc(_allocSize + sizeof(MemoryHeader)));
        return header;
    }

    MemoryHeader* header = _queue.back();
    _queue.pop_back();
    return header;
}

MemoryPoolManager::MemoryPoolManager()
{
    // �� ����� �޸� Ǯ ����
    for (uint32 size = 32; size <= 1024; size += 32)
        _pools[size] = new MemoryPool(size);

    for (uint32 size = 1024 + 128; size <= 4096; size += 128)
        _pools[size] = new MemoryPool(size);

    // 64KB¥�� ûũ ���� Ǯ
    _pools[SendBufferChunk::SEND_BUFFER_CHUNK_SIZE] = new MemoryPool(SendBufferChunk::SEND_BUFFER_CHUNK_SIZE);
}

MemoryPoolManager::~MemoryPoolManager()
{
    for (auto& pair : _pools)
        delete pair.second;
}

void* MemoryPoolManager::Allocate(uint32 size)
{
    MemoryPool* pool = nullptr;

    auto it = _pools.lower_bound(size);
    if (it != _pools.end())
        pool = it->second;

    if (pool == nullptr)
    {
        // Ǯ���� �������� �ʴ� ū ũ���� ��� ���� �Ҵ�
        MemoryHeader* header = reinterpret_cast<MemoryHeader*>(malloc(size + sizeof(MemoryHeader)));
        return MemoryHeader::AttachHeader(header, size);
    }

    MemoryHeader* header = pool->Pop();
    return MemoryHeader::AttachHeader(header, size);
}

void MemoryPoolManager::Release(void* ptr)
{
    MemoryHeader* header = MemoryHeader::DetachHeader(ptr);
    uint32_t allocSize = header->allocSize;

    auto it = _pools.find(allocSize);
    if (it == _pools.end())
    {
        // Ǯ���� �������� �ʴ� ũ���� ��� ���� ����
        free(header);
        return;
    }

    it->second->Push(header);
}