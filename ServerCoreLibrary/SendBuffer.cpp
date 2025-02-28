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
    // 1. ũ�� Ȯ��
    assert(allocSize <= SEND_BUFFER_CHUNK_SIZE);
    assert(_open == false);

    // 2. ���� ���� �� nullptr ��ȯ
    if (allocSize > FreeSize())
        return nullptr;

    // 3. ��� �� ǥ��
    _open = true;

    // 4. SendBuffer �����Ͽ� ��ȯ
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
    // 1. �����庰 SendBufferChunk Ȯ��/�Ҵ�
    if (LSendBufferChunk == nullptr)
    {
        LSendBufferChunk = Pop();  // ûũ Ǯ���� �������ų� ���� ����
        LSendBufferChunk->Reset();
    }

    // 2. ûũ�� �������� ������ Ȯ��
    assert(LSendBufferChunk->IsOpen() == false);

    // 3. ������ �����ϸ� �� ûũ �Ҵ�
    if (LSendBufferChunk->FreeSize() < size)
    {
        // ���� ûũ�� ��Ȱ�� ť�� Ǫ�� (������ ���� �κ�)
        Push(LSendBufferChunk);

        LSendBufferChunk = Pop();
        LSendBufferChunk->Reset();
    }

    // 4. ���� ����
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

    // �� ûũ ���� - PushGlobal�� ����Ͽ� �ڵ� ��ȯ
    return std::shared_ptr<SendBufferChunk>(new SendBufferChunk(), PushGlobal);
}

void SendBufferManager::Push(std::shared_ptr<SendBufferChunk> buffer)
{
    // ������ ���� ���ۿ� �����ϸ� ���� (�ߺ� Ǫ�� ����)
    if (LSendBufferChunk == buffer)
        return;

    std::lock_guard<std::mutex> lock(_lock);
    _sendBufferChunks.push_back(buffer);
}

// ûũ �Ҹ� �� �ڵ����� ȣ��Ǵ� ���� �Լ� (����� ���� �Ҹ���)
void SendBufferManager::PushGlobal(SendBufferChunk* buffer)
{
    GSendBufferManager->Push(std::shared_ptr<SendBufferChunk>(buffer, PushGlobal));
}