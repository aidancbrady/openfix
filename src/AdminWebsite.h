#pragma once

#include <openfix/Log.h>

#include <crow.h>

#include <thread>

class Application;

class AdminWebsite
{
public:
    AdminWebsite(Application& application, int port);
    ~AdminWebsite();

    void start();

private:
    Application& m_app;
    int m_port;

    crow::SimpleApp m_website;
    std::thread m_thread;

    CREATE_LOGGER("AdminWebsite");
};
