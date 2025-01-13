#pragma once

class AsioObject :public std::enable_shared_from_this<AsioObject>
{
public:
    virtual void OnDispatch(const std::error_code& ec, size_t bytesTransferred) = 0;
};


class AsiocCore
{
public:
    AsiocCore()
        : _ioc()
    {
    }

    ~AsiocCore() = default;

    asio::io_context& GetIoContext() { return _ioc; }
    void Stop() { _ioc.stop(); }
    void Reset() { _ioc.restart(); }
    void Run() { _ioc.run(); }

private:
    asio::io_context _ioc;
};