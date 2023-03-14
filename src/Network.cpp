#include "Network.h"

#include <sys/epoll.h>
#include <cerrno>
#include <iostream>

Network::Network() : m_epollFD(0)
{

}

void Network::init()
{
    m_epollFD = epoll_create1(0);
    if (m_epollFD == -1)
    {
        if (errno == EMFILE)
        {
            throw std::runtime_error("Couldn't initialize epoll, process FD limit reached");
        }
        else if (errno == ENFILE)
        {
            throw std::runtime_error("Couldn't initialize epoll, system FD limit reached");
        }
        else if (errno == ENOMEM)
        {
            throw std::runtime_error("Couldn't initialize epoll, system memory limit reached");
        }
    }

    std::cout << m_epollFD << std::endl;
}

void Network::start()
{

}