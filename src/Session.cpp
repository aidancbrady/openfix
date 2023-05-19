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
    , m_state(SessionState::LOGON)
{
    m_dictionary = DictionaryRegistry::instance().load(settings.getString(SessionSettings::FIX_DICTIONARY));

    m_heartbeatInterval = settings.getLong(SessionSettings::HEARTBEAT_INTERVAL);
    m_logonInterval = settings.getLong(SessionSettings::LOGON_INTERVAL);
    m_reconnectInterval = settings.getLong(SessionSettings::RECONNECT_INTERVAL);
    
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
    LOG_DEBUG("Received message: " << text);

    auto msg = m_dictionary->parse(m_settings, text);
    auto msgType = msg.getHeader().getField(FIELD::MsgType);

    if (!validateMessage(msg))
    {
        return;
    }

    auto time = Utils::getEpochMillis();

    if (msgType == MESSAGE::LOGON)
    {
        handleLogon(msg);
    }
    else if (msgType == MESSAGE::LOGOUT)
    {
        if (m_state == SessionState::LOGOUT)
        {
            // TODO log successful logout
            return;
        }

        terminate("Successful logout");
    }
    else if (msgType == MESSAGE::HEARTBEAT)
    {
        if (m_state == SessionState::TEST_REQUEST)
        {
            if (msg.getBody().has(FIELD::TestReqID))
            {
                if (msg.getBody().get(FIELD::TestReqID) == std::to_string(m_testReqID))
                {
                    m_state = SessionState::READY;
                }
            }

            return;
        }

        m_lastRecvHeartbeat = time;
    }
    else if (msgType == MESSAGE::TEST_REQUEST)
    {
        sendHeartbeat(time, msg.getBody().getField(FIELD::TestReqID));
    }
}

void Session::send(Message& msg)
{
    populateMessage(msg);
}

void Session::runUpdate()
{
    long time = Utils::getEpochMillis();

    if (m_state == SessionState::LOGON && (time - m_lastLogon) >= m_logonInterval)
    {
        sendLogon();
        return;
    }

    if (m_state == SessionState::TEST_REQUEST)
    {
        if ((time - m_lastRecvHeartbeat) >= m_heartbeatInterval)
        {
            terminate("Failed to respond to test request " << m_testReqID << " within heartbeat interval");
        }

        return;
    }

    if (m_state == SessionState::LOGOUT)
    {
        if ((time - m_logoutTime) >= 2 * m_heartbeatInterval)
        {
            // TODO error, did not receive logout
            terminate();
        }
    }

    if (m_state != SessionState::LOGON && m_state != SessionState::LOGOUT)
    {
        if ((time - m_lastSentHeartbeat) >= m_heartbeatInterval)
        {
            sendHeartbeat(time);
        }

        long heartbeatTimeout = static_cast<long>(m_settings.getDouble(SessionSettings::TEST_REQUEST_THRESHOLD) * m_heartbeatInterval);
        if ((time - m_lastRecvHeartbeat) >= heartbeatTimeout)
        {
            sendTestRequest();
        }
    }
}

void Session::handleLogon(const Message& msg)
{
    if (m_sessionType == SessionType::ACCEPTOR)
    {
        m_heartbeatInterval = msg.getBody().getField(FIELD::HeartBtInt);
    }
}

void Session::sendLogon()
{
    Message msg;
    msg.getHeader().setField(FIELD::MsgType, MESSAGE::LOGON);
    msg.getBody().setField(FIELD::HeartBtInt, std::to_string(m_heartbeatInterval));
    msg.getBody().setField(FIELD::EncryptMethod, "0");
    send(msg);
}

void Session::sendLogout()
{
    Message msg;
    msg.getHeader().setField(FIELD::MsgType, MESSAGE::LOGOUT);
    send(msg);
}

void Session::sendHeartbeat(long time, std::string testReqID)
{
    Message msg;
    msg.getHeader().setField(FIELD::MsgType, MESSAGE::HEARTBEAT);
    if (!testReqID.empty())
        msg.getBody().setField(FIELD::TestReqID, testReqID);
    send(msg);
}

void Session::sendTestRequest()
{
    Message msg;
    msg.getHeader().setField(FIELD::MsgType, MESSAGE::TEST_REQUEST);
    msg.getBody().setField(FIELD::TestReqID, std::to_string(++m_testReqID));
    send(msg);
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

void Session::populateMessage(Message& msg)
{
    msg.getHeader().setField(FIELD::SenderCompID, m_settings.getString(SessionSettings::SENDER_COMP_ID));
    msg.getHeader().setField(FIELD::TargetCompID, m_settings.getString(SessionSettings::TARGET_COMP_ID));
}

bool Session::validateMessage(Message& msg)
{
    // TODO terminate & logging
    if (msg.getHeader().getField(FIELD::BeginString) != m_settings.getString(SessionSettings::BEGIN_STRING))
        return;
    if (msg.getHeader().getField(FIELD::SenderCompID) != m_settings.getString(SessionSettings::TARGET_COMP_ID))
        return;
    if (msg.getHeader().getField(FIELD::BeginString) != m_settings.getString(SessionSettings::SENDER_COMP_ID))
        return;
}

int Session::getSenderSeqNum()
{
    return m_cache->getSenderSeqNum();
}

int Session::getTargetSeqNum()
{
    return m_cache->getTargetSeqNum();
}

void Session::setSenderSeqNum(int num)
{
    m_cache->setSenderSeqNum(num);
}

void Session::setTargetSeqNum(int num)
{
    m_cache->setTargetSeqNum(num);
}
