#include "pch.h"
#include "Session.h"
#include "Service.h"
#include "CorePch.h"
#include "FileTransfer.h"
#include "ThreadManager.h"

CoreGlobal Core;

using namespace std;

// ��Ŷ ID ���� - FileTransfer.h�� FileTransferPacketId �������� ���߱�
enum PacketId
{
    PKT_C_CHAT = 1,
    PKT_S_CHAT = 2,

    // ������ �׽�Ʈ�� ��Ŷ ID �߰�
    PKT_C_STRESS_START = 3,    // Ŭ���̾�Ʈ�� ������ ������ �׽�Ʈ ���� ��û
    PKT_S_STRESS_START = 4,    // ������ Ŭ���̾�Ʈ�� ������ �׽�Ʈ ���� Ȯ��
    PKT_C_STRESS_DATA = 5,     // Ŭ���̾�Ʈ�� ������ ������ �׽�Ʈ ������
    PKT_S_STRESS_DATA = 6,     // ������ ������ ������ �׽�Ʈ ������
    PKT_C_STRESS_END = 7,      // Ŭ���̾�Ʈ�� ������ ������ �׽�Ʈ ���� �˸�
    PKT_S_STRESS_RESULT = 8,   // ������ ������ ������ �׽�Ʈ ���

    // ���� ���� ���� ��Ŷ ID (FileTransfer.h�� FileTransferPacketId�� ��ġ��Ŵ)
    PKT_FILE_REQUEST = static_cast<uint16_t>(FileTransferPacketId::FileTransferRequest),
    PKT_FILE_RESPONSE = static_cast<uint16_t>(FileTransferPacketId::FileTransferResponse),
    PKT_FILE_DATA = static_cast<uint16_t>(FileTransferPacketId::FileDataChunk),
    PKT_FILE_COMPLETE = static_cast<uint16_t>(FileTransferPacketId::FileTransferComplete),
    PKT_FILE_ERROR = static_cast<uint16_t>(FileTransferPacketId::FileTransferError)
};

struct ChatData
{
    char msg[100]; // �޽��� �ִ� 99�� + null
};

// ������ �׽�Ʈ ���� ��û ��Ŷ
struct StressTestStartData
{
    uint32_t messageCount;     // ������ �޽��� ��
    uint32_t messageSize;      // �޽��� ũ�� (����Ʈ)
    uint32_t intervalMs;       // ���� ���� (�и���)
};

// ������ �׽�Ʈ ������ ��Ŷ
struct StressTestData
{
    uint32_t sequenceNumber;   // �޽��� ����
    uint32_t timestamp;        // ���� �ð� (�и���)
    char data[4000];           // ������ ���� (���� ũ��� ���)
};

// ������ �׽�Ʈ ��� ��Ŷ
struct StressTestResult
{
    uint32_t totalMessages;     // �� �޽��� ��
    uint32_t receivedMessages;  // ���� �޽��� ��
    uint32_t lostMessages;      // �սǵ� �޽��� ��
    float avgLatencyMs;         // ��� ���� �ð� (�и���)
    float maxLatencyMs;         // �ִ� ���� �ð� (�и���)
    float minLatencyMs;         // �ּ� ���� �ð� (�и���)
    float dataRateMBps;         // ������ ���۷� (MB/s)
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
        // ���� ���� ���丮 ����
        SetFileReceiveDirectory("./client_received_files");

        // ���� ���� �Ϸ� �ݹ� ����
        GetFileTransferManager()->SetTransferCompleteCallback(
            [this](uint32_t connectionId, bool success, const std::string& filePath) {
                if (success) {
                    cout << "File transfer completed: " << filePath << endl;

                    // ��� ûũ ���� �� �Ϸ� �޽��� 
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
        // ������ �׽�Ʈ ���� Ȯ�� ��Ŷ
        else if (header->id == PKT_S_STRESS_START)
        {
            cout << "Server acknowledged stress test start" << endl;
            _stressTestActive = true;

            // �׽�Ʈ ���� �ҷ�����
            _stressTestCurrentSeq = 0;
            StartStressTest();
        }
        // ������ �׽�Ʈ ������ ��Ŷ (�������� �� ����)
        else if (header->id == PKT_S_STRESS_DATA)
        {
            if (!_stressTestActive) return;

            StressTestData* stressData = reinterpret_cast<StressTestData*>(buffer + sizeof(PacketHeader));
            _stressTestReceivedCount++;

            // ���� �ð����� RTT ���
            uint32_t currentTimeMs = static_cast<uint32_t>(chrono::duration_cast<chrono::milliseconds>(
                chrono::steady_clock::now().time_since_epoch()).count());
            uint32_t rtt = currentTimeMs - stressData->timestamp;

            // ��� ������Ʈ
            _totalRtt += rtt;
            _maxRtt = max(_maxRtt, rtt);
            _minRtt = min(_minRtt, rtt);

            // ���� ��Ȳ ������Ʈ (10% ��������)
            uint32_t progress = (_stressTestReceivedCount * 100) / _stressTestConfig.messageCount;
            if (progress % 10 == 0 && progress != _lastReportedProgress) {
                cout << "Stress test progress: " << progress << "% ("
                    << _stressTestReceivedCount << "/" << _stressTestConfig.messageCount
                    << " messages, Avg RTT: " << (_totalRtt / _stressTestReceivedCount) << "ms)" << endl;
                _lastReportedProgress = progress;
            }

            // ��� �޽����� �޾����� ����
            if (_stressTestReceivedCount >= _stressTestConfig.messageCount) {
                EndStressTest();
            }
        }
        // ������ �׽�Ʈ ��� ��Ŷ
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
        // ���� ���� ���� ��Ŷ�� �θ� Ŭ����(FilePacketSession)���� ó��
        else if (header->id == PKT_FILE_REQUEST ||
            header->id == PKT_FILE_RESPONSE ||
            header->id == PKT_FILE_DATA ||
            header->id == PKT_FILE_COMPLETE ||
            header->id == PKT_FILE_ERROR)
        {
            // �θ� Ŭ������ OnRecvPacket ȣ��
            FilePacketSession::OnRecvPacket(buffer, len);
        }
    }

    void SendChatPacket(const char* msg)
    {
        // 1. SendBuffer �Ҵ� ��û
        SendBufferRef sendBuffer = GSendBufferManager->Open(sizeof(PacketHeader) + sizeof(ChatData));
        if (sendBuffer == nullptr)
            return;

        // 2. ���ۿ� ������ ����
        PacketHeader* header = reinterpret_cast<PacketHeader*>(sendBuffer->Buffer());
        ChatData* chatData = reinterpret_cast<ChatData*>(sendBuffer->Buffer() + sizeof(PacketHeader));

        cout << "Client Says: " << msg << endl;

        // 3. ����� ������ ä���
        header->size = sizeof(PacketHeader) + sizeof(ChatData);
        header->id = PKT_C_CHAT;
        strcpy_s(chatData->msg, msg);

        // 4. ���� �ݱ� (���� �� ũ�� ����)
        sendBuffer->Close(header->size);

        // 5. ���� ��û
        Send(sendBuffer);
    }

    // ���� ���� ���� �޼���
    bool SendFile(const std::string& filePath)
    {
        if (!fs::exists(filePath)) {
            std::cout << "[Client] Error: File does not exist: " << filePath << std::endl;
            return false;
        }

        // ���� ũ�� �α�
        std::error_code ec;
        uintmax_t fileSize = fs::file_size(filePath, ec);
        if (ec) {
            std::cout << "[Client] Error reading file size: " << ec.message() << std::endl;
            return false;
        }

        std::cout << "\n[Client] Starting file transfer: " << filePath << std::endl;
        std::cout << "[Client] File size: " << fileSize << " bytes" << std::endl;

        // ���� ����
        bool result = GetFileTransferManager()->StartFileSend(
            shared_from_this(),
            filePath,
            FileTransferManager::DEFAULT_CHUNK_SIZE
        );

        if (result) {
            std::cout << "[Client] File transfer initiated successfully" << std::endl;

            // ù ��° ûũ �ٷ� ���� (���� ûũ�� �� �Լ� ������ �ڵ����� ���۵�)
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

    // ������ �׽�Ʈ ���� ��û
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

        // �׽�Ʈ ���� ����
        _stressTestConfig.messageCount = messageCount;
        _stressTestConfig.messageSize = messageSize;
        _stressTestConfig.intervalMs = intervalMs;

        // ��� �ʱ�ȭ
        _stressTestCurrentSeq = 0;
        _stressTestReceivedCount = 0;
        _totalRtt = 0;
        _maxRtt = 0;
        _minRtt = UINT32_MAX;
        _lastReportedProgress = 0;

        // 1. SendBuffer �Ҵ�
        SendBufferRef sendBuffer = GSendBufferManager->Open(sizeof(PacketHeader) + sizeof(StressTestStartData));
        if (sendBuffer == nullptr) {
            cout << "Failed to allocate send buffer" << endl;
            return;
        }

        // 2. ���ۿ� ������ ����
        PacketHeader* header = reinterpret_cast<PacketHeader*>(sendBuffer->Buffer());
        StressTestStartData* startData = reinterpret_cast<StressTestStartData*>(sendBuffer->Buffer() + sizeof(PacketHeader));

        // 3. ����� ������ ä���
        header->size = sizeof(PacketHeader) + sizeof(StressTestStartData);
        header->id = PKT_C_STRESS_START;
        startData->messageCount = messageCount;
        startData->messageSize = messageSize;
        startData->intervalMs = intervalMs;

        // 4. ���� �ݱ�
        sendBuffer->Close(header->size);

        // 5. ����
        Send(sendBuffer);

        cout << "Requested stress test: " << messageCount << " messages, "
            << messageSize << " bytes each, " << intervalMs << "ms interval" << endl;
    }

private:
    // ������ �׽�Ʈ ������ ����
    void StartStressTest()
    {
        // ù ��° �޽��� ����
        SendStressTestData();
    }

    void SendStressTestData()
    {
        if (!_stressTestActive || _stressTestCurrentSeq >= _stressTestConfig.messageCount) {
            return;
        }

        // 1. SendBuffer �Ҵ�
        size_t packetSize = sizeof(PacketHeader) + sizeof(StressTestData);
        SendBufferRef sendBuffer = GSendBufferManager->Open(packetSize);
        if (sendBuffer == nullptr) {
            cout << "Failed to allocate send buffer for stress test data" << endl;
            return;
        }

        // 2. ���ۿ� ������ ����
        PacketHeader* header = reinterpret_cast<PacketHeader*>(sendBuffer->Buffer());
        StressTestData* stressData = reinterpret_cast<StressTestData*>(sendBuffer->Buffer() + sizeof(PacketHeader));

        // 3. ����� ������ ä���
        header->size = packetSize;
        header->id = PKT_C_STRESS_DATA;

        stressData->sequenceNumber = ++_stressTestCurrentSeq;
        stressData->timestamp = static_cast<uint32_t>(chrono::duration_cast<chrono::milliseconds>(
            chrono::steady_clock::now().time_since_epoch()).count());

        // �׽�Ʈ ������ ä���
        for (uint32_t i = 0; i < _stressTestConfig.messageSize && i < 4000; ++i) {
            stressData->data[i] = static_cast<char>((i + _stressTestCurrentSeq) % 256);
        }

        // 4. ���� �ݱ�
        sendBuffer->Close(packetSize);

        // 5. ����
        Send(sendBuffer);

        // ���� �޽��� ����
        if (_stressTestCurrentSeq < _stressTestConfig.messageCount) {
            _stressTestTimer.expires_after(chrono::milliseconds(_stressTestConfig.intervalMs));
            _stressTestTimer.async_wait([this](const std::error_code& ec) {
                if (!ec && _stressTestActive) {
                    SendStressTestData();
                }
                });
        }
    }

    // ������ �׽�Ʈ ����
    void EndStressTest()
    {
        if (!_stressTestActive) return;

        // ���� ��Ŷ ����
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

    // ������ �׽�Ʈ ���� ��� ����
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

// ����� ��ɾ� ó�� �Լ�
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

        // ���� ��ɾ�
        if (input == "/quit" || input == "q" || input == "Q")
            break;

        // ���� ���� ��ɾ�: /send <filepath>
        else if (input.substr(0, 6) == "/send ")
        {
            string filePath = input.substr(6);
            cout << "Sending file: " << filePath << endl;

            // ���� ���� ����
            if (!session->SendFile(filePath)) {
                cout << "Failed to start file transfer" << endl;
            }
        }
        // ������ �׽�Ʈ ��ɾ�: /stress <count> <size> <interval>
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
        // �Ϲ� ä�� �޽���
        else
        {
            session->SendChatPacket(input.c_str());
        }
    }
}

int main()
{
    asio::io_context ioc;

    // Ŭ���̾�Ʈ ���� ����
    auto session = make_shared<ClientSession>(ioc);
    auto service = make_shared<ClientService>(
        ioc,
        NetAddress("127.0.0.1", 7777),
        [session](asio::io_context& ioc) { return session; },
        1);

    // ���� ����
    cout << "Connecting to server..." << endl;
    service->Start();

    // IO ������ ����
    GThreadManager->Launch([&ioc]()
        {
            ioc.run();
        });

    // ���� �����忡�� ����� �Է� ó��
    ProcessUserCommands(session);

    // ���� ó��
    ioc.stop();
    GThreadManager->Join();

    return 0;
}