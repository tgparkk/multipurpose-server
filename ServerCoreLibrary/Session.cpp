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
        // 현재 진행 중인 send 작업이 없을 때만 새로운 send 작업을 등록
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

void Session::Dispatch(EventType type, size_t bytes)
{
    switch (type) {
    case EventType::Recv:
        ProcessRecv(bytes);
        break;
    case EventType::Send:
        ProcessSend(bytes);
        break;
    }
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

    // 보낼 데이터 준비
    std::vector<asio::const_buffer> sendBuffers;
    std::vector<std::shared_ptr<SendBuffer>> pendingBuffers;
    {
        std::lock_guard<std::mutex> lock(_sendLock);
        while (!_sendQueue.empty()) {
            auto buffer = _sendQueue.front();
            _sendQueue.pop();

            pendingBuffers.push_back(buffer);  // 참조 유지를 위해 저장
            sendBuffers.push_back(
                asio::buffer(buffer->Buffer(), buffer->WriteSize())
            );
        }
    }

    // 보낼 데이터가 없다면 종료
    if (sendBuffers.empty()) {
        _sendRegistered.store(false);
        return;
    }

    // 비동기 쓰기 작업 등록
    auto self = shared_from_this();  // 수명 관리
    _socket.async_write_some(
        sendBuffers,
        [this, self, pendingBuffers](const std::error_code& error, size_t bytesTransferred) {
            if (!error) {
                Dispatch(EventType::Send, bytesTransferred);
            }
            else {
                HandleError(error);
            }
        }
    );
}

void Session::ProcessConnect()
{
    _connected.store(true);

    // 세션 등록
    GetService()->AddSession(GetSessionRef());

    // 컨텐츠 코드에서 재정의
    OnConnected();

    // 수신 등록
    RegisterRecv();
}

void Session::ProcessDisconnect()
{


    OnDisconnected(); // 컨텐츠 코드에서 재정의
    GetService()->ReleaseSession(GetSessionRef());
}

void Session::ProcessRecv(size_t bytesTransferred)
{
}

void Session::ProcessSend(size_t bytesTransferred)
{
    if (!IsConnected())
        return;

    if (bytesTransferred == 0) {
        Disconnect("Send bytesTransferred is 0");
        return;
    }

    // 컨텐츠 코드에서 재정의
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