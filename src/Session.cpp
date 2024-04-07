#include "Session.h"

#include <openfix/Utils.h>
#include <strings.h>

#include "Exception.h"
#include "Fields.h"
#include "Messages.h"

Session::Session(SessionSettings settings, Network& network, std::shared_ptr<IFIXLogger>& logger, std::shared_ptr<IFIXStore>& store)
    : m_settings(settings)
    , m_logger(logger->createLogger(settings))
    , m_state(SessionState::LOGON)
{
    m_dictionary = DictionaryRegistry::instance().load(settings.getString(SessionSettings::FIX_DICTIONARY));
    m_cache = std::make_unique<MemoryCache>(settings, m_dictionary, store);

    m_heartbeatInterval = settings.getLong(SessionSettings::HEARTBEAT_INTERVAL) * 1000;
    m_logonInterval = settings.getLong(SessionSettings::LOGON_INTERVAL) * 1000;
    m_reconnectInterval = settings.getLong(SessionSettings::RECONNECT_INTERVAL) * 1000;

    m_network = std::make_shared<NetworkHandler>(m_settings, network, std::bind(&Session::onMessage, this, std::placeholders::_1));

    // load from store
    load();
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

void Session::onMessage(const std::string& text)
{
    dispatcher.dispatch([this, text] {
        if (m_state == SessionState::KILLING) {
            LOG_WARN("Received message while killing session, ignoring");
            return;
        }

        try {
            // parse message
            auto msg = m_dictionary->parse(m_settings, text);
            m_logger.logMessage(msg.toString(), Direction::INBOUND);

            LOG_DEBUG("Received: " << msg);

            auto time = Utils::getEpochMillis();
            m_lastRecvHeartbeat = time;

            processMessage(msg, time);

            // handle inbound queue
            auto& queue = m_cache->getInboundQueue();
            while (!queue.empty() && getTargetSeqNum() == queue.begin()->first) {
                processMessage(queue.begin()->second, time);
                queue.erase(queue.begin());
            }
        } catch (const MessageParsingError& e) {
            LOG_ERROR("Error while parsing message: " << e.what() << "\nfull message:\n" << text);
        } catch (...) {
            LOG_ERROR("Unknown error while handling message!");
        }
    });
}

void Session::processMessage(const Message& msg, long time)
{
    auto msgType = msg.getHeader().getField(FIELD::MsgType);

    if (!validateMessage(msg, time)) {
        LOG_ERROR("Message failed validations: " << msg);
        return;
    }

    // handle logons and non-gapfill sequence resets before checking seqnum
    if (msgType == MESSAGE::SEQUENCE_RESET) {
        handleSequenceReset(msg);
        return;
    } else if (msgType == MESSAGE::LOGON) {
        handleLogon(msg);
        return;
    } else if (msgType == MESSAGE::RESEND_REQUEST) {
        handleResendRequest(msg);
        return;
    }

    if (!validateSeqNum(msg)) {
        LOG_INFO("Message failed SeqNum validation: " << msg);
        return;
    }

    if (msgType == MESSAGE::LOGOUT) {
        if (m_state == SessionState::LOGOUT) {
            LOG_INFO("Successful logout");
            m_state = SessionState::LOGON;
            m_network->disconnect();
            return;
        }

        sendLogout("Successful logout", true);
    } else if (msgType == MESSAGE::HEARTBEAT) {
        if (m_state == SessionState::TEST_REQUEST) {
            if (msg.getBody().has(FIELD::TestReqID)) {
                if (msg.getBody().getField(FIELD::TestReqID) == std::to_string(m_testReqID)) {
                    m_state = SessionState::READY;
                }
            }
        }
    } else if (msgType == MESSAGE::TEST_REQUEST) {
        auto test_id = msg.getBody().getField(FIELD::TestReqID);
        LOG_DEBUG("Responding to test request ID=" << test_id << " with heartbeat");
        sendHeartbeat(time, test_id);
    } else if (msgType == MESSAGE::REJECT) {
        LOG_INFO("Received reject message: " << msg);
    }
}

void Session::send(Message& msg, SendCallback_T callback)
{
    int seqnum = populateMessage(msg);
    m_cache->cache(seqnum, msg);
    m_cache->nextSenderSeqNum();
    internal_send(msg, callback);
}

void Session::internal_send(const Message& msg, SendCallback_T callback)
{
    if (m_network->isConnected()) {
        auto msg_str = msg.toString();
        LOG_DEBUG("Sending: " << msg_str);
        m_logger.logMessage(msg_str, Direction::OUTBOUND);
        m_network->send({msg.toString(true), callback});

        // update our heartbeat monitor as we just sent data
        m_lastSentHeartbeat = Utils::getEpochMillis();
    }
}

void Session::runUpdate()
{
    dispatcher.dispatch([this] {
        try {
            internal_update();
        } catch (...) {
            LOG_ERROR("Error during update loop!");
        }
    });
}

void Session::internal_update()
{
    long time = Utils::getEpochMillis();

    if (!m_network->isConnected()) {
        m_state = SessionState::LOGON;
        if ((time - m_lastReconnect) >= m_reconnectInterval) {
            LOG_DEBUG("Reconnect interval exceeded (" << (time - m_lastReconnect) << " >= " << m_reconnectInterval << "), attempting reconnect");
            m_network->start();
            m_lastReconnect = time;
        }

        return;
    }

    if (m_settings.getSessionType() == SessionType::INITIATOR && m_state == SessionState::LOGON && (time - m_lastLogon) >= m_logonInterval) {
        LOG_DEBUG("Logon interval exceeded (" << (time - m_lastLogon) << " >= " << m_logonInterval << "), attempting logon");
        sendLogon();
        return;
    }

    if (m_state == SessionState::TEST_REQUEST) {
        if ((time - m_lastRecvHeartbeat) >= m_heartbeatInterval) {
            terminate("Failed to respond to test request " + std::to_string(m_testReqID) + " within heartbeat interval");
        }
    }

    if (m_state == SessionState::LOGOUT && m_state != SessionState::KILLING) {
        if ((time - m_logoutTime) >= 2 * m_heartbeatInterval) {
            terminate("Didn't receive logout ack in time");
        }
    }

    if (m_state != SessionState::LOGON && m_state != SessionState::LOGOUT && m_state != SessionState::KILLING) {
        if ((time - m_lastSentHeartbeat) >= m_heartbeatInterval) {
            LOG_DEBUG("Heartbeat threshold exceeded (" << (time - m_lastSentHeartbeat) << " >= " << m_heartbeatInterval << "), sending heartbeat");
            sendHeartbeat(time);
        }

        // send a test request if it's been too long since receiving
        if (m_state != SessionState::TEST_REQUEST) {
            long heartbeatTimeout = static_cast<long>(m_settings.getDouble(SessionSettings::TEST_REQUEST_THRESHOLD) * m_heartbeatInterval);
            if ((time - m_lastRecvHeartbeat) >= heartbeatTimeout) {
                LOG_WARN("Heartbeat timeout exceeded (" << (time - m_lastRecvHeartbeat) << " >= " << heartbeatTimeout << "), sending test request");
                sendTestRequest();
                m_state = SessionState::TEST_REQUEST;
            }
        }
    }
}

void Session::handleLogon(const Message& msg)
{
    bool isTest = msg.getBody().tryGetBool(FIELD::TestMessageIndicator);
    if (isTest != m_settings.getBool(SessionSettings::IS_TEST)) {
        logout("Sender/target test session mismatch", false);
        return;
    }

    bool isPosDup = msg.getBody().tryGetBool(FIELD::PosDupFlag);
    if (isPosDup && !msg.getBody().has(FIELD::OrigSendingTime)) {
        sendReject(msg, SessionRejectReason::RequiredTagMissing);
        return;
    }

    int seqNum = std::stoi(msg.getHeader().getField(FIELD::MsgSeqNum));
    if (!isPosDup && seqNum < m_cache->getTargetSeqNum()) {
        logout("MsgSeqNum too low, expected " + std::to_string(m_cache->getTargetSeqNum()), true);
        return;
    }

    if (m_settings.getSessionType() == SessionType::ACCEPTOR) {
        // set hbint, send a logon
        m_heartbeatInterval = std::stol(msg.getBody().getField(FIELD::HeartBtInt)) * 1000;
        sendLogon();
    }

    m_state = SessionState::READY;

    if (seqNum > m_cache->getTargetSeqNum()) {
        LOG_INFO("Incoming MsgSeqNum higher than expected, requesting resend from " << m_cache->getTargetSeqNum());
        sendResendRequest(m_cache->getTargetSeqNum(), 0);
        return;
    }

    m_cache->nextTargetSeqNum();
}

void Session::handleResendRequest(const Message& msg)
{
    int seqNo = std::stoi(msg.getHeader().getField(FIELD::MsgSeqNum));
    int beginSeqNo = std::stoi(msg.getBody().getField(FIELD::BeginSeqNo));
    int endSeqNo = std::stoi(msg.getBody().getField(FIELD::EndSeqNo));

    LOG_INFO("Received resend request from " << beginSeqNo << " to " << endSeqNo);

    // cap at our senderseqnum
    endSeqNo = std::min(endSeqNo, getSenderSeqNum());

    int ptr = beginSeqNo;
    m_cache->getMessages(beginSeqNo, endSeqNo, [&](int seqno, Message msg) {
        // skip session-level messages
        if (MESSAGE::SESSION_MSGS.count(msg.getHeader().getField(FIELD::MsgType)))
            return;

        // send gapfill if we need to
        if (seqno != ptr)
            sendSequenceReset(ptr, seqno);

        msg.getHeader().setField(FIELD::PosDupFlag, "Y");
        msg.getHeader().setField(FIELD::OrigSendingTime, msg.getHeader().getField(FIELD::SendingTime));
        msg.getHeader().setField(FIELD::SendingTime, Utils::getUTCTimestamp());

        internal_send(msg, {});
        // expect to send the next message
        ptr = seqno + 1;
    });

    if (ptr != beginSeqNo)
        --ptr;

    endSeqNo = endSeqNo != 0 ? endSeqNo : getSenderSeqNum();
    // send final gapfill if we need to
    if (ptr < endSeqNo)
        sendSequenceReset(ptr, endSeqNo);

    // unless we're waiting on replay ourselves, we would expect the seqnum to be incremented
    if (seqNo == getTargetSeqNum())
        m_cache->nextTargetSeqNum();
    else
        LOG_DEBUG("Not increasing target MsgSeqNum, awaiting our own replay");
}

void Session::handleSequenceReset(const Message& msg)
{
    bool gapFill = msg.getBody().tryGetBool(FIELD::GapFillFlag);
    int newSeqNum = std::stoi(msg.getBody().getField(FIELD::NewSeqNo));
    int seqNum = std::stoi(msg.getHeader().getField(FIELD::MsgSeqNum));

    if (newSeqNum <= seqNum) {
        sendReject(msg, SessionRejectReason::IncorrectValueForTag, "Attempt to lower sequence number, invalid value NewSeqNo(36)=" + std::to_string(newSeqNum));
        return;
    }

    if (newSeqNum < m_cache->getTargetSeqNum()) {
        logout("Unable to set SeqNum to " + std::to_string(newSeqNum) + ", next expected is " + std::to_string(m_cache->getTargetSeqNum()), true);
        return;
    }

    // queue out-of-sequence gapfills
    if (gapFill && !validateSeqNum(msg))
        return;

    m_cache->setTargetSeqNum(newSeqNum);
}

void Session::sendLogon()
{
    auto msg = m_dictionary->create(MESSAGE::LOGON);
    msg.getBody().setField(FIELD::HeartBtInt, std::to_string(m_heartbeatInterval / 1000));
    msg.getBody().setField(FIELD::EncryptMethod, "0");
    send(msg);

    // update last time
    m_lastLogon = Utils::getEpochMillis();
}

void Session::sendLogout(const std::string& reason, bool terminate)
{
    auto msg = m_dictionary->create(MESSAGE::LOGOUT);
    if (!reason.empty())
        msg.getBody().setField(FIELD::Text, reason);

    if (terminate)
        send(msg, [this, reason] { this->terminate(reason); });
    else
        send(msg);
}

void Session::sendResendRequest(int from, int to)
{
    auto msg = m_dictionary->create(MESSAGE::RESEND_REQUEST);
    msg.getBody().setField(FIELD::BeginSeqNo, std::to_string(from));
    msg.getBody().setField(FIELD::EndSeqNo, std::to_string(to));
    send(msg);
}

void Session::sendSequenceReset(int seqno, int new_seqno, bool gapfill)
{
    auto msg = m_dictionary->create(MESSAGE::SEQUENCE_RESET);
    populateMessage(msg);
    msg.getHeader().setField(FIELD::MsgSeqNum, std::to_string(seqno));
    msg.getBody().setField(FIELD::NewSeqNo, std::to_string(new_seqno));

    if (gapfill)
        msg.getBody().setField(FIELD::GapFillFlag, "Y");

    internal_send(msg, {});
}

void Session::logout(const std::string& reason, bool terminate)
{
    LOG_INFO("Logging out (" << (terminate ? "terminal" : "clean") << "), reason: " << reason);

    if (terminate)
        m_state = SessionState::KILLING;
    else
        m_state = SessionState::LOGOUT;

    sendLogout(reason, terminate);
}

void Session::sendHeartbeat(long time, std::string testReqID)
{
    auto msg = m_dictionary->create(MESSAGE::HEARTBEAT);
    if (!testReqID.empty())
        msg.getBody().setField(FIELD::TestReqID, testReqID);
    send(msg);
}

void Session::sendTestRequest()
{
    auto msg = m_dictionary->create(MESSAGE::TEST_REQUEST);
    msg.getBody().setField(FIELD::TestReqID, std::to_string(++m_testReqID));
    send(msg);
}

void Session::sendReject(const Message& rejectedMsg, SessionRejectReason reason, std::string text)
{
    auto msg = m_dictionary->create(MESSAGE::REJECT);
    msg.getBody().setField(FIELD::RefSeqNum, rejectedMsg.getHeader().getField(FIELD::MsgSeqNum));
    msg.getBody().setField(FIELD::SessionRejectReason, std::to_string(static_cast<int>(reason)));
    if (!text.empty())
        msg.getBody().setField(FIELD::Text, text);
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
    int seqnum = m_cache->getSenderSeqNum();

    msg.getHeader().setField(FIELD::BeginString, m_settings.getString(SessionSettings::BEGIN_STRING));
    msg.getHeader().setField(FIELD::SenderCompID, m_settings.getString(SessionSettings::SENDER_COMP_ID));
    msg.getHeader().setField(FIELD::TargetCompID, m_settings.getString(SessionSettings::TARGET_COMP_ID));
    msg.getHeader().setField(FIELD::SendingTime, Utils::getUTCTimestamp());
    msg.getHeader().setField(FIELD::MsgSeqNum, std::to_string(seqnum));

    return seqnum;
}

bool Session::validateMessage(const Message& msg, long time)
{
    auto fail = [&](const std::string& reason) {
        if (m_state == SessionState::LOGON)
            terminate(reason);
        else
            logout(reason, true);
    };

    // ensure MsgSeqNum is stamped
    if (!msg.getHeader().has(FIELD::MsgSeqNum)) {
        logout("Message missing MsgSeqNum(34)", true);
        return false;
    }

    // validate BeginString, SenderCompID, TargetCompID
    if (msg.getHeader().getField(FIELD::BeginString) != m_settings.getString(SessionSettings::BEGIN_STRING)) {
        fail("Failed to validate BeginString(8): " + msg.getHeader().getField(FIELD::BeginString));
        return false;
    } else if (msg.getHeader().getField(FIELD::SenderCompID) != m_settings.getString(SessionSettings::TARGET_COMP_ID)) {
        fail("Failed to validate SenderCompID(49): " + msg.getHeader().getField(FIELD::SenderCompID));
        return false;
    } else if (msg.getHeader().getField(FIELD::TargetCompID) != m_settings.getString(SessionSettings::SENDER_COMP_ID)) {
        fail("Failed to validate TargetCompID(56): " + msg.getHeader().getField(FIELD::TargetCompID));
        return false;
    }

    // validate SendingTime
    auto sendingTime = Utils::parseUTCTimestamp(msg.getHeader().getField(FIELD::SendingTime));
    auto diff = time - sendingTime;
    if (std::abs(diff) > (m_settings.getLong(SessionSettings::SENDING_TIME_THRESHOLD) * 1000)) {
        LOG_ERROR("Sending time error on incoming message, current time=" << time << ", diff=" << diff);
        sendReject(msg, SessionRejectReason::SendingTimeProblem);
        logout("SendingTime(52) outside of threshold", false);
        return false;
    }

    std::string msgType = msg.getHeader().getField(FIELD::MsgType);
    if (m_state == SessionState::LOGON && msgType != MESSAGE::LOGON) {
        logout("Received unexpected MsgType(35) during logon state: " + msgType, true);
        return false;
    }

    if (m_state == SessionState::LOGOUT && msgType != MESSAGE::LOGOUT && msgType != MESSAGE::RESEND_REQUEST) {
        logout("Received unexpected MsgType(35) during logoff state: " + msgType, true);
        return false;
    }

    return true;
}

bool Session::validateSeqNum(const Message& msg)
{
    int seqNum = std::stoi(msg.getHeader().getField(FIELD::MsgSeqNum));
    if (seqNum == getTargetSeqNum()) {
        m_cache->nextTargetSeqNum();
        return true;
    }

    if (seqNum < getTargetSeqNum()) {
        logout("MsgSeqNum too low, expected " + std::to_string(m_cache->getTargetSeqNum()), true);
    } else if (seqNum) {
        // queue this message and send resend request for gap
        m_cache->getInboundQueue()[seqNum] = msg;
        sendResendRequest(getTargetSeqNum(), seqNum);
    }

    m_cache->nextTargetSeqNum();
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
