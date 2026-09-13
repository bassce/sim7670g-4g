#pragma once
#include <ArduinoJson.h>
#include <ELMQueryConfig.h>

namespace QueryConfigJson {
inline bool integer(JsonVariantConst value, uint32_t max) {
    return value.isNull() || (value.is<uint32_t>() && value.as<uint32_t>() <= max);
}
inline bool valid(JsonObjectConst pid) {
    if (pid.isNull()) return true;
    if (!integer(pid["protocol"], 9) || !integer(pid["receiveHeader"], 0x1FFFFFFF) ||
        !integer(pid["flowControlHeader"], 0x1FFFFFFF) || !integer(pid["flowControlData"], 0xFFFFFF) ||
        !integer(pid["dataOffset"], 7) || !integer(pid["dataLength"], 8) ||
        (!pid["signedValue"].isNull() && !pid["signedValue"].is<bool>())) return false;
    ELMQueryConfig config;
    config.protocol = pid["protocol"] | uint8_t(0);
    config.receiveHeader = pid["receiveHeader"] | uint32_t(0);
    config.flowControlHeader = pid["flowControlHeader"] | uint32_t(0);
    config.flowControlData = pid["flowControlData"] | uint32_t(0x300000);
    if (!config.valid()) return false;
    if ((config.protocol == 6 || config.protocol == 8) &&
        (config.receiveHeader > 0x7FF || config.flowControlHeader > 0x7FF ||
         pid["header"].as<uint32_t>() > 0x7FF)) return false;
    const unsigned offset = pid["dataOffset"] | 0U;
    const unsigned length = pid["dataLength"] | 0U;
    const bool isSigned = pid["signedValue"] | false;
    if (!length) return !offset && !isSigned;
    return integer(pid["numExpectedBytes"], 8) &&
        length + offset <= pid["numExpectedBytes"].as<unsigned>();
}
inline bool validStates(JsonVariantConst states) {
    if (!states.is<JsonArrayConst>()) return false;
    for (JsonObjectConst state : states.as<JsonArrayConst>()) {
        if (state.isNull() || !valid(state["pid"].as<JsonObjectConst>())) return false;
    }
    return true;
}
}
