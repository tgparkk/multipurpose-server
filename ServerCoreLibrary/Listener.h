#pragma once

class Session;
class Service;
class ServerService;

class Listener : public std::enable_shared_from_this<Listener>
{
public:
    Listener(asio::io_context& ioc, const asio::ip::tcp::endpoint& endpoint);
    ~Listener();

public:
    /* External Interface */
    bool StartAccept(std::shared_ptr<ServerService> service);
    void Stop();

private:
    /* Accept Related */
    void RegisterAccept();
    void HandleAccept(std::shared_ptr<Session> session, const std::error_code& error);

private:
    asio::io_context& _ioContext;
    asio::ip::tcp::acceptor _acceptor;
    std::shared_ptr<ServerService> _service;
    bool _isRunning = false;
};