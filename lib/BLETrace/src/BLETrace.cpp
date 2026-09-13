#include "BLETrace.h"
#if defined(BLE_DIAGNOSTICS)
#include "TraceFormat.h"
#include <Arduino.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace BLETrace {
namespace {
struct Record { uint32_t ms, seq, attempt, session; const char* kind; char text[192]; };
QueueHandle_t queue = nullptr;
std::atomic_bool active{false};
std::atomic_uint session{0};
std::atomic_uint sequence{0}, attempt{0}, packet{0}, dropped{0};
std::atomic_uint rxBytes{0}, txBytes{0}, lastRxMs{0}, lastTxMs{0};
void writer(void*) {
    Record record;
    uint32_t previousDrops = 0;
    for (;;) {
        if (xQueueReceive(queue, &record, portMAX_DELAY) != pdTRUE) continue;
        if (!active.load() || record.session != session.load()) continue;
        Serial.printf("\n[BTDBG t=%lu seq=%lu attempt=%lu kind=%s] %s\n",
            static_cast<unsigned long>(record.ms), static_cast<unsigned long>(record.seq),
            static_cast<unsigned long>(record.attempt), record.kind, record.text);
        const uint32_t count = dropped.load();
        if (count != previousDrops) {
            Serial.printf("\n[BTDBG kind=LOG_LOSS] dropped_total=%lu\n", static_cast<unsigned long>(count));
            previousDrops = count;
        }
    }
}
}
void init() {
    if (queue) return;
    queue = xQueueCreate(64, sizeof(Record));
    if (!queue) { Serial.println("[BTDBG] logger_allocation_failed"); return; }
    if (xTaskCreatePinnedToCore(writer, "BLETrace", 4096, nullptr, 1, nullptr, 1) != pdPASS) {
        vQueueDelete(queue); queue = nullptr;
        Serial.println("[BTDBG] logger_task_failed"); return;
    }
}
bool enabled() { return active.load(); }
// Called by the OBD worker. Keep allocated storage alive across toggles so
// an in-flight Bluetooth callback can never access a deleted queue.
void setEnabled(bool enable) {
    if (enable == active.load()) return;
    if (enable) {
        init();
        if (!queue) return;
        ++session;
        active = true;
        log("BUILD", "revision=v1.0.0 queue_records=64 max_payload=256 nimble=2.3.7 session=%lu",
            static_cast<unsigned long>(session.load()));
    } else {
        active = false;
        ++session;
    }
}
void setAttempt(uint32_t id) { attempt = id; }
void log(const char* kind, const char* format, ...) {
    if (!active.load()) return;
    if (!queue) { ++dropped; return; }
    Record record{};
    record.ms = millis(); record.seq = ++sequence; record.attempt = attempt.load(); record.kind = kind;
    record.session = session.load();
    va_list args; va_start(args, format);
    const int needed = vsnprintf(record.text, sizeof(record.text), format, args);
    va_end(args);
    if (needed >= static_cast<int>(sizeof(record.text))) {
        constexpr char suffix[] = " [truncated]";
        memcpy(record.text + sizeof(record.text) - sizeof(suffix), suffix, sizeof(suffix));
    }
    // Device names and received text cannot insert fake log records/newlines.
    for (char* p = record.text; *p; ++p) if (static_cast<unsigned char>(*p) < 32 || *p == 127) *p = '.';
    if (xQueueSend(queue, &record, 0) != pdTRUE) ++dropped;
}
void bytes(const char* kind, const uint8_t* data, size_t length) {
    if (!active.load() || !data) return;
    const uint32_t id = ++packet;
    const size_t limit = length > 256 ? 256 : length;
    for (size_t offset = 0; offset < limit; offset += 32) {
        char hex[65], ascii[33];
        const size_t size = limit-offset > 32 ? 32 : limit-offset;
        formatBytes(data+offset, size, hex, sizeof(hex), ascii, sizeof(ascii));
        log(kind, "packet=%lu offset=%u total=%u hex=%s ascii=%s", static_cast<unsigned long>(id),
            static_cast<unsigned>(offset), static_cast<unsigned>(length), hex, ascii);
    }
    if (length > limit) log(kind, "packet=%lu payload_truncated=%u", static_cast<unsigned long>(id), static_cast<unsigned>(length-limit));
}
void received(size_t length) { rxBytes += length; lastRxMs = millis(); }
void transmitted(size_t length) { txBytes += length; lastTxMs = millis(); }
Stats stats() {
    return {queue ? static_cast<uint32_t>(uxQueueMessagesWaiting(queue)) : 0, dropped.load(),
        rxBytes.load(), txBytes.load(), lastRxMs.load(), lastTxMs.load()};
}
}
#endif
