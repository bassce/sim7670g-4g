#pragma once
#include <stddef.h>
#include <stdint.h>

namespace BLETrace {
// Each byte has an exact hex representation. ASCII is only a reading aid.
inline size_t formatBytes(const uint8_t* data, size_t size, char* hex, size_t hexSize,
                          char* ascii, size_t asciiSize) {
    static const char digits[] = "0123456789abcdef";
    if (!hexSize || !asciiSize) return 0;
    size_t count = size;
    if (count > (hexSize - 1) / 2) count = (hexSize - 1) / 2;
    if (count > asciiSize - 1) count = asciiSize - 1;
    for (size_t i = 0; i < count; ++i) {
        hex[i*2] = digits[data[i] >> 4]; hex[i*2+1] = digits[data[i] & 15];
        ascii[i] = data[i] >= 32 && data[i] <= 126 ? static_cast<char>(data[i]) : '.';
    }
    hex[count*2] = 0; ascii[count] = 0;
    return count;
}
}
