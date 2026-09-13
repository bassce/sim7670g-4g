// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace sim7670g {
enum class CoordinateFormat { DecimalDegrees, DegreesMinutes, Auto };

inline bool number(const char* text, double& result) {
    if (!text || !*text) return false;
    char* end = nullptr;
    result = strtod(text, &end);
    return end != text && *end == '\0' && isfinite(result) && result >= 0;
}

inline bool coordinate(double raw, double limit, bool minutes, double& result) {
    result = raw;
    if (minutes) {
        double degrees = floor(raw / 100.0);
        double fraction = raw - degrees * 100.0;
        if (fraction >= 60.0) return false;
        result = degrees + fraction / 60.0;
    }
    return result <= limit;
}

// Parse the SIM767XX CGNSSINFO layout including four satellite-count fields.
// Outputs are assigned only for a complete, valid 2D/3D fix.
inline bool parseGnss(const char* response, CoordinateFormat format, float& latitude, float& longitude) {
    if (!response || strlen(response) >= 256) return false;
    char buffer[256];
    strcpy(buffer, response);
    char* fields[20] = {buffer};
    size_t count = 1;
    for (char* p = buffer; *p; ++p) {
        if (*p == ',') {
            *p = '\0';
            if (count == 20) return false;
            fields[count++] = p + 1;
        }
    }
    if (count < 17 || (strcmp(fields[0], "2") && strcmp(fields[0], "3"))) return false;
    if ((strcmp(fields[6], "N") && strcmp(fields[6], "S")) ||
        (strcmp(fields[8], "E") && strcmp(fields[8], "W"))) return false;
    double lat, lon;
    if (!number(fields[5], lat) || !number(fields[7], lon)) return false;
    bool minutes = format == CoordinateFormat::DegreesMinutes ||
                   (format == CoordinateFormat::Auto && (lat > 90 || lon > 180));
    if (!coordinate(lat, 90, minutes, lat) || !coordinate(lon, 180, minutes, lon)) return false;
    latitude = static_cast<float>(lat * (fields[6][0] == 'S' ? -1 : 1));
    longitude = static_cast<float>(lon * (fields[8][0] == 'W' ? -1 : 1));
    return true;
}
} // namespace sim7670g
