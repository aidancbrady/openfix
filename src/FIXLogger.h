#pragma once

#include "Message.h"

class IFIXLogger
{
public:
    virtual ~IFIXLogger() = default;

    virtual void log(const Message& msg) = 0;
};