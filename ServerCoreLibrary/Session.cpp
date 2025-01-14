#include "pch.h"
#include "Session.h"
#include "Service.h"
#include "SocketUtils.h"
#include <iostream>

Session::Session(asio::io_context& ioc)
    : _socket(ioc)
    , _recvBuffer(BUFFER_SIZE)
{
}

Session::~Session()
{
    Disconnect("Destructor");
}

void Session::Start()
{
    RegisterRecv();
}

void Session::Send(std::shared_ptr<SendBuffer> sendBuffer)
{
    if (!IsConnected())
        return;

    bool registerSend = false;
    {
        std::lock_guard<std::mutex> lock(_sendLock);
        _sendQueue.push(sendBuffer);
        if (_sendRegistered.exchange(true) == false)
            registerSend = true;
    }

    if (registerSend)
        RegisterSend();
}

bool Session::Connect()
{
    if (IsConnected())
        return false;

    if (auto service = GetService())
    {
        const NetAddress& address = service->GetNetAddress();
        _socket.async_connect(
            address.GetEndpoint(),
            [this](const std::error_code& error)
            {
                if (!error)
                {
                    ProcessConnect();
                }
                else
                {
                    HandleError(error);
                }
            });
        return true;
    }
    return false;
}

void Session::Disconnect(const char* cause)
{
    if (_connected.exchange(false) == false)
        return;

    std::error_code ec;
    _socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
    _socket.close(ec);

    OnDisconnected();
    if (auto service = GetService())
        service->ReleaseSession(GetSessionRef());
}

void Session::RegisterRecv()
{
    if (!IsConnected())
        return;

    BYTE* buffer = _recvBuffer.WritePos();
    int32_t len = _recvBuffer.FreeSize();

    GetSocket().async_read_some(
        asio::buffer(buffer, len),
        [this](const std::error_code& error, size_t bytesTransferred)
        {
            if (!error)
            {
                if (_recvBuffer.OnWrite(bytesTransferred))
                {
                    int32_t dataSize = _recvBuffer.DataSize();
                    int32_t processLen = OnRecv(_recvBuffer.ReadPos(), dataSize);
                    if (processLen < 0 || dataSize < processLen || !_recvBuffer.OnRead(processLen))
                    {
                        Disconnect("Read Overflow");
                        return;
                    }

                    _recvBuffer.Clean();
                    RegisterRecv();  // 다음 수신 대기
                }
            }
            else
            {
                Disconnect("RegisterRecv Error");
            }
        });
}

void Session::RegisterSend()
{
    if (!IsConnected())
        return;

    std::vector<asio::const_buffer> sendBuffers;
    {
        std::lock_guard<std::mutex> lock(_sendLock);
        while (!_sendQueue.empty())
        {
            std::shared_ptr<SendBuffer> buffer = _sendQueue.front();
            _sendQueue.pop();
            sendBuffers.push_back(asio::buffer(buffer->Buffer(), buffer->WriteSize()));
        }
    }

    _socket.async_write_some(
        sendBuffers,
        [this](const std::error_code& error, size_t bytesTransferred)
        {
            if (!error)
            {
                ProcessSend(bytesTransferred);
            }
            else
            {
                HandleError(error);
            }
        });
}

void Session::ProcessConnect()
{
    _connected.store(true);
    OnConnected();
    RegisterRecv();  // 이 부분이 반드시 필요합니다
}

void Session::ProcessSend(size_t bytesTransferred)
{
    if (!IsConnected())
        return;

    OnSend(bytesTransferred);

    std::lock_guard<std::mutex> lock(_sendLock);
    if (_sendQueue.empty())
        _sendRegistered.store(false);
    else
        RegisterSend();
}

void Session::HandleError(const std::error_code& error)
{
    if (error == asio::error::operation_aborted ||
        error == asio::error::connection_reset ||
        error == asio::error::connection_aborted)
    {
        Disconnect("Error");
    }
    else
    {
        // Log error
    }
}

/* PacketSession Implementation */
int32_t PacketSession::OnRecv(BYTE* buffer, int32_t len)
{
    int32_t processLen = 0;

    while (true)
    {
        int32_t dataSize = len - processLen;
        if (dataSize < sizeof(PacketHeader))
            break;

        PacketHeader* header = reinterpret_cast<PacketHeader*>(&buffer[processLen]);
        if (dataSize < header->size)
            break;

        OnRecvPacket(&buffer[processLen], header->size);
        processLen += header->size;
    }

    return processLen;
}