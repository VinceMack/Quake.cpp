// string_utils.cpp -- String utilities implementation
#include "core/string_utils.hpp"
#include "platform/crt_compat.hpp"
#include <cctype>
#include <cstdio>

namespace Common {

int Q_strncasecmp(const char* s1, const char* s2, int n) {
    while (n-- > 0) {
        char c1 = *s1++, c2 = *s2++;
        if (c1 != c2) {
            char lc1 = static_cast<char>(std::tolower(static_cast<unsigned char>(c1)));
            char lc2 = static_cast<char>(std::tolower(static_cast<unsigned char>(c2)));
            if (lc1 != lc2) return (lc1 < lc2) ? -1 : 1;
        }
        if (c1 == '\0') break;
    }
    return 0;
}

int Q_strcasecmp(const char* s1, const char* s2) {
    if (!s1 || !s2) return s1 == s2 ? 0 : (s1 ? 1 : -1);
    while (*s1 && *s2) {
        char c1 = static_cast<char>(std::tolower(static_cast<unsigned char>(*s1++)));
        char c2 = static_cast<char>(std::tolower(static_cast<unsigned char>(*s2++)));
        if (c1 != c2) return (c1 < c2) ? -1 : 1;
    }
    return static_cast<int>(static_cast<unsigned char>(*s1)) - static_cast<int>(static_cast<unsigned char>(*s2));
}

int Q_strcasecmp(std::string_view s1, std::string_view s2) {
    size_t min_len = std::min(s1.size(), s2.size());
    for (size_t i = 0; i < min_len; ++i) {
        char c1 = static_cast<char>(std::tolower(static_cast<unsigned char>(s1[i])));
        char c2 = static_cast<char>(std::tolower(static_cast<unsigned char>(s2[i])));
        if (c1 < c2) return -1;
        if (c1 > c2) return 1;
    }
    if (s1.size() < s2.size()) return -1;
    if (s1.size() > s2.size()) return 1;
    return 0;
}

int Q_atoi(std::string_view str) {
    if (str.empty()) return 0;
    size_t pos = 0; int sign = 1;
    if (str[pos] == '-') { sign = -1; pos++; }
    if (pos >= str.size()) return 0;
    if (pos + 1 < str.size() && str[pos] == '0' && (str[pos + 1] == 'x' || str[pos + 1] == 'X')) {
        pos += 2; int val = 0;
        while (pos < str.size()) {
            char c = str[pos++];
            if (c >= '0' && c <= '9') val = (val << 4) + (c - '0');
            else if (c >= 'a' && c <= 'f') val = (val << 4) + (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') val = (val << 4) + (c - 'A' + 10);
            else break;
        }
        return val * sign;
    }
    if (str[pos] == '\'' && pos + 1 < str.size()) return sign * static_cast<unsigned char>(str[pos + 1]);
    int val = 0;
    while (pos < str.size() && std::isdigit(static_cast<unsigned char>(str[pos]))) {
        val = val * 10 + (str[pos++] - '0');
    }
    return val * sign;
}

float Q_atof(std::string_view str) {
    if (str.empty()) return 0.0f;
    size_t pos = 0; int sign = 1;
    if (str[pos] == '-') { sign = -1; pos++; }
    if (pos >= str.size()) return 0.0f;
    if (pos + 1 < str.size() && str[pos] == '0' && (str[pos + 1] == 'x' || str[pos + 1] == 'X')) {
        pos += 2; double val = 0.0;
        while (pos < str.size()) {
            char c = str[pos++];
            if (c >= '0' && c <= '9') val = val * 16 + (c - '0');
            else if (c >= 'a' && c <= 'f') val = val * 16 + (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') val = val * 16 + (c - 'A' + 10);
            else break;
        }
        return static_cast<float>(val * sign);
    }
    if (str[pos] == '\'' && pos + 1 < str.size()) return static_cast<float>(sign * static_cast<unsigned char>(str[pos + 1]));
    double val = 0.0; int decimal = -1, total = 0;
    while (pos < str.size()) {
        char c = str[pos++];
        if (c == '.') { decimal = total; continue; }
        if (!std::isdigit(static_cast<unsigned char>(c))) break;
        val = val * 10 + (c - '0'); total++;
    }
    if (decimal != -1) {
        while (total > decimal) { val /= 10.0; total--; }
    }
    return static_cast<float>(val * sign);
}

char* va(const char* format, ...) {
    va_list argptr; static char string[1024];
    va_start(argptr, format); vsprintf_s(string, sizeof(string), format, argptr); va_end(argptr);
    return string;
}

} // namespace Common
