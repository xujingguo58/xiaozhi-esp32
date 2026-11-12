#ifndef LIVIS_FORMAT_H
#define LIVIS_FORMAT_H

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace livis {
inline std::string FormatTemperature(double value) {
    if (!std::isfinite(value) || value < -100 || value > 100) return "--°";
    char result[24];
    std::snprintf(result, sizeof(result), "%.0f°", value);
    return result;
}

inline std::string FormatTemperatureRange(const std::string& maximum, const std::string& minimum) {
    return minimum + " ~ " + maximum;
}

inline bool ParseNumber(const std::string& text, double& out) {
    if (text.empty()) return false;
    char* end = nullptr;
    out = std::strtod(text.c_str(), &end);
    if (end == text.c_str()) return false;
    if (end != text.c_str() && *end == '%') ++end;
    return end != text.c_str() && *end == '\0' && std::isfinite(out);
}

inline std::string FormatPrice(const std::string& text) {
    double value = 0;
    if (!ParseNumber(text, value) || value <= 0 || value > 1000000) return "--";
    char result[32];
    std::snprintf(result, sizeof(result), "%.2f", value);
    return result;
}

inline std::string FormatChange(const std::string& text) {
    double value = 0;
    if (!ParseNumber(text, value) || std::fabs(value) > 10000) return "--";
    if (value == 0) value = 0;  // Avoid a misleading negative zero.
    char result[32];
    std::snprintf(result, sizeof(result), "%+.2f%%", value);
    return result;
}
}  // namespace livis

#endif  // LIVIS_FORMAT_H
