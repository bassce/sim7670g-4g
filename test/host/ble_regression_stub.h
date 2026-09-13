#include <cassert>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <set>
using String = std::string;
using byte = unsigned char;
inline unsigned long millis() { return 1000; }
namespace BLETrace { inline void setAttempt(uint32_t) {} }
#define BT_TRACE(...) ((void)0)
#define BT_BYTES(...) ((void)0)
struct Console { template<class... T> void printf(const char*, T...) {} void println(const char*) {} } Serial;
struct Stream {};
struct NimBLEAddress { std::string toString() const { return "00:11:22:33:44:55"; } unsigned getType() const { return 0; } };
struct NimBLEUUID {
    std::string value;
    NimBLEUUID(const char* v):value(v) {}
    std::string toString() const { return value; }
    bool operator==(const NimBLEUUID& other) const { return value==other.value; }
};
struct NimBLERemoteCharacteristic {
    bool writable=true, noResponse=true, notify=true, indicate=false, subscribeOK=true;
    bool notifications=false, responseUsed=false;
    int written=0;
    bool canWrite() { return writable; }
    bool canWriteNoResponse() { return noResponse; }
    bool canNotify() { return notify; }
    bool canIndicate() { return indicate; }
    template<class F> bool subscribe(bool n, F) { notifications=n; return subscribeOK; }
    bool writeValue(uint8_t, bool response) { responseUsed=response; ++written; return true; }
};
struct Scenario {
    bool allocation=true, link=true, service=true, rx=true, tx=true, deletion=true;
    bool connectResult=true;
    int linkError=13;
    bool vlinkService=false;
    int vlinkProbes=0;
    std::string selectedRx, selectedTx;
    NimBLERemoteCharacteristic receiver, transmitter;
} scenario;
struct NimBLERemoteService {
    NimBLERemoteCharacteristic* getCharacteristic(const NimBLEUUID& uuid) {
        if (uuid.value=="FFF1" || uuid.value=="2AF0") {
            scenario.selectedRx=uuid.value; return scenario.rx ? &scenario.receiver : nullptr;
        }
        scenario.selectedTx=uuid.value; return scenario.tx ? &scenario.transmitter : nullptr;
    }
};
struct NimBLEClient;
struct NimBLEClientCallbacks {
    virtual ~NimBLEClientCallbacks() = default;
    virtual void onConnect(NimBLEClient*) {}
    virtual void onDisconnect(NimBLEClient*, int) {}
};
struct NimBLEClient {
    bool alive=true, link=false, autoDelete=false;
    NimBLEClientCallbacks* callbacks=nullptr;
    NimBLERemoteService service;
    void setSelfDelete(bool a,bool b) { autoDelete=a||b; }
    void setClientCallbacks(NimBLEClientCallbacks* c,bool) { callbacks=c; }
    void setConnectionParams(int,int,int,int) {}
    void setConnectTimeout(int) {}
    bool connect(const NimBLEAddress&) { link=scenario.link; return link && scenario.connectResult; }
    bool isConnected() const { assert(alive && "use after free"); return link; }
    int getLastError() const { return scenario.linkError; }
    int getRssi() const { return -50; }
    uint16_t getMTU() const { return 23; }
    NimBLERemoteService* getService(const NimBLEUUID& uuid) {
        if (uuid.value=="FFF0") return scenario.service ? &service : nullptr;
        if (uuid.value=="18F0") { ++scenario.vlinkProbes; return scenario.vlinkService ? &service : nullptr; }
        return nullptr;
    }
};
struct NimBLEDevice {
    static std::vector<NimBLEClient*> clients;
    static NimBLEClient* createClient() { if(!scenario.allocation) return nullptr; auto c=new NimBLEClient; clients.push_back(c); return c; }
    static bool deleteClient(NimBLEClient* c) {
        assert(c->alive && "double delete");
        c->autoDelete=true;
        if (!scenario.deletion) return false;
        c->alive=false; c->link=false; // Keep tombstone to detect later dereferences.
        return true;
    }
};
std::vector<NimBLEClient*> NimBLEDevice::clients;
class BLESerial {
public:
    NimBLEClient* pClient=nullptr;
    NimBLERemoteCharacteristic *pRxCharacteristic=nullptr, *pTxCharacteristic=nullptr;
    NimBLEUUID serviceUUID{"FFF0"}, rxUUID{"FFF1"}, txUUID{"FFF2"};
    std::atomic<const char*> lastError{"none"};
    std::atomic_int lastDisconnectReason{0};
    std::function<void()> pConnectCb, pDisconnectCb;
    std::function<void(NimBLERemoteCharacteristic*,uint8_t*,size_t)> pDataCb;
    std::mutex bufferMutex;
    std::string buffer;
    bool writeWithResponse=false;
    bool connect(const NimBLEAddress&);
    bool connected() const;
    bool disconnect();
    void flush();
    size_t write(uint8_t);
    size_t write(const uint8_t*,size_t);
    void notifyCallback(NimBLERemoteCharacteristic*,uint8_t*,size_t,bool);
};
void printFriendlyResponse(const uint8_t*,size_t) {}
#define log_d(...) ((void)0)
constexpr int BLE_HS_EALREADY=2;
constexpr int8_t ELM_GENERAL_ERROR=-1;
constexpr int ELM_SUCCESS=0, SEND_COMMAND=0;
class ELM327 {
public:
    Stream* elm_port=nullptr;
    bool debugMode=false, connected=false, initializing=false, initResult=false;
    uint16_t PAYLOAD_LEN=0,timeout_ms=0;
    char* payload=nullptr;
    const char* lastInitError="none";
    char lastInitCommand[20]={};
    int8_t lastInitState=ELM_GENERAL_ERROR;
    int nb_query_state=0, nb_rx_state=0;
    bool numericResponse=false,recoveryRequired=false,pendingResponse=false,headerDirty=false;
    char activeProtocol='0';byte configuredDataTimeout=0;
    bool begin(Stream&, const bool&,const uint16_t&,const char&,const uint16_t&,const byte&);
    bool initializeELM(char,byte) { return connected=initResult; }
    ~ELM327();
};
std::set<void*> allocations;
bool allocationFailure=false;
void* checkedRealloc(void* old,size_t n) {
    if (allocationFailure) return nullptr;
    if (old) assert(allocations.erase(old)==1);
    auto p=std::realloc(old,n);
    assert(p); allocations.insert(p); return p;
}
void checkedFree(void* p) { assert(allocations.erase(p)==1); std::free(p); }
