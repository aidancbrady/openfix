#include "Dictionary.h"

#include "Fields.h"

GroupSpec GroupSpec::UNKNOWN;

enum class MessageState
{
    // validate first three fields
    BEGIN_STRING,
    BODY_LENGTH,
    MSG_TYPE,

    HEADER,
    BODY,
    FOOTER
};

enum class ParserState
{
    START,
    NEXT,
    KEY,
    VAL
};

struct ParserGroupInfo
{
    ParserGroupInfo(std::reference_wrapper<const GroupSpec> spec, std::reference_wrapper<FieldMap> group) : m_spec(spec), m_group(group), 
        m_groupTag(0), m_groupCount(0), m_groupMaxCount(0)
    {}

    std::reference_wrapper<const GroupSpec> m_spec;
    std::reference_wrapper<FieldMap> m_group;

    int m_groupTag;

    size_t m_groupCount;
    size_t m_groupMaxCount;
};

#define TRY_LOG_WARN(msg) if (loudParsing) { LOG_WARN(msg); }
#define TRY_LOG_ERROR(msg) if (loudParsing) { LOG_ERROR(msg); }
#define TRY_LOG_THROW(msg) {                             \
    std::ostringstream ostr;                             \
    ostr << msg;                                         \
    TRY_LOG_ERROR(ostr.str());                           \
    if (!relaxedParsing) throw ParsingError(ostr.str()); \
}

Message Dictionary::parse(const SessionSettings& settings, const std::string& text) const
{
    bool loudParsing = settings.getBool(SessionSettings::LOUD_PARSING);
    bool relaxedParsing = settings.getBool(SessionSettings::RELAXED_PARSING);

    Message ret;

    MessageState msgState = MessageState::BEGIN_STRING;
    ParserState state = ParserState::START;

    std::string key;
    std::string value;

    std::vector<ParserGroupInfo> groupStack;
    // start with header
    groupStack.emplace_back(m_headerSpec, ret.getHeader());

    auto curGroup = [&]() -> FieldMap& { return groupStack[groupStack.size()-1].m_group.get(); };
    auto curSpec = [&]() -> const GroupSpec& { return groupStack[groupStack.size()-1].m_spec.get(); };

    int checksum = 0;
    int tag = 0;
    int bodyLengthStart = 0;

    for (size_t i = 0; i < text.size(); ++i)
    {
        char c = text[i];
        if (i < text.size() - 7)
            checksum += c;
        if (c == INTERNAL_SOH_CHAR)
        {
            // beginning SOH char
            if (state == ParserState::START)
            {
                TRY_LOG_THROW("Message begins with SOH character (idx=" << i << ")");
                state = ParserState::NEXT;
                continue;
            }

            // repeated SOH chars
            if (state == ParserState::NEXT)
            {
                TRY_LOG_WARN("Message has repeating SOH characters (idx=" << i << ")");
                continue;
            }
            
            auto overwriteStack = [&](int curIdx) {
                while (groupStack.size()-1 > curIdx)
                {
                    auto& group = groupStack[groupStack.size()-1];
                    if (group.m_groupTag > 0 && group.m_groupCount < group.m_groupMaxCount)
                        TRY_LOG_THROW("Repeating group terminated with count less than NumInGroup (tag=" << group.m_groupTag << ")");
                    groupStack.pop_back();
                }
            };

            auto handleRepeatingTag = [&](ParserGroupInfo& group, int groupIdx) {
                // we've already seen this tag; are we in a group?
                if (group.m_groupTag > 0)
                {
                    if (group.m_groupCount == group.m_groupMaxCount)
                    {
                        TRY_LOG_THROW("Repeating group count exceeds NumInGroup (tag=" << tag << ")");
                        return -1;
                    }

                    // create a duplicate group with this tag
                    auto& newGroup = groupStack[groupIdx-1].m_group.get().addGroup(group.m_groupTag);
                    newGroup.setField(tag, value);
                    // replace current group on stack with new gruop
                    group.m_group = std::ref(newGroup);
                    ++group.m_groupCount;
                    return groupIdx;
                }
                else
                {
                    TRY_LOG_THROW("Message contains duplicate tags (tag=" << tag << ")");
                    return -1;
                }
            };

            auto trySetField = [&](ParserGroupInfo& group, int groupIdx) {
                // spec contains field
                auto fieldIt = group.m_spec.get().m_fields.find(tag);
                if (fieldIt != m_headerSpec.m_fields.end())
                {
                    if (group.m_group.get().has(tag))
                        return handleRepeatingTag(group, groupIdx);

                    group.m_group.get().setField(tag, value);
                    return groupIdx;
                }

                // spec contains group
                auto groupIt = group.m_spec.get().m_groups.find(tag);
                if (groupIt != group.m_spec.get().m_groups.end())
                {
                    if (group.m_group.get().getGroupCount(tag) > 0)
                        return handleRepeatingTag(group, groupIdx);
                    
                    // create a new group
                    auto& fieldMap = group.m_group.get().addGroup(tag);
                    ParserGroupInfo newGroup{groupIt->second, fieldMap};
                    newGroup.m_groupTag = tag;
                    newGroup.m_groupCount = 1;

                    try {
                        newGroup.m_groupMaxCount = std::atoi(value.c_str());
                    } catch (...) {
                        TRY_LOG_THROW("Couldn't parse NumInGroup (tag=" << tag << ")");
                        return -1;
                    }

                    groupStack.push_back(std::move(newGroup));
                    return groupIdx;
                }

                // tag not in spec fields nor groups
                return -1;
            };

            auto verifyField = [&](int testTag, MessageState testState, MessageState nextState, const std::string& error) {
                if (msgState == testState)
                {
                    if (tag != testTag)
                    {
                        TRY_LOG_THROW(error);
                        return false;
                    }
                    if (tag == FIELDS::BodyLength)
                        bodyLengthStart = i;
                }
                return true;
            };

            verifyField(FIELDS::BeginString, MessageState::BEGIN_STRING, MessageState::BODY_LENGTH, "Missing BeginString as first tag");
            verifyField(FIELDS::BodyLength, MessageState::BODY_LENGTH, MessageState::MSG_TYPE, "Missing BodyLength as second tag");
            verifyField(FIELDS::MsgType, MessageState::MSG_TYPE, MessageState::HEADER, "Missing MsgType as third tag");

            // if we have a spec for this group
            if (!curSpec().empty())
            {
                // try and set the field starting in the current group, and traverse up the stack on failure
                bool didSet = false;
                for (size_t i = groupStack.size()-1; i >= 0; --i)
                {
                    int newIdx = trySetField(groupStack[i], i);
                    if (newIdx >= 0)
                    {
                        overwriteStack(newIdx);
                        didSet = true;
                    }
                }

                if (didSet)
                    continue;
            }

            // not in current structural group. if we're in the header, move to the body
            if (msgState == MessageState::HEADER)
            {
                groupStack.pop_back();
                groupStack.emplace_back(m_headerSpec, ret.getBody());
                msgState = MessageState::BODY;
            }

            // if we're in the body, see if we can move to the footer
            if (msgState == MessageState::BODY)
            {
                auto test = ParserGroupInfo(m_footerSpec, ret.getFooter());
                if (trySetField(test, 0))
                {
                    groupStack[0] = test;
                    continue;
                }
            }

            // unknown field, all attempts failed; we assume it's in the current group
            TRY_LOG_ERROR("Unknown field (tag=" << tag << ")");
            groupStack[groupStack.size()-1].m_group.get().setField(tag, value);

            key.clear();
            value.clear();

            state = ParserState::NEXT;
            continue;
        }
        else if (c == TAG_ASSIGNMENT_CHAR)
        {
            bool fail = false;
            if (state != ParserState::KEY)
            {
                // missing tag
                if (state == ParserState::START || state == ParserState::NEXT)
                {
                    TRY_LOG_THROW("Missing tag (idx=" << i << ")");
                }
                // multiple assignments
                else if (state == ParserState::VAL)
                {
                    TRY_LOG_THROW("Multiple assignments (idx=" << i << ")");
                }

                fail = true;
            }

            state = ParserState::VAL;

            try {
                tag = std::atoi(key.c_str());
            } catch (...) {
                TRY_LOG_THROW("Tag not int (tag=" << tag << ")");
                fail = true;
            }

            if (!fail)
            {
                // data field handling
                const auto& dataLengthTagMap = curSpec().m_dataLengthTags;
                auto it = dataLengthTagMap.find(tag);
                if (it != dataLengthTagMap.end())
                {
                    int length = 0;
                    try {
                        length = std::atoi(curGroup().getField(it->second).c_str());
                    } catch (...) {
                        TRY_LOG_THROW("Couldn't parse data field (tag=" << tag << ")");
                    }
                    for (size_t j = 0; j < length; ++j)
                        value += text[i + j + 1];
                    curGroup().setField(tag, value);
                    i += length;
                    continue;
                }
            }

            if (fail)
            {
                // go to next SOH
                while (i < text.size()-1 && text[i+1] != INTERNAL_SOH_CHAR)
                    ++i;
            }

            continue;
        }

        if (state != ParserState::VAL)
        {
            state = ParserState::KEY;
            key += c;
        }
        else
        {
            value += c;
        }
    }

    // missing trailing SOH char
    if (state != ParserState::NEXT)
        TRY_LOG_THROW("Missing trailing SOH character");

    if (!relaxedParsing)
    {
        // verify bodylength
        auto expectedLength = text.size() - bodyLengthStart - 7;
        try {
            auto bodyLength = std::atoi(ret.getHeader().getField(FIELDS::BodyLength).c_str());
            if (expectedLength != bodyLength)
                throw std::exception();
        } catch (...) {
            TRY_LOG_THROW("Invalid BodyLength: expected " << expectedLength);
        }

        // verify checksum
        if (!ret.getFooter().has(FIELDS::CheckSum))
        {
            TRY_LOG_THROW("Footer missing CheckSum");
        }
        else
        {
            checksum %= 256;
            std::string checksumStr = "";
            // trailing zeros
            for (int tmp = checksum; tmp < 100; checksumStr += "0", tmp *= 10);
            checksumStr += std::to_string(checksum);

            if (tag != FIELDS::CheckSum)
                TRY_LOG_THROW("Message didn't end in checksum");
            auto& checksumRet = ret.getFooter().getField(FIELDS::CheckSum);

            if (checksumRet != checksumStr)
            {
                TRY_LOG_THROW("Invalid checksum: expected " << checksumStr << ", received " << checksumRet);
            }
        }
    }

    // remove checksum
    ret.getFooter().removeField(FIELDS::CheckSum);

    return ret;
}

std::shared_ptr<Dictionary> DictionaryRegistry::load(const std::string& path)
{
    auto it = m_dictionaries.find(path);
    if (it != m_dictionaries.end())
        return it->second;
    
    auto dict = std::make_shared<Dictionary>();

    

    m_dictionaries[path] = dict;
    return dict;
}