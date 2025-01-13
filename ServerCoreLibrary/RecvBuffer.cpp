#include "pch.h"
#include "RecvBuffer.h"

RecvBuffer::RecvBuffer(int32_t bufferSize) : _bufferSize(bufferSize)
{
    _capacity = bufferSize * BUFFER_COUNT;
    _buffer.resize(_capacity);
}

void RecvBuffer::Clean()
{
    int32_t dataSize = DataSize();
    if (dataSize == 0)
    {
        _readPos = _writePos = 0;
    }
    else
    {
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
    if (numOfBytes > DataSize())
        return false;

    _readPos += numOfBytes;
    return true;
}

bool RecvBuffer::OnWrite(int32_t numOfBytes)
{
    if (numOfBytes > FreeSize())
        return false;

    _writePos += numOfBytes;
    return true;
}