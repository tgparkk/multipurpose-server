#pragma once
class Session : public std::enable_shared_from_this<Session>
{
public:
	Session(asio::ip::tcp::socket socket);
	~Session();

protected:
	virtual void OnReceive(const char* buffer, size_t length) = 0;
public:
	void Send(const char* buffer, size_t length);
	void Start();

public:
	virtual void DoRead();
	virtual void DoWrite();

private:
	asio::ip::tcp::socket m_socket;
	std::vector<char> m_receiveBuffer;
	std::vector<char> m_sendBuffer;
};

