#include "pch.h"
#include "Listener.h"
#include "Session.h"
#include "Service.h"
#include <iostream>

Listener::Listener(asio::io_context& ioc, const asio::ip::tcp::endpoint& endpoint)
    : _ioContext(ioc)
    , _acceptor(ioc, endpoint)
{
    _acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
}

Listener::~Listener()
{
    Stop();
}

bool Listener::StartAccept(std::shared_ptr<ServerService> service)
{
    _service = service;
    if (!_service)
        return false;

    _isRunning = true;

    // Start accepting connections
    RegisterAccept();

    return true;
}

void Listener::Stop()
{
    _isRunning = false;

    std::error_code ec;
    _acceptor.close(ec);

    _service = nullptr;
}

void Listener::RegisterAccept()
{
    if (!_isRunning)
        return;
    /*
    std::shared_ptr<Session> session = _service->CreateSession();
    if (!session)
        return;

    _acceptor.async_accept(
        session->GetSocket(),
        [this, session](const std::error_code& error)
        {
            HandleAccept(session, error);
        });
    */
}

void Listener::HandleAccept(std::shared_ptr<Session> session, const std::error_code& error)
{
    if (!_isRunning)
        return;

    if (!error)
    {
        std::error_code ec;
        auto endpoint = session->GetSocket().remote_endpoint(ec);
        if (!ec)
        {
            session->SetNetAddress(endpoint);
            //session->ProcessConnect();
        }
    }
    else
    {
        std::cerr << "Accept failed: " << error.message() << std::endl;
    }

    RegisterAccept();
}