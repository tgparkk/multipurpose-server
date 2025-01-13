#include "pch.h"
#include "SocketUtils.h"


bool SocketUtils::SetReuseAddress(asio::ip::tcp::socket& socket, bool flag)
{
    std::error_code ec;
    socket.set_option(asio::socket_base::reuse_address(flag), ec);
    return !ec;
}

bool SocketUtils::SetTcpNoDelay(asio::ip::tcp::socket& socket, bool flag)
{
    std::error_code ec;
    socket.set_option(asio::ip::tcp::no_delay(flag), ec);
    return !ec;
}

bool SocketUtils::SetLinger(asio::ip::tcp::socket& socket, bool onoff, int32_t linger_time)
{
    std::error_code ec;
    socket.set_option(asio::socket_base::linger(onoff, linger_time), ec);
    return !ec;
}

bool SocketUtils::SetReceiveBufferSize(asio::ip::tcp::socket& socket, int32_t size)
{
    std::error_code ec;
    socket.set_option(asio::socket_base::receive_buffer_size(size), ec);
    return !ec;
}

bool SocketUtils::SetSendBufferSize(asio::ip::tcp::socket& socket, int32_t size)
{
    std::error_code ec;
    socket.set_option(asio::socket_base::send_buffer_size(size), ec);
    return !ec;
}

bool SocketUtils::SetKeepAlive(asio::ip::tcp::socket& socket, bool flag)
{
    std::error_code ec;
    socket.set_option(asio::socket_base::keep_alive(flag), ec);
    return !ec;
}

bool SocketUtils::IsConnected(const asio::ip::tcp::socket& socket)
{
    return socket.is_open();
}

std::string SocketUtils::GetRemoteAddress(const asio::ip::tcp::socket& socket)
{
    std::error_code ec;
    auto endpoint = socket.remote_endpoint(ec);
    if (ec)
        return "";
    return endpoint.address().to_string();
}

uint16_t SocketUtils::GetRemotePort(const asio::ip::tcp::socket& socket)
{
    std::error_code ec;
    auto endpoint = socket.remote_endpoint(ec);
    if (ec)
        return 0;
    return endpoint.port();
}

std::string SocketUtils::GetLocalAddress(const asio::ip::tcp::socket& socket)
{
    std::error_code ec;
    auto endpoint = socket.local_endpoint(ec);
    if (ec)
        return "";
    return endpoint.address().to_string();
}

uint16_t SocketUtils::GetLocalPort(const asio::ip::tcp::socket& socket)
{
    std::error_code ec;
    auto endpoint = socket.local_endpoint(ec);
    if (ec)
        return 0;
    return endpoint.port();
}

asio::ip::tcp::endpoint SocketUtils::CreateEndpoint(const std::string& address, uint16_t port)
{
    std::error_code ec;
    auto addr = asio::ip::make_address(address, ec);
    if (ec)
        return asio::ip::tcp::endpoint();
    return asio::ip::tcp::endpoint(addr, port);
}

void SocketUtils::Close(asio::ip::tcp::socket& socket)
{
    std::error_code ec;

    // Cancel any asynchronous operations
    socket.cancel(ec);

    // Shutdown both send and receive operations
    if (socket.is_open())
    {
        socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        socket.close(ec);
    }
}

bool SocketUtils::ConfigureBasicOptions(asio::ip::tcp::socket& socket)
{
    return SetTcpNoDelay(socket, true) &&
        SetReuseAddress(socket, true) &&
        SetKeepAlive(socket, true);
}

bool SocketUtils::IsConnectionReset(const std::error_code& ec)
{
    return ec == asio::error::connection_reset;
}

bool SocketUtils::IsConnectionAborted(const std::error_code& ec)
{
    return ec == asio::error::connection_aborted;
}

bool SocketUtils::IsOperationAborted(const std::error_code& ec)
{
    return ec == asio::error::operation_aborted;
}

std::string SocketUtils::GetErrorMessage(const std::error_code& ec)
{
    return ec.message();
}