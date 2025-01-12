#pragma once
#include <asio.hpp>

using asio::ip::tcp;

class Service;
class SendBuffer;
using SendBufferRef = std::shared_ptr<SendBuffer>;

class Session : public std::enable_shared_from_this<Session>
{
    friend class Listener;
    friend class Service;

    enum {
        BUFFER_SIZE = 0x10000, // 64KB
    };

public:
    Session(asio::io_context& ioc);
    virtual ~Session();

    // External Interface
    void Send(SendBufferRef sendBuffer);
    bool Connect(const std::string& host, unsigned short port);
    void Disconnect(const std::wstring& cause);
    std::shared_ptr<Service> GetService() { return _service.lock(); }
    void SetService(std::shared_ptr<Service> service) { _service = service; }

    // Info Related
    void SetNetAddress(const tcp::endpoint& endpoint) { _endpoint = endpoint; }
    tcp::endpoint GetAddress() const { return _endpoint; }
    const tcp::socket& GetSocket() const { return _socket; }
    bool IsConnected() const { return _connected; }
    std::shared_ptr<Session> GetSessionRef() { return shared_from_this(); }

protected:
    // Content code override
    virtual void OnConnected() {}
    virtual int32_t OnRecv(uint8_t* buffer, int32_t len) { return len; }
    virtual void OnSend(int32_t len) {}
    virtual void OnDisconnected() {}

private:
    void StartReceive();
    void HandleReceive(const std::error_code& error, size_t bytes_transferred);
    void StartSend();
    void HandleSend(const std::error_code& error, size_t bytes_transferred);
    void HandleConnect(const std::error_code& error);
    void HandleError(const std::error_code& error);

private:
    std::weak_ptr<Service> _service;
    tcp::socket _socket;
    tcp::endpoint _endpoint;
    std::atomic<bool> _connected{ false };

    std::mutex _mutex;
    std::vector<uint8_t> _receiveBuffer;
    std::queue<SendBufferRef> _sendQueue;
    std::atomic<bool> _sendRegistered{ false };
    size_t _writePos{ 0 };
    size_t _readPos{ 0 };
};