#include "pch.h"
#include "FileTransfer.h"
#include "CoreGlobal.h"

/*----------------
    FileTransferManager
-----------------*/
FileTransferManager::FileTransferManager()
{
}

FileTransferManager::~FileTransferManager()
{
    // 모든 파일 스트림 정리
    std::lock_guard<std::mutex> guard(_lock);
    for (auto& pair : _transfers)
    {
        if (pair.second.fileStream.is_open())
            pair.second.fileStream.close();
    }
}

bool FileTransferManager::StartFileSend(std::shared_ptr<Session> session, const std::string& filePath, uint32_t chunkSize)
{
    std::error_code ec;
    if (!fs::exists(filePath, ec) || ec)
        return false;

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
        return false;

    // 파일 크기 계산
    uint64_t fileSize = fs::file_size(filePath, ec);
    if (ec)
        return false;

    // 전송 컨텍스트 생성
    uint32_t connectionId;
    {
        std::lock_guard<std::mutex> guard(_lock);
        connectionId = _nextConnectionId++;

        FileTransferContext& context = _transfers[connectionId];
        context.filePath = filePath;
        context.fileStream = std::move(file);
        context.fileSize = fileSize;
        context.bytesSent = 0;
        context.chunkSize = chunkSize;
        context.chunksTotal = static_cast<uint32_t>((fileSize + chunkSize - 1) / chunkSize); // 올림 계산
        context.chunksSent = 0;
        context.isCompleted = false;
    }

    // 파일 전송 요청 패킷 전송
    auto packet = CreateFileRequestPacket(filePath, fileSize, chunkSize);
    session->Send(packet);

    return true;
}

bool FileTransferManager::StartFileReceive(const std::string& targetDir, const FileHeader& header)
{
    // 디버깅 로그
    std::cout << "\n[FileTransfer] Receiving file: " << header.filename << std::endl;
    std::cout << "[FileTransfer] File size: " << header.fileSize << " bytes" << std::endl;
    std::cout << "[FileTransfer] Total chunks: " << header.chunksTotal << std::endl;

    // 파일 경로 구성
    std::string filename(header.filename);
    std::string filePath = targetDir + "/" + filename;

    // 디렉토리 존재 확인 및 생성
    if (!fs::exists(targetDir)) {
        std::cout << "[FileTransfer] Creating directory: " << targetDir << std::endl;
        std::error_code ec;
        fs::create_directories(targetDir, ec);
        if (ec) {
            std::cerr << "[FileTransfer] Error creating directory: " << ec.message() << std::endl;
            return false;
        }
    }

    // 기존 파일이 있다면 백업
    if (fs::exists(filePath)) {
        std::string backupPath = filePath + ".bak";
        std::error_code ec;
        std::cout << "[FileTransfer] Backing up existing file to: " << backupPath << std::endl;
        fs::rename(filePath, backupPath, ec);
        if (ec) {
            std::cerr << "[FileTransfer] Error backing up file: " << ec.message() << std::endl;
            // 계속 진행할 수 있지만 경고는 표시
        }
    }

    // 파일 생성 및 공간 할당
    std::cout << "[FileTransfer] Creating file: " << filePath << std::endl;
    std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "[FileTransfer] Error: Cannot create file: " << filePath << std::endl;
        return false;
    }

    // 파일 크기만큼 공간 미리 할당
    try {
        file.seekp(header.fileSize - 1);
        file.put(0);
        file.flush();
    }
    catch (const std::exception& e) {
        std::cerr << "[FileTransfer] Error pre-allocating file space: " << e.what() << std::endl;
        file.close();
        return false;
    }

    if (!file.good()) {
        std::cerr << "[FileTransfer] Error writing to file" << std::endl;
        file.close();
        return false;
    }

    file.close();

    // 전송 컨텍스트 생성
    uint32_t connectionId;
    {
        std::lock_guard<std::mutex> guard(_lock);
        connectionId = _nextConnectionId++;

        FileTransferContext& context = _transfers[connectionId];
        context.filePath = filePath;
        context.fileSize = header.fileSize;
        context.bytesSent = 0;
        context.chunkSize = DEFAULT_CHUNK_SIZE;
        context.chunksTotal = header.chunksTotal;
        context.chunksSent = 0;
        context.isCompleted = false;
    }

    std::cout << "[FileTransfer] Created transfer context with ID: " << connectionId << std::endl;
    std::cout << "[FileTransfer] Target file path: " << filePath << std::endl;

    return true;
}

bool FileTransferManager::ProcessFileChunk(const FileChunk& chunk, const void* data)
{
    // 디버깅 출력
    std::cout << "[FileTransfer] Processing chunk: ID=" << chunk.chunkId
        << ", Size=" << chunk.chunkSize
        << ", IsLast=" << (chunk.isLast ? "Yes" : "No") << std::endl;

    std::lock_guard<std::mutex> guard(_lock);

    if (_transfers.empty()) {
        std::cerr << "[FileTransfer] Error: No active file transfers" << std::endl;
        return false;
    }

    // 가장 최근의 전송 컨텍스트 사용
    auto it = _transfers.rbegin();
    FileTransferContext& context = it->second;

    std::cout << "[FileTransfer] Using transfer context: ID=" << it->first
        << ", FilePath=" << context.filePath << std::endl;

    // 파일 경로 디버그
    std::cout << "[FileTransfer] Full file path: " << fs::absolute(context.filePath).string() << std::endl;

    // 디렉토리 확인 및 생성
    std::string dirPath = fs::path(context.filePath).parent_path().string();
    if (!fs::exists(dirPath)) {
        std::cout << "[FileTransfer] Creating directory: " << dirPath << std::endl;
        std::error_code ec;
        fs::create_directories(dirPath, ec);
        if (ec) {
            std::cerr << "[FileTransfer] Failed to create directory: " << ec.message() << std::endl;
            return false;
        }
    }

    // 파일 열기
    std::ofstream file(context.filePath, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open()) {
        std::cerr << "[FileTransfer] Error: Cannot open file for writing: " << context.filePath << std::endl;

        // 다른 모드로 시도
        file.open(context.filePath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            std::cerr << "[FileTransfer] Error: Still cannot open file after trying trunc mode" << std::endl;

            // 마지막 시도: 다른 이름으로 저장
            std::string altPath = dirPath + "/received_" + std::to_string(std::time(nullptr)) + ".bin";
            file.open(altPath, std::ios::binary | std::ios::trunc);

            if (!file.is_open()) {
                std::cerr << "[FileTransfer] Error: All attempts to open file failed" << std::endl;
                return false;
            }

            std::cout << "[FileTransfer] Opened alternative file path: " << altPath << std::endl;
            context.filePath = altPath;
        }
    }

    // 파일이 정상적으로 열렸다면 크기 확인
    std::error_code ec;
    uintmax_t fileSize = fs::file_size(context.filePath, ec);
    if (!ec) {
        std::cout << "[FileTransfer] Current file size: " << fileSize << " bytes" << std::endl;

        // 파일이 아직 작다면 미리 공간 할당
        if (fileSize < context.fileSize) {
            file.seekp(context.fileSize - 1);
            file.put(0);
            file.flush();
            std::cout << "[FileTransfer] Pre-allocated file space to " << context.fileSize << " bytes" << std::endl;
        }
    }

    // 파일 포인터 이동
    uint64_t offset = static_cast<uint64_t>(chunk.chunkId) * context.chunkSize;
    file.seekp(offset);

    std::cout << "[FileTransfer] Writing " << chunk.chunkSize << " bytes at offset " << offset << std::endl;

    // 데이터 쓰기
    file.write(static_cast<const char*>(data), chunk.chunkSize);

    if (!file.good()) {
        std::cerr << "[FileTransfer] Error: Failed to write data to file" << std::endl;
        file.close();
        return false;
    }

    file.flush();
    file.close();

    // 쓰기 확인
    std::ifstream checkFile(context.filePath, std::ios::binary);
    if (checkFile.is_open()) {
        checkFile.seekg(0, std::ios::end);
        std::streampos fileSize = checkFile.tellg();
        checkFile.close();
        std::cout << "[FileTransfer] File size after write: " << fileSize << " bytes" << std::endl;
    }

    // 전송 상태 업데이트
    context.chunksSent++;
    context.bytesSent += chunk.chunkSize;

    // 진행 상황 출력
    double progressPct = static_cast<double>(context.bytesSent) * 100.0 / context.fileSize;
    std::cout << "[FileTransfer] Progress: " << context.chunksSent << "/" << context.chunksTotal
        << " chunks (" << std::fixed << std::setprecision(2) << progressPct << "%)" << std::endl;

    // 모든 청크를 받았는지 확인
    if (chunk.isLast || context.chunksSent >= context.chunksTotal) {
        context.isCompleted = true;

        // 파일 크기 검증
        std::error_code ec;
        uintmax_t actualSize = fs::file_size(context.filePath, ec);
        if (ec) {
            std::cerr << "[FileTransfer] Error checking final file size: " << ec.message() << std::endl;
        }
        else {
            std::cout << "[FileTransfer] Final file size: " << actualSize << " bytes "
                << "(expected: " << context.fileSize << " bytes)" << std::endl;
        }

        std::cout << "[FileTransfer] File transfer completed: " << context.filePath << std::endl;

        if (_transferCompleteCallback) {
            std::cout << "[FileTransfer] Calling transfer complete callback" << std::endl;
            _transferCompleteCallback(it->first, true, context.filePath);
        }
    }

    return true;
}

// SendNextChunk 함수도 수정이 필요합니다.
bool FileTransferManager::SendNextChunk(std::shared_ptr<Session> session, uint32_t connectionId)
{
    std::lock_guard<std::mutex> guard(_lock);

    auto it = _transfers.find(connectionId);
    if (it == _transfers.end()) {
        // connectionId 1로도 시도해 봄 (첫 번째 전송일 가능성)
        it = _transfers.find(1);
        if (it == _transfers.end()) {
            std::cerr << "Error: No file transfer found with ID " << connectionId << std::endl;
            return false;
        }
    }

    FileTransferContext& context = it->second;

    if (context.isCompleted) {
        std::cout << "File transfer already completed" << std::endl;
        return true;
    }

    if (!context.fileStream.is_open()) {
        // 파일이 닫혀있으면 다시 열기
        context.fileStream.open(context.filePath, std::ios::binary);
        if (!context.fileStream.is_open()) {
            std::cerr << "Error: Cannot open file for reading: " << context.filePath << std::endl;
            return false;
        }
    }

    // 현재 위치로 이동
    context.fileStream.seekg(context.bytesSent);

    // 남은 사이즈 계산
    uint64_t remainingBytes = context.fileSize - context.bytesSent;
    if (remainingBytes == 0) {
        // 전송 완료
        context.isCompleted = true;
        context.fileStream.close();

        std::cout << "File transfer completed (no more bytes to send)" << std::endl;

        if (_transferCompleteCallback)
            _transferCompleteCallback(connectionId, true, context.filePath);

        return true;
    }

    // 이번에 전송할 청크 크기 결정
    // SendBuffer의 최대 크기를 고려하여 청크 크기 제한
    const uint32_t maxChunkSize = SendBufferChunk::SEND_BUFFER_CHUNK_SIZE - sizeof(FileChunk) - 16; // 추가 안전 마진
    uint32_t currentChunkSize = static_cast<uint32_t>(std::min<uint64_t>(remainingBytes, context.chunkSize));
    currentChunkSize = std::min(currentChunkSize, maxChunkSize);

    bool isLastChunk = (remainingBytes <= currentChunkSize);

    // 파일에서 데이터 읽기
    std::vector<char> buffer(currentChunkSize);
    context.fileStream.read(buffer.data(), currentChunkSize);

    if (!context.fileStream.good() && !context.fileStream.eof()) {
        std::cerr << "Error: Failed to read data from file" << std::endl;
        return false;
    }

    // 청크 패킷 생성 및 전송
    auto packet = CreateFileChunkPacket(buffer.data(), currentChunkSize, context.chunksSent, isLastChunk);
    if (!packet) {
        std::cerr << "Error: Failed to create file chunk packet" << std::endl;
        return false;
    }

    session->Send(packet);

    std::cout << "Sent chunk " << context.chunksSent
        << " (" << currentChunkSize << " bytes, "
        << (isLastChunk ? "last chunk" : "more to come") << ")" << std::endl;

    // 상태 업데이트
    context.bytesSent += currentChunkSize;
    context.chunksSent++;

    // 모든 청크를 전송했는지 확인
    if (isLastChunk) {
        context.isCompleted = true;
        context.fileStream.close();

        std::cout << "File transfer completed (last chunk sent)" << std::endl;

        if (_transferCompleteCallback)
            _transferCompleteCallback(connectionId, true, context.filePath);
    }
    else {
        // 다음 청크 전송 예약 (짧은 지연 후)
        std::thread([this, session, connectionId]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            SendNextChunk(session, connectionId);
            }).detach();
    }

    return true;
}

void FileTransferManager::CancelTransfer(uint32_t connectionId)
{
    std::lock_guard<std::mutex> guard(_lock);

    auto it = _transfers.find(connectionId);
    if (it != _transfers.end())
    {
        if (it->second.fileStream.is_open())
            it->second.fileStream.close();

        _transfers.erase(it);
    }
}

void FileTransferManager::SetTransferCompleteCallback(TransferCompleteCallback callback)
{
    _transferCompleteCallback = callback;
}

std::shared_ptr<SendBuffer> FileTransferManager::CreateFileRequestPacket(const std::string& filePath, uint64_t fileSize, uint32_t chunkSize)
{
    // 파일 이름만 추출
    std::string filename = fs::path(filePath).filename().string();

    // 패킷 크기 계산
    uint16_t packetSize = sizeof(FileHeader);

    // SendBuffer 생성
    auto sendBuffer = GSendBufferManager->Open(packetSize);

    // 패킷 구성
    FileHeader* header = reinterpret_cast<FileHeader*>(sendBuffer->Buffer());
    header->size = packetSize;
    header->id = static_cast<uint16_t>(FileTransferPacketId::FileTransferRequest);

    strncpy_s(header->filename, filename.c_str(), filename.length());
    header->filename[filename.length()] = '\0';

    header->fileSize = fileSize;
    header->chunksTotal = static_cast<uint32_t>((fileSize + chunkSize - 1) / chunkSize);

    sendBuffer->Close(packetSize);
    return sendBuffer;
}

std::shared_ptr<SendBuffer> FileTransferManager::CreateFileChunkPacket(const void* data, uint32_t chunkSize, uint32_t chunkId, bool isLast)
{
    // 패킷 크기 계산
    uint16_t packetSize = sizeof(FileChunk) + chunkSize;

    // SendBuffer 최대 크기 확인 (SendBufferChunk::SEND_BUFFER_CHUNK_SIZE보다 작아야 함)
    if (packetSize > SendBufferChunk::SEND_BUFFER_CHUNK_SIZE) {
        std::cerr << "Error: Chunk size too large for SendBuffer. Max allowed: "
            << (SendBufferChunk::SEND_BUFFER_CHUNK_SIZE - sizeof(FileChunk))
            << ", Requested: " << chunkSize << std::endl;
        return nullptr;
    }

    // SendBuffer 생성
    auto sendBuffer = GSendBufferManager->Open(packetSize);
    if (!sendBuffer) {
        std::cerr << "Error: Failed to allocate SendBuffer for file chunk" << std::endl;
        return nullptr;
    }

    // 패킷 구성
    FileChunk* chunk = reinterpret_cast<FileChunk*>(sendBuffer->Buffer());
    chunk->size = packetSize;
    chunk->id = static_cast<uint16_t>(FileTransferPacketId::FileDataChunk);
    chunk->chunkId = chunkId;
    chunk->chunkSize = chunkSize;
    chunk->isLast = isLast ? 1 : 0;

    // 데이터 복사
    memcpy(chunk + 1, data, chunkSize);

    sendBuffer->Close(packetSize);
    return sendBuffer;
}

/*----------------
    FilePacketSession
-----------------*/
FilePacketSession::FilePacketSession(asio::io_context& ioc)
    : PacketSession(ioc)
{
    _fileTransferManager = std::make_shared<FileTransferManager>();
}

void FilePacketSession::SetFileReceiveDirectory(const std::string& dir)
{
    _fileReceiveDirectory = dir;

    // 디렉토리가 존재하지 않으면 생성
    if (!fs::exists(dir))
        fs::create_directories(dir);
}

void FilePacketSession::OnRecvPacket(BYTE* buffer, int32_t len)
{
    PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);
    uint16_t packetId = header->id;

    // 디버깅: 모든 패킷 정보 출력
    std::cout << "[FilePacketSession] Received packet: ID=" << packetId
        << ", Size=" << header->size << std::endl;

    // FileTransferPacketId와 비교 (명시적 캐스팅)
    if (packetId == static_cast<uint16_t>(FileTransferPacketId::FileTransferRequest)) {
        FileHeader* fileHeader = reinterpret_cast<FileHeader*>(buffer);
        std::cout << "[FilePacketSession] File transfer request: " << fileHeader->filename << std::endl;
        HandleFileRequest(buffer);
    }
    else if (packetId == static_cast<uint16_t>(FileTransferPacketId::FileTransferResponse)) {
        std::cout << "[FilePacketSession] File transfer response" << std::endl;
        HandleFileResponse(buffer);
    }
    else if (packetId == static_cast<uint16_t>(FileTransferPacketId::FileDataChunk)) {
        FileChunk* chunk = reinterpret_cast<FileChunk*>(buffer);
        std::cout << "[FilePacketSession] File data chunk: ID=" << chunk->chunkId << std::endl;
        HandleFileChunk(buffer);
    }
    else if (packetId == static_cast<uint16_t>(FileTransferPacketId::FileTransferComplete)) {
        std::cout << "[FilePacketSession] File transfer complete" << std::endl;
        HandleFileComplete(buffer);
    }
    else if (packetId == static_cast<uint16_t>(FileTransferPacketId::FileTransferError)) {
        std::cout << "[FilePacketSession] File transfer error" << std::endl;
        // 오류 처리
    }
    else {
        std::cout << "[FilePacketSession] Unknown packet type: " << packetId << std::endl;
    }
}

//==========================================
// FilePacketSession.cpp의 HandleFileRequest 함수 수정
//==========================================

void FilePacketSession::HandleFileRequest(BYTE* buffer)
{
    FileHeader* header = reinterpret_cast<FileHeader*>(buffer);

    std::cout << "\n[FilePacketSession] File request received: " << header->filename << std::endl;
    std::cout << "[FilePacketSession] File size: " << header->fileSize << " bytes" << std::endl;
    std::cout << "[FilePacketSession] Total chunks: " << header->chunksTotal << std::endl;
    std::cout << "[FilePacketSession] Receive directory: " << _fileReceiveDirectory << std::endl;

    // 파일 수신 시작
    bool result = _fileTransferManager->StartFileReceive(_fileReceiveDirectory, *header);

    if (result) {
        std::cout << "[FilePacketSession] File receive started successfully" << std::endl;
    }
    else {
        std::cerr << "[FilePacketSession] Failed to start file receive" << std::endl;
    }
}


void FilePacketSession::HandleFileResponse(BYTE* buffer)
{
    // TODO: 파일 전송 요청에 대한 응답 처리
}

void FilePacketSession::HandleFileChunk(BYTE* buffer)
{
    FileChunk* chunk = reinterpret_cast<FileChunk*>(buffer);
    void* data = chunk + 1; // 데이터는 헤더 바로 뒤에 위치

    std::cout << "[FilePacketSession] Processing file chunk: ID=" << chunk->chunkId
        << ", Size=" << chunk->chunkSize
        << ", IsLast=" << (chunk->isLast ? "Yes" : "No") << std::endl;

    bool result = _fileTransferManager->ProcessFileChunk(*chunk, data);

    if (!result) {
        std::cerr << "[FilePacketSession] Failed to process file chunk" << std::endl;
    }

    // 마지막 청크면 완료 처리
    if (chunk->isLast) {
        std::cout << "[FilePacketSession] Last chunk received, file transfer complete" << std::endl;
    }
}

void FilePacketSession::HandleFileComplete(BYTE* buffer)
{
    // TODO: 파일 전송 완료 처리
}