#pragma once

#include <openfix/Log.h>

#include <memory>
#include <string>

#include "Config.h"
#include "Fields.h"
#include "Message.h"

class Dictionary
{
public:
    Message parse(const SessionSettings& settings, std::string text) const;

    Message create(const std::string& msg_type) const
    {
        Message msg;

        auto msg_spec = getMessageSpec(msg_type);
        if (msg_spec == nullptr)
            throw MessageParsingError("Unknown message: " + msg_type);
        msg.getBody().setSpec(msg_spec);

        msg.getHeader().setSpec(getHeaderSpec());
        msg.getTrailer().setSpec(getTrailerSpec());

        msg.getHeader().setField(FIELD::MsgType, msg_type);

        return msg;
    }

    FieldType getFieldType(int tag) const
    {
        auto it = m_fields.find(tag);
        if (it == m_fields.end())
            return FieldType::UNKNOWN;
        return it->second;
    }

    std::shared_ptr<GroupSpec> getMessageSpec(const std::string& msg_type) const
    {
        auto it = m_bodySpecs.find(msg_type);
        if (it == m_bodySpecs.end())
            return nullptr;
        return it->second;
    }

    const std::shared_ptr<GroupSpec>& getHeaderSpec() const
    {
        return m_headerSpec;
    }

    const std::shared_ptr<GroupSpec>& getTrailerSpec() const
    {
        return m_trailerSpec;
    }

private:
    std::shared_ptr<GroupSpec> m_headerSpec;
    std::shared_ptr<GroupSpec> m_trailerSpec;

    HashMapT<std::string, std::shared_ptr<GroupSpec>> m_bodySpecs;
    HashMapT<int, FieldType> m_fields;

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
    HashMapT<std::string, std::shared_ptr<Dictionary>> m_dictionaries;

    CREATE_LOGGER("DictionaryRegistry");
};
