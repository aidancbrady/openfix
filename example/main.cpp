#include <openfix/Application.h>
#include <openfix/SignalHandler.h>

#include <memory>
#include <iostream>
#include <thread>

// https://stackoverflow.com/questions/31357215/is-it-ok-to-share-the-same-epoll-file-descriptor-among-threads

int main(int argc, char** argv)
{
    if (argc != 2)
        throw std::runtime_error("usage: app <acceptor | initiator>");
    
    Application app;
    SessionSettings settings;

    if (strcasecmp("acceptor", argv[1]) == 0)
    {
        std::cout << "starting acceptor" << std::endl;
        settings.setString(SessionSettings::SESSION_TYPE_STR, "acceptor");
        settings.setString(SessionSettings::BEGIN_STRING, "FIX.4.2");
        settings.setString(SessionSettings::SENDER_COMP_ID, "ACCEPTOR");
        settings.setString(SessionSettings::TARGET_COMP_ID, "INITIATOR");
        settings.setString(SessionSettings::FIX_DICTIONARY, "/home/aidancbrady/Documents/Projects/openfix/test/FIXDictionary.xml");
        settings.setLong(SessionSettings::ACCEPT_PORT, 12121);

        app.createSession("TEST_ACCEPTOR", settings);
    }
    else if (strcasecmp("initiator", argv[1]) == 0)
    {
        std::cout << "starting initiator" << std::endl;
        settings.setString(SessionSettings::SESSION_TYPE_STR, "initiator");
        settings.setString(SessionSettings::BEGIN_STRING, "FIX.4.2");
        settings.setString(SessionSettings::SENDER_COMP_ID, "INITIATOR");
        settings.setString(SessionSettings::TARGET_COMP_ID, "ACCEPTOR");
        settings.setString(SessionSettings::FIX_DICTIONARY, "/home/aidancbrady/Documents/Projects/openfix/test/FIXDictionary.xml");
        settings.setLong(SessionSettings::CONNECT_PORT, 12121);
        settings.setString(SessionSettings::CONNECT_HOST, "localhost");

        app.createSession("TEST_INITIATOR", settings);
    }
    else {
        throw std::runtime_error("unknown type: " + std::string(argv[1]));
    }
 
    app.start();

    SignalHandler::static_wait();

    return EXIT_SUCCESS;
}
