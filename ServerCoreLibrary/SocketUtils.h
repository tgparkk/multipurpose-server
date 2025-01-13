#pragma once


class SocketUtils
{
public:
    // Socket options
    static bool SetReuseAddress(asio::ip::tcp::socket& socket, bool flag);
    static bool SetTcpNoDelay(asio::ip::tcp::socket& socket, bool flag);
    static bool SetLinger(asio::ip::tcp::socket& socket, bool onoff, int32_t linger_time);
    static bool SetReceiveBufferSize(asio::ip::tcp::socket& socket, int32_t size);
    static bool SetSendBufferSize(asio::ip::tcp::socket& socket, int32_t size);
    static bool SetKeepAlive(asio::ip::tcp::socket& socket, bool flag);

    // Socket state
    static bool IsConnected(const asio::ip::tcp::socket& socket);

    // Address conversion utilities
    static std::string GetRemoteAddress(const asio::ip::tcp::socket& socket);
    static uint16_t GetRemotePort(const asio::ip::tcp::socket& socket);
    static std::string GetLocalAddress(const asio::ip::tcp::socket& socket);
    static uint16_t GetLocalPort(const asio::ip::tcp::socket& socket);

    // Create endpoint
    static asio::ip::tcp::endpoint CreateEndpoint(const std::string& address, uint16_t port);

    // Safe close
    static void Close(asio::ip::tcp::socket& socket);

    // Configure common socket options
    static bool ConfigureBasicOptions(asio::ip::tcp::socket& socket);

    // Error handling utilities
    static bool IsConnectionReset(const std::error_code& ec);
    static bool IsConnectionAborted(const std::error_code& ec);
    static bool IsOperationAborted(const std::error_code& ec);
    static std::string GetErrorMessage(const std::error_code& ec);
};