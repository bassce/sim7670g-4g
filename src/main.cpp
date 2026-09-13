/*
 * This program is free software; you can use it, redistribute it
 * and / or modify it under the terms of the GNU General Public License
 * (GPL) as published by the Free Software Foundation; either version 3
 * of the License or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program, in a file called gpl.txt or license.txt.
 * If not, write to the Free Software Foundation Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307 USA
 */
#include "mqtt.h"

#include <WiFi.h>
#include <atomic>
#include <cmath>

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif
#if !defined(CONFIG_BT_SPP_ENABLED) && !defined(USE_BLE)
#error Serial Bluetooth not available or not enabled. It is only available for the ESP32 chip.
#endif

#ifndef BUILD_GIT_BRANCH
#define BUILD_GIT_BRANCH ""
#endif
#ifndef BUILD_GIT_COMMIT_HASH
#define BUILD_GIT_COMMIT_HASH ""
#endif

#define MIN_VOLTAGE_LEVEL       3300
#define LOW_VOLTAGE_LEVEL       3600            // Sleep shutdown voltage

#include <LittleFS.h>

#define FORMAT_LITTLEFS_IF_FAILED false

#define DISCOVERED_DEVICES_FILE "/discovered_devices.json"

#define HA_T_CPUTEMP            "cpuTemp"
#define HA_T_FREEMEM            "freeMem"
#define HA_T_UPTIME             "uptime"
#define HA_T_RECONNECTS         "reconnects"
#define HA_T_IP_ADDR            "ipAddress"
#define HA_T_SQ                 "signalQuality"
#define HA_T_BAT_VOL            "internalBatteryVoltage"
#define HA_T_BAT_LVL            "internalBatteryLevel"
#define HA_T_GSM_LOC            "gsmLocation"
#define HA_T_GPS_LOC            "gpsLocation"
#define HAT_T_DTC               "dtc"
#define HAT_T_CLEAR_DTC         "clearDTC"

#include <numeric>

#include "settings.h"
#include "helper.h"
#include "obd.h"
#include "gsm.h"
#include "http.h"
#include "ble_connection.h"
#include "RuntimeAccess.h"

HTTPServer server(80);

#define DEBUG_PORT Serial

// #define DUMP_AT_COMMANDS

#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, Serial);
GSM gsm(debugger);
#else
GSM gsm(SerialAT);
#endif

MQTT mqtt = MQTT();

std::atomic_bool wifiAPStarted{false};
std::atomic_bool wifiAPInUse{false};
std::atomic<unsigned int> wifiAPStaConnected{0};

std::atomic_bool obdConnected{false};
std::atomic<int> obdConnectErrors{0};

std::atomic<unsigned long> startTime{0};

std::vector<uint32_t> batteryVoltages;
std::atomic<unsigned long> batteryTime{0};

std::atomic_bool allDiscoverySend{false};
std::atomic_bool allDiagnosticDiscoverySend{false};
std::atomic_bool allStaticDiagnosticDiscoverySend{false};

std::atomic<unsigned long> lastDebugOutput{0};
std::atomic<unsigned long> lastMQTTDiscoveryOutput{0};
std::atomic<unsigned long> lastMQTTDiagnosticDiscoveryOutput{0};
std::atomic<unsigned long> lastMQTTStaticDiagnosticDiscoveryOutput{0};
std::atomic<unsigned long> lastMQTTOutput{0};
std::atomic<unsigned long> lastMQTTDiagnosticOutput{0};
std::atomic<unsigned long> lastMQTTStaticDiagnosticOutput{0};
std::atomic<unsigned long> lastMQTTDTCDiagnosticOutput{0};
std::atomic<unsigned long> lastMQTTLocationOutput{0};

std::atomic<int> signalQuality{0};
std::atomic<float> gsmLatitude{0};
std::atomic<float> gsmLongitude{0};
std::atomic<float> gsmAccuracy{0};
std::atomic<float> gpsLatitude{0};
std::atomic<float> gpsLongitude{0};
std::atomic<float> gpsAccuracy{0};
std::atomic_bool gpsFixValid{false};

std::atomic_bool clearDTC{false};

TaskHandle_t outputTaskHdl;
TaskHandle_t stateTaskHdl;

size_t getESPHeapSize() {
    return heap_caps_get_free_size(MALLOC_CAP_8BIT);
}

void deepSleep(const int sec) {
    log_d("Prepare nap...");
    WiFi.disconnect(true);
    OBD.end();
    gsm.powerOff();
    if (outputTaskHdl != nullptr) {
        vTaskDelete(outputTaskHdl);
    }
    if (stateTaskHdl != nullptr) {
        vTaskDelete(stateTaskHdl);
    }
    log_d("...ZzZzZz.");
    GSM::deepSleep(sec * 1000);
}

void consoleSendHeader(const char *str) {
    DEBUG_PORT.printf("Send %s data...", str);
}

void consoleSendFooter(const bool success, const unsigned long time) {
    DEBUG_PORT.printf("...%s (%lums)\n", success ? "done" : "failed", time);
}

std::string buildDTCPayload(DTCs *dtcs) {
    JsonDocument doc;
    JsonArray a = doc["dtc"].to<JsonArray>();
    for (int i = 0; i < dtcs->getCount(); ++i) {
        a.add(dtcs->getCode(i)->c_str());
    }
    std::string payload;
    serializeJson(doc, payload);
    return payload;
}

void WiFiAPStart(WiFiEvent_t event, WiFiEventInfo_t info) {
    wifiAPStarted = true;
    DEBUG_PORT.println("AP started.");

    DEBUG_PORT.printf("AP - IP address: %s\n", WiFi.softAPIP().toString().c_str());
}

void WiFiAPStop(WiFiEvent_t event, WiFiEventInfo_t info) {
    wifiAPStarted = false;
    wifiAPInUse = false;
    wifiAPStaConnected = 0;
    DEBUG_PORT.println("AP stopped.");
}

void WiFiAPStationConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
    ++wifiAPStaConnected;
    wifiAPInUse = true;

    if (wifiAPStaConnected == 1) {
        DEBUG_PORT.println("AP in use.");
#if !defined(WS_SIM7670G_V2)
        OBD.end();
#endif
    }
}

void WiFiAPStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (wifiAPStaConnected != 0) {
        --wifiAPStaConnected;
    }

    if (wifiAPStaConnected == 0) {
        DEBUG_PORT.println("AP all clients disconnected.");
#if !defined(WS_SIM7670G_V2)
        OBD.begin(Settings.OBD2.getName(OBD_ADP_NAME), Settings.OBD2.getMAC(), Settings.OBD2.getProtocol(),
                  Settings.OBD2.getCheckPIDSupport(), Settings.OBD2.getDebug(), Settings.OBD2.getSpecifyNumResponses());
        OBD.connect(true);
#endif
        wifiAPInUse = false;
    }
}

void startWiFiAP() {
    DEBUG_PORT.print("Start AP...");

    WiFi.disconnect(true);

    WiFi.mode(WIFI_AP);

    WiFi.onEvent(WiFiAPStart, WiFiEvent_t::ARDUINO_EVENT_WIFI_AP_START);
    WiFi.onEvent(WiFiAPStop, WiFiEvent_t::ARDUINO_EVENT_WIFI_AP_STOP);
    WiFi.onEvent(WiFiAPStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_AP_STACONNECTED);
    WiFi.onEvent(WiFiAPStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);

    String ssid = Settings.WiFi.getAPSSID();
    if (ssid.isEmpty()) {
        ssid = "OBD2-MQTT-" + String(stripChars(WiFi.macAddress().c_str()).c_str());
        Settings.WiFi.setAPSSID(ssid.c_str());
    }
    WiFi.softAP(
        ssid.c_str(),
        Settings.WiFi.getAPPassword()
    );
}

void startHttpServer() {
    server.on("/api/obd/status", HTTP_GET, [](AsyncWebServerRequest *request) {
#if defined(WS_SIM7670G_V2)
        request->send(200, MIME_TYPE_JSON, BLEConnection::statusJSON().c_str());
#else
        request->send(200, MIME_TYPE_JSON, "{\"supported\":false}");
#endif
    });
#if defined(WS_SIM7670G_V2)
    server.on("/api/obd/scan", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!BLEConnection::scan()) { request->send(409, MIME_TYPE_PLAIN, "Bluetooth is busy"); return; }
        request->send(202, MIME_TYPE_JSON, BLEConnection::statusJSON().c_str());
    });
    server.on("/api/obd/connect", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("mac") || !request->hasParam("addressType") || !request->hasParam("protocol")) {
            request->send(400, MIME_TYPE_PLAIN, "Select a scanned device and Protocol"); return;
        }
        const String type = request->getParam("addressType")->value();
        const String protocol = request->getParam("protocol")->value();
        if ((type != "0" && type != "1") || protocol.length() != 1 ||
            !BLEConnection::connect(request->getParam("mac")->value(), type.toInt(), protocol[0])) {
            request->send(409, MIME_TYPE_PLAIN, "Bluetooth is busy or scan selection has expired"); return;
        }
        request->send(202, MIME_TYPE_JSON, BLEConnection::statusJSON().c_str());
    });
#endif
    server.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, MIME_TYPE_PLAIN, getVersion());
    });

    server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
        RuntimeAccess::Read access;
        if (!access) { request->send(409, MIME_TYPE_PLAIN, "Configuration is being saved; retry shortly"); return; }
        request->send(200, MIME_TYPE_JSON, Settings.buildJson().c_str());
    });

    server.on(
        "/api/settings",
        HTTP_PUT,
        [](AsyncWebServerRequest *request) {
        },
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (request->contentType() == MIME_TYPE_JSON) {
                if (!index) {
                    if (total == 0 || total > 16384) { request->send(413); return; }
                    request->_tempObject = calloc(total, 1);
                    if (!request->_tempObject) { request->send(503); return; }
                }

                if (request->_tempObject != nullptr) {
                    if (index > total || len > total - index) { request->send(400); return; }
                    memcpy(static_cast<uint8_t *>(request->_tempObject) + index, data, len);

                    if (index + len == total) {
                        RuntimeAccess::Write access;
                        if (!access) { request->send(409, MIME_TYPE_PLAIN, "Device is busy; retry saving shortly"); return; }
#if defined(WS_SIM7670G_V2)
                        if (BLEConnection::isBusy()) {
                            request->send(409, MIME_TYPE_PLAIN, "Wait for Bluetooth operation to finish"); return;
                        }
#endif
                        auto json = std::string(static_cast<const char *>(request->_tempObject), total);
                        auto candidate = Settings;
                        if (!candidate.parseJson(json)) { request->send(400); return; }
                        if (!candidate.writeSettings(LittleFS)) { request->send(500); return; }
                        Settings = candidate;
                        request->send(200);
                    }
                }
            } else {
                request->send(406);
            }
        }
    );

    server.on("/api/states", HTTP_GET, [](AsyncWebServerRequest *request) {
        RuntimeAccess::Read access;
        if (!access) { request->send(409, MIME_TYPE_PLAIN, "Configuration is being saved; retry shortly"); return; }
        request->send(200, MIME_TYPE_JSON, OBD.buildJSON().c_str());
    });

    server.on(
        "/api/states",
        HTTP_PUT,
        [](AsyncWebServerRequest *request) {
        },
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (request->contentType() == MIME_TYPE_JSON) {
                if (!index) {
                    if (total == 0 || total > 262144) { request->send(413); return; }
                    request->_tempObject = calloc(total, 1);
                    if (!request->_tempObject) { request->send(503); return; }
                }

                if (request->_tempObject != nullptr) {
                    if (index > total || len > total - index) { request->send(400); return; }
                    memcpy(static_cast<uint8_t *>(request->_tempObject) + index, data, len);

                    if (index + len == total) {
                        RuntimeAccess::Write access;
                        if (!access) { request->send(409, MIME_TYPE_PLAIN, "Device is busy; retry saving shortly"); return; }
#if defined(WS_SIM7670G_V2)
                        if (BLEConnection::isBusy()) {
                            request->send(409, MIME_TYPE_PLAIN, "Wait for Bluetooth operation to finish"); return;
                        }
#endif
                        auto json = std::string(static_cast<const char *>(request->_tempObject), total);
                        if (OBD.hasPendingQuery()) {
                            request->send(409, MIME_TYPE_PLAIN, "Wait for the current OBD reply to finish"); return;
                        }
                        auto previous = OBD.buildJSON();
                        if (!OBD.parseJSON(json)) { request->send(400); return; }
                        if (!OBD.writeStates(LittleFS)) {
                            OBD.parseJSON(previous);
                            request->send(500); return;
                        }
                        request->send(200);
                    }
                }
            } else {
                request->send(406);
            }
        }
    );

    server.on("/api/canDeepSleep", HTTP_GET, [](AsyncWebServerRequest *request) {
        std::string payload;
        JsonDocument doc;

        doc["canDeepSleep"] = GSM::canDeepSleep();

        serializeJson(doc, payload);

        request->send(200, MIME_TYPE_JSON, payload.c_str());
    });

    server.on("/api/wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
        std::string payload;
        JsonDocument wifiInfo;

        wifiInfo["hostname"] = WiFi.softAPgetHostname();
        wifiInfo["SSID"] = WiFi.softAPSSID();
        wifiInfo["ip"] = WiFi.softAPIP().toString();
        wifiInfo["mac"] = WiFi.macAddress();

        serializeJson(wifiInfo, payload);

        request->send(200, MIME_TYPE_JSON, payload.c_str());
    });

    server.on("/api/modem", HTTP_GET, [](AsyncWebServerRequest *request) {
        std::string payload;
        JsonDocument modemInfo;

        modemInfo["name"] = gsm.modem.getModemName();
        modemInfo["info"] = gsm.modem.getModemInfo();
        modemInfo["signalQuality"] = gsm.modem.getSignalQuality();
        modemInfo["ip"] = gsm.modem.getLocalIP();
        modemInfo["IMEI"] = gsm.modem.getIMEI();
        modemInfo["IMSI"] = gsm.modem.getIMSI();
        modemInfo["CCID"] = gsm.modem.getSimCCID();
        modemInfo["operator"] = gsm.modem.getOperator();

        serializeJson(modemInfo, payload);

        request->send(200, MIME_TYPE_JSON, payload.c_str());
    });

    server.on("/api/discoveredDevices", HTTP_GET, [](AsyncWebServerRequest *request) {
        File file = LittleFS.open(DISCOVERED_DEVICES_FILE, FILE_READ);
        if (file && !file.isDirectory()) {
            JsonDocument doc;
            if (!deserializeJson(doc, file)) {
                std::string payload;
                serializeJson(doc, payload);
                request->send(200, MIME_TYPE_JSON, payload.c_str());
            } else {
                request->send(500);
            }
            file.close();
        } else {
            request->send(404);
        }
    });

    server.on("/api/DTCs", HTTP_GET, [](AsyncWebServerRequest *request) {
        DTCs *dtcs = OBD.getDTCs();
        if (dtcs != nullptr && dtcs->getCount() != 0) {
            request->send(200, MIME_TYPE_JSON, buildDTCPayload(dtcs).c_str());
        } else {
            request->send(404);
        }
    });

    server.begin(LittleFS);
}

void onOBDConnected() {
    obdConnected = true;
    obdConnectErrors = 0;
}

void onOBDConnectError() {
    obdConnected = false;
    ++obdConnectErrors;
#if DEVICE_CAN_DEEP_SLEEP && DEVICE_HAS_BATTERY
    if (GSM::isBatteryUsed() && obdConnectErrors > 5) {
        deepSleep(Settings.General.getSleepDuration());
    }
#endif
}

#ifdef USE_BLE
void onBLEDevicesDiscovered(BLEScanResultsSet *btDeviceList) {
    JsonDocument devices;

    File file = LittleFS.open(DISCOVERED_DEVICES_FILE, FILE_WRITE);
    if (!file) {
        log_d("Failed to open file discovered_devices.json for writing.");
        return;
    }

    for (int i = 0; i < btDeviceList->getCount(); i++) {
        JsonDocument dev;
        BLEAdvertisedDevice *device = btDeviceList->getDevice(i);
        if (device && !device->getName().empty()) {
            dev["name"] = device->getName();
            dev["mac"] = device->getAddress().toString();
            devices["device"].add(dev);
        }
    }

    serializeJson(devices, file);

    file.close();
}

#else
void onBTDevicesDiscovered(BTScanResults *btDeviceList) {
    JsonDocument devices;

    File file = LittleFS.open(DISCOVERED_DEVICES_FILE, FILE_WRITE);
    if (!file) {
        log_d("Failed to open file discovered_devices.json for writing.");
        return;
    }

    for (int i = 0; i < btDeviceList->getCount(); i++) {
        JsonDocument dev;
        BTAdvertisedDevice *device = btDeviceList->getDevice(i);
        dev["name"] = device->getName();
        dev["mac"] = device->getAddress().toString();
        devices["device"].add(dev);
    }

    serializeJson(devices, file);

    file.close();
}
#endif

bool sendDiscoveryData() {
    const unsigned long start = millis();
    bool allSendsSuccessed = false;
    bool allowOffline = Settings.MQTT.getAllowOffline();

    consoleSendHeader("discovery");

    std::vector<OBDState *> states{};
    OBD.getStates([](const OBDState *state) {
        return state->isVisible() && state->isEnabled() && state->isSupported() && !(
                   state->isDiagnostic() && state->getUpdateInterval() == -1);
    }, states);
    if (!states.empty()) {
        for (auto &state: states) {
            allSendsSuccessed |= mqtt.sendTopicConfig(state->getName(), state->getDescription(), state->getIcon(),
                                                      state->getUnit(), state->getDeviceClass(),
                                                      state->isMeasurement() ? SC_MEASUREMENT : "",
                                                      state->isDiagnostic() ? EC_DIAGNOSTIC : "",
                                                      state->valueType() == OBD_STATE_TYPE_BOOL
                                                          ? TT_B_SENSOR
                                                          : TT_SENSOR,
                                                      "", allowOffline);
        }
    } else {
        allSendsSuccessed = true;
    }

    consoleSendFooter(allSendsSuccessed, millis() - start);

    return allSendsSuccessed;
}

bool sendDiagnosticDiscoveryData() {
    const unsigned long start = millis();
    bool allSendsSuccessed = false;

    consoleSendHeader("diagnostic discovery");

    allSendsSuccessed |= mqtt.sendTopicConfig(HA_T_CPUTEMP, "CPU Temperature", "thermometer", "°C", "temperature",
                                              SC_MEASUREMENT, EC_DIAGNOSTIC);
    allSendsSuccessed |= mqtt.sendTopicConfig(HA_T_FREEMEM, "Free Memory", "memory", "B", "", SC_MEASUREMENT,
                                              EC_DIAGNOSTIC);
    allSendsSuccessed |= mqtt.sendTopicConfig(HA_T_UPTIME, "Uptime", "timer-play", "sec", "", SC_MEASUREMENT,
                                              EC_DIAGNOSTIC);
    allSendsSuccessed |= mqtt.sendTopicConfig(HA_T_RECONNECTS, "Number of reconnects", "connection", "", "",
                                              SC_MEASUREMENT, EC_DIAGNOSTIC);

    if (!gsm.getIpAddress().empty()) {
        allSendsSuccessed |= mqtt.sendTopicConfig(HA_T_IP_ADDR, "IP Address", "network-outline", "", "", "",
                                                  EC_DIAGNOSTIC);
    }

    if (GSM::isUseGPRS()) {
        allSendsSuccessed |= mqtt.sendTopicConfig(HA_T_SQ, "Signal Quality", "signal", "dBm",
                                                  "signal_strength", "", EC_DIAGNOSTIC);
    }

    if (GSM::hasGSMLocation()) {
        allSendsSuccessed |= mqtt.sendTopicConfig(HA_T_GSM_LOC, "GSM Location", "crosshairs-gps", "", "", "",
                                                  EC_DIAGNOSTIC, TT_D_TRACKER, "gps", true);
    }

    if (GSM::hasGPSLocation()) {
        allSendsSuccessed |= mqtt.sendTopicConfig(HA_T_GPS_LOC, "GPS Location", "crosshairs-gps", "", "", "",
                                                  EC_DIAGNOSTIC, TT_D_TRACKER, "gps", true);
    }

#if DEVICE_HAS_BATTERY
#if DEVICE_BATTERY_VOLTAGE
    allSendsSuccessed |= mqtt.sendTopicConfig(HA_T_BAT_VOL, "Internal Battery Voltage", "battery",
                                              "mV", "voltage", "", EC_DIAGNOSTIC);
#endif
#if DEVICE_BATTERY_LEVEL
    allSendsSuccessed |= mqtt.sendTopicConfig(HA_T_BAT_LVL, "Internal Battery Level", "battery",
                                              "%", "battery", "", EC_DIAGNOSTIC);
#endif
#endif

    consoleSendFooter(allSendsSuccessed, millis() - start);

    return allSendsSuccessed;
}

bool sendStaticDiagnosticDiscoveryData() {
    const unsigned long start = millis();
    bool allSendsSuccessed = false;

    consoleSendHeader("static diagnostic discovery");

    std::vector<OBDState *> states{};
    OBD.getStates([](const OBDState *state) {
        return state->isVisible() && state->isEnabled() && state->isSupported() && state->isDiagnostic() && state->
               getUpdateInterval() == -1;
    }, states);
    if (!states.empty()) {
        for (auto &state: states) {
            allSendsSuccessed |= mqtt.sendTopicConfig(state->getName(), state->getDescription(), state->getIcon(),
                                                      state->getUnit(), state->getDeviceClass(),
                                                      state->isMeasurement() ? SC_MEASUREMENT : "",
                                                      state->isDiagnostic() ? EC_DIAGNOSTIC : "",
                                                      state->valueType() == OBD_STATE_TYPE_BOOL
                                                          ? TT_B_SENSOR
                                                          : TT_SENSOR);
        }
    } else {
        allSendsSuccessed = true;
    }

    consoleSendFooter(allSendsSuccessed, millis() - start);

    return allSendsSuccessed;
}

bool sendStates(std::vector<OBDState *> &states, bool allSendsSuccessed) {
    if (!states.empty()) {
        for (auto &state: states) {
            // A failed/unread item must not be republished as a fresh reading.
            if (!state->getLastUpdate() || state->getUpdateStatus() != ELM_SUCCESS) continue;
            char* formatted = nullptr;
            if (strcmp(state->valueType(), OBD_STATE_TYPE_INT) == 0)
                formatted = static_cast<OBDStateInt*>(state)->formatValue();
            else if (strcmp(state->valueType(), OBD_STATE_TYPE_FLOAT) == 0)
                formatted = static_cast<OBDStateFloat*>(state)->formatValue();
            else if (strcmp(state->valueType(), OBD_STATE_TYPE_BOOL) == 0)
                formatted = static_cast<OBDStateBool*>(state)->formatValue();
            if (!formatted) continue;
            const std::string value(formatted);
            free(formatted);
            allSendsSuccessed |= mqtt.sendTopicUpdate(state->getName(), value);
        }
    } else {
        allSendsSuccessed = true;
    }

    return allSendsSuccessed;
}

bool sendOBDData() {
    const unsigned long start = millis();
    bool allSendsSuccessed = false;

    consoleSendHeader("OBD");

    allSendsSuccessed |= mqtt.sendTopicUpdate(LWT_TOPIC, LWT_CONNECTED);

    std::vector<OBDState *> states{};
    OBD.getStates([](const OBDState *state) {
        return state->isVisible() && state->isEnabled() && state->isSupported() && !(
                   state->isDiagnostic() && state->getUpdateInterval() == -1);
    }, states);
    allSendsSuccessed = sendStates(states, allSendsSuccessed);

    consoleSendFooter(allSendsSuccessed, millis() - start);

    return allSendsSuccessed;
}

bool sendDiagnosticData() {
    const unsigned long start = millis();
    bool allSendsSuccessed = false;
    char tmp_char[50];

    consoleSendHeader("diagnostic");

    sprintf(tmp_char, "%d", static_cast<int>(temperatureRead()));
    allSendsSuccessed |= mqtt.sendTopicUpdate(HA_T_CPUTEMP, std::string(tmp_char));

    sprintf(tmp_char, "%lu", static_cast<long>(getESPHeapSize()));
    allSendsSuccessed |= mqtt.sendTopicUpdate(HA_T_FREEMEM, std::string(tmp_char));

    sprintf(tmp_char, "%lu", (millis() - startTime) / 1000);
    allSendsSuccessed |= mqtt.sendTopicUpdate(HA_T_UPTIME, std::string(tmp_char));

    sprintf(tmp_char, "%d", mqtt.reconnectAttemps());
    allSendsSuccessed |= mqtt.sendTopicUpdate(HA_T_RECONNECTS, std::string(tmp_char));

    if (!gsm.getIpAddress().empty()) {
        sprintf(tmp_char, "%s", gsm.getIpAddress().c_str());
        allSendsSuccessed |= mqtt.sendTopicUpdate(HA_T_IP_ADDR, std::string(tmp_char));
    }

    if (GSM::isUseGPRS() && signalQuality != SQ_NOT_KNOWN) {
        sprintf(tmp_char, "%d", GSM::convertSQToRSSI(signalQuality));
        allSendsSuccessed |= mqtt.sendTopicUpdate(HA_T_SQ, std::string(tmp_char));
    }

#if DEVICE_HAS_BATTERY
#if DEVICE_BATTERY_VOLTAGE
    const unsigned int batteryVoltage = GSM::getBatteryVoltage();
    if (batteryVoltage > 0) {
        sprintf(tmp_char, "%u", batteryVoltage);
        allSendsSuccessed |= mqtt.sendTopicUpdate(HA_T_BAT_VOL, std::string(tmp_char));
    }
#endif
#if DEVICE_BATTERY_LEVEL
    const float batteryLevel = GSM::getBatteryLevel();
    if (std::isfinite(batteryLevel)) {
        sprintf(tmp_char, "%d", static_cast<int>(batteryLevel));
        allSendsSuccessed |= mqtt.sendTopicUpdate(HA_T_BAT_LVL, std::string(tmp_char));
    }
#endif
#endif

    consoleSendFooter(allSendsSuccessed, millis() - start);

    return allSendsSuccessed;
}

bool sendStaticDiagnosticData() {
    const unsigned long start = millis();
    bool allSendsSuccessed = false;

    consoleSendHeader("static diagnostic");

    std::vector<OBDState *> states{};
    OBD.getStates([](const OBDState *state) {
        return state->isVisible() && state->isEnabled() && state->isSupported() && state->isDiagnostic() && state->
               getUpdateInterval() == -1;
    }, states);
    allSendsSuccessed = sendStates(states, allSendsSuccessed);

    consoleSendFooter(allSendsSuccessed, millis() - start);

    return allSendsSuccessed;
}

bool sendDTCDiagnosticData() {
    const unsigned long start = millis();
    bool allSendsSuccessed = false;

    consoleSendHeader("DTC diagnostic");

    DTCs *dtcs = OBD.getDTCs();
    if (dtcs != nullptr) {
        allSendsSuccessed |= mqtt.sendTopicConfig(HAT_T_DTC, "DTC", "engine",
                                                  "", "", "", EC_DIAGNOSTIC, TT_SENSOR, "", false,
                                                  "{{ value_json.dtc | join(\",\") }}");
        if (dtcs->getCount() != 0) {
            allSendsSuccessed |= mqtt.sendTopicConfig(HAT_T_CLEAR_DTC, "Clear DTC", "",
                                                      "", "", "", EC_DIAGNOSTIC, TT_BUTTON, "", false);

            mqtt.subscribe(HAT_T_CLEAR_DTC, [](const char *) {
                clearDTC = true;
            });
        }

        allSendsSuccessed |= mqtt.sendTopicUpdate(HAT_T_DTC, buildDTCPayload(dtcs));
    } else {
        allSendsSuccessed = true;
    }

    consoleSendFooter(allSendsSuccessed, millis() - start);

    return allSendsSuccessed;
}

std::string buildLocationAttrib(const float lat, const float lon, const float acc) {
    std::string payload;
    JsonDocument attribs;

    attribs["latitude"] = lat;
    attribs["longitude"] = lon;
    if (std::isfinite(acc)) attribs["gps_accuracy"] = acc;

    serializeJson(attribs, payload);

    return payload;
}

bool sendLocationData() {
    // A missing fix is normal at startup or without satellite coverage.
    if (!GSM::hasGSMLocation() && !(GSM::hasGPSLocation() && gpsFixValid)) {
        DEBUG_PORT.println("Send location data...skipped (no valid fix).");
        return true;
    }

    const unsigned long start = millis();
    bool allSendsSuccessed = false;

    consoleSendHeader("location");

    if (GSM::hasGSMLocation()) {
        allSendsSuccessed |= mqtt.sendTopicUpdate(
            HA_T_GSM_LOC,
            buildLocationAttrib(gsmLatitude, gsmLongitude, gsmAccuracy),
            true
        );
    }
    if (GSM::hasGPSLocation() && gpsFixValid) {
        allSendsSuccessed |= mqtt.sendTopicUpdate(
            HA_T_GPS_LOC,
            buildLocationAttrib(gpsLatitude, gpsLongitude, gpsAccuracy),
            true
        );
    }

    consoleSendFooter(allSendsSuccessed, millis() - start);

    return allSendsSuccessed;
}

unsigned long calcTimestamp(const unsigned int interval) {
    return millis() + interval * 1000L;
}

void mqttSendData() {
    if (millis() < lastMQTTOutput) {
        return;
    }

    if (mqtt.connected()) {
        if (millis() > lastMQTTDiscoveryOutput) {
            allDiscoverySend = false;
        }

        if (!allDiscoverySend) {
            if ((allDiscoverySend = sendDiscoveryData())) {
                lastMQTTDiscoveryOutput = calcTimestamp(Settings.MQTT.getDiscoveryInterval());
            } else {
                return;
            }
        }

        if (millis() > lastMQTTDiagnosticDiscoveryOutput) {
            allDiagnosticDiscoverySend = false;
        }

        if (!allDiagnosticDiscoverySend) {
            if ((allDiagnosticDiscoverySend = sendDiagnosticDiscoveryData())) {
                lastMQTTDiagnosticDiscoveryOutput = calcTimestamp(Settings.MQTT.getDiscoveryInterval());
            } else {
                return;
            }
        }

        if (millis() > lastMQTTStaticDiagnosticDiscoveryOutput) {
            allStaticDiagnosticDiscoverySend = false;
        }

        if (!allStaticDiagnosticDiscoverySend) {
            if ((allStaticDiagnosticDiscoverySend = sendStaticDiagnosticDiscoveryData())) {
                lastMQTTStaticDiagnosticDiscoveryOutput = calcTimestamp(Settings.MQTT.getDiscoveryInterval());
            } else {
                return;
            }
        }

        if (millis() > lastMQTTDiagnosticOutput) {
            if (sendDiagnosticData()) {
                lastMQTTDiagnosticOutput = calcTimestamp(Settings.MQTT.getDiagnosticInterval());
            } else {
                return;
            }
        }

        if (obdConnected) {
            if (millis() > lastMQTTStaticDiagnosticOutput) {
                if (sendStaticDiagnosticData()) {
                    lastMQTTStaticDiagnosticOutput = calcTimestamp(Settings.MQTT.getDiagnosticInterval() * 2);
                } else {
                    return;
                }
            }

            if (millis() > lastMQTTDTCDiagnosticOutput) {
                if (sendDTCDiagnosticData()) {
                    lastMQTTDTCDiagnosticOutput = calcTimestamp(Settings.MQTT.getDiagnosticInterval());
                } else {
                    return;
                }
            }

            if (sendOBDData()) {
                lastMQTTOutput = calcTimestamp(Settings.MQTT.getDataInterval());
            }
        } else if (mqtt.sendTopicUpdate(LWT_TOPIC, LWT_CONNECTED)) {
            const uint iv = Settings.MQTT.getDataInterval() * 5;
            lastMQTTOutput = calcTimestamp(iv < MQTT_KEEPALIVE ? iv : MQTT_KEEPALIVE - 1);
        }

        // Location is optional. Neither a missing fix nor a failed publication
        // may block diagnostics, vehicle data or the online status above.
        if (millis() > lastMQTTLocationOutput) {
            sendLocationData();
            lastMQTTLocationOutput = calcTimestamp(Settings.MQTT.getLocationInterval());
        }
    } else {
        delay(500);
    }
}

[[noreturn]] void readStatesTask(void *parameters) {
    for (;;) {
        {
        RuntimeAccess::Read access;
        if (!access) { delay(10); continue; }
#if defined(WS_SIM7670G_V2)
        if (!wifiAPInUse && clearDTC && obdConnected && !OBD.hasPendingQuery()) {
            OBD.resetDTCs();
            clearDTC = false;
        }
        BLEConnection::tick(!wifiAPInUse);
#else
        if (!wifiAPInUse) {
            if (clearDTC && !OBD.hasPendingQuery()) {
                DEBUG_PORT.print("DTC reset ");
                if (OBD.resetDTCs()) {
                    DEBUG_PORT.println("done.");
                } else {
                    DEBUG_PORT.println("failed.");
                }
                clearDTC = false;
            }

            OBD.loop();
        }
#endif
        }
        delay(10);
    }
}

void sendOBDConnectionStatus() {
#if defined(WS_SIM7670G_V2)
    static unsigned long discoveryAt = 0, statusAt = 0;
    static std::string previous;
    if (!mqtt.connected()) { discoveryAt = 0; statusAt = 0; return; }
    if (!discoveryAt || static_cast<int32_t>(millis() - discoveryAt) >= 0) {
        const bool a = mqtt.sendTopicConfig("obdConnectionState", "OBD Connection Status", "bluetooth",
            "", "", "", EC_DIAGNOSTIC);
        const bool b = mqtt.sendTopicConfig("obdConnected", "OBD Adapter Connected", "car-connected",
            "", "connectivity", "", EC_DIAGNOSTIC, TT_B_SENSOR);
        if (a && b) discoveryAt = millis() + 300000UL;
    }
    const std::string phase = OBD.connectionPhase.load();
    if (phase != previous || !statusAt || static_cast<int32_t>(millis() - statusAt) >= 0) {
        const bool a = mqtt.sendTopicUpdate("obdConnectionState", phase);
        const bool b = mqtt.sendTopicUpdate("obdConnected", phase == "connected" ? "on" : "off");
        if (a && b) { previous = phase; statusAt = millis() + 5000UL; }
    }
#endif
}

[[noreturn]] void outputTask(void *parameters) {
    unsigned long checkInterval = 0;
    for (;;) {
        {
        RuntimeAccess::Read access;
        if (!access) { delay(10); continue; }
        if (!wifiAPInUse) {
#if DEVICE_CAN_DEEP_SLEEP && DEVICE_HAS_BATTERY
            if (GSM::isBatteryUsed()) {
                const unsigned int batVoltage = GSM::getBatteryVoltage();
                if (batVoltage > MIN_VOLTAGE_LEVEL) {
                    const int sum = std::accumulate(batteryVoltages.begin(), batteryVoltages.end(), 0);
                    const double avgBat = static_cast<double>(sum) / batteryVoltages.size();
                    if (millis() > batteryTime + 5000) {
                        if (batteryVoltages.size() > 10) {
                            batteryVoltages.erase(batteryVoltages.begin());
                        }
                        batteryVoltages.push_back(batVoltage);
                        batteryTime = millis();
                    }
                    const bool drain = batteryVoltages.size() > 10 && batVoltage < (avgBat - 10);

                    const double avgLU = OBD.avgLastUpdate([](const OBDState *state) {
                        return state->isEnabled() &&
                               state->getType() == obd::READ && state->getUpdateInterval() > 0 &&
                               state->getUpdateInterval() <= 5 * 60 * 1000;
                    });

                    if (batVoltage < LOW_VOLTAGE_LEVEL || (
                            drain && avgLU > Settings.General.getSleepTimeout() * 1000)) {
                        if (batVoltage < LOW_VOLTAGE_LEVEL) {
                            log_d("Battery has low voltage.");
                        }
                        deepSleep(Settings.General.getSleepDuration());
                    }
                }
            }
#endif

            if (!gsm.checkNetwork()) {
                gpsFixValid = false;
                delay(250);
                continue;
            }

            mqtt.loop();

            if (GSM::isUseGPRS()) {
                signalQuality = gsm.getSignalQuality();
            }

            if (!mqtt.connected()) {
                auto client_id = String(MQTT_CLIENT_ID) + "-" + stripChars(mqtt.getIdentifier()).c_str();
                if (!mqtt.connect(
                    client_id.c_str(),
                    Settings.MQTT.getHostname().c_str(),
                    Settings.MQTT.getPort(),
                    Settings.MQTT.getUsername().c_str(),
                    Settings.MQTT.getPassword().c_str(),
                    static_cast<mqttProtocol>(Settings.MQTT.getProtocol())
                )) {
                    gsm.checkNetwork(true);
#if defined(WS_SIM7670G_V2)
                    delay(10000); // Back off on broker/DNS/authentication failure.
#endif
                } else {
                    lastMQTTOutput = 0; // Report immediately after connecting.
                }
            }

            // Service HA before any potentially slow positioning query.
            if (mqtt.connected()) {
                mqttSendData();
            }
            sendOBDConnectionStatus();

            if ((GSM::hasGSMLocation() || GSM::hasGPSLocation()) && millis() > checkInterval) {
                unsigned long start = millis();
                bool allReadSuccessed = false;

                DEBUG_PORT.print("Read location...");
                if (gsm.isNetworkConnected()) {
                    float gsm_latitude = 0;
                    float gsm_longitude = 0;
                    float gsm_accuracy = 0;

                    if ((allReadSuccessed |= gsm.readGSMLocation(gsm_latitude, gsm_longitude, gsm_accuracy))) {
                        gsmLatitude = gsm_latitude;
                        gsmLongitude = gsm_longitude;
                        gsmAccuracy = gsm_accuracy;
                    }
                }

                if (GSM::hasGPSLocation()) {
                    float gps_latitude = 0;
                    float gps_longitude = 0;
                    float gps_accuracy = 0;

                    const bool gpsRead = gsm.readGPSLocation(gps_latitude, gps_longitude, gps_accuracy);
                    gpsFixValid = gpsRead;
                    allReadSuccessed |= gpsRead;
                    if (!gpsRead) {
                        gsm.checkGPS();
                    } else {
                        gpsLatitude = gps_latitude;
                        gpsLongitude = gps_longitude;
                        gpsAccuracy = gps_accuracy;
                    }
                }

                checkInterval = millis() + Settings.MQTT.getLocationInterval() * 1000L;
                consoleSendFooter(allReadSuccessed, millis() - start);
            }
#if defined(WS_SIM7670G_V2)
        } else {
            // Configuration pauses vehicle reads, not MQTT keepalive/status.
            mqtt.loop();
            sendOBDConnectionStatus();
#endif
        }
        }
        delay(50);
    }
}

String buildIdentifier(const char *devMac) {
    String mID = "";
    if (static_cast<MQTTSettings::MQTTIdentifierType>(Settings.MQTT.getIdType()) ==
        MQTTSettings::MQTTIdentifierType::CUSTOM && Settings.MQTT.getIdSuffix().length() > 0) {
        mID = Settings.MQTT.getIdSuffix().c_str();
    } else if (devMac != nullptr && strlen(devMac) > 0) {
        mID = devMac;
        if (static_cast<MQTTSettings::MQTTIdentifierType>(Settings.MQTT.getIdType()) ==
            MQTTSettings::MQTTIdentifierType::MAC_IMEI) {
            mID += "-";
            mID += gsm.modem.getIMEI().substring(gsm.modem.getIMEI().length() - 4).c_str();
        }
    }
    return mID;
}

void startOutputTask(const char *id) {
    if (!Settings.MQTT.getHostname().isEmpty()) {
        mqtt.setClient(gsm.getClient(Settings.MQTT.getSecure()));
        mqtt.setIdentifier(id);

        xTaskCreatePinnedToCore(outputTask, "OutputTask", 9216, nullptr, 10, &outputTaskHdl, 0);
    }
}

void startReadTask() {
#if defined(WS_SIM7670G_V2)
    // Always create the worker, including when OBD starts disabled. The web
    // connection page can then enable OBD without a reboot.
    xTaskCreatePinnedToCore(readStatesTask, "OBDWorker", 12288, nullptr, 1, &stateTaskHdl, 1);
#else
    if (!Settings.OBD2.getDisable()) {
#ifdef USE_BLE
        OBD.onDevicesDiscovered(onBLEDevicesDiscovered);
#else
        OBD.onDevicesDiscovered(onBTDevicesDiscovered);
#endif
        OBD.connect();

        xTaskCreatePinnedToCore(readStatesTask, "ReadStatesTask", 9216, nullptr, 1, &stateTaskHdl, 1);
    }
#endif
}

void setup() {
    RuntimeAccess::Read startupAccess;
    startTime = millis();

    DEBUG_PORT.begin(115200);

    if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
        Serial.println("LittleFS mount failed; stored configuration was not formatted.");
        return;
    }

    // init the coprozessor
    GSM::ulpInit();

    // init battery if needed
    GSM::initBattery();

    Settings.readSettings(LittleFS);
    OBD.readStates(LittleFS);
#if defined(WS_SIM7670G_V2)
    BLEConnection::init();
#endif

    // disable Watch Dog for Core 0 - should fix crashes
    disableCore0WDT();

    startWiFiAP();
    startHttpServer();

    // will be ignored if the device does not support
    gsm.setNetworkMode(Settings.Mobile.getNetworkMode());
    gsm.connectToNetwork();
#if !defined(WS_SIM7670G_V2)
    gsm.enableGPS();
#endif
    // SIM7670G starts GNSS on its first location read, after the first HA report.

    OBD.onConnected(onOBDConnected);
    OBD.onConnectError(onOBDConnectError);
    OBD.begin(Settings.OBD2.getName(OBD_ADP_NAME), Settings.OBD2.getMAC(), Settings.OBD2.getProtocol(),
              Settings.OBD2.getCheckPIDSupport(), Settings.OBD2.getDebug(), Settings.OBD2.getSpecifyNumResponses());

    String mID = buildIdentifier(Settings.OBD2.getMAC().c_str());
#if defined(WS_SIM7670G_V2)
    if (mID.isEmpty()) mID = buildIdentifier(WiFi.macAddress().c_str());
#endif
    if (!mID.isEmpty()) {
        startOutputTask(mID.c_str());
        startReadTask();
    } else {
        startReadTask();
        mID = buildIdentifier(OBD.getConnectedBTAddress().c_str());
        startOutputTask(mID.c_str());
    }
}

void loop() {
    vTaskDelete(nullptr);
}
