#pragma once

#include "Message.h"

class IFIXStore
{
public:
    virtual ~IFIXStore() = default;

    virtual void persist(const Message& msg) = 0;
};