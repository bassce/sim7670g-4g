// SPDX-License-Identifier: GPL-3.0-or-later
#include "sim7670g_gnss.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

using sim7670g::CoordinateFormat;
static int checks = 0;
static const char* fix(const char* lat, const char* ns, const char* lon, const char* ew) {
    static char text[512];
    snprintf(text, sizeof(text), "3,09,05,00,00,%s,%s,%s,%s,131117,091918.00,32.9,0.0,255.0,1.1,0.8,0.7,14", lat, ns, lon, ew);
    return text;
}
static void valid(const char* text, CoordinateFormat fmt, float expectedLat, float expectedLon) {
    float lat = 888, lon = 999;
    assert(sim7670g::parseGnss(text, fmt, lat, lon));
    assert(fabs(lat - expectedLat) < 0.00002f);
    assert(fabs(lon - expectedLon) < 0.00002f);
    ++checks;
}
static void invalid(const char* text, CoordinateFormat fmt = CoordinateFormat::Auto) {
    float lat = 888, lon = 999;
    assert(!sim7670g::parseGnss(text, fmt, lat, lon));
    assert(lat == 888 && lon == 999); // rejection must not destroy last known fix
    ++checks;
}
int main() {
    valid(fix("31.2221775", "N", "121.3543759", "E"), CoordinateFormat::DecimalDegrees, 31.2221775f, 121.3543759f);
    valid(fix("31.2221775", "S", "121.3543759", "W"), CoordinateFormat::Auto, -31.2221775f, -121.3543759f);
    // Exact coordinates from the AT manual's degrees/minutes example.
    valid(fix("3113.330650", "N", "12121.262554", "E"), CoordinateFormat::Auto, 31.2221775f, 121.3543759f);
    valid(fix("3113.330650", "S", "12121.262554", "W"), CoordinateFormat::DegreesMinutes, -31.2221775f, -121.3543759f);
    valid(fix("90", "N", "180", "E"), CoordinateFormat::DecimalDegrees, 90, 180);
    valid(fix("9000", "N", "18000", "E"), CoordinateFormat::DegreesMinutes, 90, 180);
    valid(fix("0", "N", "0", "E"), CoordinateFormat::Auto, 0, 0); // valid fix at origin is allowed
    // Near the equator, auto cannot infer ddmm: explicit selection is required.
    valid(fix("30", "N", "45", "E"), CoordinateFormat::DegreesMinutes, 0.5f, 0.75f);
    valid(fix("30", "N", "45", "E"), CoordinateFormat::Auto, 30, 45);
    invalid("");
    invalid(",,,,,,,,,,,,,,,,,");
    invalid("1,09,05,00,00,31,N,121,E,131117,091918,0,0,0,1,1,1,14");
    invalid("3,09,05,00,00,31,N,121,E");
    invalid(fix("", "N", "121", "E"));
    invalid(fix("nan", "N", "121", "E"));
    invalid(fix("inf", "N", "121", "E"));
    invalid(fix("-31", "N", "121", "E"));
    invalid(fix("31junk", "N", "121", "E"));
    invalid(fix("31", "X", "121", "E"));
    invalid(fix("31", "N", "121", "EE"));
    invalid(fix("91", "N", "121", "E"), CoordinateFormat::DecimalDegrees);
    invalid(fix("3160", "N", "12100", "E"));
    invalid(fix("9000.1", "N", "12100", "E"));
    invalid(fix("3100", "N", "18000.1", "E"));
    char longInput[301];
    memset(longInput, '0', 300);
    longInput[300] = 0;
    invalid(longInput);
    printf("GNSS parser: %d cases passed\n", checks);
}
