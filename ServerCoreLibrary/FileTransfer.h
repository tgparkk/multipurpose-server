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
    char filename[256];  // 최대 파일 이름 길이
    uint64_t fileSize;   // 전체 파일 크기
    uint32_t chunksTotal; // 총 청크 수
};

/*----------------
    FileChunk
-----------------*/
struct FileChunk : public PacketHeader
{
    uint32_t chunkId;    // 청크 번호
    uint32_t chunkSize;  // 청크 크기
    uint8_t isLast;      // 마지막 청크 여부
    // 데이터는 이 구조체 뒤에 따라옴
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
    // 청크 크기를 SendBufferChunk::SEND_BUFFER_CHUNK_SIZE 보다 작게 설정
    // SendBuffer.h에서 SEND_BUFFER_CHUNK_SIZE는 일반적으로 6000 (약 6KB)
    // 여기서는 안전하게 4KB로 설정
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

    // 파일 전송 시작 (송신자용)
    bool StartFileSend(std::shared_ptr<Session> session, const std::string& filePath, uint32_t chunkSize = DEFAULT_CHUNK_SIZE);

    // 파일 수신 시작 (수신자용)
    bool StartFileReceive(const std::string& targetDir, const FileHeader& header);

    // 파일 청크 처리 (수신자용)
    bool ProcessFileChunk(const FileChunk& chunk, const void* data);

    // 다음 청크 전송 (송신자용)
    bool SendNextChunk(std::shared_ptr<Session> session, uint32_t connectionId);

    // 전송 취소
    void CancelTransfer(uint32_t connectionId);

    // 전송 완료 이벤트 콜백 등록
    using TransferCompleteCallback = std::function<void(uint32_t connectionId, bool success, const std::string& filePath)>;
    void SetTransferCompleteCallback(TransferCompleteCallback callback);

private:
    std::shared_ptr<SendBuffer> CreateFileRequestPacket(const std::string& filePath, uint64_t fileSize, uint32_t chunkSize);
    std::shared_ptr<SendBuffer> CreateFileChunkPacket(const void* data, uint32_t chunkSize, uint32_t chunkId, bool isLast);

    std::mutex _lock;
    std::map<uint32_t, FileTransferContext> _transfers; // connectionId -> 전송 컨텍스트
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