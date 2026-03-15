#include "Message.h"

#include <charconv>
#include <functional>

#include "Checksum.h"
#include "Fields.h"

const HashSetT<int> IGNORED_TAGS = {FIELD::BeginString, FIELD::BodyLength, FIELD::CheckSum};

static void appendInt(std::string& out, int val)
{
    char buf[12];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val);
    out.append(buf, ptr - buf);
}

static void appendGroup(std::string& out, const FieldMap& fieldMap, bool skipIgnoredTags, char soh_char, int& soh_char_count)
{
    if (!fieldMap.empty()) {
        for (const auto& [k, v] : fieldMap.getFields()) {
            if (skipIgnoredTags && IGNORED_TAGS.find(k) != IGNORED_TAGS.end())
                continue;

            appendInt(out, k);
            out += TAG_ASSIGNMENT_CHAR;
            out += v;
            out += soh_char;
            ++soh_char_count;

            if (fieldMap.getGroupCount(k) > 0)
                for (const auto& group : fieldMap.getGroups(k))
                    appendGroup(out, group, skipIgnoredTags, soh_char, soh_char_count);
        }

        if (!skipIgnoredTags && fieldMap.has(FIELD::CheckSum)) {
            appendInt(out, FIELD::CheckSum);
            out += TAG_ASSIGNMENT_CHAR;
            out += fieldMap.getField(FIELD::CheckSum);
            out += soh_char;
        }
    }
}

std::ostream& operator<<(std::ostream& ostr, const FieldMap& fieldMap)
{
    std::string out;
    int tmp = 0;
    appendGroup(out, fieldMap, false, EXTERNAL_SOH_CHAR, tmp);
    ostr << out;
    return ostr;
}

std::ostream& operator<<(std::ostream& ostr, const Message& msg)
{
    msg.toStream(ostr);
    return ostr;
}

void Message::toStream(std::ostream& ostr, char soh_char) const
{
    ostr << serialize(soh_char);
}

std::string Message::serialize(char soh_char) const
{
    std::string prefix;
    prefix.reserve(32);
    int soh_char_count = 1;  // at least 1 from tag 9
    auto it = m_header.getFields().find(FIELD::BeginString);
    if (it != m_header.getFields().end()) {
        appendInt(prefix, FIELD::BeginString);
        prefix += TAG_ASSIGNMENT_CHAR;
        prefix += it->second;
        prefix += soh_char;
        ++soh_char_count;
    }

    std::string body;
    body.reserve(256);
    appendGroup(body, m_header, true, soh_char, soh_char_count);
    appendGroup(body, m_body, true, soh_char, soh_char_count);
    appendGroup(body, m_trailer, true, soh_char, soh_char_count);

    // build BodyLength tag
    std::string bodyLenTag;
    bodyLenTag.reserve(16);
    appendInt(bodyLenTag, FIELD::BodyLength);
    bodyLenTag += TAG_ASSIGNMENT_CHAR;
    appendInt(bodyLenTag, static_cast<int>(body.size()));
    bodyLenTag += soh_char;

    // assemble: prefix + bodyLength + body
    std::string result;
    result.reserve(prefix.size() + bodyLenTag.size() + body.size() + 16);
    result += prefix;
    result += bodyLenTag;
    result += body;

    // get checksum (SIMD-accelerated for large messages)
    uint8_t checksum = computeChecksum(result);
    if (soh_char != INTERNAL_SOH_CHAR)
        checksum += static_cast<uint8_t>(soh_char_count * (INTERNAL_SOH_CHAR - soh_char));
    auto checksumStr = formatChecksum(checksum);

    appendInt(result, FIELD::CheckSum);
    result += TAG_ASSIGNMENT_CHAR;
    result += checksumStr;
    result += soh_char;

    return result;
}

std::string Message::toString(bool internal) const
{
    return serialize(internal ? INTERNAL_SOH_CHAR : EXTERNAL_SOH_CHAR);
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
