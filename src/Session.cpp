#include "Session.h"

#include "Exception.h"
#include "Messages.h"
#include "Fields.h"

#include <openfix/Utils.h>

#include <strings.h>

Session::Session(SessionSettings settings, std::shared_ptr<IFIXLogger>& logger, std::shared_ptr<IFIXStore>& store)
    : m_settings(settings)
    , m_logger(logger->createLogger(settings))
    , m_store(store->createStore(settings))
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

void Session::processMessage(const std::string& text)
{
    auto msg = m_dictionary->parse(m_settings, text);
    auto msgType = msg.getHeader().getField(FIELD::MsgType);


}

void Session::send(const Message& msg)
{

}

void Session::runUpdate()
{

}

bool Session::load()
{
    try {
        // load data from store
        auto data = m_store.load();
        // insert data into cache
        m_cache->load(data);
        return true;
    } catch (...) {
        return false;
    }
}
