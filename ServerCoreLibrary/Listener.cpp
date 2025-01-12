#include "pch.h"
#include "Listener.h"
#include "Session.h"
#include <iostream>

Listener::Listener(asio::io_context& context)
    : m_acceptor(context)
    , m_context(context)
{
}

bool Listener::Start(const std::string& address, unsigned short port)
{
    try
    {
        asio::ip::tcp::endpoint endpoint(
            asio::ip::make_address(address), port);

        m_acceptor.open(endpoint.protocol());
        m_acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
        m_acceptor.bind(endpoint);
        m_acceptor.listen();

        DoAccept();
        return true;
    }
    catch (std::exception& e)
    {
        std::cerr << "Listener::Start failed: " << e.what() << std::endl;
        return false;
    }
}

void Listener::Stop()
{
    m_acceptor.close();
}

void Listener::DoAccept()
{
    m_acceptor.async_accept(
        [this](std::error_code ec, asio::ip::tcp::socket socket)
        {
            if (!ec)
            {
                std::shared_ptr<Session> session = CreateSession(std::move(socket));
                if (session)
                {
                    // Start session operations
                    session->DoRead();
                }
            }
            else
            {
                std::cerr << "Accept failed: " << ec.message() << std::endl;
            }

            // Continue accepting
            if (m_acceptor.is_open())
            {
                DoAccept();
            }
        });
}