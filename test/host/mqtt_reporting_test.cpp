// Host regression tests. The runner extracts the actual reporting functions
// from main.cpp; only their hardware dependencies are replaced here.
#include <stdio.h>
#include <string.h>
using uint = unsigned int;
static unsigned long now;
unsigned long millis() { return now; }
void delay(unsigned long ms) { now += ms; }
static int diagnostics, obdReports, staticReports, dtcReports, online, locations;
static int skipped, failures;
static bool network, publishLocation, hasGsm, hasGps;
static bool obdConnected, gpsFixValid;
static float gpsLatitude, gpsLongitude, gpsAccuracy;
static float gsmLatitude, gsmLongitude, gsmAccuracy;
static bool allDiscoverySend, allDiagnosticDiscoverySend, allStaticDiagnosticDiscoverySend;
static unsigned long lastMQTTOutput, lastMQTTDiscoveryOutput;
static unsigned long lastMQTTDiagnosticDiscoveryOutput, lastMQTTStaticDiagnosticDiscoveryOutput;
static unsigned long lastMQTTLocationOutput, lastMQTTDiagnosticOutput;
static unsigned long lastMQTTStaticDiagnosticOutput, lastMQTTDTCDiagnosticOutput;
constexpr unsigned int MQTT_KEEPALIVE = 15;
constexpr const char *LWT_TOPIC = "connection", *LWT_CONNECTED = "connected";
constexpr const char *HA_T_GPS_LOC = "gps", *HA_T_GSM_LOC = "gsm";
struct {
    struct {
        uint getDiscoveryInterval() { return 600; }
        uint getLocationInterval() { return 10; }
        uint getDiagnosticInterval() { return 2; }
        uint getDataInterval() { return 1; }
    } MQTT;
} Settings;
struct GSM {
    static bool hasGSMLocation() { return hasGsm; }
    static bool hasGPSLocation() { return hasGps; }
};
struct {
    bool connected() { return network; }
    bool sendTopicUpdate(const char *topic, const char *, bool = false) {
        if (!strcmp(topic, LWT_TOPIC)) { ++online; return true; }
        ++locations;
        return publishLocation;
    }
} mqtt;
struct { void println(const char *) { ++skipped; } } DEBUG_PORT;
void consoleSendHeader(const char *) {}
void consoleSendFooter(bool, unsigned long) {}
const char *buildLocationAttrib(float, float, float) { return "coordinates"; }
bool sendDiscoveryData() { return true; }
bool sendDiagnosticDiscoveryData() { return true; }
bool sendStaticDiagnosticDiscoveryData() { return true; }
bool sendDiagnosticData() { ++diagnostics; return true; }
bool sendStaticDiagnosticData() { ++staticReports; return true; }
bool sendDTCDiagnosticData() { ++dtcReports; return true; }
bool sendOBDData() { ++obdReports; ++online; return true; }

#include "mqtt_reporting_functions.inc"

#define CHECK(condition) do { if (!(condition)) { \
    printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)
void reset() {
    now = 100;
    diagnostics = obdReports = staticReports = dtcReports = online = locations = skipped = 0;
    network = publishLocation = hasGps = true;
    hasGsm = obdConnected = gpsFixValid = false;
    allDiscoverySend = allDiagnosticDiscoverySend = allStaticDiagnosticDiscoverySend = false;
    lastMQTTOutput = lastMQTTDiscoveryOutput = lastMQTTDiagnosticDiscoveryOutput = 0;
    lastMQTTStaticDiagnosticDiscoveryOutput = lastMQTTLocationOutput = lastMQTTDiagnosticOutput = 0;
    lastMQTTStaticDiagnosticOutput = lastMQTTDTCDiagnosticOutput = 0;
}
int main() {
    reset(); // Cold start, no OBD and no GPS fix.
    mqttSendData();
    CHECK(diagnostics == 1 && online == 1 && locations == 0);
    CHECK(lastMQTTLocationOutput > now);
    for (int i = 0; i < 6; ++i) { now += 6000; mqttSendData(); }
    CHECK(diagnostics == 7 && online == 7 && locations == 0);
    CHECK(skipped < 7); // Missing fix must not cause an immediate retry loop.

    reset(); // Vehicle data must also survive missing GPS.
    obdConnected = true;
    mqttSendData();
    CHECK(diagnostics == 1 && obdReports == 1 && staticReports == 1 && dtcReports == 1);

    reset(); // Actual location publish failure must not gate other topics.
    gpsFixValid = true;
    publishLocation = false;
    mqttSendData();
    CHECK(locations == 1 && diagnostics == 1 && online == 1);
    CHECK(lastMQTTLocationOutput > now);
    now += 6000;
    mqttSendData();
    CHECK(locations == 1 && diagnostics == 2 && online == 2);

    reset(); // Fix appears, then disappears, then returns.
    mqttSendData();
    now += 11000; gpsFixValid = true; mqttSendData();
    CHECK(locations == 1 && diagnostics == 2 && online == 2);
    now += 11000; gpsFixValid = false; mqttSendData();
    CHECK(locations == 1 && diagnostics == 3 && online == 3);
    now += 11000; gpsFixValid = true; mqttSendData();
    CHECK(locations == 2 && diagnostics == 4 && online == 4);

    reset(); // Builds without a positioning source still report diagnostics.
    hasGps = false;
    mqttSendData();
    CHECK(diagnostics == 1 && online == 1 && locations == 0);

    reset(); // GSM location remains supported.
    hasGps = false; hasGsm = true;
    mqttSendData();
    CHECK(locations == 1 && diagnostics == 1 && online == 1);

    reset(); // Disconnected MQTT never attempts publications.
    network = false;
    mqttSendData();
    CHECK(diagnostics == 0 && locations == 0 && online == 0);
    printf("MQTT reporting regression: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
