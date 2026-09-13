"""Compile actual vendored methods against fault-injecting host BLE mocks."""
from pathlib import Path
import re, subprocess, sys
root=Path(__file__).resolve().parents[2]
out=root.parent/'outputs'/'ble-regression'
out.mkdir(parents=True,exist_ok=True)
ble=(root/'vendor/BLESerial/src/BLESerial.cpp').read_text()
elm=(root/'vendor/ELMDuino/src/ELMduino.cpp').read_text()
def function(source,signature):
    start=re.search(r'(?m)^'+re.escape(signature),source).start()
    opening=source.index('{',start); depth=1; i=opening+1
    while depth:
        if source[i]=='{': depth+=1
        elif source[i]=='}': depth-=1
        i+=1
    return source[start:i]+'\n'
code=(Path(__file__).with_name('ble_regression_stub.h')).read_text()
code+=ble[ble.index('class ClientCallbacks final'):ble.index('// Constructor')]
for signature in ['bool BLESerial::connect(const NimBLEAddress', 'bool BLESerial::connected()',
                  'bool BLESerial::disconnect()', 'void BLESerial::flush()',
                  'size_t BLESerial::write(uint8_t', 'size_t BLESerial::write(const uint8_t',
                  'void BLESerial::notifyCallback(']:
    code+=function(ble,signature)
code+='\n#define realloc checkedRealloc\n#define free checkedFree\n'
code+=function(elm,'bool ELM327::begin(')+function(elm,'ELM327::~ELM327()')
code+='\n#undef realloc\n#undef free\n'
app=(root/'src/ble_connection.cpp').read_text()
code+=r'''
namespace app_test {
struct Lock {};
struct Device { std::string name,mac; uint8_t type; };
struct { unsigned getFreeHeap() { return 100000; } } ESP;
struct Config {
    bool getCheckPIDSupport() { return false; }
    bool getDebug() { return false; }
    bool getSpecifyNumResponses() { return false; }
};
struct { Config OBD2; } Settings;
struct FakeOBD {
    std::atomic<const char*> connectionError{"none"},connectionPhase{"disabled"};
    std::atomic_int bleDisconnectReason{0};
    bool result=false;
    const char* nextError="link_connect_failed";
    const char* nextPhase="bluetooth_error";
    void begin(const char*,const char*,char,bool,bool,bool) {}
    bool connectBLE(const char*,const char*,uint8_t) {
        connectionError=nextError; connectionPhase=nextPhase; return result;
    }
} OBD;
uint32_t attemptId=0;
std::string error,activeName,activeMac;
char activeProtocol='0';
'''
code+=function(app,'std::string failureMessage()')+function(app,'bool connectDevice(')
code+=r'''
void test() {
    Device device{"OBD","00:11:22:33:44:55",0};
    assert(!connectDevice(device,'0'));
    assert(error=="Bluetooth connection failed: link_connect_failed" && attemptId==1);
    OBD.nextError="initialization_failed"; OBD.nextPhase="elm_error";
    assert(!connectDevice(device,'0'));
    assert(error=="ELM327 initialization failed: initialization_failed" && attemptId==2);
    OBD.result=true; OBD.nextError="none"; OBD.nextPhase="connected";
    assert(connectDevice(device,'0') && error.empty() && attemptId==3);
    OBD.connectionError="link_disconnected"; OBD.connectionPhase="disconnected";
    assert(failureMessage()=="Bluetooth connection failed: link_disconnected");
}
}
'''
code+=(root/'lib/BLETrace/src/TraceFormat.h').read_text().replace('#pragma once','')
code+=r'''
int main() {
    app_test::test();
    const uint8_t sample[]={0,13,255,65}; char hex[9],ascii[5];
    assert(BLETrace::formatBytes(sample,4,hex,sizeof(hex),ascii,sizeof(ascii))==4);
    assert(std::string(hex)=="000dff41" && std::string(ascii)=="...A");
    char shortHex[5],shortAscii[3];
    assert(BLETrace::formatBytes(sample,4,shortHex,sizeof(shortHex),shortAscii,sizeof(shortAscii))==2);
    assert(std::string(shortHex)=="000d" && std::string(shortAscii)=="..");
    char emptyHex[1]={'x'},emptyAscii[1]={'x'};
    assert(BLETrace::formatBytes(sample,4,emptyHex,1,emptyAscii,1)==0 && !emptyHex[0] && !emptyAscii[0]);
    BLESerial serial; NimBLEAddress address;
    auto fail = [&](const char* code) {
        assert(!serial.connect(address));
        assert(std::string(serial.lastError.load())==code);
        assert(serial.pClient==nullptr);
        assert(serial.pRxCharacteristic==nullptr && serial.pTxCharacteristic==nullptr);
        assert(serial.disconnect());
        scenario=Scenario{};
    };
    scenario.allocation=false; fail("client_allocation_failed");
    for(int i=0;i<100;i++) { scenario.link=false; fail("link_connect_failed"); }
    // NimBLE 2.3.7 can return EALREADY from MTU exchange after GAP is connected.
    scenario.connectResult=false; scenario.linkError=BLE_HS_EALREADY;
    int connectedCallbacks=0; serial.pConnectCb=[&] { ++connectedCallbacks; };
    assert(serial.connect(address));
    assert(serial.connected() && serial.lastDisconnectReason==0 && connectedCallbacks==1);
    serial.disconnect(); serial.pConnectCb={}; scenario=Scenario{};
    // Neither a disconnected EALREADY nor a different connected error is success.
    scenario.link=false; scenario.linkError=BLE_HS_EALREADY; fail("link_connect_failed");
    scenario.connectResult=false; scenario.linkError=13; fail("link_connect_failed");
    scenario.connectResult=false; scenario.linkError=BLE_HS_EALREADY;
    scenario.receiver.subscribeOK=false; fail("subscribe_failed");
    scenario.service=false; fail("service_not_found");
    scenario.service=false; scenario.vlinkService=true;
    assert(serial.connect(address));
    assert(scenario.selectedRx=="2AF0" && scenario.selectedTx=="2AF1");
    serial.disconnect(); scenario=Scenario{};
    scenario.vlinkService=true;
    assert(serial.connect(address));
    assert(scenario.selectedRx=="FFF1" && scenario.selectedTx=="FFF2" && scenario.vlinkProbes==0);
    serial.disconnect(); scenario=Scenario{};
    scenario.service=false; scenario.vlinkService=true; scenario.tx=false;
    fail("tx_characteristic_not_found");
    serial.serviceUUID=NimBLEUUID("1234"); scenario.vlinkService=true;
    assert(!serial.connect(address) && scenario.vlinkProbes==0);
    serial.serviceUUID=NimBLEUUID("FFF0"); scenario=Scenario{};
    scenario.rx=false; fail("rx_characteristic_not_found");
    scenario.tx=false; fail("tx_characteristic_not_found");
    scenario.transmitter.writable=false; scenario.transmitter.noResponse=false; fail("tx_not_writable");
    scenario.receiver.notify=false; fail("rx_not_subscribable");
    scenario.receiver.subscribeOK=false; fail("subscribe_failed");
    scenario.receiver.notify=false; scenario.receiver.indicate=true;
    scenario.transmitter.noResponse=false;
    assert(serial.connect(address)); assert(!scenario.receiver.notifications);
    assert(serial.write(uint8_t('A'))==1 && scenario.transmitter.responseUsed);
    serial.disconnect(); scenario=Scenario{};
    assert(serial.connect(address)); assert(scenario.receiver.notifications);
    auto old=serial.pClient;
    assert(serial.disconnect()); assert(serial.write(uint8_t('A'))==0);
    assert(serial.connect(address));
    old->callbacks->onDisconnect(old,99); // Late close callback from previous generation.
    assert(serial.lastDisconnectReason==0);
    int callbacks=0; serial.pDisconnectCb=[&] { ++callbacks; };
    serial.pClient->callbacks->onDisconnect(serial.pClient,19);
    assert(serial.lastDisconnectReason==19 && callbacks==1);
    uint8_t data[]={'O','K'};
    serial.notifyCallback(serial.pRxCharacteristic,data,2,true);
    serial.notifyCallback(serial.pRxCharacteristic,data,2,true);
    assert(serial.buffer=="OKOK"); // Repeated bytes are not discarded.
    scenario.deletion=false;
    assert(!serial.disconnect()); assert(serial.pClient!=nullptr);
    assert(!serial.pClient->autoDelete);
    scenario.deletion=true; assert(serial.disconnect());
    assert(serial.buffer.empty());
    for(auto c:NimBLEDevice::clients) { assert(!c->alive); delete c->callbacks; delete c; }
    Stream stream;
    {
        ELM327 elm;
        for(int i=0;i<1000;i++) {
            elm.initResult=(i%2)==0;
            assert(elm.begin(stream,false,2000,'0',128,0)==elm.initResult);
            assert(allocations.size()==1 && elm.PAYLOAD_LEN==128);
        }
        auto previous=elm.payload;
        allocationFailure=true;
        assert(!elm.begin(stream,false,2000,'0',256,0));
        assert(elm.payload==previous && elm.PAYLOAD_LEN==128 && !elm.connected);
        assert(std::string(elm.lastInitError)=="out_of_memory");
        allocationFailure=false;
        elm.initResult=true;
        assert(elm.begin(stream,false,2000,'0',256,0));
        assert(allocations.size()==1 && elm.PAYLOAD_LEN==256);
    }
    assert(allocations.empty());
    puts("PASS: BLE failure cleanup, subscription, delayed callbacks, RX/write and 1000 ELM initializations");
}
'''
cpp=out/'ble_regression.cpp'; cpp.write_text(code)
import ziglang
zig=Path(ziglang.__file__).parent/'zig.exe'
exe=out/'ble_regression.exe'
subprocess.run([str(zig),'c++','-std=c++17','-target','x86_64-windows-gnu','-O0','-g',str(cpp),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
