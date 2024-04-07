#pragma once

#include <openfix/LinkedHashMap.h>

#include <map>
#include <memory>
#include <sstream>
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
enum class FieldType {
    F(FIELD_TYPES)
};
#undef F
struct FieldTypes
{
#define F(x) {#x, FieldType::x}
    inline static std::unordered_map<std::string, FieldType> LOOKUP{FIELD_TYPES};
#undef F
};

struct GroupSpec
{
    using FieldSet = std::unordered_map<int, bool>;

    bool empty() const
    {
        return m_fields.empty() && m_groups.empty();
    }

    FieldSet m_fields;
    std::unordered_map<int, std::shared_ptr<GroupSpec>> m_groups;
    bool m_ordered = false;
    std::vector<int> m_fieldOrder;
};

class FieldMap
{
public:
    std::string getField(int tag) const
    {
        auto fit = m_fields.find(tag);
        if (fit != m_fields.end())
            return fit->second;

        auto git = m_groups.find(tag);
        if (git != m_groups.end())
            return std::to_string(git->second.size());

        throw FieldNotFound(tag);
    }

    bool tryGetBool(int tag) const
    {
        auto it = m_fields.find(tag);
        if (it == m_fields.end())
            return false;
        return it->second == "Y";
    }

    bool removeField(int tag)
    {
        return m_fields.erase(tag);
    }

    void setField(int tag, std::string value, bool order = true);

    const auto& getFields() const
    {
        return m_fields;
    }

    size_t getGroupCount(int tag) const
    {
        auto it = m_groups.find(tag);
        if (it == m_groups.end())
            return 0;
        return it->second.size();
    }

#define GET_GROUPS                \
        auto it = m_groups.find(tag); \
        if (it == m_groups.end())     \
            throw FieldNotFound(tag); \
        return it->second;

    std::vector<FieldMap>& getGroups(int tag)
    {
        GET_GROUPS
    }
    const std::vector<FieldMap>& getGroups(int tag) const
    {
        GET_GROUPS
    }
#undef GET_GROUPS

#define GET_GROUP                                                                \
        auto& vec = getGroups(tag);                                                  \
        if (idx >= vec.size())                                                       \
            throw std::out_of_range("Tried to access group " + std::to_string(tag)   \
                + " with out-of-bounds index " + std::to_string(idx));               \
        return vec[idx];

    FieldMap& getGroup(int tag, size_t idx)
    {
        GET_GROUP
    }
    const FieldMap& getGroup(int tag, size_t idx) const {GET_GROUP}
#undef GET_GROUP

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

    const std::unordered_map<int, std::vector<FieldMap>>& getGroups() const
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
    LinkedHashMap<int, std::string> m_fields;
    std::unordered_map<int, std::vector<FieldMap>> m_groups;

    // if this is a group present in our dictionary, we can reference additional metadata here
    std::shared_ptr<GroupSpec> m_groupSpec;

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

private:
    void toStream(std::ostream& ostr, char soh_char = EXTERNAL_SOH_CHAR) const;

    FieldMap m_header;
    FieldMap m_trailer;

    FieldMap m_body;

    friend std::ostream& operator<<(std::ostream&, const Message&);
};

std::ostream& operator<<(std::ostream& ostr, const FieldMap& fieldMap);
std::ostream& operator<<(std::ostream& ostr, const Message& msg);