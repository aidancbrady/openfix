#include "Session.h"

#include "Exception.h"
#include "Messages.h"
#include "Fields.h"

#include <openfix/Utils.h>

#include <strings.h>

Session::Session(SessionSettings settings, Network& network, std::shared_ptr<IFIXLogger>& logger, std::shared_ptr<IFIXStore>& store)
    : m_settings(settings)
    , m_logger(logger->createLogger(settings))
    , m_state(SessionState::LOGON)
{
    m_dictionary = DictionaryRegistry::instance().load(settings.getString(SessionSettings::FIX_DICTIONARY));
    m_cache = std::make_unique<MemoryCache>(settings, m_dictionary, store);

    m_heartbeatInterval = settings.getLong(SessionSettings::HEARTBEAT_INTERVAL);
    m_logonInterval = settings.getLong(SessionSettings::LOGON_INTERVAL);
    m_reconnectInterval = settings.getLong(SessionSettings::RECONNECT_INTERVAL);
    
    m_network = std::make_shared<NetworkHandler>(m_settings, network, std::bind(&Session::processMessage, this, std::placeholders::_1));
}

Session::~Session()
{
    stop();
}

void Session::start()
{
    runUpdate();
}

void Session::stop()
{
    m_network->stop();
}

void Session::processMessage(const std::string& text)
{
    // lock whenever we're processing a message
    std::lock_guard<std::mutex> lock(m_mutex);

    LOG_DEBUG("Received message: " << text);

    if (m_state == SessionState::KILLING)
    {
        LOG_WARN("Received message while killing session, ignoring");
        return;
    }

    auto msg = m_dictionary->parse(m_settings, text);
    auto msgType = msg.getHeader().getField(FIELD::MsgType);

    auto time = Utils::getEpochMillis();
    m_lastRecvHeartbeat = time;

    if (!validateMessage(msg, time))
    {
        return;
    }

    // handle logons and non-gapfill sequence resets before checking seqnum
    if (msgType == MESSAGE::SEQUENCE_RESET)
    {
        handleSequenceReset(msg);
        return;
    }
    else if (msgType == MESSAGE::LOGON)
    {
        handleLogon(msg);
        return;
    }

    if (!validateSeqNum(msg))
    {
        return;
    }

    if (msgType == MESSAGE::LOGOUT)
    {
        if (m_state == SessionState::LOGOUT)
        {
            // TODO log successful logout
            m_state = SessionState::LOGOUT;
            // disconnect
            return;
        }

        sendLogout("Successful logout", true);
    }
    else if (msgType == MESSAGE::HEARTBEAT)
    {
        if (m_state == SessionState::TEST_REQUEST)
        {
            if (msg.getBody().has(FIELD::TestReqID))
            {
                if (msg.getBody().getField(FIELD::TestReqID) == std::to_string(m_testReqID))
                {
                    m_state = SessionState::READY;
                }
            }
        }
    }
    else if (msgType == MESSAGE::TEST_REQUEST)
    {
        sendHeartbeat(time, msg.getBody().getField(FIELD::TestReqID));
    }
    else if (msgType == MESSAGE::RESEND_REQUEST)
    {
        handleResendRequest(msg);
    }
}

void Session::send(Message& msg, SendCallback_T callback)
{
    int seqnum = populateMessage(msg);
    m_cache->cache(seqnum, msg);
    m_network->send({msg.toString(), callback});
}

void Session::runUpdate()
{
    // lock during our update loop
    std::lock_guard<std::mutex> lock(m_mutex);
    
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
            terminate("Failed to respond to test request " + std::to_string(m_testReqID) + " within heartbeat interval");
        }
    }

    if (m_state == SessionState::LOGOUT)
    {
        if ((time - m_logoutTime) >= 2 * m_heartbeatInterval)
        {
            terminate("Didn't receive logout ack in time");
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
    bool isTest = msg.getBody().tryGetBool(FIELD::TestMessageIndicator);
    if (isTest != m_settings.getBool(SessionSettings::IS_TEST))
    {
        logout("Sender/target test session mismatch", false);
        return;
    }

    if (m_settings.SESSION_TYPE == SessionType::ACCEPTOR)
        m_heartbeatInterval = std::stol(msg.getBody().getField(FIELD::HeartBtInt));

    int seqNum = std::stoi(msg.getHeader().getField(FIELD::MsgSeqNum));
    if (seqNum < m_cache->getTargetSeqNum())
    {
        logout("MsgSeqNum too low, expected " + m_cache->getTargetSeqNum(), true);
        return;
    }

    if (seqNum > m_cache->getTargetSeqNum())
    {
        sendResendRequest(m_cache->getTargetSeqNum(), 0);
        return;
    }

    m_state = SessionState::READY;
}

void Session::handleResendRequest(const Message& msg)
{
    int beginSeqNo = std::stoi(msg.getBody().getField(FIELD::BeginSeqNo));
    int endSeqNo = std::stoi(msg.getBody().getField(FIELD::EndSeqNo));

    LOG_INFO(beginSeqNo << endSeqNo);
}

void Session::handleSequenceReset(const Message& msg)
{
    bool gapFill = msg.getBody().tryGetBool(FIELD::GapFillFlag);
    int newSeqNum = std::stoi(msg.getBody().getField(FIELD::NewSeqNo));
    //int seqNum = std::stoi(msg.getHeader().getField(FIELD::MsgSeqNum));

    // queue out-of-sequence gapfills
    if (gapFill && !validateSeqNum(msg))
        return;

    m_cache->setTargetSeqNum(newSeqNum);
}

void Session::sendLogon()
{
    Message msg;
    msg.getHeader().setField(FIELD::MsgType, MESSAGE::LOGON);
    msg.getBody().setField(FIELD::HeartBtInt, std::to_string(m_heartbeatInterval));
    msg.getBody().setField(FIELD::EncryptMethod, "0");
    send(msg);
}

void Session::sendLogout(const std::string& reason, bool terminate)
{
    Message msg;
    msg.getHeader().setField(FIELD::MsgType, MESSAGE::LOGOUT);
    if (!reason.empty())
        msg.getBody().setField(FIELD::Text, reason);
    
    if (terminate)
        send(msg, [this, reason]{ this->terminate(reason); });
    else
        send(msg);
}

void Session::sendResendRequest(int from, int to)
{
    Message msg;
    msg.getHeader().setField(FIELD::MsgType, MESSAGE::RESEND_REQUEST);
    msg.getBody().setField(FIELD::BeginSeqNo, std::to_string(from));
    msg.getBody().setField(FIELD::EndSeqNo, std::to_string(to));
    send(msg);
}

void Session::logout(const std::string& reason, bool terminate)
{
    if (terminate)
        m_state = SessionState::KILLING;
    else
        m_state = SessionState::LOGOUT;
    
    sendLogout(reason, terminate);
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

void Session::sendReject(const Message& rejectedMsg, SessionRejectReason reason)
{
    Message msg;
    msg.getHeader().setField(FIELD::MsgType, MESSAGE::REJECT);
    msg.getBody().setField(FIELD::RefSeqNum, rejectedMsg.getHeader().getField(FIELD::MsgSeqNum));
    msg.getBody().setField(FIELD::SessionRejectReason, std::to_string(static_cast<int>(reason)));
    send(msg);
}

void Session::terminate(const std::string& reason)
{
    LOG_ERROR("Terminating connection: " << reason);
    m_logger.logEvent(reason);
    m_network->disconnect();
    m_state = SessionState::LOGON;
}

bool Session::load()
{
    try {
        // load data from store & insert into cache
        m_cache->load();
        return true;
    } catch (...) {
        return false;
    }
}

int Session::populateMessage(Message& msg)
{
    int seqnum = m_cache->nextSenderSeqNum();

    msg.getHeader().setField(FIELD::SenderCompID, m_settings.getString(SessionSettings::SENDER_COMP_ID));
    msg.getHeader().setField(FIELD::TargetCompID, m_settings.getString(SessionSettings::TARGET_COMP_ID));
    msg.getHeader().setField(FIELD::MsgSeqNum, std::to_string(seqnum));

    return seqnum;
}

bool Session::validateMessage(const Message& msg, long time)
{
    auto fail = [&](const std::string& reason){
        if (m_state == SessionState::LOGON)
            terminate(reason);
        else
            logout(reason, true);
    };

    // ensure MsgSeqNum is stamped
    if (!msg.getHeader().has(FIELD::MsgSeqNum))
    {
        logout("Message missing MsgSeqNum(34)", true);
        return false;
    }

    // validate BeginString, SenderCompID, TargetCompID
    if (msg.getHeader().getField(FIELD::BeginString) != m_settings.getString(SessionSettings::BEGIN_STRING))
    {
        fail("Failed to validate BeginString(8): " + msg.getHeader().getField(FIELD::BeginString));
        return false;
    }
    else if (msg.getHeader().getField(FIELD::SenderCompID) != m_settings.getString(SessionSettings::TARGET_COMP_ID))
    {
        fail("Failed to validate SenderCompID(49): " + msg.getHeader().getField(FIELD::SenderCompID));
        return false;
    }
    else if (msg.getHeader().getField(FIELD::TargetCompID) != m_settings.getString(SessionSettings::SENDER_COMP_ID))
    {
        fail("Failed to validate TargetCompID(56): " + msg.getHeader().getField(FIELD::TargetCompID));
        return false;
    }

    // validate SendingTime
    auto sendingTime = Utils::parseUTCTimestamp(msg.getHeader().getField(FIELD::SendingTime));
    if (std::abs(time - sendingTime) > m_settings.getLong(SessionSettings::SENDING_TIME_THRESHOLD))
    {
        sendReject(msg, SessionRejectReason::SendingTimeProblem);
        logout("SendingTime(52) outside of threshold", false);
        return false;
    }

    std::string msgType = msg.getHeader().getField(FIELD::MsgType);
    if (m_state == SessionState::LOGON && msgType != MESSAGE::LOGON)
    {
        logout("Received unexpected MsgType(35) during logon state", true);
        return false;
    }

    if (m_state == SessionState::LOGOUT && msgType != MESSAGE::LOGOUT && msgType != MESSAGE::RESEND_REQUEST)
    {
        logout("Received unexpected MsgType(35) during logoff state", true);
        return false;
    }

    return true;
}

bool Session::validateSeqNum(const Message& msg)
{
    int seqNum = std::stoi(msg.getHeader().getField(FIELD::MsgSeqNum));
    if (seqNum == getTargetSeqNum())
        return true;

    if (seqNum < getTargetSeqNum())
    {
        logout("MsgSeqNum too low, expected " + m_cache->getTargetSeqNum(), true);
    }
    else
    {
        // queue this message and send resend request for gap
        m_cache->getInboundQueue()[seqNum] = msg;
        sendResendRequest(getTargetSeqNum(), seqNum);
    }

    return false;
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
