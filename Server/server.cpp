#include "pch.h"

#include "Session.h"
#include "ThreadManager.h"
#include "CorePch.h"
#include "Service.h"


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

class GameSession : public PacketSession
{
public:
    GameSession(asio::io_context& ioc) : PacketSession(ioc) {}

    virtual void OnConnected() override
    {
        cout << "Client Connected" << endl;
    }

    virtual void OnRecvPacket(BYTE* buffer, int32_t len) override
    {
        PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);

        if (header->id == PKT_C_CHAT)
        {
            ChatData* chatData = reinterpret_cast<ChatData*>(buffer + sizeof(PacketHeader));
            cout << "Client Says: " << chatData->msg << endl;

            // 에코 응답
            SendBufferRef sendBuffer = GSendBufferManager->Open(sizeof(PacketHeader) + sizeof(ChatData));
            PacketHeader* resHeader = reinterpret_cast<PacketHeader*>(sendBuffer->Buffer());
            ChatData* resData = reinterpret_cast<ChatData*>(sendBuffer->Buffer() + sizeof(PacketHeader));

            resHeader->size = sizeof(PacketHeader) + sizeof(ChatData);
            resHeader->id = PKT_S_CHAT;
            strcpy_s(resData->msg, "Server received your message!");

            sendBuffer->Close(resHeader->size);
            Send(sendBuffer);
        }
    }

    void SendChatPacket(const char* msg)
    {
        SendBufferRef sendBuffer = GSendBufferManager->Open(sizeof(PacketHeader) + sizeof(ChatData));
        if (sendBuffer == nullptr)
            return;

        PacketHeader* header = reinterpret_cast<PacketHeader*>(sendBuffer->Buffer());
        ChatData* chatData = reinterpret_cast<ChatData*>(sendBuffer->Buffer() + sizeof(PacketHeader));

        header->size = sizeof(PacketHeader) + sizeof(ChatData);
        header->id = PKT_C_CHAT;
        strcpy_s(chatData->msg, msg);

        sendBuffer->Close(header->size);
        Send(sendBuffer);
    }
};

int main()
{
    asio::io_context ioc;

    auto service = make_shared<ServerService>(
        ioc,
        NetAddress("127.0.0.1", 7777),
        [](asio::io_context& ioc) { return make_shared<GameSession>(ioc); },
        100);

    std::cout << "Server Starting..." << std::endl;
    service->Start();
    std::cout << "Server Started" << std::endl;

    // 서버가 계속 실행되도록 유지
    std::vector<std::thread> threads;
    for (int32_t i = 0; i < 4; i++)
    {
        threads.push_back(std::thread([&ioc]()
            {
                ioc.run();
            }));
    }

    for (auto& t : threads)
        t.join();

    return 0;
}