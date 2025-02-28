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

class ClientSession : public FilePacketSession
{
public:
    ClientSession(asio::io_context& ioc)
        : FilePacketSession(ioc)
        , _timer(ioc)
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

        // 4. ���� �ݱ� (���� ���� ũ�� ����)
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

private:
    asio::steady_timer _timer;
};

// ����� ��ɾ� ó�� �Լ�
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