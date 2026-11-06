#ifndef MINOS_ELF_STRING_H
#define MINOS_ELF_STRING_H
#include <stddef.h>
void *memcpy(void *restrict dest, const void *restrict src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *dest, int value, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int value, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *restrict dest, const char *restrict src);
char *strncpy(char *restrict dest, const char *restrict src, size_t n);
char *strcat(char *restrict dest, const char *restrict src);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *s, const char *needle);
char *strerror(int errnum);
#endif
