#include "pch.h"

#include "Session.h"
#include "Service.h"

class TestSession : public Session {
public:
    TestSession(asio::ip::tcp::socket socket)
        : Session(std::move(socket)) {
        // ���� ���� ù �޽��� ����
        std::string init_msg = "Hello from client!";
        Send(init_msg.c_str(), init_msg.length());
    }
protected:
    virtual void OnReceive(const char* data, size_t length) override {
        // �����κ��� ���� �޽��� ���
        std::cout << "Client received: " << std::string(data, length) << std::endl;

        // 1�� ��� �� ���� �޽��� ����
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::string msg = "Ping from client!";
        Send(msg.c_str(), msg.length());
    }
};


int main() {
    try {
        Service service(1);
        service.Start();

        auto& context = service.GetContext();
        asio::ip::tcp::socket socket(context);

        // ������ ����
        asio::ip::tcp::resolver resolver(context);
        auto endpoints = resolver.resolve("127.0.0.1", "7777");
        asio::connect(socket, endpoints);

        auto session = std::make_shared<TestSession>(std::move(socket));
        session->Start();  // ���� ����

        // ���� ���� ù �޽��� ����
        std::string init_msg = "Hello from client!";
        std::cout << "Sending initial message: " << init_msg << std::endl;
        session->Send(init_msg.c_str(), init_msg.length());

        std::cout << "Press Enter to exit." << std::endl;
        std::getchar();

        service.Stop();
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}