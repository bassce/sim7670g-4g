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

#include "BLESerial.h"
#include <BLETrace.h>

static BLEScanResultsSet scanResults;

static void printFriendlyResponse(const uint8_t *pData, size_t length) {
    log_d("");
    for (int i = 0; i < length; i++) {
        char recChar = static_cast<char>(pData[i]);
        if (recChar == '\f')
            log_d("\\f");
        else if (recChar == '\n')
            log_d("\\n");
        else if (recChar == '\r')
            log_d("\\r");
        else if (recChar == '\t')
            log_d("\\t");
        else if (recChar == '\v')
            log_d("\\v");
        else if (recChar == ' ')
            // convert spaces to underscore, easier to see in debug output
            log_d("_");
        else
            // display regular printable
            log_d("%c", recChar);
    }
    log_d("");
}

class AdvertisedDeviceCallbacks final : public NimBLEScanCallbacks {
    NimBLEUUID serviceUUID;
    BLEAdvertisedDeviceCb callback = nullptr;

public:
    AdvertisedDeviceCallbacks(const NimBLEUUID &serviceFilter) {
        serviceUUID = serviceFilter;
    }

    explicit AdvertisedDeviceCallbacks(const NimBLEUUID &serviceFilter, const BLEAdvertisedDeviceCb &cb) {
        serviceUUID = serviceFilter;
        callback = cb;
    }

    void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override {
        // filter for required service
        if (advertisedDevice != nullptr && advertisedDevice->isAdvertisingService(serviceUUID) &&
            scanResults.add(*advertisedDevice) && callback) {
            callback(advertisedDevice);
        }
    }
};

class ClientCallbacks final : public NimBLEClientCallbacks {
    BLESerial& owner;
public:
    explicit ClientCallbacks(BLESerial& serial) : owner(serial) {}
    void onConnect(NimBLEClient* client) override {
        BT_TRACE("GAP_CONNECTED", "client=%p current=%u mtu=%u", static_cast<void*>(client), client==owner.pClient ? 1U : 0U, client->getMTU());
        if (client == owner.pClient && owner.pConnectCb) owner.pConnectCb();
    }
    void onDisconnect(NimBLEClient* client, int reason) override {
        BT_TRACE("GAP_DISCONNECTED", "client=%p current=%u reason=%d hex=0x%x text=%s",
            static_cast<void*>(client), client==owner.pClient ? 1U : 0U, reason, reason, NimBLEUtils::returnCodeToString(reason));
        // A detached, deliberately closed client cannot clobber a newer attempt.
        if (client != owner.pClient) return;
        owner.lastDisconnectReason = reason;
        owner.lastError = "link_disconnected";
        Serial.printf("[BLE] disconnect reason=%d local=0\n", reason);
        if (owner.pDisconnectCb) owner.pDisconnectCb();
    }
#if defined(BLE_DIAGNOSTICS)
    void onConnectFail(NimBLEClient* client, int reason) override {
        BT_TRACE("GAP_CONNECT_FAIL", "client=%p current=%u reason=%d hex=0x%x text=%s",
            static_cast<void*>(client), client==owner.pClient ? 1U : 0U, reason, reason, NimBLEUtils::returnCodeToString(reason));
    }
    void onMTUChange(NimBLEClient* client, uint16_t mtu) override {
        BT_TRACE("MTU", "client=%p mtu=%u", static_cast<void*>(client), mtu);
    }
    bool onConnParamsUpdateRequest(NimBLEClient* client, const ble_gap_upd_params* params) override {
        const bool accepted = NimBLEClientCallbacks::onConnParamsUpdateRequest(client, params);
        BT_TRACE("CONN_PARAMS", "min_interval=%u max_interval=%u latency=%u timeout=%u accepted=%u",
            params->itvl_min, params->itvl_max, params->latency, params->supervision_timeout, accepted ? 1U : 0U);
        return accepted;
    }
    void onAuthenticationComplete(NimBLEConnInfo& info) override {
        BT_TRACE("AUTH", "peer=%s encrypted=%u authenticated=%u bonded=%u", info.getAddress().toString().c_str(),
            info.isEncrypted() ? 1U : 0U, info.isAuthenticated() ? 1U : 0U, info.isBonded() ? 1U : 0U);
        NimBLEClientCallbacks::onAuthenticationComplete(info);
    }
#endif
};

// Constructor
BLESerial::BLESerial() = default;

// Destructor
BLESerial::~BLESerial() = default;

BLESerial::operator bool() const {
    return init;
}

void BLESerial::notifyCallback(NimBLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData,
                               size_t length,
                               bool isNotify) {
    if (pBLERemoteCharacteristic != pRxCharacteristic) return;
#if defined(BLE_DIAGNOSTICS)
    BLETrace::received(length);
    BT_TRACE("RX_EVENT", "handle=%u notify=%u bytes=%u buffered=%u", pBLERemoteCharacteristic->getHandle(),
        isNotify ? 1U : 0U, static_cast<unsigned>(length), static_cast<unsigned>(available()));
    BT_BYTES("RX", pData, length);
#endif
    if (pDataCb) {
        pDataCb(pBLERemoteCharacteristic, pData, length);
    }

    log_d("ELM RESPONSE > ");
    printFriendlyResponse(pData, length);

    std::lock_guard<std::mutex> lock(bufferMutex);
    buffer.append(reinterpret_cast<char*>(pData), length);

}

// Begin bluetooth serial
bool BLESerial::begin(const String &localName, const std::string &serviceUUID, const std::string &rxUUID,
                      const std::string &txUUID) {
    local_name = localName;
    this->serviceUUID = NimBLEUUID(serviceUUID);
    this->rxUUID = NimBLEUUID(rxUUID);
    this->txUUID = NimBLEUUID(txUUID);
    NimBLEDevice::init(local_name.c_str());

    NimBLEDevice::setPower(ESP_PWR_LVL_P9); /** +9db */
    NimBLEDevice::setSecurityAuth(false, false, true);

    init = true;
    return init;
}

int BLESerial::available() {
    std::lock_guard<std::mutex> lock(bufferMutex);
    // reply with data available
    return static_cast<int>(buffer.length());
}

int BLESerial::peek() {
    std::lock_guard<std::mutex> lock(bufferMutex);
    // return first character available
    // but don't remove it from the buffer
    if ((!buffer.empty())) {
        uint8_t c = buffer[0];
        return c;
    }

    return -1;
}

bool BLESerial::connect(const String &remoteName) {
    NimBLEAdvertisedDevice *device = nullptr;
    BLEScanResultsSet *bleDeviceList = discover();

    if (bleDeviceList != nullptr && bleDeviceList->getCount() > 0) {
        for (int i = 0; i < bleDeviceList->getCount(); i++) {
            NimBLEAdvertisedDevice *dev = bleDeviceList->getDevice(i);
            if (strcmp(dev->getName().c_str(), remoteName.c_str()) == 0) {
                device = dev;
            }
        }
    } else {
        return false;
    }

    if (device == nullptr) {
        return false;
    }

    disconnect();

    return connect(device->getAddress());
}

bool BLESerial::connect(const NimBLEAddress &remoteAddress) {
    const uint32_t started = millis();
    if (pClient && !disconnect()) return false;
    flush();
    lastError = "none";
    lastDisconnectReason = 0;
    auto fail = [this, started](const char* error) {
        BT_TRACE("CONNECT_RESULT", "ok=0 stage=%s elapsed_ms=%lu rc=%d reason=%d", error,
            static_cast<unsigned long>(millis()-started), pClient ? pClient->getLastError() : 0, lastDisconnectReason.load());
        Serial.printf("[BLE] failed stage=%s\n", error);
        disconnect();
        lastError = error;
        return false;
    };
    pClient = NimBLEDevice::createClient();
    if (!pClient) return fail("client_allocation_failed");
    pClient->setSelfDelete(false, false);
    pClient->setClientCallbacks(new ClientCallbacks(*this), true);
    pClient->setConnectionParams(12, 12, 0, 150);
    pClient->setConnectTimeout(5000);
    BT_TRACE("LINK_START", "peer=%s type=%u timeout_ms=5000 interval=12 latency=0 supervision=150 client=%p",
        remoteAddress.toString().c_str(), remoteAddress.getType(), static_cast<void*>(pClient));
    Serial.printf("[BLE] connect peer=%s type=%u\n", remoteAddress.toString().c_str(), remoteAddress.getType());
    if (!pClient->connect(remoteAddress)) {
        const int error = pClient->getLastError();
        const bool linkEstablished = pClient->isConnected();
        BT_TRACE("CONNECT_CHECK", "returned=0 connected=%u rc=%d text=%s mtu=%u", linkEstablished ? 1U : 0U,
            error, NimBLEUtils::returnCodeToString(error), pClient->getMTU());
        // NimBLE 2.3.7 propagates a duplicate MTU request's EALREADY through
        // synchronous connect(), even after GAP has established the link.
        // Preserve only that specific live connection; still validate GATT below.
        if (error != BLE_HS_EALREADY || !linkEstablished) {
            lastDisconnectReason = error;
            Serial.printf("[BLE] connect failed rc=%d\n", error);
            return fail("link_connect_failed");
        }
        BT_TRACE("MTU_ALREADY", "keeping_established_link=1 mtu=%u", pClient->getMTU());
        Serial.println("[BLE] MTU exchange already started; preserving established link");
        // NimBLE did not deliver its normal onConnect callback on this path.
        if (pConnectCb) pConnectCb();
        if (!connected()) return fail("disconnected_during_setup");
    }
    Serial.printf("[BLE] link ready RSSI=%d service=%s rx=%s tx=%s\n", pClient->getRssi(),
                  serviceUUID.toString().c_str(), rxUUID.toString().c_str(), txUUID.toString().c_str());
#if defined(BLE_DIAGNOSTICS)
    BT_TRACE("LINK_READY", "elapsed_ms=%lu mtu=%u rssi=%d", static_cast<unsigned long>(millis()-started), pClient->getMTU(), pClient->getRssi());
    if (BLETrace::enabled()) {
    // Diagnostic build enumerates UUIDs/properties only; it does not read/write values.
    const auto& services = pClient->getServices(true);
    BT_TRACE("GATT_DISCOVERY", "services=%u connected=%u last_rc=%d", static_cast<unsigned>(services.size()),
        pClient->isConnected() ? 1U : 0U, pClient->getLastError());
    unsigned serviceCount = 0;
    for (auto* item : services) {
        if (++serviceCount > 32) { BT_TRACE("GATT_LIMIT", "service_limit=32"); break; }
        BT_TRACE("GATT_SERVICE", "uuid=%s", item->getUUID().toString().c_str());
        unsigned charCount = 0;
        for (auto* characteristic : item->getCharacteristics(true)) {
            if (++charCount > 64) { BT_TRACE("GATT_LIMIT", "characteristic_limit=64"); break; }
            BT_TRACE("GATT_CHAR", "service=%s uuid=%s handle=%u read=%u write=%u write_nr=%u notify=%u indicate=%u",
                item->getUUID().toString().c_str(), characteristic->getUUID().toString().c_str(), characteristic->getHandle(),
                characteristic->canRead() ? 1U : 0U, characteristic->canWrite() ? 1U : 0U,
                characteristic->canWriteNoResponse() ? 1U : 0U, characteristic->canNotify() ? 1U : 0U, characteristic->canIndicate() ? 1U : 0U);
        }
    }
    }
#endif
    NimBLEUUID selectedService = serviceUUID;
    NimBLEUUID selectedRx = rxUUID;
    NimBLEUUID selectedTx = txUUID;
    auto service = pClient->getService(selectedService);
    // IOS-Vlink exposes its ELM UART on 18F0 / 2AF0 / 2AF1. Fall back only
    // from the stock profile; preserve explicit caller-supplied UUIDs.
    if (!service && serviceUUID == NimBLEUUID("FFF0") && rxUUID == NimBLEUUID("FFF1") && txUUID == NimBLEUUID("FFF2")) {
        selectedService = NimBLEUUID("18F0");
        service = pClient->getService(selectedService);
        if (service) {
            selectedRx = NimBLEUUID("2AF0");
            selectedTx = NimBLEUUID("2AF1");
        }
    }
    if (!service) return fail("service_not_found");
    BT_TRACE("UART_PROFILE", "service=%s rx=%s tx=%s", selectedService.toString().c_str(),
        selectedRx.toString().c_str(), selectedTx.toString().c_str());
    pRxCharacteristic = service->getCharacteristic(selectedRx);
    pTxCharacteristic = service->getCharacteristic(selectedTx);
    if (!pRxCharacteristic) return fail("rx_characteristic_not_found");
    if (!pTxCharacteristic) return fail("tx_characteristic_not_found");
    if (!pTxCharacteristic->canWrite() && !pTxCharacteristic->canWriteNoResponse())
        return fail("tx_not_writable");
    writeWithResponse = !pTxCharacteristic->canWriteNoResponse();
    const bool notify = pRxCharacteristic->canNotify();
    BT_TRACE("SUBSCRIBE_START", "notify=%u indicate=%u write_response=%u", notify ? 1U : 0U,
        pRxCharacteristic->canIndicate() ? 1U : 0U, writeWithResponse ? 1U : 0U);
    if (!notify && !pRxCharacteristic->canIndicate()) return fail("rx_not_subscribable");
    if (!pRxCharacteristic->subscribe(notify,
        [this](NimBLERemoteCharacteristic* characteristic, uint8_t* data, size_t size, bool isNotify) {
            notifyCallback(characteristic, data, size, isNotify);
        })) return fail("subscribe_failed");
    if (!connected()) return fail("disconnected_during_setup");
    Serial.printf("[BLE] transport ready subscription=%s\n", notify ? "notify" : "indicate");
    BT_TRACE("CONNECT_RESULT", "ok=1 subscribed=1 elapsed_ms=%lu", static_cast<unsigned long>(millis()-started));
    return true;
}

bool BLESerial::connected() const {
    return pClient != nullptr && pClient->isConnected();
}

bool BLESerial::disconnect() {
    auto client = pClient;
    BT_TRACE("LOCAL_CLOSE", "client=%p connected=%u", static_cast<void*>(client), client && client->isConnected() ? 1U : 0U);
    pClient = nullptr;
    pRxCharacteristic = nullptr;
    pTxCharacteristic = nullptr;
    flush();
    if (!client) return true;
    Serial.println("[BLE] closing client local=1");
    // NimBLE owns the callback while it completes asynchronous disconnect/delete.
    if (!NimBLEDevice::deleteClient(client)) {
        // deleteClient sets these flags before requesting a disconnect. If the
        // request failed, do not let a later event delete our retained pointer.
        client->setSelfDelete(false, false);
        pClient = client;  // Deletion rejected: retain ownership for a later cleanup.
        lastError = "client_cleanup_failed";
        BT_TRACE("CLEANUP_FAIL", "client=%p rc=%d", static_cast<void*>(client), client->getLastError());
        return false;
    }
    return true;
}

bool BLESerial::isClosed() const {
    return pClient == nullptr || !pClient->isConnected();
}

int BLESerial::read(void) {
    std::lock_guard<std::mutex> lock(bufferMutex);
    // read a character
    if (!buffer.empty()) {
        uint8_t c = buffer[0];
        buffer.erase(0, 1); // remove it from the buffer
        return c;
    }
    return -1;
}

size_t BLESerial::write(uint8_t c) {
    if (!connected() || !pTxCharacteristic) return 0;
    const bool ok = pTxCharacteristic->writeValue(c, writeWithResponse);
    if (!ok) BT_TRACE("WRITE_FAIL", "byte=0x%02x rc=%d", c, pClient->getLastError());
#if defined(BLE_DIAGNOSTICS)
    if (ok) BLETrace::transmitted(1);
#endif
    return ok ? 1 : 0;
}

size_t BLESerial::write(const uint8_t *data, size_t size) {
    if (!connected() || !pTxCharacteristic) return 0;
    BT_BYTES("TX_REQUEST", data, size);
    size_t sent = 0;
    while (sent < size && write(data[sent]) == 1) ++sent;
    BT_TRACE("TX_RESULT", "requested=%u sent=%u", static_cast<unsigned>(size), static_cast<unsigned>(sent));
    return sent;
}

void BLESerial::flush() {
    std::lock_guard<std::mutex> lock(bufferMutex);
    buffer.clear();
}

void BLESerial::end() {
    disconnect();
    init = false;
}

void BLESerial::onData(const BLESerialDataCb &cb) {
    pDataCb = cb;
}

void BLESerial::onConnect(const BLEConnectCb &cb) {
    pConnectCb = cb;
}

void BLESerial::onDisconnect(const BLEDisconnectCb &cb) {
    pDisconnectCb = cb;
}

BLEScanResultsSet *BLESerial::discover(int timeout) {
    BLEScanResultsSet *bleDeviceList = getScanResults();

    if (discoverAsync([](const NimBLEAdvertisedDevice *pDevice) {
        log_d("found %s - %s %d\n", pDevice->getAddress().toString().c_str(), pDevice->getName().c_str(),
              pDevice->getRSSI());
    }, timeout)) {
        delay(timeout);
        discoverAsyncStop();

        if (bleDeviceList->getCount() > 0) {
            return bleDeviceList;
        }
    }

    return nullptr;
}

bool BLESerial::discoverAsync(const BLEAdvertisedDeviceCb &cb, int timeout) {
    disconnect();
    discoverClear();

    NimBLEScan *pBLEScan = NimBLEDevice::getScan();

    if (pBLEScan != nullptr) {
        pBLEScan->stop();

        pBLEScan->setScanCallbacks(new AdvertisedDeviceCallbacks(serviceUUID, cb));
        pBLEScan->setInterval(INQ_TIME);
        pBLEScan->setWindow(INQ_TIME);
        pBLEScan->setMaxResults(0);
        pBLEScan->setActiveScan(true);
        return pBLEScan->start(timeout, false);
    }

    return false;
}

void BLESerial::discoverAsyncStop() {
    if (NimBLEDevice::getScan() != nullptr) {
        NimBLEDevice::getScan()->stop();
        NimBLEDevice::getScan()->setActiveScan(false);
    }
}

void BLESerial::discoverClear() {
    scanResults.clear();
}

BLEScanResultsSet *BLESerial::getScanResults() {
    return &scanResults;
}
