#include "Message.h"

#include <charconv>
#include <functional>

#include "Checksum.h"
#include "Fields.h"

static inline bool isIgnoredTag(int tag)
{
    return tag == FIELD::BeginString || tag == FIELD::BodyLength || tag == FIELD::CheckSum;
}

// Pre-computed "tag=" strings for tags 0..1023 (covers nearly all FIX tags).
// Merging the tag number and '=' into a single append reduces per-field overhead.
struct TagEqStr { char buf[5]; uint8_t len; };
static const auto& getTagEqTable()
{
    static const auto table = [] {
        std::array<TagEqStr, 1024> t{};
        for (int i = 0; i < 1024; ++i) {
            const auto [ptr, ec] = std::to_chars(t[i].buf, t[i].buf + sizeof(t[i].buf) - 1, i);
            *ptr = '=';
            t[i].len = static_cast<uint8_t>(ptr - t[i].buf + 1);
        }
        return t;
    }();
    return table;
}

// Append "tag=" to out in a single operation.
static void appendTagEq(std::string& out, int tag)
{
    if (tag >= 0 && tag < 1024) [[likely]] {
        const auto& entry = getTagEqTable()[tag];
        out.append(entry.buf, entry.len);
    } else {
        char buf[12];
        const auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), tag);
        out.append(buf, ptr - buf);
        out += TAG_ASSIGNMENT_CHAR;
    }
}

static void appendInt(std::string& out, int val)
{
    char buf[12];
    const auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val);
    out.append(buf, ptr - buf);
}

static void appendGroup(std::string& out, const FieldMap& fieldMap, bool skipIgnoredTags, char soh_char, int& soh_char_count)
{
    if (fieldMap.empty())
        return;

    const auto& groups = fieldMap.getGroups();
    for (const auto& [k, v] : fieldMap.getFields()) {
        if (skipIgnoredTags && isIgnoredTag(k))
            continue;

        appendTagEq(out, k);
        out.append(v.data(), v.size());
        out += soh_char;
        ++soh_char_count;

        if (!groups.empty()) {
            const auto it = groups.find(k);
            if (it != groups.end())
                for (const auto& group : it->second)
                    appendGroup(out, group, skipIgnoredTags, soh_char, soh_char_count);
        }
    }

    if (!skipIgnoredTags && fieldMap.has(FIELD::CheckSum)) {
        appendTagEq(out, FIELD::CheckSum);
        out += fieldMap.getField(FIELD::CheckSum);
        out += soh_char;
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

void Message::serializeTo(std::string& result, char soh_char) const
{
    // Phase 1: serialize body content (header + body + trailer, minus ignored tags)
    // into a temporary buffer so we can measure BodyLength.
    // Thread-local buffer avoids re-allocation on each call.
    thread_local std::string body;
    body.clear();
    int soh_char_count = 1;  // at least 1 from the BodyLength tag itself
    appendGroup(body, m_header, true, soh_char, soh_char_count);
    appendGroup(body, m_body, true, soh_char, soh_char_count);
    appendGroup(body, m_trailer, true, soh_char, soh_char_count);

    // Phase 2: build result in a single buffer = prefix + BodyLength + body + checksum.
    // prefix: "8=<BeginString>SOH"  (typically 12-14 bytes)
    // bodyLen: "9=NNN...SOH"        (typically 5-8 bytes)
    // checksum: "10=NNNSO"          (always 7 bytes)
    result.clear();
    const size_t needed = 20 + body.size() + 8;
    if (result.capacity() < needed)
        result.reserve(needed);

    // Write BeginString prefix
    const auto bsIt = m_header.getFields().find(FIELD::BeginString);
    if (bsIt != m_header.getFields().end()) {
        result.append("8=", 2);
        result.append(bsIt->second.data(), bsIt->second.size());
        result += soh_char;
        ++soh_char_count;
    }

    // Write BodyLength tag
    result.append("9=", 2);
    appendInt(result, static_cast<int>(body.size()));
    result += soh_char;

    // Append body content
    result.append(body);

    // Compute checksum (SIMD-accelerated)
    uint8_t checksum = computeChecksum(result);
    if (soh_char != INTERNAL_SOH_CHAR)
        checksum += static_cast<uint8_t>(soh_char_count * (INTERNAL_SOH_CHAR - soh_char));
    const auto checksumStr = formatChecksum(checksum);

    result.append("10=", 3);
    result.append(checksumStr.data(), checksumStr.size());
    result += soh_char;
}

std::string Message::serialize(char soh_char) const
{
    std::string result;
    serializeTo(result, soh_char);
    return result;
}

std::string Message::toString(bool internal) const
{
    return serialize(internal ? INTERNAL_SOH_CHAR : EXTERNAL_SOH_CHAR);
}

void Message::toString(std::string& out, bool internal) const
{
    serializeTo(out, internal ? INTERNAL_SOH_CHAR : EXTERNAL_SOH_CHAR);
}

// deep-copy all string_views into owned storage
FieldMap::FieldMap(const FieldMap& other)
    : m_groups(other.m_groups)
    , m_groupSpec(other.m_groupSpec)
{
    const size_t n = other.m_fields.size();
    m_fields.reserve(n);
    m_ownedStorage.reserve(std::max(n, static_cast<size_t>(OWNED_STORAGE_CAPACITY)));
    for (const auto& [tag, sv] : other.m_fields) {
        m_ownedStorage.emplace_back(tag, std::string(sv));
        m_fields.insert({tag, std::string_view(m_ownedStorage.back().second)});
    }
}

FieldMap& FieldMap::operator=(const FieldMap& other)
{
    if (this == &other)
        return *this;

    m_fields = {};
    m_ownedStorage.clear();
    m_groups = other.m_groups;
    m_groupSpec = other.m_groupSpec;

    const size_t n = other.m_fields.size();
    m_fields.reserve(n);
    if (m_ownedStorage.capacity() < n)
        m_ownedStorage.reserve(std::max(n, static_cast<size_t>(OWNED_STORAGE_CAPACITY)));
    for (const auto& [tag, sv] : other.m_fields) {
        m_ownedStorage.emplace_back(tag, std::string(sv));
        m_fields.insert({tag, std::string_view(m_ownedStorage.back().second)});
    }
    return *this;
}

void FieldMap::setField(int tag, std::string_view value, bool order)
{
    // Linear scan is fast for typical FIX messages (5-20 fields) and
    // avoids per-node heap allocation of std::unordered_map.
    for (auto& [t, s] : m_ownedStorage) {
        if (t == tag) {
            s.assign(value.data(), value.size());
            insertFieldView(tag, std::string_view(s), order);
            return;
        }
    }
    // One-time reserve guarantees no reallocation (reference stability).
    // Zero cost for parsed messages that never reach this path.
    if (m_ownedStorage.empty())
        m_ownedStorage.reserve(OWNED_STORAGE_CAPACITY);
    m_ownedStorage.emplace_back(tag, std::string(value));
    insertFieldView(tag, std::string_view(m_ownedStorage.back().second), order);
}

void FieldMap::setFieldView(int tag, std::string_view value, bool order)
{
    insertFieldView(tag, value, order);
}

void FieldMap::insertFieldView(int tag, std::string_view value, bool order)
{
    // if we don't care about order we can put this tag anywhere
    if (!order || !m_groupSpec || !m_groupSpec->m_ordered) {
        m_fields[tag] = value;
        return;
    }

    // we care about order; we need to make sure we put this field exactly where it belongs
    const auto& field_order = m_groupSpec->m_fieldOrder;
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
    LinkedHashMap<int, std::string_view> newFields;

    if (m_groupSpec) {
        for (const auto tag : m_groupSpec->m_fieldOrder) {
            auto it = m_fields.find(tag);
            if (it != m_fields.end()) {
                newFields.insert(*it);
                m_fields.erase(it);
            }
        }
    }

    std::vector<std::pair<int, std::string_view>> entries;
    for (const auto& entry : m_fields)
        entries.emplace_back(entry);
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& entry : entries) {
        newFields.insert(entry);
    }

    m_fields = std::move(newFields);
}
