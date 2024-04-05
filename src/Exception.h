#pragma once

#include <exception>
#include <string>

struct FieldNotFound : public std::exception
{
    FieldNotFound(int tag) 
    {
        m_message = "Field not found: " + std::to_string(tag);
    }

    const char* what() const throw()
    {
        return m_message.c_str();
    }

    std::string m_message;
};

#define CREATE_STRING_EXCEPTION(name)                                           \
    struct name : public std::exception                          \
    {                                                                           \
        name(std::string error) : m_error(std::move(error)) {}   \
        const char* what() const throw() { return m_error.c_str(); }            \
        std::string m_error;                                                    \
    };

CREATE_STRING_EXCEPTION(MessageParsingError)
CREATE_STRING_EXCEPTION(DictionaryParsingError)
CREATE_STRING_EXCEPTION(MisconfiguredSessionError)
CREATE_STRING_EXCEPTION(FileStoreLoadError)
