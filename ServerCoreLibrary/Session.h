#pragma once
#include <asio.hpp>
#include "RecvBuffer.h"
#include "SendBuffer.h"
#include "NetAddress.h"

using asio::ip::tcp;

class RecvBuffer;
class Service;
class SendBuffer;
using SendBufferRef = std::shared_ptr<SendBuffer>;

class Session : public std::enable_shared_from_this<Session>
{
    friend class Service;
    friend class ServerService;

    enum
    {
        BUFFER_SIZE = 0x10000, // 64KB
    };

public:
    Session(asio::io_context& ioc);
    virtual ~Session();

    /* External Interface */
    void                Start();
    void                Send(std::shared_ptr<SendBuffer> sendBuffer);
    bool                Connect();
    void                Disconnect(const char* cause);

    void                SetService(std::shared_ptr<Service> service) { _service = service; }
    std::shared_ptr<Service> GetService() { return _service.lock(); }

    /* Info */
    void                SetNetAddress(NetAddress address) { _netAddress = address; }
    NetAddress          GetAddress() { return _netAddress; }
    asio::ip::tcp::socket& GetSocket() { return _socket; }
    bool                IsConnected() { return _connected; }
    std::shared_ptr<Session> GetSessionRef() { return std::static_pointer_cast<Session>(shared_from_this()); }

protected:
    /* ÄÁÅÙÃ÷ ÄÚµå¿¡¼­ ÀçÁ¤ÀÇ */
    virtual void        OnConnected() {}
    virtual int32_t     OnRecv(BYTE* buffer, int32_t len) { return len; }
    virtual void        OnSend(int32_t len) {}
    virtual void        OnDisconnected() {}

private:
    /* Network Core */
    void                RegisterConnect();
    void                RegisterDisconnect();
    void                RegisterRecv();
    void                RegisterSend();

    void                ProcessConnect();
    void                ProcessDisconnect();
    void                ProcessRecv(size_t bytesTransferred);
    void                ProcessSend(size_t bytesTransferred);

    void                HandleError(const std::error_code& error);

private:
    asio::ip::tcp::socket      _socket;
    NetAddress                 _netAddress;
    std::atomic<bool>          _connected = false;

    std::weak_ptr<Service>     _service;
    RecvBuffer                 _recvBuffer;

    std::mutex                 _sendLock;
    std::queue<std::shared_ptr<SendBuffer>> _sendQueue;
    std::atomic<bool>          _sendRegistered = false;
};

/*-----------------
    PacketSession
------------------*/
struct PacketHeader
{
    uint16_t size;
    uint16_t id;
};

class PacketSession : public Session
{
public:
    PacketSession(asio::io_context& ioc) : Session(ioc) {}
    virtual ~PacketSession() {}

    std::shared_ptr<PacketSession> GetPacketSessionRef()
    {
        return std::static_pointer_cast<PacketSession>(shared_from_this());
    }

protected:
    virtual int32_t OnRecv(BYTE* buffer, int32_t len) sealed;
    virtual void OnRecvPacket(BYTE* buffer, int32_t len) abstract;
};