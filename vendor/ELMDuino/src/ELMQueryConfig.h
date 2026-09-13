#pragma once
#include <cstdint>

// Per-query CAN settings. Zero means inherit protocol / automatic receive or FC.
// These are restored before another query; no arbitrary AT script is executed.
struct ELMQueryConfig {
    uint8_t protocol = 0;
    uint32_t receiveHeader = 0;
    uint32_t flowControlHeader = 0;
    uint32_t flowControlData = 0x300000;
    bool empty() const { return !protocol && !receiveHeader && !flowControlHeader; }
    bool valid() const {
        return (!protocol || (protocol >= 6 && protocol <= 9)) &&
            receiveHeader <= 0x1FFFFFFF && flowControlHeader <= 0x1FFFFFFF &&
            flowControlData <= 0xFFFFFF && (!flowControlHeader ||
            ((flowControlData >> 16) == 0x30 &&
             ((flowControlData & 0xFF) <= 0x7F ||
              ((flowControlData & 0xFF) >= 0xF1 && (flowControlData & 0xFF) <= 0xF9))));
    }
};
