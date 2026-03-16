#pragma once

#include <openfix/LinkedHashMap.h>
#include <openfix/Types.h>

#include <charconv>
#include <map>
#include <memory>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Config.h"
#include "Exception.h"

inline constexpr char INTERNAL_SOH_CHAR = '\01';
inline constexpr char EXTERNAL_SOH_CHAR = '|';

inline constexpr char TAG_ASSIGNMENT_CHAR = '=';

#define FIELD_TYPES         \
    F(UNKNOWN),             \
                            \
    F(INT),                 \
    F(LENGTH),              \
    F(NUMINGROUP),          \
    F(SEQNUM),              \
    F(TAGNUM),              \
    F(DAYOFMONTH),          \
                            \
    F(FLOAT),               \
    F(QTY),                 \
    F(PRICE),               \
    F(PRICEOFFSET),         \
    F(AMT),                 \
    F(PERCENTAGE),          \
                            \
    F(CHAR),                \
    F(BOOLEAN),             \
                            \
    F(STRING),              \
    F(MULTIPLEVALUESTRING), \
    F(COUNTRY),             \
    F(CURRENCY),            \
    F(EXCHANGE),            \
    F(MONTHYEAR),           \
    F(UTCTIMESTAMP),        \
    F(UTCTIMEONLY),         \
    F(UTCDATEONLY),         \
    F(LOCALMKTDATE),        \
                            \
    F(DATA)

#define F(x) x
enum class FieldType
{
    F(FIELD_TYPES)
};
#undef F
struct FieldTypes
{
#define F(x) {#x, FieldType::x}
    inline static HashMapT<std::string, FieldType> LOOKUP{FIELD_TYPES};
#undef F
};

struct GroupSpec
{
    using FieldSet = HashMapT<int, bool>;

    bool empty() const
    {
        return m_fields.empty() && m_groups.empty();
    }

    FieldSet m_fields;
    HashMapT<int, std::shared_ptr<GroupSpec>> m_groups;
    bool m_ordered = false;
    std::vector<int> m_fieldOrder;
};

class FieldMap
{
public:
    FieldMap() = default;

    // Materializing copy: deep-copies all string_views into owned storage.
    FieldMap(const FieldMap& other);
    FieldMap& operator=(const FieldMap& other);

    // Default move is safe: unordered_map move preserves element addresses.
    FieldMap(FieldMap&&) = default;
    FieldMap& operator=(FieldMap&&) = default;

    std::string_view getField(int tag) const
    {
        const auto fit = m_fields.find(tag);
        if (fit != m_fields.end())
            return fit->second;

        throw FieldNotFound(tag);
    }

    int getIntField(int tag) const
    {
        const auto str = getField(tag);
        int val = 0;
        std::from_chars(str.data(), str.data() + str.size(), val);
        return val;
    }

    bool tryGetBool(int tag) const
    {
        const auto it = m_fields.find(tag);
        if (it == m_fields.end())
            return false;
        return it->second == "Y";
    }

    bool removeField(int tag)
    {
        return m_fields.erase(tag);
    }

    // Owning setField: copies value into internal storage.
    // Use for outbound message construction and any case where the
    // source string may not outlive this FieldMap.
    void setField(int tag, std::string_view value, bool order = true);

    // Zero-copy setField: stores the view directly without copying.
    // The caller must guarantee the underlying data outlives this FieldMap.
    // Used by Dictionary::parse() where views reference the original message text.
    void setFieldView(int tag, std::string_view value, bool order = true);

    void setField(int tag, int value, bool order = true)
    {
        char buf[12];
        const auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value);
        setField(tag, std::string_view(buf, ptr - buf), order);
    }

    const auto& getFields() const
    {
        return m_fields;
    }

    size_t getGroupCount(int tag) const
    {
        const auto it = m_groups.find(tag);
        if (it == m_groups.end())
            return 0;
        return it->second.size();
    }

    auto& getGroups(this auto& self, int tag)
    {
        auto it = self.m_groups.find(tag);
        if (it == self.m_groups.end())
            throw FieldNotFound(tag);
        return it->second;
    }

    auto& getGroup(this auto& self, int tag, size_t idx)
    {
        auto& vec = self.getGroups(tag);
        if (idx >= vec.size())
            throw std::out_of_range("Tried to access group " + std::to_string(tag)
                + " with out-of-bounds index " + std::to_string(idx));
        return vec[idx];
    }

    FieldMap& addGroup(int tag)
    {
        auto it = m_groups.find(tag);
        if (it == m_groups.end())
            it = m_groups.insert({tag, {}}).first;
        it->second.push_back({});
        return it->second[it->second.size() - 1];
    }

    bool removeGroups(int tag)
    {
        return m_groups.erase(tag);
    }

    const HashMapT<int, std::vector<FieldMap>>& getGroups() const
    {
        return m_groups;
    }

    bool empty() const
    {
        return m_fields.empty() && m_groups.empty();
    }

    bool has(int tag) const
    {
        return m_fields.find(tag) != m_fields.end();
    }

    void reserve(size_t n) { m_fields.reserve(n); }
    void reserveIndex(size_t maxKey) { m_fields.reserveIndex(maxKey); }

    void setSpec(std::shared_ptr<GroupSpec> spec)
    {
        m_groupSpec = std::move(spec);
    }

    const std::shared_ptr<GroupSpec>& getSpec() const
    {
        return m_groupSpec;
    }

    void sortFields();

    std::string toString()
    {
        std::ostringstream ostr;
        ostr << *this;
        return ostr.str();
    }

private:
    LinkedHashMap<int, std::string_view> m_fields;
    // Stable storage for owned field values (outbound messages, copies).
    // Tag-keyed so repeated setField() on the same tag overwrites in-place.
    // std::unordered_map guarantees reference stability on insert.
    std::unordered_map<int, std::string> m_ownedStorage;
    HashMapT<int, std::vector<FieldMap>> m_groups;

    // if this is a group present in our dictionary, we can reference additional metadata here
    std::shared_ptr<GroupSpec> m_groupSpec;

    // Internal: ordered insertion of a pre-resolved view (already owned or guaranteed-live).
    void insertFieldView(int tag, std::string_view value, bool order);

    friend std::ostream& operator<<(std::ostream&, const FieldMap&);
};

class Message
{
public:
    FieldMap& getHeader()
    {
        return m_header;
    }

    const FieldMap& getHeader() const
    {
        return m_header;
    }

    FieldMap& getTrailer()
    {
        return m_trailer;
    }

    const FieldMap& getTrailer() const
    {
        return m_trailer;
    }

    FieldMap& getBody()
    {
        return m_body;
    }

    const FieldMap& getBody() const
    {
        return m_body;
    }

    std::string toString(bool internal = false) const;

    // Access the raw message text (populated for inbound/parsed messages).
    const std::string& getSourceText() const { return m_sourceText; }

private:
    void toStream(std::ostream& ostr, char soh_char = EXTERNAL_SOH_CHAR) const;
    std::string serialize(char soh_char) const;

    FieldMap m_header;
    FieldMap m_trailer;

    FieldMap m_body;

    // Owned copy of the raw FIX message for parsed messages.
    // FieldMap string_views reference into this buffer.
    std::string m_sourceText;

    friend class Dictionary;
    friend std::ostream& operator<<(std::ostream&, const Message&);
};

std::ostream& operator<<(std::ostream& ostr, const FieldMap& fieldMap);
std::ostream& operator<<(std::ostream& ostr, const Message& msg);
