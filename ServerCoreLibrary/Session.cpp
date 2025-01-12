#include "pch.h"
#include "Session.h"
#include "Service.h"
#include <iostream>

Session::Session(asio::io_context& ioc)
    : _socket(ioc)
    , _receiveBuffer(BUFFER_SIZE)
{
}

Session::~Session()
{
    Disconnect(L"Destructor called");
}

void Session::Send(SendBufferRef sendBuffer)
{
    if (!IsConnected())
        return;

    bool registerSend = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _sendQueue.push(sendBuffer);

        if (!_sendRegistered.exchange(true))
            registerSend = true;
    }

    if (registerSend)
        StartSend();
}

bool Session::Connect(const std::string& host, unsigned short port)
{
    tcp::resolver resolver(_socket.get_executor());
    auto endpoints = resolver.resolve(host, std::to_string(port));

    asio::async_connect(_socket, endpoints,
        [this](const std::error_code& error, const tcp::endpoint& endpoint) {
            HandleConnect(error);
        });

    return true;
}

void Session::HandleConnect(const std::error_code& error)
{
    if (!error) {
        _connected.store(true);
        _endpoint = _socket.remote_endpoint();

        // Add session to service
        if (auto service = _service.lock())
            service->AddSession(shared_from_this());

        OnConnected();
        StartReceive();
    }
    else {
        HandleError(error);
    }
}

void Session::Disconnect(const std::wstring& cause)
{
    if (!_connected.exchange(false))
        return;

    std::wcout << L"Disconnect: " << cause << std::endl;

    std::error_code ec;
    _socket.shutdown(tcp::socket::shutdown_both, ec);
    _socket.close(ec);

    OnDisconnected();

    if (auto service = _service.lock())
        service->ReleaseSession(shared_from_this());
}

void Session::StartReceive()
{
    if (!IsConnected())
        return;

    auto self(shared_from_this());
    _socket.async_read_some(
        asio::buffer(_receiveBuffer.data() + _writePos, _receiveBuffer.size() - _writePos),
        [this, self](const std::error_code& error, size_t bytes_transferred) {
            HandleReceive(error, bytes_transferred);
        });
}

void Session::HandleReceive(const std::error_code& error, size_t bytes_transferred)
{
    if (error) {
        HandleError(error);
        return;
    }

    if (bytes_transferred == 0) {
        Disconnect(L"Recv 0");
        return;
    }

    _writePos += bytes_transferred;

    int32_t processLen = OnRecv(_receiveBuffer.data() + _readPos, _writePos - _readPos);
    if (processLen < 0 || (_writePos - _readPos) < static_cast<size_t>(processLen)) {
        Disconnect(L"OnRead Overflow");
        return;
    }

    _readPos += processLen;

    // Buffer cleanup
    if (_readPos == _writePos) {
        _readPos = _writePos = 0;
    }
    else if (_readPos > 0) {
        std::memmove(_receiveBuffer.data(), _receiveBuffer.data() + _readPos, _writePos - _readPos);
        _writePos -= _readPos;
        _readPos = 0;
    }

    StartReceive();
}

void Session::StartSend()
{
    if (!IsConnected())
        return;

    std::vector<asio::const_buffer> buffers;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        while (!_sendQueue.empty()) {
        //    SendBufferRef sendBuffer = _sendQueue.front();
       //     buffers.push_back(asio::buffer(sendBuffer->Buffer(), sendBuffer->WriteSize()));
            _sendQueue.pop();
        }
    }

    if (buffers.empty()) {
        _sendRegistered.store(false);
        return;
    }

    auto self(shared_from_this());
    asio::async_write(_socket, buffers,
        [this, self](const std::error_code& error, size_t bytes_transferred) {
            HandleSend(error, bytes_transferred);
        });
}

void Session::HandleSend(const std::error_code& error, size_t bytes_transferred)
{
    if (error) {
        HandleError(error);
        return;
    }

    if (bytes_transferred == 0) {
        Disconnect(L"Send 0");
        return;
    }

    OnSend(bytes_transferred);

    std::lock_guard<std::mutex> lock(_mutex);
    if (_sendQueue.empty())
        _sendRegistered.store(false);
    else
        StartSend();
}

void Session::HandleError(const std::error_code& error)
{
    if (error == asio::error::eof ||
        error == asio::error::connection_reset ||
        error == asio::error::connection_aborted) {
        Disconnect(L"Connection closed by peer");
    }
    else {
        std::cout << "Handle Error: " << error.message() << std::endl;
    }
}