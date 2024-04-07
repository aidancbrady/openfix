#include <gtest/gtest.h>
#include <openfix/Dictionary.h>
#include <openfix/Fields.h>
#include <openfix/LinkedHashMap.h>
#include <openfix/Message.h>
#include <openfix/Utils.h>

std::string convert(std::string& fix)
{
    std::string ret = fix;
    for (size_t i = 0; i < ret.size(); ++i)
        if (ret[i] == EXTERNAL_SOH_CHAR)
            ret[i] = INTERNAL_SOH_CHAR;

    return ret;
}

class MessageTest : public ::testing::Test
{
protected:
    MessageTest()
    {
        dict = DictionaryRegistry::instance().load("test/FIXDictionary.xml");
    }

    ~MessageTest() override
    {}

    void SetUp() override
    {}

    void TearDown() override
    {}

    std::shared_ptr<Dictionary> dict;
};

TEST_F(MessageTest, FieldOrder)
{
    auto msg = dict->create("0");
    msg.getHeader().setField(34, "10");
    msg.getHeader().setField(35, "0");
    EXPECT_EQ(msg.toString(), "9=11|35=0|34=10|10=181|");

    msg = dict->create("4");
    msg.getHeader().setField(8, "FIX.4.2");
    msg.getHeader().setField(49, "SENDER");
    msg.getHeader().setField(56, "TARGET");
    msg.getHeader().setField(52, "TIME");
    msg.getHeader().setField(34, "TEST");

    EXPECT_EQ(msg.toString(), "8=FIX.4.2|9=41|35=4|49=SENDER|56=TARGET|34=TEST|52=TIME|10=106|");
}

TEST_F(MessageTest, SimpleTest)
{
    SessionSettings settings;
    std::string fix = "8=FIX.4.2|9=42|35=R|131=TES1|146=2|55=AAPL|55=TSLA|11=ID|10=190|";
    auto msg = dict->parse(settings, convert(fix));
    std::string text = msg.toString();
    EXPECT_EQ(text, fix);
    EXPECT_EQ(msg.getBody().getField(11), "ID");
    EXPECT_EQ(msg.getBody().getGroup(146, 0).getField(55), "AAPL");
}

TEST_F(MessageTest, OrderedFields)
{
    SessionSettings settings;
    settings.setBool(SessionSettings::RELAXED_PARSING, true);
    std::string fix = "34=3|56=TARGET|49=SENDER|35=0|11=TEST|13=TEST|12=TEST|";
    std::string ordered = "9=54|35=0|49=SENDER|56=TARGET|34=3|11=TEST|13=TEST|12=TEST|10=013|";
    auto msg = dict->parse(settings, convert(fix));
    EXPECT_EQ(msg.toString(), ordered);
}

TEST_F(MessageTest, TimeStampConverter)
{
    auto time = "20240330-12:00:00.123";
    auto ms = Utils::parseUTCTimestamp(time);
    EXPECT_EQ(ms, 1711800000123ul);
}

TEST_F(MessageTest, LinkedHashMapTest)
{
    LinkedHashMap<std::string, int> map;
    size_t cnt = 10;
    for (size_t i = 0; i < cnt; ++i) {
        map["test" + std::to_string(i)] = i;
        EXPECT_EQ(map["test" + std::to_string(i)], i);
    }

    EXPECT_EQ(map.size(), cnt);

    cnt = 0;
    for (const auto& [k, v] : map) {
        EXPECT_EQ(k, "test" + std::to_string(cnt));
        EXPECT_EQ(v, cnt);
        ++cnt;
    }

    EXPECT_EQ(cnt, 10);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}