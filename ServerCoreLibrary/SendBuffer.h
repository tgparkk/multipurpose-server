#pragma once
class SendBufferChunk;

/*----------------
    SendBuffer
-----------------*/
class SendBuffer
{
public:
    SendBuffer(std::shared_ptr<SendBufferChunk> owner, BYTE* buffer, uint32_t allocSize);
    ~SendBuffer() = default;

    BYTE* Buffer() { return _buffer; }
    uint32_t        AllocSize() const { return _allocSize; }
    uint32_t        WriteSize() const { return _writeSize; }
    void            Close(uint32_t writeSize);

private:
    BYTE* _buffer;
    uint32_t        _allocSize = 0;
    uint32_t        _writeSize = 0;
    std::shared_ptr<SendBufferChunk> _owner;
};

/*--------------------
    SendBufferChunk
--------------------*/
class SendBufferChunk : public std::enable_shared_from_this<SendBufferChunk>
{
public:
    enum { SEND_BUFFER_CHUNK_SIZE = 6000 };

public:
    SendBufferChunk();
    ~SendBufferChunk() = default;

    void                        Reset();
    std::shared_ptr<SendBuffer> Open(uint32_t allocSize);
    void                        Close(uint32_t writeSize);

    bool                        IsOpen() const { return _open; }
    BYTE* Buffer() { return &_buffer[_usedSize]; }
    uint32_t                    FreeSize() const { return static_cast<uint32_t>(_buffer.size()) - _usedSize; }

private:
    std::vector<BYTE>          _buffer;
    bool                       _open = false;
    uint32_t                   _usedSize = 0;
};

/*---------------------
    SendBufferManager
----------------------*/
class SendBufferManager
{
public:
    std::shared_ptr<SendBuffer> Open(uint32_t size);

private:
    std::shared_ptr<SendBufferChunk> Pop();
    void                        Push(std::shared_ptr<SendBufferChunk> buffer);

    static void                 PushGlobal(SendBufferChunk* buffer);

private:
    std::mutex                  _lock;
    std::vector<std::shared_ptr<SendBufferChunk>> _sendBufferChunks;
};