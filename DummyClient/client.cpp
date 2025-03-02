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

    // 과부하 테스트용 패킷 ID 추가
    PKT_C_STRESS_START = 3,    // 클라이언트가 서버에 과부하 테스트 시작 요청
    PKT_S_STRESS_START = 4,    // 서버가 클라이언트에 과부하 테스트 시작 확인
    PKT_C_STRESS_DATA = 5,     // 클라이언트가 보내는 과부하 테스트 데이터
    PKT_S_STRESS_DATA = 6,     // 서버가 보내는 과부하 테스트 데이터
    PKT_C_STRESS_END = 7,      // 클라이언트가 서버에 과부하 테스트 종료 알림
    PKT_S_STRESS_RESULT = 8,   // 서버가 보내는 과부하 테스트 결과

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

// 과부하 테스트 시작 요청 패킷
struct StressTestStartData
{
    uint32_t messageCount;     // 전송할 메시지 수
    uint32_t messageSize;      // 메시지 크기 (바이트)
    uint32_t intervalMs;       // 전송 간격 (밀리초)
};

// 과부하 테스트 데이터 패킷
struct StressTestData
{
    uint32_t sequenceNumber;   // 메시지 순번
    uint32_t timestamp;        // 전송 시간 (밀리초)
    char data[4000];           // 데이터 버퍼 (가변 크기로 사용)
};

// 과부하 테스트 결과 패킷
struct StressTestResult
{
    uint32_t totalMessages;     // 총 메시지 수
    uint32_t receivedMessages;  // 받은 메시지 수
    uint32_t lostMessages;      // 손실된 메시지 수
    float avgLatencyMs;         // 평균 지연 시간 (밀리초)
    float maxLatencyMs;         // 최대 지연 시간 (밀리초)
    float minLatencyMs;         // 최소 지연 시간 (밀리초)
    float dataRateMBps;         // 데이터 전송률 (MB/s)
};

class ClientSession : public FilePacketSession
{
public:
    ClientSession(asio::io_context& ioc)
        : FilePacketSession(ioc)
        , _timer(ioc)
        , _stressTestActive(false)
        , _stressTestTimer(ioc)
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
        // 과부하 테스트 시작 확인 패킷
        else if (header->id == PKT_S_STRESS_START)
        {
            cout << "Server acknowledged stress test start" << endl;
            _stressTestActive = true;

            // 테스트 설정 불러오기
            _stressTestCurrentSeq = 0;
            StartStressTest();
        }
        // 과부하 테스트 데이터 패킷 (서버에서 온 응답)
        else if (header->id == PKT_S_STRESS_DATA)
        {
            if (!_stressTestActive) return;

            StressTestData* stressData = reinterpret_cast<StressTestData*>(buffer + sizeof(PacketHeader));
            _stressTestReceivedCount++;

            // 현재 시간으로 RTT 계산
            uint32_t currentTimeMs = static_cast<uint32_t>(chrono::duration_cast<chrono::milliseconds>(
                chrono::steady_clock::now().time_since_epoch()).count());
            uint32_t rtt = currentTimeMs - stressData->timestamp;

            // 통계 업데이트
            _totalRtt += rtt;
            _maxRtt = max(_maxRtt, rtt);
            _minRtt = min(_minRtt, rtt);

            // 진행 상황 업데이트 (10% 간격으로)
            uint32_t progress = (_stressTestReceivedCount * 100) / _stressTestConfig.messageCount;
            if (progress % 10 == 0 && progress != _lastReportedProgress) {
                cout << "Stress test progress: " << progress << "% ("
                    << _stressTestReceivedCount << "/" << _stressTestConfig.messageCount
                    << " messages, Avg RTT: " << (_totalRtt / _stressTestReceivedCount) << "ms)" << endl;
                _lastReportedProgress = progress;
            }

            // 모든 메시지를 받았으면 종료
            if (_stressTestReceivedCount >= _stressTestConfig.messageCount) {
                EndStressTest();
            }
        }
        // 과부하 테스트 결과 패킷
        else if (header->id == PKT_S_STRESS_RESULT)
        {
            StressTestResult* result = reinterpret_cast<StressTestResult*>(buffer + sizeof(PacketHeader));

            cout << "\n===== Stress Test Results =====" << endl;
            cout << "Total messages: " << result->totalMessages << endl;
            cout << "Received messages: " << result->receivedMessages << endl;
            cout << "Lost messages: " << result->lostMessages << endl;
            cout << "Average latency: " << result->avgLatencyMs << " ms" << endl;
            cout << "Min latency: " << result->minLatencyMs << " ms" << endl;
            cout << "Max latency: " << result->maxLatencyMs << " ms" << endl;
            cout << "Data rate: " << result->dataRateMBps << " MB/s" << endl;
            cout << "==============================" << endl;

            _stressTestActive = false;
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

        // 4. 버퍼 닫기 (실제 쓴 크기 설정)
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

    // 과부하 테스트 시작 요청
    void RequestStressTest(uint32_t messageCount, uint32_t messageSize, uint32_t intervalMs)
    {
        if (_stressTestActive) {
            cout << "Stress test already in progress" << endl;
            return;
        }

        if (messageSize > 4000) {
            cout << "Message size too large (max 4000 bytes)" << endl;
            return;
        }

        // 테스트 설정 저장
        _stressTestConfig.messageCount = messageCount;
        _stressTestConfig.messageSize = messageSize;
        _stressTestConfig.intervalMs = intervalMs;

        // 통계 초기화
        _stressTestCurrentSeq = 0;
        _stressTestReceivedCount = 0;
        _totalRtt = 0;
        _maxRtt = 0;
        _minRtt = UINT32_MAX;
        _lastReportedProgress = 0;

        // 1. SendBuffer 할당
        SendBufferRef sendBuffer = GSendBufferManager->Open(sizeof(PacketHeader) + sizeof(StressTestStartData));
        if (sendBuffer == nullptr) {
            cout << "Failed to allocate send buffer" << endl;
            return;
        }

        // 2. 버퍼에 데이터 구성
        PacketHeader* header = reinterpret_cast<PacketHeader*>(sendBuffer->Buffer());
        StressTestStartData* startData = reinterpret_cast<StressTestStartData*>(sendBuffer->Buffer() + sizeof(PacketHeader));

        // 3. 헤더와 데이터 채우기
        header->size = sizeof(PacketHeader) + sizeof(StressTestStartData);
        header->id = PKT_C_STRESS_START;
        startData->messageCount = messageCount;
        startData->messageSize = messageSize;
        startData->intervalMs = intervalMs;

        // 4. 버퍼 닫기
        sendBuffer->Close(header->size);

        // 5. 전송
        Send(sendBuffer);

        cout << "Requested stress test: " << messageCount << " messages, "
            << messageSize << " bytes each, " << intervalMs << "ms interval" << endl;
    }

private:
    // 과부하 테스트 데이터 전송
    void StartStressTest()
    {
        // 첫 번째 메시지 전송
        SendStressTestData();
    }

    void SendStressTestData()
    {
        if (!_stressTestActive || _stressTestCurrentSeq >= _stressTestConfig.messageCount) {
            return;
        }

        // 1. SendBuffer 할당
        size_t packetSize = sizeof(PacketHeader) + sizeof(StressTestData);
        SendBufferRef sendBuffer = GSendBufferManager->Open(packetSize);
        if (sendBuffer == nullptr) {
            cout << "Failed to allocate send buffer for stress test data" << endl;
            return;
        }

        // 2. 버퍼에 데이터 구성
        PacketHeader* header = reinterpret_cast<PacketHeader*>(sendBuffer->Buffer());
        StressTestData* stressData = reinterpret_cast<StressTestData*>(sendBuffer->Buffer() + sizeof(PacketHeader));

        // 3. 헤더와 데이터 채우기
        header->size = packetSize;
        header->id = PKT_C_STRESS_DATA;

        stressData->sequenceNumber = ++_stressTestCurrentSeq;
        stressData->timestamp = static_cast<uint32_t>(chrono::duration_cast<chrono::milliseconds>(
            chrono::steady_clock::now().time_since_epoch()).count());

        // 테스트 데이터 채우기
        for (uint32_t i = 0; i < _stressTestConfig.messageSize && i < 4000; ++i) {
            stressData->data[i] = static_cast<char>((i + _stressTestCurrentSeq) % 256);
        }

        // 4. 버퍼 닫기
        sendBuffer->Close(packetSize);

        // 5. 전송
        Send(sendBuffer);

        // 다음 메시지 예약
        if (_stressTestCurrentSeq < _stressTestConfig.messageCount) {
            _stressTestTimer.expires_after(chrono::milliseconds(_stressTestConfig.intervalMs));
            _stressTestTimer.async_wait([this](const std::error_code& ec) {
                if (!ec && _stressTestActive) {
                    SendStressTestData();
                }
                });
        }
    }

    // 과부하 테스트 종료
    void EndStressTest()
    {
        if (!_stressTestActive) return;

        // 종료 패킷 전송
        SendBufferRef sendBuffer = GSendBufferManager->Open(sizeof(PacketHeader));
        if (sendBuffer != nullptr) {
            PacketHeader* header = reinterpret_cast<PacketHeader*>(sendBuffer->Buffer());
            header->size = sizeof(PacketHeader);
            header->id = PKT_C_STRESS_END;
            sendBuffer->Close(header->size);
            Send(sendBuffer);
        }

        cout << "Stress test completed. Waiting for server results..." << endl;
    }

private:
    asio::steady_timer _timer;

    // 과부하 테스트 관련 멤버 변수
    bool _stressTestActive;
    StressTestStartData _stressTestConfig;
    uint32_t _stressTestCurrentSeq;
    uint32_t _stressTestReceivedCount;
    uint32_t _totalRtt;
    uint32_t _maxRtt;
    uint32_t _minRtt;
    uint32_t _lastReportedProgress;
    asio::steady_timer _stressTestTimer;
};

// 사용자 명령어 처리 함수
void ProcessUserCommands(shared_ptr<ClientSession> session)
{
    cout << "=== File Transfer Client ===" << endl;
    cout << "Commands:" << endl;
    cout << "  /send <filepath> - Send a file to server" << endl;
    cout << "  /stress <count> <size> <interval> - Run stress test" << endl;
    cout << "    count: Number of messages to send" << endl;
    cout << "    size: Size of each message in bytes" << endl;
    cout << "    interval: Time between messages in milliseconds" << endl;
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
        // 과부하 테스트 명령어: /stress <count> <size> <interval>
        else if (input.substr(0, 8) == "/stress ")
        {
            string params = input.substr(8);
            stringstream ss(params);
            uint32_t count, size, interval;

            if (ss >> count >> size >> interval) {
                cout << "Starting stress test with " << count << " messages, "
                    << size << " bytes each, " << interval << "ms interval" << endl;

                session->RequestStressTest(count, size, interval);
            }
            else {
                cout << "Invalid stress test parameters. Usage: /stress <count> <size> <interval>" << endl;
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