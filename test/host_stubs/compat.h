#pragma once
#include <string.h>
#include <time.h>
#ifdef _WIN32
static inline struct tm *localtime_r(const time_t *time, struct tm *out)
{
    struct tm *value = localtime(time);
    if (!value) return NULL;
    *out = *value;
    return out;
}
static inline struct tm *gmtime_r(const time_t *time, struct tm *out)
{
    struct tm *value = gmtime(time);
    if (!value) return NULL;
    *out = *value;
    return out;
}
#endif
static inline size_t strlcpy(char *dest, const char *src, size_t size)
{
    size_t length = strlen(src);
    if (size) {
        size_t copied = length < size - 1 ? length : size - 1;
        memcpy(dest, src, copied);
        dest[copied] = 0;
    }
    return length;
}
