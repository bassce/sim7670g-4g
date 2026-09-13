#pragma once
#include <FS.h>
#include <ArduinoJson.h>

inline bool writeJsonAtomic(fs::FS& fs, const char* path, const JsonDocument& doc) {
    const String temporary = String(path) + ".tmp";
    File file = fs.open(temporary, FILE_WRITE);
    if (!file) return false;
    const size_t expected = measureJson(doc);
    const size_t written = serializeJson(doc, file);
    file.flush();
    const bool complete = written == expected && file.size() == expected;
    file.close();
    if (!complete || !fs.rename(temporary, path)) {
        fs.remove(temporary);
        return false;
    }
    return true;
}
