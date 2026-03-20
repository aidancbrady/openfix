#include "Dictionary.h"

#include <strings.h>

#include <charconv>
#include <functional>
#include <list>

#include "Checksum.h"
#include "Fields.h"
#include "pugixml.hpp"

enum class MessageState
{
    HEADER,
    BODY,
    TRAILER
};

struct ParserGroupInfo
{
    const GroupSpec* m_spec = nullptr;
    FieldMap* m_group = nullptr;

    int m_groupTag = 0;

    size_t m_groupCount = 0;
    size_t m_groupMaxCount = 0;
};

inline int fastParseTag(const char* begin, const char* end)
{
    int val = 0;
    for (const char* p = begin; p < end; ++p) {
        const unsigned int d = static_cast<unsigned int>(*p) - '0';
        if (d > 9) [[unlikely]]
            return -1;
        val = val * 10 + static_cast<int>(d);
    }
    return (begin == end) ? -1 : val;
}

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
        do {                                       \
        std::ostringstream ostr;                   \
        ostr << msg;                               \
        TRY_LOG_ERROR(msg);                        \
        if (!relaxedParsing)                       \
            throw MessageParsingError(ostr.str()); \
        } while (0);                               \
    }

Message Dictionary::parse(const SessionSettings& settings, std::string text_in) const
{
    const bool loudParsing = settings.getBool(SessionSettings::LOUD_PARSING);
    const bool relaxedParsing = settings.getBool(SessionSettings::RELAXED_PARSING);
    const bool validateRequired = settings.getBool(SessionSettings::VALIDATE_REQUIRED_FIELDS);
    const bool reorderTags = settings.getBool(SessionSettings::PARSING_REORDER_TAGS);

    Message ret;

    // for incoming messages, own the original text and provide views into it for zero-copy parsing
    ret.m_sourceText = std::move(text_in);
    const std::string& text = ret.m_sourceText;

    // pre-allocate FieldMaps: typical FIX field is ~8 chars
    const size_t estimatedFields = text.size() / 8;
    ret.getHeader().reserve(10);
    ret.getBody().reserve(estimatedFields > 11 ? estimatedFields - 11 : 4);
    ret.getTrailer().reserve(2);

    MessageState msgState = MessageState::HEADER;

    // stack-local storage for groupStack (avoid heap allocation here)
    ParserGroupInfo groupStackBuf[8];
    int groupStackSize = 0;
    auto groupStackPop = [&]() { --groupStackSize; };

    // start with header
    groupStackBuf[groupStackSize++] = {m_headerSpec.get(), &ret.getHeader()};

    auto curGroup = [&]() -> FieldMap& { return *groupStackBuf[groupStackSize - 1].m_group; };
    auto curSpec = [&]() -> const GroupSpec& { return *groupStackBuf[groupStackSize - 1].m_spec; };

    int tag = 0;
    int bodyLengthStart = 0;
    int dataLength = -1;
    int tagCount = 0;

    auto validateGroup = [&](FieldMap& group, const GroupSpec* spec) {
        group.setSpec(spec);
        if (reorderTags && spec->m_ordered)
            group.sortFields();
        if (!validateRequired)
            return;
        for (const auto& field : spec->m_fields) {
            if (field.second && !group.has(field.first))
                TRY_LOG_THROW("Message is missing required field: " << field.first);
        }
    };

    auto overwriteStack = [&](int curIdx) {
        while (groupStackSize > curIdx + 1) {
            auto& group = groupStackBuf[groupStackSize - 1];
            if (group.m_groupTag > 0 && group.m_groupCount < group.m_groupMaxCount)
                TRY_LOG_THROW("Repeating group terminated with count less than NumInGroup (tag=" << group.m_groupTag << ")");
            validateGroup(*group.m_group, group.m_spec);
            groupStackPop();
        }
    };

    // --- memchr-based field extraction loop ---
    // instead of scanning char-by-char, use memchr to jump directly.
    // glibc's memchr uses SIMD (SSE2/AVX2) internally, making these jumps fast for large messages
    const char* pos = text.data();
    const char* const textEnd = text.data() + text.size();
    // setField lambda for fallback paths (groups, header->body transition, etc.)
    auto setField = [&](FieldMap& fieldMap, int tag, std::string_view val) {
        if (getFieldType(tag) == FieldType::LENGTH) [[unlikely]] {
            const int parsed = fastParseTag(val.data(), val.data() + val.size());
            if (parsed < 0) [[unlikely]] {
                TRY_LOG_THROW("Couldn't parse data field (tag=" << tag << ")");
            } else {
                dataLength = parsed;
            }
        }

        if (tag == FIELD::BodyLength)
            bodyLengthStart = static_cast<int>(val.data() + val.size() - text.data()) + 1;
        fieldMap.setFieldView(tag, val, false);
    };

    auto handleRepeatingTag = [&](ParserGroupInfo& group, int groupIdx, int tag, std::string_view val) {
        if (group.m_groupTag > 0) {
            if (group.m_groupCount == group.m_groupMaxCount) {
                TRY_LOG_THROW("Repeating group count exceeds NumInGroup (tag=" << group.m_groupTag << ")");
                return -1;
            }

            validateGroup(*group.m_group, group.m_spec);

            auto& newGroup = groupStackBuf[groupIdx - 1].m_group->addGroup(group.m_groupTag);
            // setFieldViewOrDetectDup() required here to update bitset
            newGroup.setFieldViewOrDetectDup(tag, val);
            if (getFieldType(tag) == FieldType::LENGTH) [[unlikely]] {
                const int parsed = fastParseTag(val.data(), val.data() + val.size());
                if (parsed >= 0)
                    dataLength = parsed;
            }
            group.m_group = &newGroup;
            ++group.m_groupCount;
            return groupIdx;
        } else {
            TRY_LOG_THROW("Message contains duplicate tags (tag=" << tag << ")");
            return -1;
        }
    };

    auto trySetField = [&](ParserGroupInfo& group, int groupIdx, int tag, std::string_view val) {
        if (group.m_spec->hasField(tag)) {
            // returns true if the field was a duplicate (already seen in this group)
            if (group.m_group->setFieldViewOrDetectDup(tag, val))
                return handleRepeatingTag(group, groupIdx, tag, val);

            // field was inserted; handle LENGTH type and BodyLength tracking
            if (getFieldType(tag) == FieldType::LENGTH) [[unlikely]] {
                const int parsed = fastParseTag(val.data(), val.data() + val.size());
                if (parsed < 0) [[unlikely]] {
                    TRY_LOG_THROW("Couldn't parse data field (tag=" << tag << ")");
                } else {
                    dataLength = parsed;
                }
            }
            if (tag == FIELD::BodyLength)
                bodyLengthStart = static_cast<int>(val.data() + val.size() - text.data()) + 1;
            return groupIdx;
        }

        const auto* groupSpec = group.m_spec->findGroup(tag);
        if (groupSpec) {
            if (group.m_group->getGroupCount(tag) > 0)
                return handleRepeatingTag(group, groupIdx, tag, val);

            const int parsed = fastParseTag(val.data(), val.data() + val.size());
            if (parsed < 0) [[unlikely]] {
                TRY_LOG_THROW("Couldn't parse NumInGroup (tag=" << tag << ")");
                return -1;
            }

            auto& fieldMap = group.m_group->addGroup(tag, static_cast<size_t>(parsed));
            groupStackBuf[groupStackSize++] = {groupSpec->get(), &fieldMap, tag, 1, static_cast<size_t>(parsed)};
            setField(*group.m_group, tag, val);
            return groupIdx + 1;
        }

        return -1;
    };

    auto dispatchField = [&](int tag, std::string_view val) {
        // fast path: try current group stack with spec
        if (!curSpec().empty()) {
            for (int j = groupStackSize - 1; j >= 0; --j) {
                const int newIdx = trySetField(groupStackBuf[j], j, tag, val);
                if (newIdx >= 0) {
                    overwriteStack(newIdx);
                    return;
                }
            }
        }

        // header -> body transition
        if (msgState == MessageState::HEADER) {
            overwriteStack(-1);

            const GroupSpec* bodySpec = nullptr;
            try {
                const auto msgType = ret.getHeader().getField(FIELD::MsgType);
                bodySpec = getMessageSpecRaw(msgType);
            } catch (...) {
                TRY_LOG_THROW("Unknown message type");
            }

            groupStackBuf[groupStackSize++] = {bodySpec, &ret.getBody()};
            msgState = MessageState::BODY;
            if (trySetField(groupStackBuf[0], 0, tag, val) >= 0)
                return;
        }

        // body -> trailer transition
        if (msgState == MessageState::BODY) {
            ParserGroupInfo test{m_trailerSpec.get(), &ret.getTrailer()};
            const int newIdx = trySetField(test, 0, tag, val);

            if (newIdx >= 0) {
                validateGroup(*groupStackBuf[0].m_group, groupStackBuf[0].m_spec);
                msgState = MessageState::TRAILER;
                groupStackBuf[0] = test;
                overwriteStack(newIdx);
                return;
            }
        }

        TRY_LOG_ERROR("Unknown field (tag=" << tag << ")");
        setField(curGroup(), tag, val);
    };

    while (pos < textEnd) {
        // find '=' delimiter using memchr (SIMD-accelerated in glibc)
        const char* eq = static_cast<const char*>(memchr(pos, TAG_ASSIGNMENT_CHAR, textEnd - pos));
        if (!eq) [[unlikely]] {
            TRY_LOG_THROW("Missing tag assignment in remaining message");
            break;
        }

        // parse tag number from [pos, eq)
        tag = fastParseTag(pos, eq);
        if (tag < 0) [[unlikely]] {
            TRY_LOG_THROW("Tag not int");
            // skip to next SOH
            const char* soh = static_cast<const char*>(memchr(eq + 1, INTERNAL_SOH_CHAR, textEnd - eq - 1));
            pos = soh ? soh + 1 : textEnd;
            ++tagCount;
            continue;
        }

        if (tagCount < 3) [[unlikely]] {
            if (tagCount == 0 && tag != FIELD::BeginString)
                TRY_LOG_THROW("First field is not BeginString");
            if (tagCount == 1 && tag != FIELD::BodyLength)
                TRY_LOG_THROW("Second field is not BodyLength");
            if (tagCount == 2 && tag != FIELD::MsgType)
                TRY_LOG_THROW("Third field is not MsgType");
        }
        ++tagCount;

        const char* valStart = eq + 1;

        // handle DATA fields: value length is predetermined, may contain SOH
        if (dataLength >= 0) [[unlikely]] {
            if (getFieldType(tag) == FieldType::DATA) {
                if (valStart + dataLength > textEnd)
                    TRY_LOG_THROW("Data tag length would exceed message size");
                const std::string_view data_val(valStart, static_cast<size_t>(dataLength));
                curGroup().setFieldView(tag, data_val, false);
                // skip past the data value + trailing SOH
                pos = valStart + dataLength + 1;
                dataLength = -1;
                continue;
            }
            dataLength = -1;
        }

        // find SOH delimiter (end of value)
        const char* soh = static_cast<const char*>(memchr(valStart, INTERNAL_SOH_CHAR, textEnd - valStart));
        if (!soh) [[unlikely]] {
            TRY_LOG_THROW("Message does not end in SOH character");
            break;
        }

        const std::string_view val(valStart, soh - valStart);

        // dispatch the tag=value pair
        dispatchField(tag, val);

        pos = soh + 1;
    }

    // clear trailer
    overwriteStack(-1);

    if (msgState != MessageState::TRAILER)
        TRY_LOG_THROW("Incomplete message");

    if (!relaxedParsing) {
        // verify bodylength
        const auto expectedLength = text.size() - bodyLengthStart - 7;
        {
            const auto blStr = ret.getHeader().getField(FIELD::BodyLength);
            unsigned long bodyLength = 0;
            auto [ptr, ec] = std::from_chars(blStr.data(), blStr.data() + blStr.size(), bodyLength);
            if (ec != std::errc{} || expectedLength != bodyLength)
                TRY_LOG_THROW("Invalid BodyLength: expected " << expectedLength);
        }

        // verify checksum (SIMD-accelerated for large messages)
        if (!ret.getTrailer().has(FIELD::CheckSum)) {
            TRY_LOG_THROW("Footer missing CheckSum");
        } else {
            // checksum covers everything except the trailing "10=XXX\x01" (7 bytes)
            const auto checksumStr = formatChecksum(computeChecksum(text.data(), text.size() - 7));

            if (tag != FIELD::CheckSum)
                TRY_LOG_THROW("Message didn't end in checksum");
            const auto checksumRet = ret.getTrailer().getField(FIELD::CheckSum);

            if (checksumRet != checksumStr.view()) {
                TRY_LOG_THROW("Invalid checksum: expected " << checksumStr.view() << ", received " << checksumRet);
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
    const pugi::xml_parse_result result = doc.load_file(path.c_str());

    const auto root = doc.child("fix");

    if (!result)
        throw DictionaryParsingError("Unable to load FIX dictionary: " + std::string(result.description()));

    const auto dict = std::make_shared<Dictionary>();

    const auto header = root.child("header");
    if (header.empty())
        throw DictionaryParsingError("FIX dictionary missing <header> section");

    const auto trailer = root.child("trailer");
    if (trailer.empty())
        throw DictionaryParsingError("FIX dictionary missing <trailer> section");

    HashMapT<std::string, int> fieldMap;

    const auto fields = root.child("fields");
    for (const auto field : fields.children()) {
        const int tag = field.attribute("number").as_int(-1);
        const std::string name = field.attribute("name").as_string();
        const std::string type = field.attribute("type").as_string();
        if (tag == -1 || name.empty() || type.empty())
            throw DictionaryParsingError("Invalid <field> definition");

        auto it = FieldTypes::LOOKUP.find(type);
        if (it == FieldTypes::LOOKUP.end())
            throw DictionaryParsingError("Unknown field type: " + type);

        if (tag >= 0 && tag < Dictionary::MAX_FIELD_TAG) {
            if (dict->m_fieldTypes[tag] != FieldType::UNKNOWN)
                throw DictionaryParsingError("Multiple field definitions for tag: " + std::to_string(tag));
            dict->m_fieldTypes[tag] = it->second;
        } else {
            if (dict->m_fieldsFallback.find(tag) != dict->m_fieldsFallback.end())
                throw DictionaryParsingError("Multiple field definitions for tag: " + std::to_string(tag));
            dict->m_fieldsFallback[tag] = it->second;
        }
        fieldMap[name] = tag;
    }

    const auto components = root.child("components");
    HashMapT<std::string, GroupSpec> componentMap;

    HashMapT<std::string, pugi::xml_node> componentXMLMap;
    HashMapT<std::string, HashSetT<std::string>> componentGraph;

    // fully qualified name since we call this recursively
    std::function<void(const pugi::xml_node&, std::string)> validateGroup = [&](const pugi::xml_node& node, std::string parentComponent) {
        for (const auto& child : node) {
            if (strcasecmp(child.name(), "component") == 0) {
                const std::string componentName = child.attribute("name").as_string();
                if (componentName.empty())
                    throw DictionaryParsingError("Tried to reference a component without specifying a name");
                if (componentMap.find(componentName) == componentMap.end())
                    throw DictionaryParsingError("Tried to reference undefined component: " + componentName);

                // no dupe checking for now, doesn't have an impact anyway
                if (!parentComponent.empty())
                    componentGraph[parentComponent].insert(componentName);
            } else if (strcasecmp(child.name(), "group") == 0) {
                const std::string groupName = child.attribute("name").as_string();
                if (groupName.empty())
                    throw DictionaryParsingError("Tried to reference a group without specifying a name");
                auto it = fieldMap.find(groupName);
                if (it == fieldMap.end())
                    throw DictionaryParsingError("Tried to reference undefined group: " + groupName);

                validateGroup(child, parentComponent);
            } else if (strcasecmp(child.name(), "field") == 0) {
                const std::string fieldName = child.attribute("name").as_string();
                if (fieldName.empty())
                    throw DictionaryParsingError("Tried to reference a field without specifying a name");
                const auto it = fieldMap.find(fieldName);
                if (it == fieldMap.end())
                    throw DictionaryParsingError("Tried to reference undefined field: " + fieldName);
            } else {
                // unknown definition, ignore
            }
        }
    };

    // first pass, initialize our maps
    for (const auto& component : components.children()) {
        const std::string name = component.attribute("name").as_string();
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
        const std::string name = component.attribute("name").as_string();
        // treat this like a group, validate fields and populate our component graph
        validateGroup(component, name);
    }

    // third pass, toposort
    std::vector<std::string> sorted;
    {
        HashMapT<std::string, int> indegrees;
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

    const std::function<std::shared_ptr<GroupSpec>(const pugi::xml_node&)> buildGroup = [&](const pugi::xml_node& node) {
        const std::shared_ptr<GroupSpec> ret = std::make_shared<GroupSpec>();
        ret->m_ordered = node.attribute("ordered").as_bool();

        for (const auto& field : node) {
            if (strcasecmp(field.name(), "component") == 0) {
                const std::string componentName = field.attribute("name").as_string();
                // this must be a completed component; just merge everything in
                const auto& component = componentMap[componentName];
                for (const auto& entry : component.m_fields) {
                    if (!ret->m_fields.insert(entry).second)
                        throw DictionaryParsingError("Multiple references of field in group: " + std::to_string(entry.first));
                    ret->m_fieldOrder.push_back(entry.first);
                }

                for (const auto& entry : component.m_groups) {
                    if (!ret->m_groups.insert(entry).second)
                        throw DictionaryParsingError("Multiple references of group in group: " + std::to_string(entry.first));
                    ret->m_fieldOrder.push_back(entry.first);
                }
            } else if (strcasecmp(field.name(), "group") == 0) {
                const std::string groupName = field.attribute("name").as_string();
                const int tag = fieldMap[groupName];
                if (ret->m_groups.find(tag) != ret->m_groups.end())
                    throw DictionaryParsingError("Multiple references of group in group: " + std::to_string(tag));
                ret->m_groups[tag] = buildGroup(field);
                ret->m_fieldOrder.push_back(tag);
            } else if (strcasecmp(field.name(), "field") == 0) {
                const std::string fieldName = field.attribute("name").as_string();
                const bool required = field.attribute("required").as_bool(false);
                const int tag = fieldMap[fieldName];
                if (ret->m_fields.find(tag) != ret->m_fields.end())
                    throw DictionaryParsingError("Multiple references of field in group: " + std::to_string(tag));
                ret->m_fields.insert({fieldMap[fieldName], required});
                ret->m_fieldOrder.push_back(tag);
            }
        }

        ret->buildLookup();
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
        const std::string msgtype = node.attribute("msgtype").as_string();
        if (empty(msgtype))
            throw DictionaryParsingError("msgtype definition missing from message");
        if (dict->m_bodySpecs.find(msgtype) != dict->m_bodySpecs.end())
            throw DictionaryParsingError("Redefinition of message type: " + msgtype);
        const auto spec = buildGroup(node);
        dict->m_bodySpecs[msgtype] = spec;
        if (msgtype.size() == 1) {
            const auto c = static_cast<unsigned char>(msgtype[0]);
            if (c < Dictionary::FAST_MSGTYPE_SIZE)
                dict->m_bodySpecsFast[c] = spec;
        }
    }

    m_dictionaries[path] = dict;
    return dict;
}
