#include "Dictionary.h"

#include <strings.h>

#include <functional>
#include <list>

#include "Fields.h"
#include "pugixml.hpp"

enum class MessageState {
    HEADER,
    BODY,
    TRAILER
};

enum class ParserState {
    START,
    NEXT,
    KEY,
    VAL
};

struct ParserGroupInfo
{
    ParserGroupInfo(std::shared_ptr<GroupSpec> spec, std::reference_wrapper<FieldMap> group)
        : m_spec(std::move(spec))
        , m_group(group)
        , m_groupTag(0)
        , m_groupCount(0)
        , m_groupMaxCount(0)
    {}

    std::shared_ptr<GroupSpec> m_spec;
    std::reference_wrapper<FieldMap> m_group;

    int m_groupTag;

    size_t m_groupCount;
    size_t m_groupMaxCount;
};

#define TRY_LOG_WARN(msg) \
    if (loudParsing) {    \
        LOG_WARN(msg);    \
    }
#define TRY_LOG_ERROR(msg) \
    if (loudParsing) {     \
        LOG_ERROR(msg);    \
    }
#define TRY_LOG_THROW(msg)                         \
    {                                              \
        std::ostringstream ostr;                   \
        ostr << msg;                               \
        TRY_LOG_ERROR(msg);                        \
        if (!relaxedParsing)                       \
            throw MessageParsingError(ostr.str()); \
    }

Message Dictionary::parse(const SessionSettings& settings, const std::string& text) const
{
    bool loudParsing = settings.getBool(SessionSettings::LOUD_PARSING);
    bool relaxedParsing = settings.getBool(SessionSettings::RELAXED_PARSING);
    bool validateRequired = settings.getBool(SessionSettings::VALIDATE_REQUIRED_FIELDS);

    Message ret;

    MessageState msgState = MessageState::HEADER;
    ParserState state = ParserState::START;

    std::string key;
    std::string value;

    std::vector<ParserGroupInfo> groupStack;
    // start with header
    groupStack.emplace_back(m_headerSpec, ret.getHeader());

    auto curGroup = [&]() -> FieldMap& { return groupStack[groupStack.size() - 1].m_group.get(); };
    auto curSpec = [&]() -> const GroupSpec& { return *groupStack[groupStack.size() - 1].m_spec; };

    int checksum = 0;
    int tag = 0;
    int bodyLengthStart = 0;
    int dataLength = -1;
    int tagCount = 0;

    auto validateGroup = [&](FieldMap& group, const std::shared_ptr<GroupSpec>& spec) {
        group.setSpec(spec);
        if (spec->m_ordered)
            group.sortFields();
        if (!validateRequired)
            return;
        for (const auto& field : spec->m_fields) {
            if (field.second && !group.has(field.first))
                TRY_LOG_THROW("Message is missing required field: " << field.first);
        }
    };

    auto overwriteStack = [&](int curIdx) {
        while (static_cast<int>(groupStack.size()) > curIdx + 1) {
            auto& group = groupStack[groupStack.size() - 1];
            if (group.m_groupTag > 0 && group.m_groupCount < group.m_groupMaxCount)
                TRY_LOG_THROW("Repeating group terminated with count less than NumInGroup (tag=" << group.m_groupTag << ")");
            validateGroup(group.m_group.get(), group.m_spec);
            groupStack.pop_back();
        }
    };

    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];

        if (i == text.size() - 1 && c != INTERNAL_SOH_CHAR)
            TRY_LOG_THROW("Message does not end in SOH character");

        if (i < text.size() - 7)
            checksum += c;
        if (c == INTERNAL_SOH_CHAR) {
            // beginning SOH char
            if (state == ParserState::START) {
                TRY_LOG_THROW("Message begins with SOH character (idx=" << i << ")");
                state = ParserState::NEXT;
                continue;
            }

            // repeated SOH chars
            if (state == ParserState::NEXT) {
                TRY_LOG_WARN("Message has repeating SOH characters (idx=" << i << ")");
                continue;
            }

            auto setField = [&](FieldMap& fieldMap, int tag, std::string val) {
                if (getFieldType(tag) == FieldType::LENGTH) {
                    try {
                        dataLength = static_cast<size_t>(std::stoi(val.c_str()));
                    } catch (...) {
                        TRY_LOG_THROW("Couldn't parse data field (tag=" << tag << ")");
                    }
                }

                if (tag == FIELD::BodyLength)
                    bodyLengthStart = i + 1;
                // don't order, we batch order fields after parsing is done
                fieldMap.setField(tag, std::move(val), false);

                key.clear();
                value.clear();

                state = ParserState::NEXT;
            };

            auto handleRepeatingTag = [&](ParserGroupInfo& group, int groupIdx) {
                // we've already seen this tag; are we in a group?
                if (group.m_groupTag > 0) {
                    if (group.m_groupCount == group.m_groupMaxCount) {
                        TRY_LOG_THROW("Repeating group count exceeds NumInGroup (tag=" << tag << ")");
                        return -1;
                    }

                    validateGroup(group.m_group.get(), group.m_spec);

                    // create a duplicate group with this tag
                    auto& newGroup = groupStack[groupIdx - 1].m_group.get().addGroup(group.m_groupTag);
                    setField(newGroup, tag, value);
                    // replace current group on stack with new gruop
                    group.m_group = std::ref(newGroup);
                    ++group.m_groupCount;
                    return groupIdx;
                } else {
                    TRY_LOG_THROW("Message contains duplicate tags (tag=" << tag << ")");
                    return -1;
                }
            };

            auto trySetField = [&](ParserGroupInfo& group, int groupIdx) {
                // spec contains field
                auto fieldIt = group.m_spec->m_fields.find(tag);
                if (fieldIt != m_headerSpec->m_fields.end()) {
                    if (group.m_group.get().has(tag))
                        return handleRepeatingTag(group, groupIdx);

                    setField(group.m_group.get(), tag, value);
                    return groupIdx;
                }

                // spec contains group
                auto groupIt = group.m_spec->m_groups.find(tag);
                if (groupIt != group.m_spec->m_groups.end()) {
                    if (group.m_group.get().getGroupCount(tag) > 0)
                        return handleRepeatingTag(group, groupIdx);

                    // create a new group
                    auto& fieldMap = group.m_group.get().addGroup(tag);
                    ParserGroupInfo newGroup(groupIt->second, fieldMap);
                    newGroup.m_groupTag = tag;
                    newGroup.m_groupCount = 1;

                    try {
                        newGroup.m_groupMaxCount = std::stoi(value.c_str());
                    } catch (...) {
                        TRY_LOG_THROW("Couldn't parse NumInGroup (tag=" << tag << ")");
                        return -1;
                    }

                    groupStack.push_back(std::move(newGroup));
                    setField(group.m_group.get(), tag, value);
                    return groupIdx + 1;
                }

                // tag not in spec fields nor groups
                return -1;
            };

            // if we have a spec for this group
            if (!curSpec().empty()) {
                // try and set the field starting in the current group, and traverse up the stack on failure
                bool didSet = false;
                for (int i = groupStack.size() - 1; i >= 0; --i) {
                    int newIdx = trySetField(groupStack[i], i);
                    if (newIdx >= 0) {
                        overwriteStack(newIdx);
                        didSet = true;
                        break;
                    }
                }

                if (didSet)
                    continue;
            }

            // not in current structural group. if we're in the header, move to the body
            if (msgState == MessageState::HEADER) {
                // clear group stack
                overwriteStack(-1);

                auto it = m_bodySpecs.end();
                std::string msgType;
                try {
                    msgType = ret.getHeader().getField(FIELD::MsgType);
                    it = m_bodySpecs.find(msgType);
                } catch (...) {
                    TRY_LOG_THROW("Unknown message: " << msgType);
                }

                groupStack.emplace_back(it == m_bodySpecs.end() ? nullptr : it->second, ret.getBody());
                msgState = MessageState::BODY;
                if (trySetField(groupStack[0], 0) >= 0)
                    continue;
            }

            // if we're in the body, see if we can move to the trailer
            if (msgState == MessageState::BODY) {
                auto test = ParserGroupInfo(m_trailerSpec, ret.getTrailer());
                int newIdx = trySetField(test, 0);

                if (newIdx >= 0) {
                    validateGroup(groupStack[0].m_group.get(), groupStack[0].m_spec);
                    msgState = MessageState::TRAILER;
                    groupStack[0] = test;
                    overwriteStack(newIdx);
                    continue;
                }
            }

            // unknown field, all attempts failed; we assume it's in the current group
            TRY_LOG_ERROR("Unknown field (tag=" << tag << ")");
            setField(curGroup(), tag, value);

            continue;
        } else if (c == TAG_ASSIGNMENT_CHAR) {
            bool fail = false;
            if (state != ParserState::KEY) {
                // missing tag
                if (state == ParserState::START || state == ParserState::NEXT) {
                    TRY_LOG_THROW("Missing tag (idx=" << i << ")");
                }

                fail = true;
            }

            state = ParserState::VAL;

            try {
                tag = std::stoi(key.c_str());
            } catch (...) {
                TRY_LOG_THROW("Tag not int (tag=" << tag << ")");
                fail = true;
            }

            if (tagCount == 0 && tag != FIELD::BeginString)
                TRY_LOG_THROW("First field is not BeginString");
            if (tagCount == 1 && tag != FIELD::BodyLength)
                TRY_LOG_THROW("Second field is not BodyLength");
            if (tagCount == 2 && tag != FIELD::MsgType)
                TRY_LOG_THROW("Third field is not MsgType");

            ++tagCount;

            if (!fail && dataLength >= 0) {
                // data field handling
                if (getFieldType(tag) == FieldType::DATA) {
                    if (i + dataLength >= text.size())
                        TRY_LOG_THROW("Data tag length would exceed message size");
                    for (size_t j = 0; j < static_cast<size_t>(dataLength); ++j)
                        value += text[i + j + 1];
                    curGroup().setField(tag, value);
                    i += dataLength;
                }

                dataLength = -1;
                continue;
            }

            if (fail) {
                // go to next SOH
                while (i < text.size() - 1 && text[i + 1] != INTERNAL_SOH_CHAR)
                    ++i;
            }

            continue;
        }

        if (state != ParserState::VAL) {
            state = ParserState::KEY;
            key += c;
        } else {
            value += c;
        }
    }

    // clear trailer
    overwriteStack(-1);

    if (msgState != MessageState::TRAILER)
        TRY_LOG_THROW("Incomplete message");

    // missing trailing SOH char
    if (state != ParserState::NEXT)
        TRY_LOG_THROW("Missing trailing SOH character");

    if (!relaxedParsing) {
        // verify bodylength
        auto expectedLength = text.size() - bodyLengthStart - 7;
        try {
            auto bodyLength = static_cast<unsigned long>(std::stol(ret.getHeader().getField(FIELD::BodyLength).c_str()));
            if (expectedLength != bodyLength)
                throw std::exception();
        } catch (...) {
            TRY_LOG_THROW("Invalid BodyLength: expected " << expectedLength);
        }

        // verify checksum
        if (!ret.getTrailer().has(FIELD::CheckSum)) {
            TRY_LOG_THROW("Footer missing CheckSum");
        } else {
            checksum %= 256;
            std::string checksumStr = std::to_string(checksum);
            while (checksumStr.size() < 3)
                checksumStr = '0' + checksumStr;

            if (tag != FIELD::CheckSum)
                TRY_LOG_THROW("Message didn't end in checksum");
            const auto& checksumRet = ret.getTrailer().getField(FIELD::CheckSum);

            if (checksumRet != checksumStr) {
                TRY_LOG_THROW("Invalid checksum: expected " << checksumStr << ", received " << checksumRet);
            }
        }
    }

    // remove checksum
    ret.getTrailer().removeField(FIELD::CheckSum);

    return ret;
}

std::shared_ptr<Dictionary> DictionaryRegistry::load(const std::string& path)
{
    auto it = m_dictionaries.find(path);
    if (it != m_dictionaries.end())
        return it->second;

    LOG_INFO("Loading FIX dictionary at path: " << path);

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(path.c_str());

    auto root = doc.child("fix");

    if (!result)
        throw DictionaryParsingError("Unable to load FIX dictionary: " + std::string(result.description()));

    auto dict = std::make_shared<Dictionary>();

    auto header = root.child("header");
    if (header.empty())
        throw DictionaryParsingError("FIX dictionary missing <header> section");

    auto trailer = root.child("trailer");
    if (trailer.empty())
        throw DictionaryParsingError("FIX dictionary missing <trailer> section");

    std::unordered_map<std::string, int> fieldMap;

    auto fields = root.child("fields");
    for (auto field : fields.children()) {
        int tag = field.attribute("number").as_int(-1);
        std::string name = field.attribute("name").as_string();
        std::string type = field.attribute("type").as_string();
        if (tag == -1 || name.empty() || type.empty())
            throw DictionaryParsingError("Invalid <field> definition");

        auto it = FieldTypes::LOOKUP.find(type);
        if (it == FieldTypes::LOOKUP.end())
            throw DictionaryParsingError("Unknown field type: " + type);

        if (dict->m_fields.find(tag) != dict->m_fields.end())
            throw DictionaryParsingError("Multiple field definitions for tag: " + std::to_string(tag));

        dict->m_fields[tag] = it->second;
        fieldMap[name] = tag;
    }

    auto components = root.child("components");
    std::unordered_map<std::string, GroupSpec> componentMap;

    std::unordered_map<std::string, pugi::xml_node> componentXMLMap;
    std::unordered_map<std::string, std::unordered_set<std::string>> componentGraph;

    // fully qualified name since we call this recursively
    std::function<void(const pugi::xml_node&, std::string)> validateGroup = [&](const pugi::xml_node& node, std::string parentComponent) {
        for (const auto& child : node) {
            if (strcasecmp(child.name(), "component") == 0) {
                std::string componentName = child.attribute("name").as_string();
                if (componentName.empty())
                    throw DictionaryParsingError("Tried to reference a component without specifying a name");
                if (componentMap.find(componentName) == componentMap.end())
                    throw DictionaryParsingError("Tried to reference undefined component: " + componentName);

                // no dupe checking for now, doesn't have an impact anyway
                if (!parentComponent.empty())
                    componentGraph[parentComponent].insert(componentName);
            } else if (strcasecmp(child.name(), "group") == 0) {
                std::string groupName = child.attribute("name").as_string();
                if (groupName.empty())
                    throw DictionaryParsingError("Tried to reference a group without specifying a name");
                auto it = fieldMap.find(groupName);
                if (it == fieldMap.end())
                    throw DictionaryParsingError("Tried to reference undefined group: " + groupName);

                validateGroup(child, parentComponent);
            } else if (strcasecmp(child.name(), "field") == 0) {
                std::string fieldName = child.attribute("name").as_string();
                if (fieldName.empty())
                    throw DictionaryParsingError("Tried to reference a field without specifying a name");
                auto it = fieldMap.find(fieldName);
                if (it == fieldMap.end())
                    throw DictionaryParsingError("Tried to reference undefined field: " + fieldName);
            } else {
                // unknown definition, ignore
            }
        }
    };

    // first pass, initialize our maps
    for (const auto& component : components.children()) {
        std::string name = component.attribute("name").as_string();
        if (name.empty())
            throw DictionaryParsingError("Component definition missing name");

        if (componentMap.find(name) != componentMap.end())
            throw DictionaryParsingError("Multiple component definitions with name: " + name);

        componentMap[name] = {};
        componentGraph[name] = {};
        componentXMLMap[name] = component;
    }

    // second pass, validate & build our graph
    for (const auto& component : components.children()) {
        std::string name = component.attribute("name").as_string();
        // treat this like a group, validate fields and populate our component graph
        validateGroup(component, name);
    }

    // third pass, toposort
    std::vector<std::string> sorted;
    {
        std::unordered_map<std::string, int> indegrees;
        for (const auto& [node, children] : componentGraph) {
            if (indegrees.find(node) == indegrees.end())
                indegrees[node] = 0;
            for (const auto& child : children)
                ++indegrees[child];
        }

        std::list<std::string> workingList;
        for (const auto& [k, i] : indegrees)
            if (i == 0)
                workingList.push_front(k);

        while (!workingList.empty()) {
            std::string node = workingList.back();
            sorted.push_back(node);
            workingList.pop_back();
            for (const auto& child : componentGraph[node]) {
                --indegrees[child];
                if (indegrees[child] == 0)
                    workingList.push_front(child);
            }
        }

        if (sorted.size() != componentGraph.size())
            throw DictionaryParsingError("Cycle in component graph!");
    }

    std::function<std::shared_ptr<GroupSpec>(const pugi::xml_node&)> buildGroup = [&](const pugi::xml_node& node) {
        std::shared_ptr<GroupSpec> ret = std::make_shared<GroupSpec>();
        ret->m_ordered = node.attribute("ordered").as_bool();

        for (const auto& field : node) {
            if (strcasecmp(field.name(), "component") == 0) {
                std::string componentName = field.attribute("name").as_string();
                // this must be a completed component; just merge everything in
                auto& component = componentMap[componentName];
                for (auto entry : component.m_fields) {
                    if (!ret->m_fields.insert(entry).second)
                        throw DictionaryParsingError("Multiple references of field in group: " + std::to_string(entry.first));
                    ret->m_fieldOrder.push_back(entry.first);
                }

                for (auto& entry : component.m_groups) {
                    if (!ret->m_groups.insert(entry).second)
                        throw DictionaryParsingError("Multiple references of group in group: " + std::to_string(entry.first));
                    ret->m_fieldOrder.push_back(entry.first);
                }
            } else if (strcasecmp(field.name(), "group") == 0) {
                std::string groupName = field.attribute("name").as_string();
                int tag = fieldMap[groupName];
                if (ret->m_groups.find(tag) != ret->m_groups.end())
                    throw DictionaryParsingError("Multiple references of group in group: " + std::to_string(tag));
                ret->m_groups[tag] = buildGroup(field);
                ret->m_fieldOrder.push_back(tag);
            } else if (strcasecmp(field.name(), "field") == 0) {
                std::string fieldName = field.attribute("name").as_string();
                bool required = field.attribute("required").as_bool(false);
                int tag = fieldMap[fieldName];
                if (ret->m_fields.find(tag) != ret->m_fields.end())
                    throw DictionaryParsingError("Multiple references of field in group: " + std::to_string(tag));
                ret->m_fields.insert({fieldMap[fieldName], required});
                ret->m_fieldOrder.push_back(tag);
            }
        }

        return ret;
    };

    // go through components in reverse topologically sorted order, building out component map
    for (int i = sorted.size() - 1; i >= 0; --i) {
        const auto& name = sorted[i];
        auto& node = componentXMLMap[name];
        componentMap[name] = *buildGroup(node);
    }

    // build header
    dict->m_headerSpec = buildGroup(header);
    // build trailer
    dict->m_trailerSpec = buildGroup(trailer);
    // build messages
    for (const auto& node : root.child("messages").children()) {
        std::string msgtype = node.attribute("msgtype").as_string();
        if (empty(msgtype))
            throw DictionaryParsingError("msgtype definition missing from message");
        if (dict->m_bodySpecs.find(msgtype) != dict->m_bodySpecs.end())
            throw DictionaryParsingError("Redefinition of message type: " + msgtype);
        dict->m_bodySpecs[msgtype] = buildGroup(node);
    }

    m_dictionaries[path] = dict;
    return dict;
}
