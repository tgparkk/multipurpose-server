#include "pch.h"
#include "Session.h"
#include <iostream>

Session::Session(asio::ip::tcp::socket socket)
    : m_socket(std::move(socket))
    , m_receiveBuffer(1024)  // 초기 버퍼 크기 설정
    , m_sendBuffer(1024)
{
}

Session::~Session()
{
    if (m_socket.is_open())
    {
        m_socket.close();
    }
}

void Session::Send(const char* buffer, size_t length)
{
    bool wasEmpty = m_sendBuffer.empty();

    // 보낼 데이터를 버퍼에 추가
    m_sendBuffer.insert(m_sendBuffer.end(), buffer, buffer + length);

    // 이전에 버퍼가 비어있었다면 쓰기 시작
    if (wasEmpty)
    {
        DoWrite();
    }
}

void Session::Start()
{
    DoRead();
}

void Session::DoRead()
{
    auto self(shared_from_this());
    m_socket.async_read_some(
        asio::buffer(m_receiveBuffer),
        [this, self](std::error_code ec, std::size_t length)
        {
            if (!ec)
            {
                OnReceive(m_receiveBuffer.data(), length);
                DoRead();  // 계속해서 읽기
            }
            else
            {
                std::cerr << "Read failed: " << ec.message() << std::endl;
                m_socket.close();
            }
        });
}

void Session::DoWrite()
{
    auto self(shared_from_this());
    asio::async_write(
        m_socket,
        asio::buffer(m_sendBuffer),
        [this, self](std::error_code ec, std::size_t length)
        {
            if (!ec)
            {
                Send(m_sendBuffer.data(), length);
                m_sendBuffer.clear();  // 버퍼 클리어
            }
            else
            {
                std::cerr << "Write failed: " << ec.message() << std::endl;
                m_socket.close();
            }
        });
}