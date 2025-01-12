#include "pch.h"

#include "Session.h"

class GameSession : public Session {
public:
    GameSession(asio::ip::tcp::socket socket)
        : Session(std::move(socket)) {
    }

protected:
    virtual void OnReceive(const char* buffer, size_t length) override {
        std::string received_msg(buffer, length);
        std::cout << "Server received: " << received_msg << std::endl;

        // 에코 메시지 생성 및 전송
        std::string echo_msg = "echo: " + received_msg;
        std::cout << "Server sending: " << echo_msg << std::endl;
        Send(echo_msg.c_str(), echo_msg.length());
    }
};

// GameServer.h
#include "Service.h"
#include "Listener.h"

class GameServer : public Listener {
public:
    GameServer(Service& service)
        : Listener(service.GetContext()) {
    }

protected:
    virtual std::shared_ptr<Session> CreateSession(asio::ip::tcp::socket socket) override {
        return std::make_shared<GameSession>(std::move(socket));
    }
};

int main() {
    try {
        Service service(4); // 4개의 워커 스레드
        GameServer server(service);

        std::cout << "Server starting..." << std::endl;
        server.Start("127.0.0.1", 7777);
        service.Start();

        std::cout << "Server is running. Press Enter to exit." << std::endl;
        std::getchar();

        service.Stop();
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}