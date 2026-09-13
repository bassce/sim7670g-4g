#include "ELMResponse.h"
#include "ELMQueryConfig.h"
#include <cmath>
#include <limits>
#include <type_traits>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <numeric>
#include <string>
#include <vector>
#define F(x) x
#define BT_TRACE(...) ((void)0)
uint32_t nowMs=1000;
uint32_t millis() { return nowMs; }
struct Console { template<class T> void print(T) {} template<class T> void println(T) {} } Serial;
constexpr int MALLOC_CAP_SPIRAM=0, PID_INTERVAL_OFFSET=32;
constexpr int ELM_SUCCESS=0, ELM_NO_DATA=5, ELM_GETTING_MSG=8, ELM_GENERAL_ERROR=-1, ELM_BUFFER_OVERFLOW=-2;
constexpr int SEND_COMMAND=0, WAITING_RESP=1;
constexpr const char* SET_HEADER="AT SH %s";
constexpr const char* SET_ALL_TO_DEFAULTS="AT D";
constexpr const char* RESPONSE_OK="OK";
bool failAllocation=false;
void* heap_caps_realloc(void* old,size_t size,int) { return failAllocation ? nullptr : std::realloc(old,size); }
size_t strlcpy(char* dst,const char* src,size_t n) { auto len=std::strlen(src); if(n){auto size=std::min(len,n-1);std::memcpy(dst,src,size);dst[size]=0;} return len; }
namespace obd { enum OBDStateType {READ,CALC}; enum OBDResponseFormat {PREDEFINED}; }
struct ELM327 {
    struct Step { int state; double value; };
    std::deque<Step> steps;
    std::vector<unsigned> queried;
    void* elm_port=this;
    int nb_rx_state=ELM_SUCCESS, nb_query_state=SEND_COMMAND;
    bool headerOK=true,restoreOK=true;
    uint8_t lastNRC=0;
    ELMResponse::Error responseError=ELMResponse::Error::none;
    bool prepareHeader(uint32_t) { return headerOK; }
    bool prepareQuery(uint32_t, const ELMQueryConfig&) { return headerOK; }
    bool restoreHeader() { return restoreOK; }
    bool longQuery=false,isMode0x22Query=false,debugMode=false;
    char query[32]="010C",storage[512]={}; char* payload=storage;
    uint8_t numPayChars=0;
    uint64_t response=0;
    uint8_t responseByte_0=0,responseByte_1=0,responseByte_2=0,responseByte_3=0,responseByte_4=0,responseByte_5=0,responseByte_6=0,responseByte_7=0;
    uint8_t response_A=0,response_B=0,response_C=0,response_D=0,response_E=0,response_F=0,response_G=0,response_H=0;
    double processPID(uint8_t service,uint16_t pid,uint8_t,uint8_t,double=1,float=0,uint8_t=0,uint8_t=0,bool=false) {
        queried.push_back((unsigned(service)<<16)|pid);assert(!steps.empty());auto s=steps.front();steps.pop_front();nb_rx_state=s.state;return s.value;
    }
    int sendCommand_Blocking(const char*) { std::strcpy(storage,"OK");return ELM_SUCCESS; }
    int8_t nextIndex(const char* text,const char* needle,int occurrence) {
        const char* found=text; for(int i=0;i<occurrence;i++){found=std::strstr(found,needle);if(!found)return -1;if(i+1<occurrence)++found;}
        return static_cast<int8_t>(found-text);
    }
    int ctoi(char c) { return c<='9' ? c-'0' : c-'A'+10; }
    uint64_t findResponse();
};
struct RealELM : ELM327 {
    int sent=0;
    void queryPID(uint8_t,uint16_t,uint8_t) {++sent;nb_rx_state=ELM_GETTING_MSG;}
    void get_response() {}
    static double (*selectCalculator(uint16_t))() { return nullptr; }
    double conditionResponse(uint8_t,double scale,float bias) {return response*scale+bias;}
    double conditionResponse(double(*calculator)()) {return calculator();}
    double processPID(const uint8_t&,const uint16_t&,const uint8_t&,const uint8_t&,const double&,const float&);
};
struct OBDState {
    ELM327* elm327=nullptr;
    uint8_t service=1,numResponses=1,numExpectedBytes=1;
    uint16_t pid=1;
    uint32_t header=0;
    ELMQueryConfig queryConfig;
    uint8_t dataOffset=0,dataLength=0;
    bool signedValue=false;
    bool init=false,checkPidSupport=false,setHeader=false,supported=true,processing=false,enabled=true;
    long lastUpdate=0,previousUpdate=0,updateInterval=100;
    uint32_t lastAttempt=0;
    int updateStatus=0;
    double scaleFactor=1;float bias=0;
    obd::OBDStateType type=obd::READ;
    obd::OBDResponseFormat responseFormat=obd::PREDEFINED;
    const char* name="test";
    char* payload=nullptr;
    virtual ~OBDState();
    virtual void readValue() {}
    void calcValue(const std::function<double(const char*)>&,const std::map<const char*,std::function<double(double)>>&) {}
    uint32_t supportedPIDs(const uint8_t&,const uint16_t&) const;
    bool isPIDSupported(const uint8_t&,const uint16_t&) const;
    void setCheckPidSupport(bool);
    void setPayload(const char*);
    bool isProcessing() const {return processing;}
    bool isEnabled() const {return enabled;}
    bool isInit() const {return init;}
    bool isSupported() const {return supported;}
    bool hasCalcExpression() const {return false;}
    long getLastUpdate() const {return lastUpdate;}
    uint32_t getLastAttempt() const {return lastAttempt;}
    long getUpdateInterval() const {return updateInterval;}
    obd::OBDStateType getType() const {return type;}
    const char* getName() const {return name;}
    void setELM327(ELM327* elm) {elm327=elm;}
    double conditionResponse(double value,obd::OBDResponseFormat,double scale,float offset) {return value*scale+offset;}
};
template<class T> struct TypedOBDState : OBDState {
    char valueFormatFunctionName[33] = {};
    std::atomic<T> value{T{}},oldValue{T{}};
    T getValue() const {return value.load();}
    std::function<T()> readFunction;
    std::function<void(TypedOBDState*)> postProcessFunction;
    void readValue() override;
};
struct OBDStates {
    ELM327* elm327=nullptr;
    bool checkPidSupport=false;
    std::vector<OBDState*> states;
    std::function<double(const char*)> varResolveFunction;
    std::map<const char*,std::function<double(double)>> customFunctions;
    void getStates(const std::function<bool(OBDState*)>& pred,std::vector<OBDState*>& out) {for(auto* s:states)if(pred(s))out.push_back(s);}
    template<class T> T* getStateByName(const char* name){for(auto* s:states)if(std::strcmp(name,s->name)==0)return static_cast<T*>(s);return nullptr;}
    static bool compareStates(const OBDState*,const OBDState*);
    void clearStates();
    void addState(OBDState*);
    OBDState* nextState();
    double avgLastUpdate(const std::function<bool(OBDState*)>&);
};
