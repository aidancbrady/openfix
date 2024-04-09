#include "AdminWebsite.h"

#include "Application.h"

AdminWebsite::AdminWebsite(Application& app, int port)
    : m_app(app), m_port(port)
{
    start();
}

AdminWebsite::~AdminWebsite()
{
    m_website.stop();
    m_thread.join();
}

void AdminWebsite::start()
{
    LOG_INFO("Starting with port: " << m_port);

    CROW_ROUTE(m_website, "/")([this](){
        std::ostringstream ostr;
        ostr << "<html><body><h2>openfix control panel</h2>";
        for (auto& [name, session] : m_app.m_sessionMap) {
            ostr << name << std::endl;
        }
        ostr << "</html></body>";
        return ostr.str();
    });

    m_thread = std::thread([this](){
        m_website.port(m_port).run();
    });
}
