#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#define F(x) x
using byte = uint8_t;
inline uint32_t hostMillis = 100;
inline uint32_t millis() { return hostMillis++; }
inline void delay(unsigned ms) { hostMillis += ms; }
class Stream {
public:
    virtual ~Stream() = default;
    virtual int available() = 0;
    virtual int read() = 0;
    virtual size_t write(uint8_t) = 0;
    virtual void flush() {}
    size_t print(char c) { return write(c); }
    size_t print(const char* s) { size_t n=0;while(*s)n+=write(*s++);return n; }
};
struct HostSerial {
    template<class... T> void print(T...) {}
    template<class... T> void println(T...) {}
    template<class... T> void printf(T...) {}
};
inline HostSerial Serial;
