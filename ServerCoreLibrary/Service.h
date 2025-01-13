#pragma once
#include "NetAddress.h"
#include "CorePch.h"

class NetAddress;
class Session;
using SessionRef = std::shared_ptr<Session>;
//using SessionFactory = std::function<SessionRef(asio::io_context&)>;
using SessionFactory = std::function<SessionRef(asio::io_context&)>;

enum class ServiceType : uint8_t
{
    Server,
    Client
};

/*-------------
    Service
--------------*/
class Service : public std::enable_shared_from_this<Service>
{
public:
    Service(ServiceType type, asio::io_context& ioc, const NetAddress& address,
        SessionFactory factory, int32_t maxSessionCount = 1);
    virtual ~Service();

    virtual bool Start() = 0;
    bool CanStart() const { return _sessionFactory != nullptr; }
    virtual void CloseService();

    void SetSessionFactory(SessionFactory factory) { _sessionFactory = factory; }

    void Broadcast(std::shared_ptr<class SendBuffer> sendBuffer);
    SessionRef CreateSession();
    void AddSession(SessionRef session);
    void ReleaseSession(SessionRef session);

    ServiceType GetServiceType() const { return _type; }
    const NetAddress& GetNetAddress() const { return _netAddress; }
    int32_t GetCurrentSessionCount() const { return _sessionCount; }
    int32_t GetMaxSessionCount() const { return _maxSessionCount; }
    asio::io_context& GetIOContext() { return _ioc; }

protected:
    asio::io_context& _ioc;
    ServiceType _type;
    NetAddress _netAddress;
    int32_t _maxSessionCount;
    int32_t _sessionCount = 0;
    SessionFactory _sessionFactory;
    std::recursive_mutex _lock;
    std::set<SessionRef> _sessions;
};

/*-----------------
    ClientService
------------------*/
class ClientService : public Service
{
public:
    ClientService(asio::io_context& ioc, const NetAddress& targetAddress,
        SessionFactory factory, int32_t maxSessionCount = 1);
    virtual ~ClientService() = default;

    virtual bool Start() override;
};

/*-----------------
    ServerService
------------------*/

class ServerService : public Service
{
public:
    ServerService(asio::io_context& ioc, const NetAddress& address,
        SessionFactory factory, int32_t maxSessionCount = 1);
    virtual ~ServerService();

    virtual bool Start() override;
    virtual void CloseService() override;

private:
    void StartAccept();
    std::unique_ptr<asio::ip::tcp::acceptor> _acceptor;
};
