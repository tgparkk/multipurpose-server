#include "pch.h"
#include "SendBuffer.h"

SendBuffer::SendBuffer(std::shared_ptr<SendBufferChunk> owner, BYTE* buffer, uint32_t allocSize)
    : _owner(owner), _buffer(buffer), _allocSize(allocSize)
{
}

void SendBuffer::Close(uint32_t writeSize)
{
    assert(_allocSize >= writeSize);
    _writeSize = writeSize;
    _owner->Close(writeSize);
}

/*--------------------
    SendBufferChunk
--------------------*/
SendBufferChunk::SendBufferChunk()
{
    _buffer.resize(SEND_BUFFER_CHUNK_SIZE);
}

void SendBufferChunk::Reset()
{
    _open = false;
    _usedSize = 0;
}

std::shared_ptr<SendBuffer> SendBufferChunk::Open(uint32_t allocSize)
{
    // 1. 크기 확인
    assert(allocSize <= SEND_BUFFER_CHUNK_SIZE);
    assert(_open == false);

    // 2. 공간 부족 시 nullptr 반환
    if (allocSize > FreeSize())
        return nullptr;

    // 3. 사용 중 표시
    _open = true;

    // 4. SendBuffer 생성하여 반환
    return std::make_shared<SendBuffer>(shared_from_this(), Buffer(), allocSize);
}

void SendBufferChunk::Close(uint32_t writeSize)
{
    assert(_open == true);
    _open = false;
    _usedSize += writeSize;
}

/*---------------------
    SendBufferManager
----------------------*/
thread_local std::shared_ptr<SendBufferChunk> LSendBufferChunk;

std::shared_ptr<SendBuffer> SendBufferManager::Open(uint32_t size)
{
    // 1. 스레드별 SendBufferChunk 확인/할당
    if (LSendBufferChunk == nullptr)
    {
        LSendBufferChunk = Pop();  // 청크 풀에서 가져오거나 새로 생성
        LSendBufferChunk->Reset();
    }

    // 2. 청크가 열려있지 않은지 확인
    assert(LSendBufferChunk->IsOpen() == false);

    // 3. 공간이 부족하면 새 청크 할당
    if (LSendBufferChunk->FreeSize() < size)
    {
        LSendBufferChunk = Pop();
        LSendBufferChunk->Reset();
    }

    // 4. 버퍼 열기
    return LSendBufferChunk->Open(size);
}

std::shared_ptr<SendBufferChunk> SendBufferManager::Pop()
{
    std::lock_guard<std::mutex> lock(_lock);

    if (!_sendBufferChunks.empty())
    {
        std::shared_ptr<SendBufferChunk> sendBufferChunk = _sendBufferChunks.back();
        _sendBufferChunks.pop_back();
        return sendBufferChunk;
    }

    return std::make_shared<SendBufferChunk>();
}

void SendBufferManager::Push(std::shared_ptr<SendBufferChunk> buffer)
{
    std::lock_guard<std::mutex> lock(_lock);
    _sendBufferChunks.push_back(buffer);
}

void SendBufferManager::PushGlobal(SendBufferChunk* buffer)
{
    GSendBufferManager->Push(std::shared_ptr<SendBufferChunk>(buffer, PushGlobal));
}