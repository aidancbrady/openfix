#include "Message.h"

#include "Fields.h"

#include <unordered_set>

const std::unordered_set<int> IGNORED_TAGS = {FIELD::BeginString, FIELD::BodyLength, FIELD::CheckSum};

std::string printGroup(const FieldMap& fieldMap, bool skipIgnoredTags)
{
    std::ostringstream ostr;

    if (!fieldMap.empty())
    {
        for (const auto& [k, v] : fieldMap.getFields())
        {
            // skip ignored tags
            if (skipIgnoredTags && IGNORED_TAGS.find(k) != IGNORED_TAGS.end())
                continue;

            ostr << k << TAG_ASSIGNMENT_CHAR << v << INTERNAL_SOH_CHAR;

            if (fieldMap.getGroupCount(k) > 0)
                for (const auto& group : fieldMap.getGroups(k))
                    ostr << group;
        }

        if (!skipIgnoredTags && fieldMap.has(FIELD::CheckSum))
            ostr << FIELD::CheckSum << TAG_ASSIGNMENT_CHAR << fieldMap.getField(FIELD::CheckSum) << INTERNAL_SOH_CHAR;
    }

    return ostr.str();
}

std::ostream& operator<<(std::ostream& ostr, const FieldMap& fieldMap)
{
    ostr << printGroup(fieldMap, false);
    return ostr;
}

std::ostream& operator<<(std::ostream& ostr, const Message& msg)
{
    std::ostringstream ret;
    auto it = msg.m_header.getFields().find(FIELD::BeginString);
    if (it != msg.m_header.getFields().end())
        ret << FIELD::BeginString << TAG_ASSIGNMENT_CHAR << it->second << INTERNAL_SOH_CHAR;
    std::string body;
    body += printGroup(msg.m_header, true);
    body += printGroup(msg.m_body, true);
    body += printGroup(msg.m_trailer, true);
    ret << FIELD::BodyLength << TAG_ASSIGNMENT_CHAR << body.size() << INTERNAL_SOH_CHAR;
    ret << body;
    body = ret.str();

    // get checksum
    std::string checksumStr;
    int checksum = 0;
    for (char c : body)
        checksum += c;
    checksum %= 256;
    for (int tmp = checksum; tmp < 100; checksumStr += "0", tmp *= 10);
    checksumStr += std::to_string(checksum);

    ostr << body;
    ostr << FIELD::CheckSum << TAG_ASSIGNMENT_CHAR << checksumStr << INTERNAL_SOH_CHAR;
    return ostr;
}
