#pragma once

class Session;
//class SendBuffer;
class Listener;

enum class ServiceType : uint8_t
{
	Server,
	Client
};

using SessionFactory = std::function<std::shared_ptr<Session>(asio::io_context&)>;

class Service : public std::enable_shared_from_this<Service>
{
public:
    Service(ServiceType type,
        const asio::ip::tcp::endpoint& endpoint,
        asio::io_context& ioc,
        SessionFactory factory,
        int32_t maxSessionCount = 1);
    virtual ~Service();

    virtual bool Start() = 0;
    bool CanStart() { return _sessionFactory != nullptr; }

    virtual void CloseService();
    void SetSessionFactory(SessionFactory func) { _sessionFactory = func; }

    //void Broadcast(std::shared_ptr<SendBuffer> sendBuffer);
    std::shared_ptr<Session> CreateSession();
    void AddSession(std::shared_ptr<Session> session);
    void ReleaseSession(std::shared_ptr<Session> session);
    int32_t GetCurrentSessionCount() { return _sessionCount; }
    int32_t GetMaxSessionCount() { return _maxSessionCount; }

public:
    ServiceType GetServiceType() const { return _type; }
    asio::ip::tcp::endpoint GetEndpoint() const { return _endpoint; }
    asio::io_context& GetIoContext() { return _ioContext; }

protected:
    std::mutex _mutex;
    ServiceType _type;
    asio::ip::tcp::endpoint _endpoint;
    asio::io_context& _ioContext;

    std::set<std::shared_ptr<Session>> _sessions;
    int32_t _sessionCount = 0;
    int32_t _maxSessionCount = 0;
    SessionFactory _sessionFactory;
};


/*-----------------
    ClientService
------------------*/

class ClientService : public Service
{
public:
    ClientService(const asio::ip::tcp::endpoint& targetEndpoint,
        asio::io_context& ioc,
        SessionFactory factory,
        int32_t maxSessionCount = 1);
    virtual ~ClientService() {}

    virtual bool Start() override;
};

/*-----------------
    ServerService
------------------*/

class ServerService : public Service
{
public:
    ServerService(const asio::ip::tcp::endpoint& endpoint,
        asio::io_context& ioc,
        SessionFactory factory,
        int32_t maxSessionCount = 1);
    virtual ~ServerService() {}

    virtual bool Start() override;
    virtual void CloseService() override;

private:
    std::shared_ptr<Listener> _listener = nullptr;
};