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
    // 1. 연결 상태 확인
    if (!IsConnected())
        return;

    bool registerSend = false;
    {
        // 2. 전송 큐에 버퍼 추가 (스레드 안전을 위한 락 사용)
        std::lock_guard<std::mutex> lock(_sendLock);
        _sendQueue.push(sendBuffer);

        // 3. 전송 진행 중이 아니면 전송 등록 필요
        if (_sendRegistered.exchange(true) == false)
            registerSend = true;
    }

    // 4. 필요시 전송 등록
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
    // 1. 연결 상태 확인
    if (!IsConnected())
        return;

    // 2. 수신 버퍼 준비
    BYTE* buffer = _recvBuffer.WritePos();  // 데이터를 쓸 위치
    int32_t len = _recvBuffer.FreeSize();   // 쓸 수 있는 공간


/*
    GetSocket().async_read_some(
    asio::buffer(buffer, len),
    [...콜백함수...]
    );
*/

    // 3. 비동기 수신 등록
    GetSocket().async_read_some(
        asio::buffer(buffer, len),
        [this](const std::error_code& error, size_t bytesTransferred)
        {
            // 4. 수신 성공 시
            if (!error)
            {
                // 5. 버퍼에 쓰기 처리
                if (_recvBuffer.OnWrite(bytesTransferred))
                {
                    // 6. 데이터 처리
                    int32_t dataSize = _recvBuffer.DataSize();
                    int32_t processLen = OnRecv(_recvBuffer.ReadPos(), dataSize);

                    // 7. 처리 결과 확인
                    if (processLen < 0 || dataSize < processLen || !_recvBuffer.OnRead(processLen))
                    {
                        Disconnect("Read Overflow");
                        return;
                    }

                    // 8. 버퍼 정리 및 다시 수신 등록
                    _recvBuffer.Clean();
                    RegisterRecv();
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
    // 1. 연결 상태 확인
    if (!IsConnected())
        return;

    // 2. 전송할 데이터 준비
    std::vector<asio::const_buffer> sendBuffers;
    std::vector<std::shared_ptr<SendBuffer>> pendingBuffers;
    {
        std::lock_guard<std::mutex> lock(_sendLock);
        while (!_sendQueue.empty()) {
            auto buffer = _sendQueue.front(); // shared_ptr 복사 (참조 카운트 증가)
            _sendQueue.pop(); // 큐에서 제거 (참조 카운트 감소할 수 있음)

            pendingBuffers.push_back(buffer);  // 참조 유지용 // 다시 복사하여 저장 (참조 카운트 증가)
            sendBuffers.push_back(
                asio::buffer(buffer->Buffer(), buffer->WriteSize())
            );
        }
    }

    // 3. 전송할 데이터가 없으면 상태 변경 후 종료
    if (sendBuffers.empty()) {
        _sendRegistered.store(false);
        return;
    }

    // 4. 비동기 전송 등록
    auto self = shared_from_this();  // 수명 관리
    _socket.async_write_some(
        sendBuffers,
        // pendingBuffers가 콜백에 캡처됨 (참조 카운트 유지)
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

    // 버퍼에 있는 모든 완전한 패킷 처리
    while (true)
    {
        // 1. 최소 패킷 헤더 크기 확인
        int32_t dataSize = len - processLen;
        if (dataSize < sizeof(PacketHeader))
            break;

        // 2. 패킷 헤더 추출
        PacketHeader* header = reinterpret_cast<PacketHeader*>(&buffer[processLen]);

        // 3. 완전한 패킷인지 확인
        if (dataSize < header->size)
            break;

        // 4. 패킷 처리
        OnRecvPacket(&buffer[processLen], header->size);

        // 5. 처리된 길이 업데이트
        processLen += header->size;
    }

    return processLen;  // 처리된 총 길이 반환
}