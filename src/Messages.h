#pragma once

#include <string>
#include <unordered_set>

namespace MESSAGE
{
    inline std::string HEARTBEAT = "0";
    inline std::string TEST_REQUEST = "1";
    inline std::string RESEND_REQUEST = "2";
    inline std::string REJECT = "3";
    inline std::string SEQUENCE_RESET = "4";
    inline std::string LOGOUT = "5";
    inline std::string LOGON = "A";

    inline static std::unordered_set<std::string> SESSION_MSGS = {HEARTBEAT, TEST_REQUEST, RESEND_REQUEST, REJECT, SEQUENCE_RESET, LOGOUT, LOGON};
}
