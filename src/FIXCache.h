#pragma once

#include "Message.h"

class IFIXCache
{
public:
    virtual ~IFIXCache() = default;

    virtual void cache(const Message& msg) = 0;
};

class MemoryCache : public IFIXCache
{
public:
    void cache(const Message& msg) override;
    
private:
    CREATE_LOGGER("MemoryCache");
};