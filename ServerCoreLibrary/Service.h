#pragma once
class Service
{
public:
	Service(size_t threadCount = 1);
	~Service();
	void Start();
	void Stop();

	asio::io_context& GetContext() { return m_context; }

private:
	asio::io_context m_context;
	std::vector<std::thread> m_workerThreads;
	size_t m_threadCount;
};

