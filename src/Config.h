#pragma once

#include <openfix/Log.h>
#include <strings.h>

#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

#include "Exception.h"

using SessionID_T = std::string;

enum class SessionType
{
    UNKNOWN,
    ACCEPTOR,
    INITIATOR,
};

template <typename Class, typename Type>
struct BaseConfigItem
{
    std::string name;
    Type defaultVal;
    size_t index;
};

template <typename Class>
class Config
{
public:
    Config()
        : m_stringValues(defaults().m_strings)
        , m_longValues(defaults().m_longs)
        , m_boolValues(defaults().m_bools)
        , m_doubleValues(defaults().m_doubles)
    {}

    template <typename Type>
    using ConfigItem = BaseConfigItem<Class, Type>;

    void setString(const ConfigItem<std::string>& item, std::string value)
    {
        m_stringValues[item.index] = std::move(value);
    }

    const std::string& getString(const ConfigItem<std::string>& item) const
    {
        return m_stringValues[item.index];
    }

    void setLong(const ConfigItem<long>& item, long value)
    {
        m_longValues[item.index] = value;
    }

    long getLong(const ConfigItem<long>& item) const
    {
        return m_longValues[item.index];
    }

    void setBool(const ConfigItem<bool>& item, bool value)
    {
        m_boolValues[item.index] = value;
    }

    bool getBool(const ConfigItem<bool>& item) const
    {
        return m_boolValues[item.index];
    }

    void setDouble(const ConfigItem<double>& item, double value)
    {
        m_doubleValues[item.index] = value;
    }

    double getDouble(const ConfigItem<double>& item) const
    {
        return m_doubleValues[item.index];
    }

    void load(const std::unordered_map<std::string, std::string>& settings)
    {
        for (const auto& [key, val] : settings) {
            auto it = defaults().m_fields.find(key);
            if (it == defaults().m_fields.end()) {
                LOG_WARN("Unknown configuration field: " << key);
                continue;
            }

            if (it->second.first == typeid(std::string))
                m_stringValues[it->second.second] = val;
            else if (it->second.first == typeid(long))
                m_longValues[it->second.second] = std::stol(val);
            else if (it->second.first == typeid(bool))
                m_boolValues = val == "1" || strcasecmp(val.c_str(), "Y") == 0 || strcasecmp(val.c_str(), "true") == 0;
            else if (it->second.first == typeid(double))
                m_doubleValues = std::stod(val);
        }
    }

protected:
    static inline ConfigItem<std::string> createString(std::string name, const std::string& defaultValue = "")
    {
        defaults().m_strings.push_back(defaultValue);
        defaults().m_fields.insert({name, {typeid(std::string), defaults().m_strings.size() - 1}});
        return {name, defaultValue, defaults().m_strings.size() - 1};
    }

    static inline ConfigItem<long> createLong(std::string name, long defaultValue = 0L)
    {
        defaults().m_longs.push_back(defaultValue);
        defaults().m_fields.insert({name, {typeid(long), defaults().m_longs.size() - 1}});
        return {name, defaultValue, defaults().m_longs.size() - 1};
    }

    static inline ConfigItem<bool> createBool(std::string name, bool defaultValue = false)
    {
        defaults().m_bools.push_back(defaultValue);
        defaults().m_fields.insert({name, {typeid(bool), defaults().m_bools.size() - 1}});
        return {name, defaultValue, defaults().m_bools.size() - 1};
    }

    static inline ConfigItem<double> createDouble(std::string name, double defaultValue = 0.0)
    {
        defaults().m_doubles.push_back(defaultValue);
        defaults().m_fields.insert({name, {typeid(double), defaults().m_doubles.size() - 1}});
        return {name, defaultValue, defaults().m_doubles.size() - 1};
    }

protected:
    struct Defaults
    {
        std::unordered_map<std::string, std::pair<std::type_index, size_t>> m_fields;

        std::vector<std::string> m_strings;
        std::vector<long> m_longs;
        std::vector<bool> m_bools;
        std::vector<double> m_doubles;
    };

    static Defaults& defaults()
    {
        static Defaults defaults;
        return defaults;
    }

    std::vector<std::string> m_stringValues;
    std::vector<long> m_longValues;
    std::vector<bool> m_boolValues;
    std::vector<double> m_doubleValues;

    CREATE_LOGGER("Config");
};

template <typename Class>
class StaticConfig : public Config<Class>
{
public:
    static const std::string& getString(const BaseConfigItem<Class, std::string>& item)
    {
        return instance().Config<Class>::getString(item);
    }

    static long getLong(const BaseConfigItem<Class, long>& item)
    {
        return instance().Config<Class>::getLong(item);
    }

    static bool getBool(const BaseConfigItem<Class, bool>& item)
    {
        return instance().Config<Class>::getBool(item);
    }

    static double getDouble(const BaseConfigItem<Class, double>& item)
    {
        return instance().Config<Class>::getDouble(item);
    }

    static void load(const std::unordered_map<std::string, std::string>& settings)
    {
        instance().Config<Class>::load(settings);
    }

private:
    static StaticConfig<Class>& instance()
    {
        static StaticConfig<Class> instance;
        return instance;
    }
};

struct PlatformSettings : StaticConfig<PlatformSettings>
{
    static inline ConfigItem<long> READER_THREADS = createLong("InputThreads", 1L);
    static inline ConfigItem<long> WRITER_THREADS = createLong("WriterThreads", 1L);

    static inline ConfigItem<long> SOCKET_SEND_BUF_SIZE = createLong("SocketSendBufSize");
    static inline ConfigItem<long> SOCKET_RECV_BUF_SIZE = createLong("SocketRecvBufSize");

    static inline ConfigItem<long> UPDATE_DELAY = createLong("UpdateDelay", 1000L);
    static inline ConfigItem<long> EPOLL_TIMEOUT = createLong("EpollTimeout", 1000L);

    static inline ConfigItem<std::string> LOG_PATH = createString("LogPath", "./log");
    static inline ConfigItem<std::string> DATA_PATH = createString("DataPath", "./data");

    static inline ConfigItem<long> ADMIN_WEBSITE_PORT = createLong("AdminWebsitePort", 51234);
};

struct SessionSettings : Config<SessionSettings>
{
    SessionSettings()
    {}

    static inline ConfigItem<std::string> BEGIN_STRING = createString("BeginString");

    static inline ConfigItem<std::string> SENDER_COMP_ID = createString("SenderCompID");
    static inline ConfigItem<std::string> TARGET_COMP_ID = createString("TargetCompID");

    static inline ConfigItem<bool> IS_TEST = createBool("TestSession", false);
    static inline ConfigItem<bool> SEND_NEXT_EXPECTED_MSG_SEQ_NUM = createBool("SendNextExpectedMsgSeqNum", true);

    static inline ConfigItem<bool> RESET_SEQ_NUM_ON_LOGON = createBool("ResetSeqNumOnLogon", false);
    static inline ConfigItem<bool> ALLOW_RESET_SEQ_NUM_FLAG = createBool("AllowResetSeqNumFlag", false);

    static inline ConfigItem<bool> ENABLE_TCP_QUICKACK = createBool("TCPQuickAck", true);
    static inline ConfigItem<bool> ENABLE_TCP_NODELAY = createBool("TCPNoDelay", true);

    static inline ConfigItem<std::string> FIX_DICTIONARY = createString("FIXDictionary");

    static inline ConfigItem<bool> RELAXED_PARSING = createBool("RelaxedParsing", false);
    static inline ConfigItem<bool> LOUD_PARSING = createBool("LoudParsing", true);
    static inline ConfigItem<bool> VALIDATE_REQUIRED_FIELDS = createBool("ValidateRequiredFields");

    static inline ConfigItem<std::string> START_TIME = createString("StartTime", "00:00:00");
    static inline ConfigItem<std::string> STOP_TIME = createString("StopTime", "00:00:00");

    static inline ConfigItem<long> ACCEPT_PORT = createLong("AcceptPort");

    static inline ConfigItem<std::string> CONNECT_HOST = createString("ConnectHost");
    static inline ConfigItem<long> CONNECT_PORT = createLong("ConnectPort");

    static inline ConfigItem<long> CONNECT_TIMEOUT = createLong("ConnectTimeout", 5000L);

    static inline ConfigItem<long> HEARTBEAT_INTERVAL = createLong("HeartbeatInterval", 10L);
    static inline ConfigItem<long> LOGON_INTERVAL = createLong("LogonInterval", 10L);
    static inline ConfigItem<long> RECONNECT_INTERVAL = createLong("ReconnectInterval", 10L);

    static inline ConfigItem<double> TEST_REQUEST_THRESHOLD = createDouble("TestRequestThreshold", 2.0);
    static inline ConfigItem<long> SENDING_TIME_THRESHOLD = createLong("SendingTimeThreshold", 10L);

    static inline ConfigItem<std::string> SESSION_TYPE_STR = createString("SessionType");

    SessionType getSessionType() const
    {
        std::string tmp = getString(SESSION_TYPE_STR);
        if (strcasecmp(tmp.c_str(), "initiator") == 0)
            return SessionType::INITIATOR;
        else if (strcasecmp(tmp.c_str(), "acceptor") == 0)
            return SessionType::ACCEPTOR;
        else
            throw MisconfiguredSessionError("Unknown session type: " + tmp);
    }

    SessionID_T getSessionID() const
    {
        return getString(SENDER_COMP_ID) + ":" + getString(TARGET_COMP_ID);
    }

private:
    SessionType SESSION_TYPE = SessionType::UNKNOWN;
};
