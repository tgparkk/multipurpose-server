#pragma once

class Session;

class Listener
{
public:
    Listener(asio::io_context& context);
    virtual ~Listener() = default;

    bool Start(const std::string& address, unsigned short port);
    void Stop();

protected:
    virtual std::shared_ptr<Session> 
        CreateSession(asio::ip::tcp::socket socket) = 0;

private:
    void DoAccept();

    asio::ip::tcp::acceptor m_acceptor;
    asio::io_context& m_context;
};

