#pragma once
#include <stddef.h>
#include <stdint.h>

namespace BLETrace {
struct Stats { uint32_t queued, dropped, rxBytes, txBytes, lastRxMs, lastTxMs; };
#if defined(BLE_DIAGNOSTICS)
void init();
void setEnabled(bool enabled);
bool enabled();
void setAttempt(uint32_t attempt);
void log(const char* kind, const char* format, ...) __attribute__((format(printf, 2, 3)));
void bytes(const char* kind, const uint8_t* data, size_t length);
void received(size_t length);
void transmitted(size_t length);
Stats stats();
#else
inline void init() {}
inline void setEnabled(bool) {}
inline bool enabled() { return false; }
inline void setAttempt(uint32_t) {}
inline Stats stats() { return {}; }
#endif
}
#if defined(BLE_DIAGNOSTICS)
#define BT_TRACE(...) do { if (BLETrace::enabled()) BLETrace::log(__VA_ARGS__); } while (0)
#define BT_BYTES(...) do { if (BLETrace::enabled()) BLETrace::bytes(__VA_ARGS__); } while (0)
#else
#define BT_TRACE(...) ((void)0)
#define BT_BYTES(...) ((void)0)
#endif
