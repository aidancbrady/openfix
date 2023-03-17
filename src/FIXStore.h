#pragma once

#include "Message.h"
#include "Log.h"

class IFIXStore
{
public:
    virtual ~IFIXStore() = default;

    virtual void persist(const Message& msg) = 0;
};

class FileStore : public IFIXStore
{
public:
    void persist(const Message& msg) = 0;

private:
    CREATE_LOGGER("FileStore");
};