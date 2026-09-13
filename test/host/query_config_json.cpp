#include "QueryConfigJson.h"
#include <cassert>
#include <iostream>
int main() {
    JsonDocument doc;
    const char* config = R"([{"pid":{"header":402391163,"protocol":7,"receiveHeader":402522235,"flowControlHeader":402391163,"flowControlData":3145728,"numExpectedBytes":6,"dataOffset":4,"dataLength":2,"signedValue":true}}])";
    assert(!deserializeJson(doc,config));
    assert(QueryConfigJson::validStates(doc.as<JsonVariantConst>()));
    const auto check = [&](const char* key, unsigned value) {
        JsonDocument bad;deserializeJson(bad,config);bad[0]["pid"][key]=value;
        assert(!QueryConfigJson::validStates(bad.as<JsonVariantConst>()));
    };
    check("protocol",6);check("protocol",263);check("receiveHeader",0x20000000);
    check("flowControlData",0x310000);check("flowControlData",0x300080);
    check("dataLength",0);check("dataOffset",5);check("dataOffset",256);
    doc[0]["pid"]["dataOffset"]=-1;assert(!QueryConfigJson::validStates(doc.as<JsonVariantConst>()));
    deserializeJson(doc,config);doc[0]["pid"]["signedValue"]="false";
    assert(!QueryConfigJson::validStates(doc.as<JsonVariantConst>()));
    deserializeJson(doc,R"([{"pid":{"service":34,"pid":652,"header":402391163,"numExpectedBytes":1}}])");
    assert(QueryConfigJson::validStates(doc.as<JsonVariantConst>()));
    std::cout << "PASS: production JSON configuration validation, old defaults, signed field ranges and CAN constraints\n";
}
