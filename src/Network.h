#pragma once

class Network
{
public:
    Network();

    void init();
    void start();

private:
    int m_epollFD;
};