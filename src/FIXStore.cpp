#include "FIXStore.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "Exception.h"

#define READ_BUF 1024

enum class WriteType : uint8_t { MSG, SENDER_SEQ_NUM, TARGET_SEQ_NUM };

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
    std::string path = PlatformSettings::getString(PlatformSettings::DATA_PATH) + "/" + sessionID + ".data";

    auto& writer = *m_writer.createInstance(path);

    auto storeFunc = [&](const std::string& msg) { writer.write(msg); };

    return createHandle(settings, storeFunc, path);
}

void StoreHandle::store(int seqnum, const std::string& msg)
{
    std::ostringstream ostr;
    ostr << static_cast<char>(WriteType::MSG);
    ostr.write(reinterpret_cast<char*>(&seqnum), sizeof(seqnum));
    size_t len = msg.length();
    ostr.write(reinterpret_cast<char*>(&len), sizeof(len));
    ostr << msg;
    m_writeFunc(ostr.str());
}

void StoreHandle::setSenderSeqNum(int num)
{
    std::ostringstream ostr;
    ostr << static_cast<char>(WriteType::SENDER_SEQ_NUM);
    ostr.write(reinterpret_cast<char*>(&num), sizeof(num));
    m_writeFunc(ostr.str());
}

void StoreHandle::setTargetSeqNum(int num)
{
    std::ostringstream ostr;
    ostr << static_cast<char>(WriteType::TARGET_SEQ_NUM);
    ostr.write(reinterpret_cast<char*>(&num), sizeof(num));
    m_writeFunc(ostr.str());
}

SessionData StoreHandle::load()
{
    SessionData ret;

    std::ifstream storeFile(m_path, std::ios::binary);

    char buf[READ_BUF];

    while (true) {
        WriteType type;
        storeFile.read(reinterpret_cast<char*>(&type), sizeof(WriteType));
        if (!storeFile)
            break;

        if (type == WriteType::SENDER_SEQ_NUM || type == WriteType::TARGET_SEQ_NUM) {
            int seqNum;
            if (!storeFile.read(reinterpret_cast<char*>(&seqNum), sizeof(seqNum)))
                throw FileStoreLoadError("Data file corrupted; unable to parse seqnum");

            if (type == WriteType::SENDER_SEQ_NUM)
                ret.m_senderSeqNum = seqNum;
            if (type == WriteType::TARGET_SEQ_NUM)
                ret.m_targetSeqNum = seqNum;
        } else if (type == WriteType::MSG) {
            std::ostringstream msg;

            // first read seqnum
            int seqNum;
            if (!storeFile.read(reinterpret_cast<char*>(&seqNum), sizeof(seqNum)))
                throw FileStoreLoadError("Data file corrupted; unable to parse message seqnum");

            // now read msg length
            int length;
            if (!storeFile.read(reinterpret_cast<char*>(&length), sizeof(length)))
                throw FileStoreLoadError("Data file corrupted; unable to parse message length");

            // now read msg
            int read = 0;
            while (read < length) {
                int toRead = length - read;
                if (!storeFile.read(buf, std::min(READ_BUF, toRead)))
                    throw FileStoreLoadError("Data file corrupted; unable to read complete message");
                read += toRead;
                msg.write(buf, toRead);
            }

            ret.m_messages[seqNum] = msg.str();
        }
    }

    return ret;
}