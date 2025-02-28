#include "pch.h"
#include "RecvBuffer.h"

RecvBuffer::RecvBuffer(int32_t bufferSize) : _bufferSize(bufferSize)
{
    _capacity = bufferSize * BUFFER_COUNT;  // 10배 크기로 버퍼 생성
    _buffer.resize(_capacity);  // 실제 메모리 할당
}

void RecvBuffer::Clean()
{
    int32_t dataSize = DataSize();
    if (dataSize == 0)
    {
        // 데이터가 없으면 위치 초기화
        _readPos = _writePos = 0;
    }
    else
    {
        // 남은 공간이 부족하면 데이터를 앞으로 이동
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
    // 읽을 데이터가 충분한지 확인
    if (numOfBytes > DataSize())
        return false;

    // 읽기 위치 이동
    _readPos += numOfBytes;
    return true;
}

bool RecvBuffer::OnWrite(int32_t numOfBytes)
{
    // 쓸 공간이 충분한지 확인
    if (numOfBytes > FreeSize())
        return false;

    // 쓰기 위치 이동
    _writePos += numOfBytes;
    return true;
}