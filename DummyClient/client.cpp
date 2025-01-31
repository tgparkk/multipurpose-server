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
    char msg[100]; // �޽��� �ִ� 99�� + null
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

            // ���� �޽��� ����
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
        _timer.expires_after(std::chrono::seconds(2)); // 2�ʸ���
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

    // Ŭ���̾�Ʈ �� ����
    const int CLIENT_COUNT = 200;
    vector<shared_ptr<ClientSession>> sessions;
    vector<shared_ptr<ClientService>> services;

    // ���� Ŭ���̾�Ʈ ����
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

        // �� ���� ����
        service->Start();
    }

    // worker ������ ���� (Ŭ���̾�Ʈ ���� ���� ������ ����)
    vector<thread> threads;
    int threadCount = min(CLIENT_COUNT, 4); // �ִ� 4�� ������ ���

    for (int i = 0; i < threadCount; i++)
    {
        threads.push_back(thread([&ioc]()
            {
                ioc.run();
            }));
    }

    // ���� ����
    while (true)
    {
        string cmd;
        getline(cin, cmd);
        if (cmd == "q" || cmd == "Q")
            break;
    }

    // ����
    ioc.stop();
    for (auto& t : threads)
        t.join();

    return 0;
}