#pragma once

#include "Exception.h"
#include "Config.h"

#include <vector>
#include <sstream>
#include <unordered_map>
#include <map>

inline constexpr char INTERNAL_SOH_CHAR = '\01';
inline constexpr char EXTERNAL_SOH_CHAR = '|';

inline constexpr char TAG_ASSIGNMENT_CHAR = '=';

enum class FieldType
{
    CHAR,
    BOOLEAN,
    FLOAT,
    AMT,
    PRICE,
    PRICE_OFFSET,
    QTY,
    PERCENTAGE,
    INT,
    DAY_OF_MONTH,
    LENGTH,
    NUM_IN_GROUP,
    SEQ_NUM,
    STRING,
    // DATA,
    MONTH_YEAR,
    CURRENCY,
    EXCHANGE,
    LOCAL_MKT_DATE,
    MULTIPLE_VALUE_STRING,
    UTC_DATE,
    UTC_TIME_ONLY,
    UTC_TIMESTAMP,
    COUNTRY
};

class FieldMap
{
public:
    const std::string& getField(int tag) const
    {
        auto fit = m_fields.find(tag);
        if (fit != m_fields.end())
            return fit->second;

        auto git = m_groups.find(tag);
        if (git != m_groups.end())
            return std::to_string(git->second.size());
        
        throw FieldNotFound(tag);
    }

    bool removeField(int tag)
    {
        return m_fields.erase(tag);
    }

    void setField(int tag, std::string value)
    {
        m_fields[tag] = value;
    }

    const std::map<int, std::string>& getFields() const
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

    std::vector<FieldMap>& getGroup(int tag)
    {
        auto it = m_groups.find(tag);
        if (it == m_groups.end())
            throw FieldNotFound(tag);
        return it->second;
    }

    FieldMap& getGroup(int tag, int idx)
    {
        auto& vec = getGroup(tag);
        if (idx >= vec.size())
            throw std::out_of_range("Tried to access group " + std::to_string(tag) + " with out-of-bounds index " + std::to_string(idx));
        return vec[idx];
    }

    FieldMap& addGroup(int tag)
    {
        auto it = m_groups.find(tag);
        if (it == m_groups.end())
            it = m_groups.insert({tag, {}}).first;
        it->second.push_back({});
        return it->second[it->second.size()-1];
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
        return m_fields.find(tag) != m_fields.end() || m_groups.find(tag) != m_groups.end();
    }

    std::string toString(const SessionSettings& settings);

    std::string toString()
    {
        std::ostringstream ostr;
        ostr << *this;
        return ostr.str();
    }

private:
    std::map<int, std::string> m_fields;
    std::unordered_map<int, std::vector<FieldMap>> m_groups;

    friend std::ostream& operator<<(std::ostream&, const FieldMap&);
};

class Message
{
public:
    FieldMap& getHeader()
    {
        return m_header;
    }

    FieldMap& getFooter()
    {
        return m_footer;
    }

    FieldMap& getBody()
    {
        return m_body;
    }

    std::string toString()
    {
        std::ostringstream ostr;
        ostr << *this;
        return ostr.str();
    }

private:
    FieldMap m_header;
    FieldMap m_footer;

    FieldMap m_body;

    friend std::ostream& operator<<(std::ostream&, const Message&);
};

std::ostream& operator<<(std::ostream& ostr, const FieldMap& fieldMap);
std::ostream& operator<<(std::ostream& ostr, const Message& msg);