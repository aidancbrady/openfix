#include "Session.h"

#include "Exception.h"

#include <strings.h>

Session::Session(SessionSettings settings)
    : m_senderSeqNum(1)
    , m_targetSeqNum(1)
    , m_settings(settings)
{
    m_dictionary = DictionaryRegistry::instance().load(settings.getString(SessionSettings::FIX_DICTIONARY));
    
    std::string tmp = settings.getString(SessionSettings::SESSION_TYPE);
    if (strcasecmp(tmp.c_str(), "initiator") == 0)
        m_sessionType = SessionType::INITIATOR;
    else if (strcasecmp(tmp.c_str(), "acceptor") == 0)
        m_sessionType = SessionType::ACCEPTOR;
    else
        throw MisconfiguredSessionError("Unknown session type: " + tmp);
}

Session::~Session()
{
    stop();
}

void Session::start()
{
    
}

void Session::stop()
{

}

void Session::processMessage(const std::string& msg)
{

}

void Session::runUpdate()
{

}
