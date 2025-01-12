#include "pch.h"
#include "Service.h"
#include <iostream>

Service::Service(size_t threadCount)
    : m_threadCount(threadCount)
{
}

Service::~Service()
{
    Stop();
}

void Service::Start()
{
    for (size_t i = 0; i < m_threadCount; ++i)
    {
        m_workerThreads.emplace_back([this]()
            {
                try
                {
                    m_context.run();
                }
                catch (std::exception& e)
                {
                    std::cerr << "Worker thread exception: "
                        << e.what() << std::endl;
                }
            });
    }
}

void Service::Stop()
{
    m_context.stop();

    for (auto& thread : m_workerThreads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    m_workerThreads.clear();
}