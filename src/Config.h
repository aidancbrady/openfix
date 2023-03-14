#pragma once

#include "Log.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <typeindex>
#include <typeinfo>

template<typename Class, typename Type>
struct BaseConfigItem
{
    std::string name;
    Type defaultVal;
    size_t index;
};

template<typename Class>
class Config
{
public:
    template<typename Type>
    using ConfigItem = BaseConfigItem<Class, Type>;

    const std::string& getString(const ConfigItem<std::string>& item) const
    {
        return m_stringValues[item.index];
    }

    long getLong(const ConfigItem<long>& item) const
    {
        return m_longValues[item.index];
    }

    bool getBool(const ConfigItem<bool>& item) const
    {
        return m_boolValues[item.index];
    }

    void load(const std::unordered_map<std::string, std::string>& settings)
    {
        for (const auto& [key, val] : settings)
        {
            auto it = m_fields.find(key);
            if (it == m_fields.end())
            {
                LOG_WARN("Unknown configuration field: " << key);
                continue;
            }

            if (it->second.first == typeid(std::string))
                m_stringValues[it->second.second] = val;
            else if (it->second.first == typeid(long))
                m_longValues[it->second.second] = std::atol(val);
            else if (it->second.first == typeid(bool))
                m_longValues = val == "1" || strcasecmp(val.c_str(), "Y") == 0 || strcasecmp(val.c_str(), "true") == 0;
        }
    }

protected:
    static inline ConfigItem<std::string> createString(std::string name, const std::string& defaultValue = "")
    {
        s_defaultStrings.push_back(defaultValue);
        m_fields.insert({name, {typeid(std::string), s_defaultStrings.size() - 1}});
        return {name, defaultValue, s_defaultStrings.size() - 1};
    }

    static inline ConfigItem<long> createLong(std::string name, long defaultValue = 0L)
    {
        s_defaultLongs.push_back(defaultValue);
        m_fields.insert({name, {typeid(long), s_defaultLongs.size() - 1}});
        return {name, defaultValue, s_defaultLongs.size() - 1};
    }

    static inline ConfigItem<bool> createBool(std::string name, bool defaultValue = false)
    {
        s_defaultBools.push_back(defaultValue);
        m_fields.insert({name, {typeid(bool), s_defaultBools.size() - 1}});
        return {name, defaultValue, s_defaultBools.size() - 1};
    }

private:
    Config() : m_stringValues(s_defaultStrings), m_longValues(s_defaultLongs), m_boolValues(s_defaultBools)
    {}

    static inline std::unordered_map<std::string, std::pair<std::type_index, size_t>> m_fields;

    static inline std::vector<std::string> s_defaultStrings;
    static inline std::vector<long> s_defaultLongs;
    static inline std::vector<bool> s_defaultBools;

    std::vector<std::string> m_stringValues;
    std::vector<long> m_longValues;
    std::vector<bool> m_boolValues;

    CREATE_LOGGER("Config");
};

template<typename Class>
class StaticConfig : public Config<Class>
{
public:
    static const std::string& getString(const BaseConfigItem<Class, std::string>& item)
    {
        return instance().getString(item);
    }

    static long getLong(const BaseConfigItem<Class, long>& item)
    {
        return instance().getLong(item);
    }

    static bool getBool(const BaseConfigItem<Class, bool>& item)
    {
        return instance().getBool(item);
    }

    static void load(const std::unordered_map<std::string, std::string>& settings)
    {
        instance().load(settings);
    }

private:
    static StaticConfig<Class> instance()
    {
        StaticConfig<Class> instance;
        return instance;
    }
};

struct PlatformSettings : StaticConfig<PlatformSettings>
{
    static inline ConfigItem<long> INPUT_THREADS = createLong("InputThreads", 10L);
};

struct SessionSettings : Config<SessionSettings>
{
    static inline ConfigItem<std::string> BEGIN_STRING = createString("BeginString");

    static inline ConfigItem<std::string> SENDER_COMP_ID = createString("SenderCompID");
    static inline ConfigItem<std::string> TARGET_COMP_ID = createString("TargetCompID");

    static inline ConfigItem<std::string> FIX_DICTIONARY = createString("FIXDictionary");

    static inline ConfigItem<bool> RELAXED_PARSING = createBool("RelaxedParsing");
    static inline ConfigItem<bool> LOUD_PARSING = createBool("LoudParsing", true);
};
