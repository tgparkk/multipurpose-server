#include "pch.h"
#include "Service.h"
#include "Session.h"
#include "Listener.h"

#include "ThreadManager.h"

Service::Service(ServiceType type, asio::io_context& ioc, const NetAddress& address,
    SessionFactory factory, int32_t maxSessionCount)
    : _ioc(ioc)
    , _type(type)
    , _netAddress(address)
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
    std::unique_lock<std::recursive_mutex> lock(_lock);
    for (const auto& session : _sessions)
        //session->Disconnect("Service Close");

    _sessions.clear();
}

void Service::Broadcast(std::shared_ptr<SendBuffer> sendBuffer)
{
    std::unique_lock<std::recursive_mutex> lock(_lock);
    for (const auto& session : _sessions)
        session->Send(sendBuffer);
}

SessionRef Service::CreateSession()
{
    SessionRef session = _sessionFactory(_ioc);
    session->SetService(shared_from_this());
    return session;
}

void Service::AddSession(SessionRef session)
{
    std::unique_lock<std::recursive_mutex> lock(_lock);
    _sessions.insert(session);
    _sessionCount++;
}

void Service::ReleaseSession(SessionRef session)
{
    std::unique_lock<std::recursive_mutex> lock(_lock);
    _sessions.erase(session);
    _sessionCount--;
}

/*-----------------
    ClientService
------------------*/
ClientService::ClientService(asio::io_context& ioc, const NetAddress& targetAddress,
    SessionFactory factory, int32_t maxSessionCount)
    : Service(ServiceType::Client, ioc, targetAddress, factory, maxSessionCount)
{
}

bool ClientService::Start()
{
    if (!CanStart())
        return false;

    for (int32_t i = 0; i < GetMaxSessionCount(); i++)
    {
        SessionRef session = CreateSession();
        if (!session->Connect())
            return false;
    }

    return true;
}

/*-----------------
    ServerService
------------------*/
ServerService::ServerService(asio::io_context& ioc, const NetAddress& address,
    SessionFactory factory, int32_t maxSessionCount)
    : Service(ServiceType::Server, ioc, address, factory, maxSessionCount)
{
}

ServerService::~ServerService()
{
    CloseService();
}

bool ServerService::Start()
{
    if (!CanStart())
        return false;

    std::error_code ec;
    auto endpoint = _netAddress.GetEndpoint();

    _acceptor = std::make_unique<asio::ip::tcp::acceptor>(_ioc, endpoint);
    if (_acceptor->is_open() == false)
        return false;

    StartAccept();
    return true;
}

void ServerService::CloseService()
{
    if (_acceptor)
        _acceptor->close();

    Service::CloseService();
}

void ServerService::StartAccept()
{
    SessionRef session = CreateSession();

    _acceptor->async_accept(
        session->GetSocket(),
        [this, session](const std::error_code& error)
        {
            if (!error)
            {
                session->OnConnected();
                AddSession(session);
            }

            StartAccept(); // Continue accepting
        }
    );
}