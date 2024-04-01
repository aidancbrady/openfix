#include <gtest/gtest.h>

#include <openfix/Dictionary.h>
#include <openfix/Message.h>
#include <openfix/Fields.h>
#include <openfix/Utils.h>

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
    }

    ~MessageTest() override {
    }


    void SetUp() override {
    }

    void TearDown() override {
    }
};

TEST_F(MessageTest, SimpleTest) {
    auto dict = DictionaryRegistry::instance().load("test/FIXDictionary.xml");
    SessionSettings settings;
    std::string fix = "8=FIX.4.2|9=5|35=0|10=161|";
    convert(fix);
    auto msg = dict->parse(settings, fix);
    std::string text = msg.toString();
    msg = dict->parse(settings, text);
    

    settings.setBool(SessionSettings::RELAXED_PARSING, true);
    fix = "35=0|10=161|";
    convert(fix);
    msg = dict->parse(settings, fix);
    std::cout << msg.toString() << std::endl;

    throw std::runtime_error("");
}

TEST_F(MessageTest, TimeStampConverter) {
    auto time = "20240330-12:00:00.123";
    auto ms = Utils::parseUTCTimestamp(time);
    EXPECT_EQ(ms, 1711800000000000123ul);
}

int main(int argc, char **argv) 
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}