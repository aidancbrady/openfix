#pragma once

#include <openfix/Log.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "Config.h"
#include "Message.h"

class Dictionary
{
public:
    Message parse(const SessionSettings& settings, const std::string& text) const;

    FieldType getFieldType(int tag) const
    {
        auto it = m_fields.find(tag);
        if (it == m_fields.end())
            return FieldType::UNKNOWN;
        return it->second;
    }

private:
    std::shared_ptr<GroupSpec> m_headerSpec;
    std::shared_ptr<GroupSpec> m_trailerSpec;

    std::unordered_map<std::string, std::shared_ptr<GroupSpec>> m_bodySpecs;
    std::unordered_map<int, FieldType> m_fields;

    friend class DictionaryRegistry;

    CREATE_LOGGER("Dictionary");
};

class DictionaryRegistry
{
public:
    static DictionaryRegistry& instance()
    {
        static DictionaryRegistry instance;
        return instance;
    }

    std::shared_ptr<Dictionary> load(const std::string& path);

private:
    std::unordered_map<std::string, std::shared_ptr<Dictionary>> m_dictionaries;

    CREATE_LOGGER("DictionaryRegistry");
};
