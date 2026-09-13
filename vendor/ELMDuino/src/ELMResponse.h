#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Headerless, auto-formatted ELM responses. Preserve message boundaries: a
// different ECU's reply must never become extra bytes of the selected value.
namespace ELMResponse {
enum class Error { none, malformed, overflow, missing, negative, pending, ambiguous };
struct Result {
    Error error = Error::none;
    uint8_t nrc = 0;
    uint64_t value = 0;
    size_t bytes = 0;
    std::string message;
};
inline int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
inline bool hexOnly(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if (hex(c) < 0) return false;
    return true;
}
inline Error messages(const char* input, size_t capacity, std::vector<std::string>& out) {
    if (!input || !capacity || strnlen(input, capacity) == capacity) return Error::overflow;
    size_t remaining = 0;
    unsigned sequence = 0;
    std::string assembled;
    const char* cursor = input;
    while (*cursor) {
        std::string line;
        while (*cursor && *cursor != '\r' && *cursor != '\n') {
            char c = *cursor++;
            if (c == ' ' || c == '\t') continue;
            if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
            line += c;
        }
        while (*cursor == '\r' || *cursor == '\n') ++cursor;
        if (line.empty()) continue;
        if (line == "SEARCHING..." || line == "BUSINIT...OK") continue;
        const auto colon = line.find(':');
        if (remaining) {
            // A different ECU may send a complete single-frame reply between
            // fragments. Keep it separate; never append it to this message.
            if (colon == std::string::npos && line.size() >= 4 &&
                line.size() <= 14 && !(line.size() % 2) && hexOnly(line)) {
                out.push_back(line);
                continue;
            }
            if (colon != 1 || hex(line[0]) != int(sequence & 15)) return Error::malformed;
            auto data = line.substr(2);
            if (!hexOnly(data) || data.size() % 2 || data.size() > 14) return Error::malformed;
            const size_t take = data.size() < remaining ? data.size() : remaining;
            assembled.append(data, 0, take);
            remaining -= take;
            ++sequence;
            if (!remaining) { out.push_back(assembled); assembled.clear(); }
        } else if (colon != std::string::npos) {
            return Error::malformed; // Missing total length or duplicate/out-of-order fragment.
        } else if (line.size() == 3 && hexOnly(line)) {
            size_t bytes = (hex(line[0]) << 8) | (hex(line[1]) << 4) | hex(line[2]);
            if (!bytes || bytes * 2 >= capacity) return Error::overflow;
            remaining = bytes * 2;
            sequence = 0;
        } else {
            if (!hexOnly(line) || line.size() % 2) return Error::malformed;
            out.push_back(line);
        }
    }
    return remaining ? Error::malformed : Error::none;
}
// Slice integer bytes before converting to floating point; this is essential
// for a small signed field inside a larger response (e.g. 019A bytes E/F).
inline bool fieldValue(uint64_t raw, uint8_t total, uint8_t offset, uint8_t length,
                       bool isSigned, double& value) {
    if (!total || total > 8 || !length || length > 8 || unsigned(offset) + length > total) return false;
    const unsigned bits = unsigned(length) * 8;
    const uint64_t mask = length == 8 ? UINT64_MAX : (uint64_t(1) << bits) - 1;
    const uint64_t field = (raw >> (unsigned(total - offset - length) * 8)) & mask;
    value = isSigned && (field & (uint64_t(1) << (bits - 1))) ?
        -double(((~field) & mask) + 1) : double(field);
    return true;
}
inline Result numeric(const char* input, size_t capacity, const char* expected, uint8_t service) {
    Result result;
    std::vector<std::string> decoded;
    result.error = messages(input, capacity, decoded);
    if (result.error != Error::none) return result;
    const std::string prefix(expected);
    bool found = false;
    bool denied = false;
    for (const auto& message : decoded) {
        if (message.compare(0, prefix.size(), prefix) == 0) {
            auto data = message.substr(prefix.size());
            if (data.empty() || data.size() % 2) { result.error = Error::malformed; return result; }
            if (data.size() > 16) { result.error = Error::overflow; return result; }
            if (found && message != result.message) { result.error = Error::ambiguous; return result; }
            result.value = 0;
            for (char c : data) result.value = (result.value << 4) | unsigned(hex(c));
            result.bytes = data.size() / 2;
            result.message = message;
            found = true;
        } else if (message.size() == 6 && message.compare(0, 2, "7F") == 0 &&
                   ((hex(message[2]) << 4) | hex(message[3])) == service) {
            const uint8_t nrc = (hex(message[4]) << 4) | hex(message[5]);
            // Keep a final rejection in preference to an earlier pending reply.
            if (!denied || result.nrc == 0x78) result.nrc = nrc;
            denied = true;
        }
    }
    result.error = found ? Error::none : denied ? (result.nrc == 0x78 ? Error::pending : Error::negative) : Error::missing;
    return result;
}
}
