#include "Message.h"

#include <functional>
#include <unordered_set>

#include "Fields.h"

const std::unordered_set<int> IGNORED_TAGS = {FIELD::BeginString, FIELD::BodyLength, FIELD::CheckSum};

std::string printGroup(const FieldMap& fieldMap, bool skipIgnoredTags, char soh_char, int& soh_char_count)
{
    std::ostringstream ostr;

    if (!fieldMap.empty()) {
        for (const auto& [k, v] : fieldMap.getFields()) {
            // skip ignored tags
            if (skipIgnoredTags && IGNORED_TAGS.find(k) != IGNORED_TAGS.end())
                continue;

            ostr << k << TAG_ASSIGNMENT_CHAR << v << soh_char;
            ++soh_char_count;

            if (fieldMap.getGroupCount(k) > 0)
                for (const auto& group : fieldMap.getGroups(k))
                    ostr << printGroup(group, skipIgnoredTags, soh_char, soh_char_count);
        }

        if (!skipIgnoredTags && fieldMap.has(FIELD::CheckSum))
            ostr << FIELD::CheckSum << TAG_ASSIGNMENT_CHAR << fieldMap.getField(FIELD::CheckSum) << soh_char;
    }

    return ostr.str();
}

std::ostream& operator<<(std::ostream& ostr, const FieldMap& fieldMap)
{
    int tmp = 0;
    ostr << printGroup(fieldMap, false, EXTERNAL_SOH_CHAR, tmp);
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
    int soh_char_count = 1;  // at least 1 from tag 9
    auto it = m_header.getFields().find(FIELD::BeginString);
    if (it != m_header.getFields().end()) {
        ret << FIELD::BeginString << TAG_ASSIGNMENT_CHAR << it->second << soh_char;
        ++soh_char_count;
    }

    std::string body;
    body += printGroup(m_header, true, soh_char, soh_char_count);
    body += printGroup(m_body, true, soh_char, soh_char_count);
    body += printGroup(m_trailer, true, soh_char, soh_char_count);
    ret << FIELD::BodyLength << TAG_ASSIGNMENT_CHAR << body.size() << soh_char;
    ret << body;
    body = ret.str();
    // get checksum
    int checksum = 0;
    for (char c : body)
        checksum += c;
    if (soh_char != INTERNAL_SOH_CHAR)
        checksum += soh_char_count * (INTERNAL_SOH_CHAR - soh_char);
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

void FieldMap::setField(int tag, std::string value, bool order)
{
    // if we don't care about order we can put this tag anywhere
    if (!order || !m_groupSpec || !m_groupSpec->m_ordered) {
        m_fields[tag] = value;
        return;
    }

    // we care about order; we need to make sure we put this field exactly where it belongs
    auto& field_order = m_groupSpec->m_fieldOrder;
    size_t order_ptr = 0;
    auto field_it = m_fields.begin();

    while (field_it != m_fields.end()) {
        if (tag == field_it->first) {
            // we found the field! assign and return.
            field_it->second = value;
            return;
        }

        // advance our order_ptr; if we find the tag then we know we need to insert in front of field_it
        // if we find field_it, we can advance the outer iteration
        while (order_ptr < field_order.size() && field_order[order_ptr] != field_it->first && field_order[order_ptr] != tag)
            ++order_ptr;

        // we found our tag in the ordering and it doesn't line up to this tag
        if (order_ptr < field_order.size() && field_order[order_ptr] == tag) {
            // insert just before this field
            m_fields.insert(field_it, {tag, value});
            return;
        }

        ++field_it;
    }

    // this is the next field within our ordering, we're good to go
    m_fields[tag] = value;
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
