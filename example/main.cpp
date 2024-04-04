#include <openfix/Application.h>

#include <memory>
#include <iostream>
#include <thread>

// https://stackoverflow.com/questions/31357215/is-it-ok-to-share-the-same-epoll-file-descriptor-among-threads

int main(int argc, char** argv)
{
    Application app;

    {
        SessionSettings settings;
        settings.setString(SessionSettings::SESSION_TYPE_STR, "acceptor");
        settings.setString(SessionSettings::BEGIN_STRING, "FIX.4.2");
        settings.setString(SessionSettings::SENDER_COMP_ID, "ACCEPTOR");
        settings.setString(SessionSettings::TARGET_COMP_ID, "INITIATOR");
        settings.setString(SessionSettings::FIX_DICTIONARY, "/home/aidancbrady/Documents/Projects/openfix/test/FIXDictionary.xml");
        settings.setLong(SessionSettings::ACCEPT_PORT, 12121);

        app.createSession("TEST_ACCEPTOR", settings);
    }
    {
        SessionSettings settings;
        settings.setString(SessionSettings::SESSION_TYPE_STR, "initiator");
        settings.setString(SessionSettings::BEGIN_STRING, "FIX.4.2");
        settings.setString(SessionSettings::SENDER_COMP_ID, "INITIATOR");
        settings.setString(SessionSettings::TARGET_COMP_ID, "ACCEPTOR");
        settings.setString(SessionSettings::FIX_DICTIONARY, "/home/aidancbrady/Documents/Projects/openfix/test/FIXDictionary.xml");
        settings.setLong(SessionSettings::CONNECT_PORT, 12121);
        settings.setString(SessionSettings::CONNECT_HOST, "localhost");

        app.createSession("TEST_INITIATOR", settings);
    }
 
    app.start();
    ::usleep(1000'000'000);
}
