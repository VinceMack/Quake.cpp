// crt_compat.hpp -- Platform C-runtime compatibility shims
#pragma once

#if !defined(_WIN32)
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

typedef int errno_t;

#ifndef _TRUNCATE
#define _TRUNCATE ((size_t)-1)
#endif

#ifndef STRUNCATE
#define STRUNCATE 80
#endif

inline errno_t strcpy_s(char* dest, size_t destsz, const char* src)
{
    if (!dest || destsz == 0) return EINVAL;
    if (!src) {
        dest[0] = '\0';
        return EINVAL;
    }
    size_t src_len = strlen(src);
    if (src_len >= destsz) {
        dest[0] = '\0';
        return ERANGE;
    }
    memcpy(dest, src, src_len + 1);
    return 0;
}

inline errno_t strncpy_s(char* dest, size_t destsz, const char* src, size_t count)
{
    if (!dest || destsz == 0) return EINVAL;
    if (!src) {
        dest[0] = '\0';
        return EINVAL;
    }
    if (count == _TRUNCATE) {
        size_t src_len = strlen(src);
        if (src_len >= destsz) {
            memcpy(dest, src, destsz - 1);
            dest[destsz - 1] = '\0';
            return STRUNCATE;
        }
        memcpy(dest, src, src_len + 1);
        return 0;
    } else {
        size_t src_len = strlen(src);
        size_t copy_len = (src_len < count) ? src_len : count;
        if (copy_len >= destsz) {
            dest[0] = '\0';
            return ERANGE;
        }
        memcpy(dest, src, copy_len);
        dest[copy_len] = '\0';
        return 0;
    }
}

inline errno_t strcat_s(char* dest, size_t destsz, const char* src)
{
    if (!dest || destsz == 0) return EINVAL;
    if (!src) return EINVAL;
    size_t dest_len = strlen(dest);
    if (dest_len >= destsz) {
        dest[0] = '\0';
        return EINVAL;
    }
    size_t src_len = strlen(src);
    if (dest_len + src_len >= destsz) {
        dest[0] = '\0';
        return ERANGE;
    }
    memcpy(dest + dest_len, src, src_len + 1);
    return 0;
}

inline int sprintf_s(char* dest, size_t destsz, const char* format, ...)
{
    if (!dest || destsz == 0) return -1;
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(dest, destsz, format, args);
    va_end(args);
    if (ret < 0 || (size_t)ret >= destsz) {
        dest[0] = '\0';
        return -1;
    }
    return ret;
}

inline int vsprintf_s(char* dest, size_t destsz, const char* format, va_list args)
{
    if (!dest || destsz == 0) return -1;
    int ret = vsnprintf(dest, destsz, format, args);
    if (ret < 0 || (size_t)ret >= destsz) {
        dest[0] = '\0';
        return -1;
    }
    return ret;
}

inline errno_t fopen_s(FILE** pFile, const char* filename, const char* mode)
{
    if (!pFile || !filename || !mode) return EINVAL;
    *pFile = fopen(filename, mode);
    if (*pFile == nullptr) {
        return errno;
    }
    return 0;
}

inline errno_t strerror_s(char* buf, size_t bufsz, int errnum)
{
    if (!buf || bufsz == 0) return EINVAL;
    const char* msg = strerror(errnum);
    if (!msg) {
        buf[0] = '\0';
        return EINVAL;
    }
    size_t msg_len = strlen(msg);
    if (msg_len >= bufsz) {
        memcpy(buf, msg, bufsz - 1);
        buf[bufsz - 1] = '\0';
        return ERANGE;
    }
    memcpy(buf, msg, msg_len + 1);
    return 0;
}

inline int vfscanf_s_compat(FILE* stream, const char* format, va_list ap)
{
    void* args[16] = { nullptr };
    int arg_count = 0;

    const char* p = format;
    while (*p) {
        if (*p == '%') {
            p++;
            if (*p == '%') {
                p++;
                continue;
            }
            if (*p == '*') {
                p++;
            } else {
                while (*p && strchr("0123456789+-hljztL", *p)) {
                    p++;
                }
                char type = *p;
                if (type == '[') {
                    p++;
                    if (*p == '^') p++;
                    if (*p == ']') p++;
                    while (*p && *p != ']') {
                        p++;
                    }
                    type = '[';
                }
                if (*p) p++;

                void* ptr = va_arg(ap, void*);
                if (arg_count < 16) {
                    args[arg_count++] = ptr;
                }
                if (type == 's' || type == 'c' || type == '[') {
                    (void)va_arg(ap, unsigned int);
                }
                continue;
            }
        }
        p++;
    }

    switch (arg_count) {
    case 0:
        return fscanf(stream, format);
    case 1:
        return fscanf(stream, format, args[0]);
    case 2:
        return fscanf(stream, format, args[0], args[1]);
    case 3:
        return fscanf(stream, format, args[0], args[1], args[2]);
    case 4:
        return fscanf(stream, format, args[0], args[1], args[2], args[3]);
    case 5:
        return fscanf(stream, format, args[0], args[1], args[2], args[3], args[4]);
    case 6:
        return fscanf(stream, format, args[0], args[1], args[2], args[3], args[4], args[5]);
    case 7:
        return fscanf(stream, format, args[0], args[1], args[2], args[3], args[4], args[5], args[6]);
    case 8:
        return fscanf(stream, format, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
    default:
        return -1;
    }
}

inline int vsscanf_s_compat(const char* buffer, const char* format, va_list ap)
{
    void* args[16] = { nullptr };
    int arg_count = 0;

    const char* p = format;
    while (*p) {
        if (*p == '%') {
            p++;
            if (*p == '%') {
                p++;
                continue;
            }
            if (*p == '*') {
                p++;
            } else {
                while (*p && strchr("0123456789+-hljztL", *p)) {
                    p++;
                }
                char type = *p;
                if (type == '[') {
                    p++;
                    if (*p == '^') p++;
                    if (*p == ']') p++;
                    while (*p && *p != ']') {
                        p++;
                    }
                    type = '[';
                }
                if (*p) p++;

                void* ptr = va_arg(ap, void*);
                if (arg_count < 16) {
                    args[arg_count++] = ptr;
                }
                if (type == 's' || type == 'c' || type == '[') {
                    (void)va_arg(ap, unsigned int);
                }
                continue;
            }
        }
        p++;
    }

    switch (arg_count) {
    case 0:
        return sscanf(buffer, format);
    case 1:
        return sscanf(buffer, format, args[0]);
    case 2:
        return sscanf(buffer, format, args[0], args[1]);
    case 3:
        return sscanf(buffer, format, args[0], args[1], args[2]);
    case 4:
        return sscanf(buffer, format, args[0], args[1], args[2], args[3]);
    case 5:
        return sscanf(buffer, format, args[0], args[1], args[2], args[3], args[4]);
    case 6:
        return sscanf(buffer, format, args[0], args[1], args[2], args[3], args[4], args[5]);
    case 7:
        return sscanf(buffer, format, args[0], args[1], args[2], args[3], args[4], args[5], args[6]);
    case 8:
        return sscanf(buffer, format, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
    default:
        return -1;
    }
}

inline int fscanf_s(FILE* stream, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    int ret = vfscanf_s_compat(stream, format, ap);
    va_end(ap);
    return ret;
}

inline int sscanf_s(const char* buffer, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    int ret = vsscanf_s_compat(buffer, format, ap);
    va_end(ap);
    return ret;
}

constexpr int _SH_DENYNO = 0;
constexpr int _S_IREAD = 0400;
constexpr int _S_IWRITE = 0200;

inline int _sopen_s(int* pfd, const char* filename, int oflag, int shflag, int pmode)
{
    (void)shflag;
    if (!pfd) return EINVAL;
    *pfd = open(filename, oflag, pmode);
    if (*pfd == -1) {
        return errno;
    }
    return 0;
}
#define _write write
#define _close close
#define _unlink unlink
#endif
