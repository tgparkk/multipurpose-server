#include "pch.h"
#include "Session.h"
#include "ThreadManager.h"
#include "CorePch.h"
#include "Service.h"
#include "FileTransfer.h"

CoreGlobal Core;

using namespace std;

// 패킷 ID 정의
enum PacketId
{
    PKT_C_CHAT = 1,
    PKT_S_CHAT = 2,

    // 파일 전송 관련 패킷 ID
    PKT_FILE_REQUEST = static_cast<uint16_t>(FileTransferPacketId::FileTransferRequest),
    PKT_FILE_RESPONSE = static_cast<uint16_t>(FileTransferPacketId::FileTransferResponse),
    PKT_FILE_DATA = static_cast<uint16_t>(FileTransferPacketId::FileDataChunk),
    PKT_FILE_COMPLETE = static_cast<uint16_t>(FileTransferPacketId::FileTransferComplete),
    PKT_FILE_ERROR = static_cast<uint16_t>(FileTransferPacketId::FileTransferError)
};

struct ChatData
{
    char msg[100]; // 메시지 최대 99자 + null
};

class GameSession : public FilePacketSession
{
public:
    GameSession(asio::io_context& ioc)
        : FilePacketSession(ioc)
    {
        // 파일 수신 디렉토리 설정 - 절대 경로 사용
        std::string receiveDir = "./server_received_files";

        // 디렉토리가 존재하지 않으면 생성
        if (!fs::exists(receiveDir)) {
            std::error_code ec;
            fs::create_directories(receiveDir, ec);
            if (ec) {
                std::cerr << "Error creating directory: " << ec.message() << std::endl;
            }
        }

        // 절대 경로로 변환
        std::error_code ec;
        std::string absPath = fs::absolute(receiveDir, ec).string();
        if (ec) {
            std::cerr << "Error getting absolute path: " << ec.message() << std::endl;
            absPath = receiveDir; // 실패 시 상대 경로 사용
        }

        std::cout << "Setting receive directory to: " << absPath << std::endl;
        SetFileReceiveDirectory(absPath);

        // 파일 전송 완료 콜백 설정
        GetFileTransferManager()->SetTransferCompleteCallback(
            [this](uint32_t connectionId, bool success, const std::string& filePath) {
                if (success) {
                    std::cout << "\n===================================" << std::endl;
                    std::cout << "🎉 File transfer completed!" << std::endl;
                    std::cout << "📁 File path: " << filePath << std::endl;

                    // 파일 존재 및 크기 확인
                    if (fs::exists(filePath)) {
                        std::cout << "✅ File exists on disk" << std::endl;
                        std::cout << "📊 File size: " << fs::file_size(filePath) << " bytes" << std::endl;
                    }
                    else {
                        std::cout << "❌ File does not exist on disk!" << std::endl;
                    }
                    std::cout << "===================================" << std::endl;

                    // 파일 전송 완료 알림을 클라이언트에게 보냄
                    SendFileCompleteMessage(filePath);
                }
                else {
                    std::cout << "❌ File transfer failed: " << filePath << std::endl;
                }
            });
    }

    virtual void OnConnected() override
    {
        std::cout << "Client Connected" << std::endl;
    }

    virtual void OnRecvPacket(BYTE* buffer, int32_t len) override
    {
        PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);

        // 중요: 패킷 ID 로깅
        std::cout << "Received packet with ID: " << header->id << ", Size: " << header->size << std::endl;

        // 1. 패킷 데이터 추출
        // 일반 채팅 메시지 처리
        if (header->id == PKT_C_CHAT)
        {
            ChatData* chatData = reinterpret_cast<ChatData*>(buffer + sizeof(PacketHeader));
            std::cout << "Client Says: " << chatData->msg << std::endl;

            // 2. 응답 패킷 생성
            // 에코 응답
            SendBufferRef sendBuffer = GSendBufferManager->Open(sizeof(PacketHeader) + sizeof(ChatData));
            PacketHeader* resHeader = reinterpret_cast<PacketHeader*>(sendBuffer->Buffer());
            ChatData* resData = reinterpret_cast<ChatData*>(sendBuffer->Buffer() + sizeof(PacketHeader));

            // 3. 응답 데이터 구성
            resHeader->size = sizeof(PacketHeader) + sizeof(ChatData);
            resHeader->id = PKT_S_CHAT;
            strcpy_s(resData->msg, "Server received your message!");

            // 4. 버퍼 닫고 전송
            sendBuffer->Close(resHeader->size);
            Send(sendBuffer);
        }
        // 파일 전송 관련 패킷은 부모 클래스(FilePacketSession)에서 처리
        else if (IsFileTransferPacket(header->id))
        {
            std::cout << "Processing file transfer packet" << std::endl;

            // 부모 클래스의 OnRecvPacket 호출
            FilePacketSession::OnRecvPacket(buffer, len);
        }
        else
        {
            std::cout << "Unknown packet type: " << header->id << std::endl;
        }
    }

private:
    bool IsFileTransferPacket(uint16_t packetId)
    {
        // 각 패킷 ID를 명시적으로 확인
        return packetId == static_cast<uint16_t>(FileTransferPacketId::FileTransferRequest) ||
            packetId == static_cast<uint16_t>(FileTransferPacketId::FileTransferResponse) ||
            packetId == static_cast<uint16_t>(FileTransferPacketId::FileDataChunk) ||
            packetId == static_cast<uint16_t>(FileTransferPacketId::FileTransferComplete) ||
            packetId == static_cast<uint16_t>(FileTransferPacketId::FileTransferError);
    }

    void SendFileCompleteMessage(const std::string& filePath)
    {
        // 파일명만 추출
        std::string filename = fs::path(filePath).filename().string();

        // 완료 메시지 구성
        std::string completeMsg = "Server received file: " + filename;

        // 채팅 메시지로 전송
        SendBufferRef sendBuffer = GSendBufferManager->Open(sizeof(PacketHeader) + sizeof(ChatData));
        PacketHeader* resHeader = reinterpret_cast<PacketHeader*>(sendBuffer->Buffer());
        ChatData* resData = reinterpret_cast<ChatData*>(sendBuffer->Buffer() + sizeof(PacketHeader));

        resHeader->size = sizeof(PacketHeader) + sizeof(ChatData);
        resHeader->id = PKT_S_CHAT;
        strcpy_s(resData->msg, completeMsg.c_str());

        sendBuffer->Close(resHeader->size);
        Send(sendBuffer);
    }
};

int main()
{
    // 초기화: 파일 저장 디렉토리 생성 및 테스트
    std::string receiveDir = "./server_received_files";
    std::error_code ec;

    std::cout << "=== File Transfer Server ===" << std::endl;

    // 절대 경로 출력
    std::string absPath = fs::absolute(receiveDir, ec).string();
    if (ec) {
        std::cerr << "Error getting absolute path: " << ec.message() << std::endl;
        absPath = receiveDir;
    }

    std::cout << "Receive directory (absolute): " << absPath << std::endl;

    // 디렉토리 존재 확인 및 생성
    if (!fs::exists(receiveDir)) {
        std::cout << "Creating receive directory..." << std::endl;
        ec.clear();
        fs::create_directories(receiveDir, ec);
        if (ec) {
            std::cerr << "Error creating directory: " << ec.message() << std::endl;
        }
        else {
            std::cout << "Directory created successfully" << std::endl;
        }
    }
    else {
        std::cout << "Receive directory already exists" << std::endl;
    }

    // 파일 쓰기 권한 테스트
    std::string testFilePath = receiveDir + "/test_write.tmp";
    std::ofstream testFile(testFilePath, std::ios::binary);
    if (testFile.is_open()) {
        testFile << "Test write permission";
        testFile.close();
        std::cout << "File write test: SUCCESS" << std::endl;

        // 테스트 파일 삭제
        ec.clear();
        fs::remove(testFilePath, ec);
        if (ec) {
            std::cerr << "Warning: Could not remove test file: " << ec.message() << std::endl;
        }
    }
    else {
        std::cerr << "File write test: FAILED - Cannot write to directory!" << std::endl;
    }

    asio::io_context ioc;

    auto service = make_shared<ServerService>(
        ioc,
        NetAddress("0.0.0.0", 7777),
        [](asio::io_context& ioc) { return make_shared<GameSession>(ioc); },
        100);

    std::cout << "File Transfer Server Starting..." << std::endl;
    service->Start();
    std::cout << "File Transfer Server Started" << std::endl;

    // 서버가 계속 실행되도록 유지
    for (int32_t i = 0; i < 4; i++)
    {
        GThreadManager->Launch([&ioc]()
            {
                ioc.run();
            });
    }

    // 메인 스레드에서 명령어 처리
    std::string cmd;
    while (true)
    {
        std::getline(std::cin, cmd);

        // 종료 명령어
        if (cmd == "q" || cmd == "exit")
            break;

        // 서버 상태 출력
        else if (cmd == "status")
        {
            std::cout << "Connected clients: " << service->GetCurrentSessionCount() << std::endl;

            // 수신된 파일 목록 출력
            std::cout << "\nReceived files in: " << absPath << std::endl;

            try {
                if (fs::exists(receiveDir) && fs::is_directory(receiveDir)) {
                    bool filesFound = false;
                    for (const auto& entry : fs::directory_iterator(receiveDir)) {
                        if (fs::is_regular_file(entry.path())) {
                            std::cout << " - " << entry.path().filename().string()
                                << " (" << fs::file_size(entry.path()) << " bytes)" << std::endl;
                            filesFound = true;
                        }
                    }

                    if (!filesFound) {
                        std::cout << "No files found in the directory" << std::endl;
                    }
                }
                else {
                    std::cout << "Directory does not exist or is not a directory" << std::endl;
                }
            }
            catch (const std::exception& e) {
                std::cerr << "Error listing files: " << e.what() << std::endl;
            }
        }
        // 저장 테스트 명령어
        else if (cmd.substr(0, 5) == "test ")
        {
            std::string testContent = cmd.substr(5);
            std::string testFilePath = receiveDir + "/manual_test.txt";

            std::cout << "Writing test file to: " << testFilePath << std::endl;

            std::ofstream testFile(testFilePath, std::ios::binary);
            if (testFile.is_open()) {
                testFile << testContent;
                testFile.close();

                if (fs::exists(testFilePath)) {
                    std::cout << "Test file created successfully ("
                        << fs::file_size(testFilePath) << " bytes)" << std::endl;
                }
                else {
                    std::cout << "Error: Test file not found after writing" << std::endl;
                }
            }
            else {
                std::cerr << "Error: Could not open file for writing" << std::endl;
            }
        }
    }

    // 종료 처리
    ioc.stop();
    GThreadManager->Join();

    return 0;
}