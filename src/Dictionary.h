#pragma once

#include <openfix/Log.h>

#include <array>
#include <memory>
#include <string>

#include "Config.h"
#include "Fields.h"
#include "Message.h"

class Dictionary
{
public:
    // max FIX tag supported for flat-array field type lookup
    static constexpr int MAX_FIELD_TAG = 1024;

    Message parse(const SessionSettings& settings, std::string text) const;

    Message create(const std::string& msg_type) const
    {
        Message msg;

        const auto* bodySpec = getMessageSpecRaw(msg_type);
        if (bodySpec == nullptr)
            throw MessageParsingError("Unknown message: " + msg_type);
        msg.getBody().setSpec(bodySpec);
        msg.getHeader().setSpec(m_headerSpec.get());
        msg.getTrailer().setSpec(m_trailerSpec.get());

        // assume around 8 fields in header for typical message
        msg.getHeader().reserve(8);

        msg.getHeader().setField(FIELD::MsgType, msg_type);

        return msg;
    }

    FieldType getFieldType(int tag) const
    {
        if (tag >= 0 && tag < MAX_FIELD_TAG) [[likely]]
            return m_fieldTypes[tag];
        const auto it = m_fieldsFallback.find(tag);
        if (it == m_fieldsFallback.end())
            return FieldType::UNKNOWN;
        return it->second;
    }

    std::shared_ptr<GroupSpec> getMessageSpec(const std::string& msg_type) const
    {
        if (msg_type.size() == 1) [[likely]] {
            const auto c = static_cast<unsigned char>(msg_type[0]);
            if (c < FAST_MSGTYPE_SIZE && m_bodySpecsFast[c])
                return m_bodySpecsFast[c];
        }
        const auto it = m_bodySpecs.find(msg_type);
        if (it == m_bodySpecs.end())
            return nullptr;
        return it->second;
    }

    const GroupSpec* getMessageSpecRaw(std::string_view msg_type) const
    {
        // fast path for single-char message types
        if (msg_type.size() == 1) [[likely]] {
            const auto c = static_cast<unsigned char>(msg_type[0]);
            if (c < FAST_MSGTYPE_SIZE)
                return m_bodySpecsFast[c].get();
        }
        const auto it = m_bodySpecs.find(std::string(msg_type));
        if (it == m_bodySpecs.end())
            return nullptr;
        return it->second.get();
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

    static constexpr int FAST_MSGTYPE_SIZE = 128;
    std::array<std::shared_ptr<GroupSpec>, FAST_MSGTYPE_SIZE> m_bodySpecsFast{};

    std::array<FieldType, MAX_FIELD_TAG> m_fieldTypes{};
    // fallback lookup for tags >= MAX_FIELD_TAG
    HashMapT<int, FieldType> m_fieldsFallback;

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
