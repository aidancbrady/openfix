#include "FIXStore.h"

FileStore::~FileStore()
{
    stop();
}

void FileStore::start()
{
    m_writer.start();
}

void FileStore::stop()
{
    m_writer.stop();
}

StoreHandle FileStore::createStore(const SessionSettings& settings)
{
    std::string sessionID = settings.getString(SessionSettings::SENDER_COMP_ID) + "-" + settings.getString(SessionSettings::TARGET_COMP_ID);
    
    auto& writer = *m_writer.createInstance(sessionID + ".data");

    auto storeFunc = [&](const std::string& msg) {
        writer.write(msg);
    };

    return createHandle(settings, storeFunc);
}

SessionData StoreHandle::load()
{
    SessionData ret;

    

    return ret;
}