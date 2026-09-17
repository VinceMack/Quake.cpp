// string_utils.hpp -- Pedagogical string manipulation, conversion, and case-insensitive helpers
#pragma once

#include <cstring>
#include <cstdarg>
#include <string_view>
#include <algorithm>

namespace Common {

inline void Q_memset(void* dest, int fill, int count) { std::memset(dest, fill, count); }
inline void Q_memcpy(void* dest, const void* src, int count) { std::memcpy(dest, src, count); }
inline void Q_strcpy(char* dest, const char* src) { std::strcpy(dest, src); }
inline void Q_strcpy(char* dest, std::string_view src) { std::memcpy(dest, src.data(), src.size()); dest[src.size()] = 0; }
inline void Q_strncpy(char* dest, const char* src, int count) { if (count <= 0) return; std::strncpy(dest, src, count - 1); dest[count - 1] = 0; }
inline void Q_strncpy(char* dest, std::string_view src, int count) {
    if (count <= 0) return;
    int len = static_cast<int>(std::min(src.size(), static_cast<size_t>(count - 1)));
    std::memcpy(dest, src.data(), len);
    dest[len] = 0;
}
inline int Q_strlen(const char* str) { return static_cast<int>(std::strlen(str)); }
inline const char* Q_strrchr(const char* s, char c) { return std::strrchr(s, c); }
inline char* Q_strrchr(char* s, char c) { return const_cast<char*>(Q_strrchr(static_cast<const char*>(s), c)); }
inline void Q_strcat(char* dest, const char* src) { std::strcat(dest, src); }
inline void Q_strcat(char* dest, std::string_view src) {
    dest += std::strlen(dest);
    std::memcpy(dest, src.data(), src.size());
    dest[src.size()] = 0;
}
inline int Q_strcmp(const char* s1, const char* s2) { return std::strcmp(s1, s2); }
inline int Q_strncmp(const char* s1, const char* s2, int count) { return std::strncmp(s1, s2, count); }
inline int Q_strcmp(std::string_view s1, std::string_view s2) { return s1.compare(s2); }

int Q_strncasecmp(const char* s1, const char* s2, int n);
int Q_strcasecmp(const char* s1, const char* s2);
int Q_strcasecmp(std::string_view s1, std::string_view s2);

int Q_atoi(std::string_view str);
float Q_atof(std::string_view str);

char* va(const char* format, ...);

} // namespace Common
