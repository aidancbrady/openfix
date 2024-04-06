#include "Message.h"

#include <functional>
#include <unordered_set>

#include "Fields.h"

const std::unordered_set<int> IGNORED_TAGS = {FIELD::BeginString, FIELD::BodyLength, FIELD::CheckSum};

std::string printGroup(const FieldMap& fieldMap, bool skipIgnoredTags, char soh_char)
{
    std::ostringstream ostr;

    if (!fieldMap.empty()) {
        for (const auto& [k, v] : fieldMap.getFields()) {
            // skip ignored tags
            if (skipIgnoredTags && IGNORED_TAGS.find(k) != IGNORED_TAGS.end())
                continue;

            ostr << k << TAG_ASSIGNMENT_CHAR << v << soh_char;

            if (fieldMap.getGroupCount(k) > 0)
                for (const auto& group : fieldMap.getGroups(k))
                    ostr << group;
        }

        if (!skipIgnoredTags && fieldMap.has(FIELD::CheckSum))
            ostr << FIELD::CheckSum << TAG_ASSIGNMENT_CHAR << fieldMap.getField(FIELD::CheckSum) << soh_char;
    }

    return ostr.str();
}

std::ostream& operator<<(std::ostream& ostr, const FieldMap& fieldMap)
{
    ostr << printGroup(fieldMap, false, EXTERNAL_SOH_CHAR);
    return ostr;
}

std::ostream& operator<<(std::ostream& ostr, const Message& msg)
{
    std::ostringstream ret;
    msg.toStream(ostr);
    return ostr;
}

void Message::toStream(std::ostream& ostr, char soh_char) const
{
    std::ostringstream ret;
    auto it = m_header.getFields().find(FIELD::BeginString);
    if (it != m_header.getFields().end())
        ret << FIELD::BeginString << TAG_ASSIGNMENT_CHAR << it->second << soh_char;
    std::string body;
    body += printGroup(m_header, true, soh_char);
    body += printGroup(m_body, true, soh_char);
    body += printGroup(m_trailer, true, soh_char);
    ret << FIELD::BodyLength << TAG_ASSIGNMENT_CHAR << body.size() << soh_char;
    ret << body;
    body = ret.str();

    // get checksum
    int checksum = 0;
    for (char c : body)
        checksum += c;
    checksum %= 256;
    auto checksumStr = std::to_string(checksum);
    while (checksumStr.size() < 3)
        checksumStr = '0' + checksumStr;

    ostr << body;
    ostr << FIELD::CheckSum << TAG_ASSIGNMENT_CHAR << checksumStr << soh_char;
}

std::string Message::toString(bool internal) const
{
    std::ostringstream ostr;
    toStream(ostr, internal ? INTERNAL_SOH_CHAR : EXTERNAL_SOH_CHAR);
    return ostr.str();
}

void FieldMap::sortFields()
{
    LinkedHashMap<int, std::string> newFields;

    if (m_groupSpec) {
        for (auto tag : m_groupSpec->m_fieldOrder) {
            auto it = m_fields.find(tag);
            if (it != m_fields.end()) {
                newFields.insert(*it);
                m_fields.erase(it);
            }
        }
    }

    std::vector<std::pair<int, std::string>> entries;
    for (const auto& entry : m_fields)
        entries.emplace_back(entry);
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& entry : entries) {
        newFields.insert(entry);
    }

    m_fields = std::move(newFields);
}
