#pragma once

namespace FIELD
{
    inline constexpr int BeginSeqNo = 7;
    inline constexpr int BeginString = 8;
    inline constexpr int BodyLength = 9;
    inline constexpr int CheckSum = 10;
    inline constexpr int EndSeqNo = 16;
    inline constexpr int MsgType = 35;
    inline constexpr int NewSeqNo = 36;
    inline constexpr int PosDupFlag = 43;
    inline constexpr int RefSeqNum = 45;
    inline constexpr int SenderCompID = 49;
    inline constexpr int SenderSubID = 50;
    inline constexpr int SendingTime = 52;
    inline constexpr int TargetCompID = 56;
    inline constexpr int TargetSubID = 57;
    inline constexpr int Text = 58;
    inline constexpr int PossResend = 97;
    inline constexpr int EncryptMethod = 98;
    inline constexpr int HeartBtInt = 108;
    inline constexpr int TestReqID = 112;
    inline constexpr int OrigSendingTime = 122;
    inline constexpr int GapFillFlag = 123;
    inline constexpr int ResetSeqNumFlag = 141;
    inline constexpr int RefTagID = 371;
    inline constexpr int RefMsgType = 372;
    inline constexpr int SessionRejectReason = 373;
    inline constexpr int NextExpectedMsgSeqNum = 789;
}
