#include "FIXStore.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "Exception.h"

enum class WriteType : uint8_t
{
    MSG,
    SENDER_SEQ_NUM,
    TARGET_SEQ_NUM
};

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
    const std::string sessionID = settings.getString(SessionSettings::SENDER_COMP_ID) + "-" + settings.getString(SessionSettings::TARGET_COMP_ID);
    const std::string path = PlatformSettings::getString(PlatformSettings::DATA_PATH) + "/" + sessionID + ".data";

    auto& writer = *m_writer.createInstance(path);
    return createHandle(settings, writer, path);
}

void StoreHandle::store(int seqnum, const std::string& msg)
{
    // header: type(1) + seqnum(4) + length(8) = 13 bytes
    char hdr[1 + sizeof(seqnum) + sizeof(size_t)];
    hdr[0] = static_cast<char>(WriteType::MSG);
    std::memcpy(hdr + 1, &seqnum, sizeof(seqnum));
    const size_t len = msg.length();
    std::memcpy(hdr + 1 + sizeof(seqnum), &len, sizeof(len));
    m_writer.writeRaw(hdr, sizeof(hdr), msg);
}

void StoreHandle::setSenderSeqNum(int num)
{
    char buf[1 + sizeof(num)];
    buf[0] = static_cast<char>(WriteType::SENDER_SEQ_NUM);
    std::memcpy(buf + 1, &num, sizeof(num));
    m_writer.write(std::string(buf, sizeof(buf)));
}

void StoreHandle::setTargetSeqNum(int num)
{
    char buf[1 + sizeof(num)];
    buf[0] = static_cast<char>(WriteType::TARGET_SEQ_NUM);
    std::memcpy(buf + 1, &num, sizeof(num));
    m_writer.write(std::string(buf, sizeof(buf)));
}

SessionData StoreHandle::load()
{
    SessionData ret;

    if (!std::filesystem::exists(m_path)) {
        LOG_INFO("Store file doesn't exist, not loading session state.");
        return ret;
    }

    LOG_INFO("Loading session state from store file: " << m_path);
    std::ifstream storeFile(m_path, std::ios::binary);

    size_t cnt = 0;
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
            // first read seqnum
            int seqNum;
            if (!storeFile.read(reinterpret_cast<char*>(&seqNum), sizeof(seqNum)))
                throw FileStoreLoadError("Data file corrupted; unable to parse message seqnum");

            // now read msg length
            size_t length;
            if (!storeFile.read(reinterpret_cast<char*>(&length), sizeof(length)))
                throw FileStoreLoadError("Data file corrupted; unable to parse message length");

            // read msg directly into a pre-sized string
            std::string msg(length, '\0');
            size_t read = 0;
            while (read < length) {
                const size_t toRead = length - read;
                if (!storeFile.read(msg.data() + read, toRead))
                    throw FileStoreLoadError("Data file corrupted; unable to read complete message");
                read += toRead;
            }

            ret.m_messages[seqNum] = std::move(msg);
            ++cnt;
        }
    }

    LOG_INFO("Loaded " << cnt << " messages from file store.");

    return ret;
}

void StoreHandle::reset()
{
    LOG_INFO("Resetting session store, wiping data file...");
    m_writer.reset();
}
