#include <gtest/gtest.h>

#include <openfix/Dictionary.h>
#include <openfix/Message.h>
#include <openfix/Fields.h>
#include <openfix/Utils.h>
#include <openfix/LinkedHashMap.h>

void convert(std::string& fix)
{
    for (size_t i = 0; i < fix.size(); ++i)
    {
        if (fix[i] == EXTERNAL_SOH_CHAR)
            fix[i] = INTERNAL_SOH_CHAR;
    }
}

class MessageTest : public ::testing::Test {
protected:
    MessageTest() {
        dict = DictionaryRegistry::instance().load("test/FIXDictionary.xml");
    }

    ~MessageTest() override {
    }


    void SetUp() override {
    }

    void TearDown() override {
    }

    std::shared_ptr<Dictionary> dict;
};

TEST_F(MessageTest, SimpleTest) {
    SessionSettings settings;
    std::string fix = "8=FIX.4.2|9=42|35=R|131=TES1|146=2|55=AAPL|55=TSLA|11=ID|10=190|";
    convert(fix);
    auto msg = dict->parse(settings, fix);
    std::cout << msg << std::endl;
    std::string text = msg.toString();
    EXPECT_EQ(text, fix);
    EXPECT_EQ(msg.getBody().getField(11), "ID");
    EXPECT_EQ(msg.getBody().getGroup(146, 0).getField(55), "AAPL");
}

TEST_F(MessageTest, OrderedFields) {
    SessionSettings settings;
    settings.setBool(SessionSettings::RELAXED_PARSING, true);
    std::string fix = "34=3|56=TARGET|49=SENDER|35=0|11=TEST|13=TEST|12=TEST|";
    std::string ordered = "9=54|35=0|49=SENDER|56=TARGET|34=3|11=TEST|13=TEST|12=TEST|10=013|";
    convert(fix);
    convert(ordered);
    auto msg = dict->parse(settings, fix);
    std::string text = msg.toString();
    EXPECT_EQ(text, ordered);
}

TEST_F(MessageTest, TimeStampConverter) {
    auto time = "20240330-12:00:00.123";
    auto ms = Utils::parseUTCTimestamp(time);
    EXPECT_EQ(ms, 1711800000000000123ul);
}

TEST_F(MessageTest, LinkedHashMapTest) {
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

int main(int argc, char **argv) 
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}