#include "pch.h"
#include "Session.h"
#include "Service.h"
#include "CorePch.h"
#include "FileTransfer.h"
#include "ThreadManager.h"

CoreGlobal Core;

using namespace std;

// 패킷 ID 정의 - FileTransfer.h의 FileTransferPacketId 열거형과 맞추기
enum PacketId
{
    PKT_C_CHAT = 1,
    PKT_S_CHAT = 2,

    // 파일 전송 관련 패킷 ID (FileTransfer.h의 FileTransferPacketId와 일치시킴)
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

class ClientSession : public FilePacketSession
{
public:
    ClientSession(asio::io_context& ioc)
        : FilePacketSession(ioc)
        , _timer(ioc)
    {
        // 파일 수신 디렉토리 설정
        SetFileReceiveDirectory("./client_received_files");

        // 파일 전송 완료 콜백 설정
        GetFileTransferManager()->SetTransferCompleteCallback(
            [this](uint32_t connectionId, bool success, const std::string& filePath) {
                if (success) {
                    cout << "File transfer completed: " << filePath << endl;

                    // 모든 청크 전송 후 완료 메시지 
                    SendChatPacket(("File transfer completed: " + fs::path(filePath).filename().string()).c_str());
                }
                else {
                    cout << "File transfer failed: " << filePath << endl;
                }
            });
    }

    virtual void OnConnected() override
    {
        cout << "Connected to Server" << endl;
    }

    virtual void OnRecvPacket(BYTE* buffer, int32_t len) override
    {
        PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);

        if (header->id == PKT_S_CHAT)
        {
            ChatData* chatData = reinterpret_cast<ChatData*>(buffer + sizeof(PacketHeader));
            cout << "Server Says: " << chatData->msg << endl;
        }
        // 파일 전송 관련 패킷은 부모 클래스(FilePacketSession)에서 처리
        else if (header->id == PKT_FILE_REQUEST ||
            header->id == PKT_FILE_RESPONSE ||
            header->id == PKT_FILE_DATA ||
            header->id == PKT_FILE_COMPLETE ||
            header->id == PKT_FILE_ERROR)
        {
            // 부모 클래스의 OnRecvPacket 호출
            FilePacketSession::OnRecvPacket(buffer, len);
        }
    }

    void SendChatPacket(const char* msg)
    {
        // 1. SendBuffer 할당 요청
        SendBufferRef sendBuffer = GSendBufferManager->Open(sizeof(PacketHeader) + sizeof(ChatData));
        if (sendBuffer == nullptr)
            return;

        // 2. 버퍼에 데이터 구성
        PacketHeader* header = reinterpret_cast<PacketHeader*>(sendBuffer->Buffer());
        ChatData* chatData = reinterpret_cast<ChatData*>(sendBuffer->Buffer() + sizeof(PacketHeader));

        cout << "Client Says: " << msg << endl;

        // 3. 헤더와 데이터 채우기
        header->size = sizeof(PacketHeader) + sizeof(ChatData);
        header->id = PKT_C_CHAT;
        strcpy_s(chatData->msg, msg);

        // 4. 버퍼 닫기 (실제 쓰인 크기 설정)
        sendBuffer->Close(header->size);

        // 5. 전송 요청
        Send(sendBuffer);
    }

    // 파일 전송 시작 메서드
    bool SendFile(const std::string& filePath)
    {
        if (!fs::exists(filePath)) {
            std::cout << "[Client] Error: File does not exist: " << filePath << std::endl;
            return false;
        }

        // 파일 크기 로깅
        std::error_code ec;
        uintmax_t fileSize = fs::file_size(filePath, ec);
        if (ec) {
            std::cout << "[Client] Error reading file size: " << ec.message() << std::endl;
            return false;
        }

        std::cout << "\n[Client] Starting file transfer: " << filePath << std::endl;
        std::cout << "[Client] File size: " << fileSize << " bytes" << std::endl;

        // 전송 시작
        bool result = GetFileTransferManager()->StartFileSend(
            shared_from_this(),
            filePath,
            FileTransferManager::DEFAULT_CHUNK_SIZE
        );

        if (result) {
            std::cout << "[Client] File transfer initiated successfully" << std::endl;

            // 첫 번째 청크 바로 전송 (다음 청크는 이 함수 내에서 자동으로 전송됨)
            if (!GetFileTransferManager()->SendNextChunk(shared_from_this(), 1)) {
                std::cerr << "[Client] Failed to send first chunk" << std::endl;
                return false;
            }
        }
        else {
            std::cerr << "[Client] Failed to initiate file transfer" << std::endl;
        }

        return result;

    }

private:
    asio::steady_timer _timer;
};

// 사용자 명령어 처리 함수
void ProcessUserCommands(shared_ptr<ClientSession> session)
{
    cout << "=== File Transfer Client ===" << endl;
    cout << "Commands:" << endl;
    cout << "  /send <filepath> - Send a file to server" << endl;
    cout << "  /quit - Quit the application" << endl;
    cout << "  <message> - Send a chat message" << endl;
    cout << "=========================" << endl;

    string input;
    while (true)
    {
        getline(cin, input);

        // 종료 명령어
        if (input == "/quit" || input == "q" || input == "Q")
            break;

        // 파일 전송 명령어: /send <filepath>
        else if (input.substr(0, 6) == "/send ")
        {
            string filePath = input.substr(6);
            cout << "Sending file: " << filePath << endl;

            // 파일 전송 시작
            if (!session->SendFile(filePath)) {
                cout << "Failed to start file transfer" << endl;
            }
        }
        // 일반 채팅 메시지
        else
        {
            session->SendChatPacket(input.c_str());
        }
    }
}

int main()
{
    asio::io_context ioc;

    // 클라이언트 세션 생성
    auto session = make_shared<ClientSession>(ioc);
    auto service = make_shared<ClientService>(
        ioc,
        NetAddress("127.0.0.1", 7777),
        [session](asio::io_context& ioc) { return session; },
        1);

    // 서비스 시작
    cout << "Connecting to server..." << endl;
    service->Start();

    // IO 스레드 생성
    GThreadManager->Launch([&ioc]()
        {
            ioc.run();
        });

    // 메인 스레드에서 사용자 입력 처리
    ProcessUserCommands(session);

    // 종료 처리
    ioc.stop();
    GThreadManager->Join();

    return 0;
}