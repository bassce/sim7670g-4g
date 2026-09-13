// SPDX-License-Identifier: GPL-3.0-or-later
#include "gsm.h"
#if defined(WS_SIM7670G_V2)
#include <settings.h>
#include "sim7670g_gnss.h"

bool GSM::initSIM7670G(bool cyclePower) {
    modemReady = false;
    gpsReady = false;
    dataConnected = false;
    ipAddress.clear();
    // Reset socket state without deleting the Client held by MQTT.
    if (client) { client->stop(0); client->init(&modem); }
    if (secureClient) { secureClient->stop(0); secureClient->init(&modem); }
    if (cyclePower) {
        // A responsive modem gets a normal shutdown before power is removed.
        if (modem.testAT(1000)) {
            modem.sendAT("+CPOF");
            modem.waitResponse(5000L);
            delay(3000);
        }
        digitalWrite(BOARD_POWERON_PIN, LOW);
        delay(3000); // allow VVBAT to discharge; BAT_SET must not force it on
    }
    digitalWrite(BOARD_POWERON_PIN, HIGH);
    const uint32_t started = millis();
    bool responding = false;
    do {
        if ((responding = modem.testAT(1000))) break;
        delay(100);
    } while (millis() - started < 20000UL);
    if (!responding) {
        Serial.println("SIM7670G: no AT response; check power/BAT_SET and UART. Will retry.");
        return false;
    }
    if (!modem.init(Settings.Mobile.getPin().c_str())) {
        Serial.println("SIM7670G: initialization failed; check SIM/PIN. Will retry.");
        return false;
    }
    // Only TX/RX and DTR/RI are wired. Keep UART awake without RTS/CTS.
    modem.sendAT("+IFC=0,0");
    bool uartOK = modem.waitResponse(1000L) == 1;
    modem.sendAT("+CSCLK=0");
    bool sleepOK = modem.waitResponse(1000L) == 1;
    if (!uartOK || !sleepOK) {
        Serial.println("SIM7670G: UART/sleep configuration failed. Will retry.");
        return false;
    }
    modemReady = true;
    reconnectAttempts = 0;
    Serial.printf("Modem: %s\n", modem.getModemInfo().c_str());
    // This LTE-only driver has no A76XX NetworkMode API.
    if (networkMode != 2 && networkMode != 38) {
        Serial.println("SIM7670G: legacy network selection ignored (LTE-only module).");
    }
    return true;
}

bool GSM::checkSIM7670GNetwork() {
    // Cache health for five seconds; failed attempts back off for 30 seconds.
    // MQTT authentication failures must not cause continuous modem power cycles.
    if (static_cast<int32_t>(millis() - nextNetworkCheck) < 0) return dataConnected;
    nextNetworkCheck = millis() + 30000UL;
    if (!modemReady && !initSIM7670G(reconnectAttempts++ >= 2)) return false;
    if (Settings.Mobile.getAPN().isEmpty()) {
        dataConnected = false;
        ipAddress.clear();
        return false;
    }
    bool registered = modem.isNetworkConnected();
    bool attached = registered && modem.isGprsConnected();
    if (registered && attached) {
        dataConnected = true;
        reconnectAttempts = 0;
        if (ipAddress.empty()) ipAddress = modem.getLocalIP().c_str();
        nextNetworkCheck = millis() + 5000UL;
        return true;
    }
    dataConnected = false;
    ipAddress.clear();
    if (client) client->stop(0);
    if (secureClient) secureClient->stop(0);
    if (++reconnectAttempts > 10) {
        initSIM7670G(true);
        return false;
    }
    if (!registered && !modem.waitForNetwork(10000L)) {
        Serial.println("SIM7670G: waiting for LTE coverage; retry in 30 seconds.");
        nextNetworkCheck = millis() + 30000UL;
        return false;
    }
    if (!modem.gprsConnect(Settings.Mobile.getAPN().c_str(),
                           Settings.Mobile.getUsername().c_str(), Settings.Mobile.getPassword().c_str())) {
        Serial.println("SIM7670G: PDP connection failed; check APN. Will retry.");
        nextNetworkCheck = millis() + 30000UL;
        return false;
    }
    dataConnected = modem.isGprsConnected();
    if (dataConnected) {
        reconnectAttempts = 0;
        ipAddress = modem.getLocalIP().c_str();
        Serial.printf("LTE IP: %s\n", ipAddress.c_str());
    }
    nextNetworkCheck = millis() + (dataConnected ? 5000UL : 30000UL);
    return dataConnected;
}

bool GSM::enableSIM7670GGPS() {
    if (!modemReady) return false;
    if (gpsReady) return true;
    if (static_cast<int32_t>(millis() - nextGPSAttempt) < 0) return false;
    nextGPSAttempt = millis() + 30000UL;
    // GNSS is inside the modem; no ESP GPIO127 or external GPS UART is used.
    // The module controls its internal GNSS power line with CGNSSPWR.
    modem.sendAT("+CGNSSPWR=1");
    gpsReady = modem.waitResponse(10000L) == 1;
    if (!gpsReady) Serial.println("SIM7670G: GNSS start failed; will retry.");
    return gpsReady;
}

bool GSM::readSIM7670GGPS(float& latitude, float& longitude, float& accuracy) {
    if (!enableSIM7670GGPS()) return false;
    modem.sendAT("+CGNSSINFO");
    if (modem.waitResponse(10000L, "\r\n+CGNSSINFO:") != 1) {
        gpsReady = false;
        return false;
    }
    String response = stream.readStringUntil('\n');
    if (modem.waitResponse(1000L) != 1) return false;
    response.trim();
    static_assert(WS_GNSS_COORDINATE_FORMAT >= 0 && WS_GNSS_COORDINATE_FORMAT <= 2,
                  "WS_GNSS_COORDINATE_FORMAT must be 0, 1 or 2");
    if (!sim7670g::parseGnss(response.c_str(),
            static_cast<sim7670g::CoordinateFormat>(WS_GNSS_COORDINATE_FORMAT), latitude, longitude)) return false;
    // DOP is dimensionless; it is not a measured accuracy in metres.
    accuracy = NAN;
    return true;
}
#endif
