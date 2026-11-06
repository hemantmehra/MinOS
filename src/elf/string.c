#include <errno.h>
#include <string.h>

int errno;

void *memcpy(void *restrict dest, const void *restrict src, size_t n)
{
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

void *memmove(void *dest, const void *src, size_t n)
{
    unsigned char *d = dest;
    const unsigned char *s = src;
    if ((unsigned long)d < (unsigned long)s)
        while (n--) *d++ = *s++;
    else
        while (n) { --n; d[n] = s[n]; }
    return dest;
}

void *memset(void *dest, int value, size_t n)
{
    unsigned char *d = dest;
    while (n--) *d++ = (unsigned char)value;
    return dest;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    while (n--) { if (*x != *y) return *x - *y; ++x; ++y; }
    return 0;
}

void *memchr(const void *s, int value, size_t n)
{
    const unsigned char *p = s;
    while (n--) { if (*p == (unsigned char)value) return (void *)p; ++p; }
    return 0;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) ++p;
    return (size_t)(p - s);
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) { --n; ++a; ++b; }
    return n ? (unsigned char)*a - (unsigned char)*b : 0;
}

char *strcpy(char *restrict dest, const char *restrict src)
{
    char *d = dest;
    while ((*d++ = *src++) != 0) {}
    return dest;
}

char *strncpy(char *restrict dest, const char *restrict src, size_t n)
{
    size_t i = 0;
    while (i < n && src[i]) { dest[i] = src[i]; ++i; }
    while (i < n) dest[i++] = 0;
    return dest;
}

char *strcat(char *restrict dest, const char *restrict src)
{
    strcpy(dest + strlen(dest), src);
    return dest;
}

char *strchr(const char *s, int c)
{
    unsigned char target = (unsigned char)c;
    do { if ((unsigned char)*s == target) return (char *)s; } while (*s++);
    return 0;
}

char *strrchr(const char *s, int c)
{
    const char *found = 0;
    unsigned char target = (unsigned char)c;
    do { if ((unsigned char)*s == target) found = s; } while (*s++);
    return (char *)found;
}

char *strstr(const char *s, const char *needle)
{
    size_t length = strlen(needle);
    if (!length) return (char *)s;
    while (*s) { if (!strncmp(s, needle, length)) return (char *)s; ++s; }
    return 0;
}

char *strerror(int errnum)
{
    switch (errnum)
    {
    case EDOM: return "Domain error";
    case ERANGE: return "Result out of range";
    case ENOMEM: return "Out of memory";
    case EINVAL: return "Invalid argument";
    case EIO: return "I/O error";
    case ENOENT: return "File not found";
    case EMFILE: return "Too many open files";
    case EBADF: return "Invalid stream";
    case EEXIST: return "File exists";
    case ENOSPC: return "No space left";
    default: return "Unknown error";
    }
}
