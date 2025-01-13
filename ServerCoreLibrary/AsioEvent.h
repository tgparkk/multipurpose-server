#pragma once

class AsioObject;

enum class EventType : uint8_t
{
    Connect,
    Disconnect,
    Accept,
    Recv,
    Send
};

class AsioEvent
{
public:
    AsioEvent(EventType type) : eventType(type) {}
    virtual ~AsioEvent() = default;

    EventType getEventType() const { return eventType; }

    void SetOwner(std::shared_ptr<AsioObject> owner) { _owner = owner; }
    std::shared_ptr<AsioObject> GetOwner() { return _owner; }

private:
    EventType eventType;
    std::shared_ptr<AsioObject> _owner;
};


class ConnectEvent : public AsioEvent
{
public:
    ConnectEvent() : AsioEvent(EventType::Connect) {}
};

class DisconnectEvent : public AsioEvent
{
public:
    DisconnectEvent() : AsioEvent(EventType::Disconnect) {}
};

class AcceptEvent : public AsioEvent
{
public:
    AcceptEvent() : AsioEvent(EventType::Accept) {}
    std::shared_ptr<class Session> session;
};

class RecvEvent : public AsioEvent
{
public:
    RecvEvent() : AsioEvent(EventType::Recv) {}
};

class SendEvent : public AsioEvent
{
public:
    SendEvent() : AsioEvent(EventType::Send) {}
    std::vector<char> sendBuffers;
};