#include "FIXLogger.h"

FileLogger::~FileLogger()
{
    stop();
}

void FileLogger::start()
{
    m_writer.start();
}

void FileLogger::stop()
{
    m_writer.stop();
}

LoggerHandle IFIXLogger::createHandle(LoggerFunction evtLogger, LoggerFunction msgLogger) const
{
    return {std::move(evtLogger), std::move(msgLogger)};
}

LoggerHandle FileLogger::createLogger(const SessionSettings& settings)
{
    std::string sessionID = settings.getString(SessionSettings::SENDER_COMP_ID) + "-" + settings.getString(SessionSettings::TARGET_COMP_ID);

    auto& evtLogger = *m_writer.createInstance(PlatformSettings::getString(PlatformSettings::LOG_PATH) + "/" + sessionID + ".event.log");
    auto& msgLogger = *m_writer.createInstance(PlatformSettings::getString(PlatformSettings::LOG_PATH) + "/" + sessionID + ".messages.log");

    auto evtFunction = [&](const std::string& msg) { evtLogger.write(msg); };
    auto msgFunction = [&](const std::string& msg) { msgLogger.write(msg); };

    return createHandle(std::move(evtFunction), std::move(msgFunction));
}
