#include "ble_connection.h"
#if defined(WS_SIM7670G_V2)
#include "obd.h"
#include "settings.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <freertos/semphr.h>
#include <algorithm>
#include <BLETrace.h>
#include "RuntimeAccess.h"

namespace BLEConnection {
namespace {
struct Device { std::string name, mac; uint8_t type; int rssi; bool advertisedOBD; };
SemaphoreHandle_t mutex;
struct Lock {
    Lock() { xSemaphoreTake(mutex, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(mutex); }
};
enum Action { NONE, SCAN, CONNECT, SAVE };
Action pending = NONE;
bool busy = false;
uint32_t jobId = 0;
std::vector<Device> devices;
Device target;
char targetProtocol = '0';
std::string error, activeName, activeMac, lastConfig;
char activeProtocol = '0';
unsigned long retryAt = 0;
uint32_t attemptId = 0;

std::string failureMessage() {
    const char* code = OBD.connectionError.load();
    if (strcmp(code, "none") == 0) return {};
    const bool elm = strcmp(OBD.connectionPhase.load(), "elm_error") == 0;
    return std::string(elm ? "ELM327 initialization failed: " : "Bluetooth connection failed: ") + code;
}

std::string configKey() {
    return std::string(Settings.OBD2.getName().c_str()) + "|" + Settings.OBD2.getMAC().c_str() + "|" +
        std::to_string(Settings.OBD2.getAddressType()) + Settings.OBD2.getProtocol() +
        (Settings.OBD2.getCheckPIDSupport() ? "check" : "all") +
        (Settings.OBD2.getSpecifyNumResponses() ? "count" : "nocount") +
        (Settings.OBD2.getDisable() ? "disabled" : "enabled");
}

void scanDevices() {
    Serial.println("[BLE] scan started duration_ms=5000");
    OBD.prepareBLEScan();
    auto scanner = NimBLEDevice::getScan();
    // Do not use BLESerial's FFF0-only advertisement filter: some adapters
    // expose that service only after connection. Bound memory and scan time.
    scanner->setScanCallbacks(nullptr);
    scanner->clearResults();
    scanner->setMaxResults(40);
    scanner->setActiveScan(true);
    auto results = scanner->getResults(5000, false);
    std::vector<Device> found;
    for (int i = 0; i < results.getCount(); ++i) {
        auto device = results.getDevice(i);
        BT_TRACE("SCAN_DEVICE", "index=%d name=%.48s name_len=%u mac=%s type=%u rssi=%d connectable=%u adv=%u services=%u first_uuid=%s",
            i, device->getName().c_str(), static_cast<unsigned>(device->getName().size()),
            device->getAddress().toString().c_str(), device->getAddress().getType(), device->getRSSI(),
            device->isConnectable() ? 1U : 0U, device->getAdvType(), device->getServiceUUIDCount(),
            device->getServiceUUIDCount() ? device->getServiceUUID().toString().c_str() : "none");
        found.push_back({device->getName(), device->getAddress().toString(),
            device->getAddress().getType(), device->getRSSI(),
            device->isAdvertisingService(NimBLEUUID("FFF0"))});
    }
    std::sort(found.begin(), found.end(), [](const Device &a, const Device &b) { return a.rssi > b.rssi; });
    scanner->clearResults();
    Serial.printf("[BLE] scan completed devices=%u\n", static_cast<unsigned>(found.size()));
    { Lock lock; devices = std::move(found); }
    OBD.connectionPhase = "scan_complete";
}

bool connectDevice(const Device &device, char protocol, const char* source = "unknown") {
    uint32_t attempt;
    {
        Lock lock;
        attempt = ++attemptId;
        error.clear();
        activeName = device.name; activeMac = device.mac; activeProtocol = protocol;
    }
    BLETrace::setAttempt(attempt);
    BT_TRACE("ATTEMPT", "source=%s peer=%s name=%.64s type=%u protocol=%c heap=%lu", source,
        device.mac.c_str(), device.name.c_str(), device.type, protocol, static_cast<unsigned long>(ESP.getFreeHeap()));
    Serial.printf("[BLE] attempt=%lu started\n", static_cast<unsigned long>(attempt));
    OBD.begin(device.name.c_str(), device.mac.c_str(), protocol,
        Settings.OBD2.getCheckPIDSupport(), Settings.OBD2.getDebug(), Settings.OBD2.getSpecifyNumResponses());
    const bool connected = OBD.connectBLE(device.name.c_str(), device.mac.c_str(), device.type);
    { Lock lock; error = connected ? std::string() : failureMessage(); }
    Serial.printf("[BLE] attempt=%lu result=%s code=%s reason=%d heap=%lu\n", static_cast<unsigned long>(attempt),
                  OBD.connectionPhase.load(), OBD.connectionError.load(), OBD.bleDisconnectReason.load(),
                  static_cast<unsigned long>(ESP.getFreeHeap()));
    return connected;
}
}

void init() {
    mutex = xSemaphoreCreateMutex();
    BLETrace::setEnabled(Settings.OBD2.getDebug());
    Serial.println("[BLE] firmware revision=v1.0.0 debug=runtime");
}
bool isBusy() { Lock lock; return busy; }

bool scan() {
    Lock lock;
    if (busy) return false;
    busy = true; pending = SCAN; error.clear(); devices.clear(); ++jobId;
    return true;
}

bool connect(const String &mac, unsigned addressType, char protocol) {
    Lock lock;
    if (busy || addressType > 1 || !strchr("0123456789A", protocol)) return false;
    for (const auto &device : devices) {
        if (mac.equalsIgnoreCase(device.mac.c_str()) && addressType == device.type) {
            target = device; targetProtocol = protocol;
            busy = true; pending = CONNECT; error.clear(); ++jobId;
            return true;
        }
    }
    return false;
}

std::string statusJSON() {
    Lock lock;
    JsonDocument doc;
    doc["supported"] = true;
    doc["state"] = pending != NONE ? "queued" : OBD.connectionPhase.load();
    doc["connected"] = strcmp(OBD.connectionPhase.load(), "connected") == 0 && pending == NONE;
    doc["busy"] = busy;
    doc["jobId"] = jobId;
    doc["attemptId"] = attemptId;
    doc["errorCode"] = OBD.connectionError.load();
    doc["disconnectReason"] = OBD.bleDisconnectReason.load();
#if defined(BLE_DIAGNOSTICS)
    const auto trace = BLETrace::stats();
    doc["debugBuild"] = "v1.0.0";
    doc["debugEnabled"] = BLETrace::enabled();
    doc["traceDropped"] = trace.dropped;
    doc["traceQueued"] = trace.queued;
    doc["bleRxBytes"] = trace.rxBytes;
    doc["bleTxBytes"] = trace.txBytes;
    doc["lastBleRxMs"] = trace.lastRxMs;
    doc["lastBleTxMs"] = trace.lastTxMs;
#endif
    doc["name"] = activeName;
    doc["mac"] = activeMac;
    doc["protocol"] = std::string(1, activeProtocol);
    doc["error"] = strcmp(OBD.connectionPhase.load(), "disconnected") == 0 ? failureMessage() : error;
    JsonArray list = doc["devices"].to<JsonArray>();
    for (const auto &device : devices) {
        auto row = list.add<JsonObject>();
        row["name"] = device.name; row["mac"] = device.mac;
        row["addressType"] = device.type; row["rssi"] = device.rssi;
        row["advertisedOBD"] = device.advertisedOBD;
    }
    std::string result; serializeJson(doc, result); return result;
}

void tick(bool allowVehicleReads) {
    // Debug can change without disconnecting an established adapter.
    BLETrace::setEnabled(Settings.OBD2.getDebug());
    OBD.setDebug(Settings.OBD2.getDebug());
    // Also expose idle/disabled/not-found states when a monitor opens after boot.
    static unsigned long lastStatusAt = 0;
    if (millis() - lastStatusAt >= 30000) {
        lastStatusAt = millis();
        Serial.printf("[BLE] status state=%s enabled=%u code=%s reason=%d heap=%lu\n",
            OBD.connectionPhase.load(), Settings.OBD2.getDisable() ? 0U : 1U,
            OBD.connectionError.load(), OBD.bleDisconnectReason.load(),
            static_cast<unsigned long>(ESP.getFreeHeap()));
#if defined(BLE_DIAGNOSTICS)
        const auto trace = BLETrace::stats();
        BT_TRACE("HEALTH", "revision=v1.0.0 state=%s heap=%lu min_heap=%lu rx=%lu tx=%lu last_rx=%lu last_tx=%lu dropped=%lu queued=%lu",
            OBD.connectionPhase.load(), static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
            static_cast<unsigned long>(trace.rxBytes), static_cast<unsigned long>(trace.txBytes),
            static_cast<unsigned long>(trace.lastRxMs), static_cast<unsigned long>(trace.lastTxMs),
            static_cast<unsigned long>(trace.dropped), static_cast<unsigned long>(trace.queued));
#endif
    }
    Action action; Device selected; char protocol;
    {
        Lock lock;
        action = pending; pending = NONE; selected = target; protocol = targetProtocol;
    }
    if (action != NONE) {
        if (action == SCAN) {
            scanDevices();
        } else if (action == SAVE || connectDevice(selected, protocol, "manual")) {
            RuntimeAccess::Upgrade access;
            if (!access) {
                Lock lock; pending = SAVE;
                return;
            }
            // Save only the selected adapter and existing Protocol choice.
            const auto previous = Settings.OBD2;
            Settings.OBD2.setName(selected.name.c_str());
            Settings.OBD2.setMAC(selected.mac.c_str());
            Settings.OBD2.setAddressType(selected.type);
            Settings.OBD2.setProtocol(protocol);
            Settings.OBD2.setDisable(false);
            if (!Settings.writeSettings(LittleFS)) {
                Settings.OBD2 = previous;
                OBD.closeBLE(); OBD.connectionPhase = "save_error";
                OBD.connectionError = "config_save_failed";
                Lock lock; error = "Could not save the adapter. Please retry.";
            }
        }
        lastConfig = configKey();
        retryAt = millis() + 30000;
        { Lock lock; busy = false; }
        return;
    }

    const std::string config = configKey();
    if (config != lastConfig) {
        BT_TRACE("CONFIG", "enabled=%u name=%.64s mac=%s type=%u protocol=%c",
            Settings.OBD2.getDisable() ? 0U : 1U, Settings.OBD2.getName().c_str(), Settings.OBD2.getMAC().c_str(),
            Settings.OBD2.getAddressType(), Settings.OBD2.getProtocol());
        OBD.closeBLE(); lastConfig = config; retryAt = 0;
        OBD.connectionError = "none";
        OBD.bleDisconnectReason = 0;
        { Lock lock; error.clear(); }
    }
    if (Settings.OBD2.getDisable()) {
        // Preserve the completed scan/error so a page opened while disabled
        // can still show empty results or the last connection failure.
        if (strcmp(OBD.connectionPhase.load(), "disconnected") == 0)
            OBD.connectionPhase = "disabled";
        return;
    }
    if (strcmp(OBD.connectionPhase.load(), "connected") == 0) {
        // Keep BLE connected while the settings page is open, but do not read
        // the mutable PID list until the configuration hotspot is released.
        if (allowVehicleReads || OBD.hasPendingQuery()) OBD.loop();
        return;
    }
    // Configuration Wi-Fi pauses vehicle polling, not BLE recovery/diagnostics.
    if (static_cast<int32_t>(millis() - retryAt) < 0) return;
    {
        Lock lock;
        if (busy) return;
        busy = true;
    }
    Device saved{Settings.OBD2.getName().c_str(), Settings.OBD2.getMAC().c_str(),
        Settings.OBD2.getAddressType(), 0, false};
    if (saved.mac.empty()) {
        if (saved.name.empty()) saved.name = OBD_ADP_NAME;
        scanDevices();
        Lock lock;
        for (const auto &device : devices) if (device.name == saved.name) { saved = device; break; }
    }
    if (!saved.mac.empty()) connectDevice(saved, Settings.OBD2.getProtocol(), "automatic");
    else {
        OBD.connectionPhase = "not_found";
        OBD.connectionError = "adapter_not_found";
        Serial.printf("[BLE] saved adapter not found name=%s\n", saved.name.c_str());
        Lock lock; error = failureMessage();
    }
    retryAt = millis() + 30000;
    { Lock lock; busy = false; }
}
}
#endif
