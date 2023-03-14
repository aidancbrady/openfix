#pragma once

#include "Message.h"
#include "Config.h"
#include "Log.h"

#include <string>
#include <memory>
#include <unordered_set>
#include <unordered_map>

struct GroupSpec
{
    using FieldSet = std::unordered_map<int, FieldType>;
    
    bool empty() const
    {
        return m_fields.empty() && m_groups.empty();
    }

    FieldSet m_fields;
    std::unordered_map<int, std::unique_ptr<GroupSpec>> m_groups;
    std::unordered_map<int, int> m_dataLengthTags;

    static GroupSpec UNKNOWN;
};

class Dictionary
{
public:
    Message parse(const SessionSettings& settings, const std::string& text) const;

private:
    GroupSpec m_headerSpec;
    GroupSpec m_footerSpec;

    std::unordered_map<std::string, GroupSpec> m_bodySpecs;

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
};
