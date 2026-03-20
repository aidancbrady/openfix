#pragma once

#include <openfix/LinkedHashMap.h>
#include <openfix/Types.h>

#include <array>
#include <charconv>
#include <cstdint>
#include <map>
#include <memory>
#include <sstream>
#include <string_view>
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
    static constexpr int FAST_LOOKUP_SIZE = 1024;

    using FieldSet = HashMapT<int, bool>;

    bool empty() const
    {
        return m_fields.empty() && m_groups.empty();
    }

    bool hasField(int tag) const
    {
        if (tag >= 0 && tag < FAST_LOOKUP_SIZE) [[likely]]
            return m_fieldLookup[tag];
        return m_fields.find(tag) != m_fields.end();
    }

    const std::shared_ptr<GroupSpec>* findGroup(int tag) const
    {
        if (tag >= 0 && tag < FAST_LOOKUP_SIZE && !m_groupLookup[tag]) [[likely]]
            return nullptr;
        const auto it = m_groups.find(tag);
        return it != m_groups.end() ? &it->second : nullptr;
    }

    // build flag lookup arrays for O(1) membership checks on hot path
    void buildLookup()
    {
        m_fieldLookup.fill(false);
        m_groupLookup.fill(false);
        for (const auto& [tag, _] : m_fields)
            if (tag >= 0 && tag < FAST_LOOKUP_SIZE)
                m_fieldLookup[tag] = true;
        for (const auto& [tag, _] : m_groups)
            if (tag >= 0 && tag < FAST_LOOKUP_SIZE)
                m_groupLookup[tag] = true;
    }

    FieldSet m_fields;
    HashMapT<int, std::shared_ptr<GroupSpec>> m_groups;
    bool m_ordered = false;
    std::vector<int> m_fieldOrder;

    // Flat bitset for O(1) field/group membership checks.
    std::array<bool, FAST_LOOKUP_SIZE> m_fieldLookup{};
    std::array<bool, FAST_LOOKUP_SIZE> m_groupLookup{};
};

class FieldMap
{
public:
    FieldMap() = default;

    // deep-copies all string_views into owned storage
    FieldMap(const FieldMap& other);
    FieldMap& operator=(const FieldMap& other);

    // default move is safe: unordered_map move preserves element addresses
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

    void setField(int tag, std::string_view value, bool order = true);
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

    FieldMap& addGroup(int tag, size_t reserveHint = 0)
    {
        auto it = m_groups.find(tag);
        if (it == m_groups.end()) {
            it = m_groups.insert({tag, {}}).first;
            if (reserveHint > 0)
                it->second.reserve(reserveHint);
        }
        it->second.push_back({});
        return it->second.back();
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

    void setSpec(std::shared_ptr<GroupSpec> spec)
    {
        m_groupSpec = spec.get();
    }

    void setSpec(const GroupSpec* spec)
    {
        m_groupSpec = spec;
    }

    const GroupSpec* getSpec() const
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
    // Parser-only combined check-and-insert: returns true if the field already
    // existed (duplicate). Uses a flat bitset for O(1) duplicate detection on
    // common tags (0..1023), falling back to O(n) LinkedHashMap scan for rare
    // high-numbered tags. Only valid on FieldMaps populated exclusively through
    // this method (the bitset is not updated by setField/setFieldView).
    bool setFieldViewOrDetectDup(int tag, std::string_view value)
    {
        if (tag >= 0 && tag < FAST_SEEN_SIZE) [[likely]] {
            const size_t word = static_cast<size_t>(tag) / 64;
            const uint64_t bit = uint64_t(1) << (static_cast<unsigned>(tag) % 64);
            if (m_seenTags[word] & bit)
                return true; // duplicate
            m_seenTags[word] |= bit;
            m_fields.push_back_unchecked_dangerous({tag, value});
            return false;
        }
        const auto [it, inserted] = m_fields.insert({tag, value});
        return !inserted;
    }

    // Flat bitset for O(1) duplicate detection in setFieldViewOrDetectDup().
    // 16 x uint64_t = 128 bytes, covers tags 0..1023.
    static constexpr int FAST_SEEN_SIZE = 1024;
    std::array<uint64_t, FAST_SEEN_SIZE / 64> m_seenTags{};

    LinkedHashMap<int, std::string_view> m_fields;
    // stable storage for owned field values (i.e. outbound messages, copies)
    static constexpr size_t OWNED_STORAGE_CAPACITY = 16;
    std::vector<std::pair<int, std::string>> m_ownedStorage;
    HashMapT<int, std::vector<FieldMap>> m_groups;

    // if this is a group present in our dictionary, we can reference additional metadata here
    const GroupSpec* m_groupSpec = nullptr;

    // (internal only) ordered insertion of a pre-resolved view (already owned or guaranteed-live).
    void insertFieldView(int tag, std::string_view value, bool order);

    friend class Dictionary;
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
    void toString(std::string& out, bool internal = false) const;

    const std::string& getSourceText() const { return m_sourceText; }

private:
    void toStream(std::ostream& ostr, char soh_char = EXTERNAL_SOH_CHAR) const;
    std::string serialize(char soh_char) const;
    void serializeTo(std::string& result, char soh_char) const;

    FieldMap m_header;
    FieldMap m_trailer;

    FieldMap m_body;

    // owned copy of the raw FIX message for parsed messages
    std::string m_sourceText;

    friend class Dictionary;
    friend std::ostream& operator<<(std::ostream&, const Message&);
};

std::ostream& operator<<(std::ostream& ostr, const FieldMap& fieldMap);
std::ostream& operator<<(std::ostream& ostr, const Message& msg);
