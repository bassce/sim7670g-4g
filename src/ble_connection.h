#pragma once
#if defined(WS_SIM7670G_V2)
#include <Arduino.h>
#include <string>
namespace BLEConnection {
void init();
void tick(bool allowVehicleReads);
bool isBusy();
std::string statusJSON();
// Requests enqueue work; HTTP/WiFi callbacks never operate the Bluetooth stack.
bool scan();
bool connect(const String &mac, unsigned addressType, char protocol);
}
#endif
