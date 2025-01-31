#include "pch.h"

#include "Session.h"
#include "Service.h"
#include "CorePch.h"

CoreGlobal Core;

using namespace std;

enum PacketId
{
    PKT_C_CHAT = 1,
    PKT_S_CHAT = 2
};

struct ChatData
{
    char msg[100]; // 메시지 최대 99자 + null
};

class ClientSession : public PacketSession
{
    static const vector<string> RANDOM_MESSAGES;

public:
    ClientSession(asio::io_context& ioc)
        : PacketSession(ioc)
        , _timer(ioc)
    {
    }

    virtual void OnConnected() override
    {
        cout << "Connected to Server" << endl;
        ScheduleRandomMessage();
    }

    virtual void OnRecvPacket(BYTE* buffer, int32_t len) override
    {
        PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);

        if (header->id == PKT_S_CHAT)
        {
            ChatData* chatData = reinterpret_cast<ChatData*>(buffer + sizeof(PacketHeader));
            cout << "Server Says: " << chatData->msg << endl;

            // 다음 메시지 예약
            ScheduleRandomMessage();
        }
    }

    void SendChatPacket(const char* msg)
    {
        SendBufferRef sendBuffer = GSendBufferManager->Open(sizeof(PacketHeader) + sizeof(ChatData));
        if (sendBuffer == nullptr)
            return;

        PacketHeader* header = reinterpret_cast<PacketHeader*>(sendBuffer->Buffer());
        ChatData* chatData = reinterpret_cast<ChatData*>(sendBuffer->Buffer() + sizeof(PacketHeader));

        cout << "Client Says: " << msg << endl;

        header->size = sizeof(PacketHeader) + sizeof(ChatData);
        header->id = PKT_C_CHAT;
        strcpy_s(chatData->msg, msg);

        sendBuffer->Close(header->size);
        Send(sendBuffer);
    }

private:
    void ScheduleRandomMessage()
    {
        _timer.expires_after(std::chrono::seconds(2)); // 2초마다
        _timer.async_wait([this](const std::error_code& error)
            {
                if (!error)
                {
                    int index = rand() % RANDOM_MESSAGES.size();
                    SendChatPacket(RANDOM_MESSAGES[index].c_str());
                }
            });
    }

private:
    asio::steady_timer _timer;
};

const vector<string> ClientSession::RANDOM_MESSAGES = {
    "Hello from Client!--------------------------------------------------------",
    "How are you doing?",
    "Nice weather today!",
    "I'm a happy client!",
    "This is an automated message",
    "Testing the connection",
    "Ping from client",
    "Another random message",
    "Having fun with networking",
    "Keep the server busy"
};

int main()
{
    srand(static_cast<unsigned>(time(nullptr)));
    asio::io_context ioc;

    // 클라이언트 수 설정
    const int CLIENT_COUNT = 200;
    vector<shared_ptr<ClientSession>> sessions;
    vector<shared_ptr<ClientService>> services;

    // 여러 클라이언트 생성
    for (int i = 0; i < CLIENT_COUNT; i++)
    {
        auto session = make_shared<ClientSession>(ioc);
        auto service = make_shared<ClientService>(
            ioc,
            NetAddress("127.0.0.1", 7777),
            [session](asio::io_context& ioc) { return session; },
            1);

        sessions.push_back(session);
        services.push_back(service);

        // 각 서비스 시작
        service->Start();
    }

    // worker 스레드 생성 (클라이언트 수에 따라 적절히 조정)
    vector<thread> threads;
    int threadCount = min(CLIENT_COUNT, 4); // 최대 4개 스레드 사용

    for (int i = 0; i < threadCount; i++)
    {
        threads.push_back(thread([&ioc]()
            {
                ioc.run();
            }));
    }

    // 메인 루프
    while (true)
    {
        string cmd;
        getline(cin, cmd);
        if (cmd == "q" || cmd == "Q")
            break;
    }

    // 정리
    ioc.stop();
    for (auto& t : threads)
        t.join();

    return 0;
}