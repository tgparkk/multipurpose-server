#pragma once
#include "Session.h"
#include "SendBuffer.h"
#include <fstream>
#include <filesystem>
#include <map>

namespace fs = std::filesystem;

/*----------------
    FileHeader
-----------------*/
struct FileHeader : public PacketHeader
{
    char filename[256];  // �ִ� ���� �̸� ����
    uint64_t fileSize;   // ��ü ���� ũ��
    uint32_t chunksTotal; // �� ûũ ��
};

/*----------------
    FileChunk
-----------------*/
struct FileChunk : public PacketHeader
{
    uint32_t chunkId;    // ûũ ��ȣ
    uint32_t chunkSize;  // ûũ ũ��
    uint8_t isLast;      // ������ ûũ ����
    // �����ʹ� �� ����ü �ڿ� �����
};

enum class FileTransferPacketId : uint16_t
{
    FileTransferRequest = 100,
    FileTransferResponse = 101,
    FileDataChunk = 102,
    FileTransferComplete = 103,
    FileTransferError = 104

};

/*----------------
    FileTransferManager
-----------------*/
class FileTransferManager
{
public:
    // ûũ ũ�⸦ SendBufferChunk::SEND_BUFFER_CHUNK_SIZE ���� �۰� ����
    // SendBuffer.h���� SEND_BUFFER_CHUNK_SIZE�� �Ϲ������� 6000 (�� 6KB)
    // ���⼭�� �����ϰ� 4KB�� ����
    static const uint32_t DEFAULT_CHUNK_SIZE = 4 * 1024;

    struct FileTransferContext
    {
        std::string filePath;
        std::ifstream fileStream;
        uint64_t fileSize;
        uint64_t bytesSent;
        uint32_t chunkSize;
        uint32_t chunksTotal;
        uint32_t chunksSent;
        bool isCompleted;
    };

    FileTransferManager();
    ~FileTransferManager();

    // ���� ���� ���� (�۽��ڿ�)
    bool StartFileSend(std::shared_ptr<Session> session, const std::string& filePath, uint32_t chunkSize = DEFAULT_CHUNK_SIZE);

    // ���� ���� ���� (�����ڿ�)
    bool StartFileReceive(const std::string& targetDir, const FileHeader& header);

    // ���� ûũ ó�� (�����ڿ�)
    bool ProcessFileChunk(const FileChunk& chunk, const void* data);

    // ���� ûũ ���� (�۽��ڿ�)
    bool SendNextChunk(std::shared_ptr<Session> session, uint32_t connectionId);

    // ���� ���
    void CancelTransfer(uint32_t connectionId);

    // ���� �Ϸ� �̺�Ʈ �ݹ� ���
    using TransferCompleteCallback = std::function<void(uint32_t connectionId, bool success, const std::string& filePath)>;
    void SetTransferCompleteCallback(TransferCompleteCallback callback);

private:
    std::shared_ptr<SendBuffer> CreateFileRequestPacket(const std::string& filePath, uint64_t fileSize, uint32_t chunkSize);
    std::shared_ptr<SendBuffer> CreateFileChunkPacket(const void* data, uint32_t chunkSize, uint32_t chunkId, bool isLast);

    std::mutex _lock;
    std::map<uint32_t, FileTransferContext> _transfers; // connectionId -> ���� ���ؽ�Ʈ
    TransferCompleteCallback _transferCompleteCallback;
    uint32_t _nextConnectionId = 1;
};

/*----------------
    FilePacketSession
-----------------*/
class FilePacketSession : public PacketSession
{
public:
    FilePacketSession(asio::io_context& ioc);

    void SetFileReceiveDirectory(const std::string& dir);
    std::shared_ptr<FileTransferManager> GetFileTransferManager() { return _fileTransferManager; }

protected:
    virtual void OnRecvPacket(BYTE* buffer, int32_t len) override;

private:
    void HandleFileRequest(BYTE* buffer);
    void HandleFileResponse(BYTE* buffer);
    void HandleFileChunk(BYTE* buffer);
    void HandleFileComplete(BYTE* buffer);

    std::shared_ptr<FileTransferManager> _fileTransferManager;
    std::string _fileReceiveDirectory = "./received_files";
};