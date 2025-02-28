#include "pch.h"
#include "RecvBuffer.h"

RecvBuffer::RecvBuffer(int32_t bufferSize) : _bufferSize(bufferSize)
{
    _capacity = bufferSize * BUFFER_COUNT;  // 10�� ũ��� ���� ����
    _buffer.resize(_capacity);  // ���� �޸� �Ҵ�
}

void RecvBuffer::Clean()
{
    int32_t dataSize = DataSize();
    if (dataSize == 0)
    {
        // �����Ͱ� ������ ��ġ �ʱ�ȭ
        _readPos = _writePos = 0;
    }
    else
    {
        // ���� ������ �����ϸ� �����͸� ������ �̵�
        if (FreeSize() < _bufferSize)
        {
            ::memcpy(&_buffer[0], &_buffer[_readPos], dataSize);
            _readPos = 0;
            _writePos = dataSize;
        }
    }
}

bool RecvBuffer::OnRead(int32_t numOfBytes)
{
    // ���� �����Ͱ� ������� Ȯ��
    if (numOfBytes > DataSize())
        return false;

    // �б� ��ġ �̵�
    _readPos += numOfBytes;
    return true;
}

bool RecvBuffer::OnWrite(int32_t numOfBytes)
{
    // �� ������ ������� Ȯ��
    if (numOfBytes > FreeSize())
        return false;

    // ���� ��ġ �̵�
    _writePos += numOfBytes;
    return true;
}