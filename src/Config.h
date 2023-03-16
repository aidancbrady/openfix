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
    Config() : m_stringValues(defaults().m_strings), m_longValues(defaults().m_longs), m_boolValues(defaults().m_bools)
    {}

    template<typename Type>
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

    void load(const std::unordered_map<std::string, std::string>& settings)
    {
        for (const auto& [key, val] : settings)
        {
            auto it = defaults().m_fields.find(key);
            if (it == defaults().m_fields.end())
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

private:
    struct Defaults
    {
        std::unordered_map<std::string, std::pair<std::type_index, size_t>> m_fields;

        std::vector<std::string> m_strings;
        std::vector<long> m_longs;
        std::vector<bool> m_bools;
    };

    static Defaults& defaults()
    {
        static Defaults defaults;
        return defaults;
    }

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
