#include "pch.h"
#include "Service.h"
#include "Session.h"
#include "Listener.h"

#include "ThreadManager.h"

Service::Service(ServiceType type,
    const asio::ip::tcp::endpoint& endpoint,
    asio::io_context& ioc,
    SessionFactory factory,
    int32_t maxSessionCount)
    : _type(type)
    , _endpoint(endpoint)
    , _ioContext(ioc)
    , _sessionFactory(factory)
    , _maxSessionCount(maxSessionCount)
{
}

Service::~Service()
{
    CloseService();
}

void Service::CloseService()
{
    std::set<std::shared_ptr<Session>> sessions;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        sessions = _sessions;
    }

    for (auto& session : sessions)
        session->Disconnect(L"Service closed");

    sessions.clear();
}

/*
void Service::Broadcast(SendBufferRef sendBuffer)
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (const auto& session : _sessions)
    {
        session->Send(sendBuffer);
    }
}
*/

std::shared_ptr<Session> Service::CreateSession()
{
    std::shared_ptr<Session> session = _sessionFactory(_ioContext);
    session->SetService(shared_from_this());
    return session;
}

void Service::AddSession(std::shared_ptr<Session> session)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _sessionCount++;
    _sessions.insert(session);
}

void Service::ReleaseSession(std::shared_ptr<Session> session)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _sessions.erase(session);
    _sessionCount--;
}

/*-----------------
    ClientService
------------------*/

ClientService::ClientService(const asio::ip::tcp::endpoint& targetEndpoint,
    asio::io_context& ioc,
    SessionFactory factory,
    int32_t maxSessionCount)
    : Service(ServiceType::Client, targetEndpoint, ioc, factory, maxSessionCount)
{
}

bool ClientService::Start()
{
    if (!CanStart())
        return false;

    const int32_t sessionCount = GetMaxSessionCount();
    for (int32_t i = 0; i < sessionCount; i++)
    {
        std::shared_ptr<Session> session = CreateSession();
        if (!session->Connect(_endpoint.address().to_string(), _endpoint.port()))
            return false;
    }

    return true;
}

/*-----------------
    ServerService
------------------*/

ServerService::ServerService(const asio::ip::tcp::endpoint& endpoint,
    asio::io_context& ioc,
    SessionFactory factory,
    int32_t maxSessionCount)
    : Service(ServiceType::Server, endpoint, ioc, factory, maxSessionCount)
{
}

bool ServerService::Start()
{
    if (!CanStart())
        return false;

    _listener = std::make_shared<Listener>(_ioContext, _endpoint);
    if (!_listener)
        return false;

    auto service = std::static_pointer_cast<ServerService>(shared_from_this());
    if (!_listener->StartAccept(service))
        return false;

    return true;
}

void ServerService::CloseService()
{
    if (_listener)
        _listener->Stop();

    Service::CloseService();
}