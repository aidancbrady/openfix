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

void create_link_button(std::ostream& stream, const std::string& text, const std::string& url)
{
    stream << "<form action=\"" << url << "\"><input type=\"submit\" value=\"" << url << "\"/></form>";
}

void AdminWebsite::start()
{
    LOG_INFO("Starting with port: " << m_port);

    CROW_ROUTE(m_website, "/")([this](const crow::request& req, crow::response& res){
        std::ostringstream ostr;
        ostr << "<html><body><h2>openfix control panel</h2>";
        char* session_name = req.url_params.get("session");
        if (session_name) {
            auto it = m_app.m_sessionMap.find(session_name);
            if (it == m_app.m_sessionMap.end()) {
                ostr << "<h3>Session not found: " << session_name << "</h3>" << std::endl;
                ostr << "<br/>" << std::endl;
                ostr << "<a href=\"" << req.url << "\">Return to homepage</a>" << std::endl;
            } else {
                auto& session = it->second;
                ostr << "<h3>" << session_name << "</h3>" << std::endl;
                ostr << "Enabled: " << session->isEnabled() << "<br/>";
                ostr << "Connected: " << session->getNetwork()->isConnected() << "<br/>";
                ostr << "SenderSeqNum: " << session->getSenderSeqNum() << "<br/>";
                ostr << "TargetSeqNum: " << session->getTargetSeqNum() << "<br/>";
                ostr << "<br/>";
                ostr << "<a href=\"" << req.url << "\">Return to homepage</a>" << std::endl;
            }
        } else {
            for (auto& [name, session] : m_app.m_sessionMap) {
                ostr << "<h4><a href=\"" << req.url << "?session=" << name << "\">" << name << "</a></h4>" << std::endl;
            }
        }

        ostr << "</html></body>";
        res.body = ostr.str();
        res.end();
    });
    CROW_ROUTE(m_website, "/update")([this](const crow::request& req, crow::response& res){
        char* session_name = req.url_params.get("session");

        if (session_name) {
            auto it = m_app.m_sessionMap.find(session_name);
            if (it != m_app.m_sessionMap.end()) {
/*
                auto& session = *it->second;

                if (strcasecmp(action, "senderseqnum") == 0) {

                } else if (strcasecmp(action, "targetseqnum") == 0) {

                } else if (strcasecmp(action, "enable") == 0) {

                } else if (strcasecmp(action, "disconnect") == 0) {
                    session.getNetwork()->disconnect();
                }
*/
            }
        }

        if (session_name)
            res.moved(req.url.substr(0, req.url.rfind('/')) + "?session=" + session_name);
        else
            res.moved(req.url.substr(0, req.url.rfind('/')));

        res.end();
    });

    m_thread = std::thread([this](){
        m_website.port(m_port).run();
    });
}
