#ifndef MINOS_ELF_STDLIB_H
#define MINOS_ELF_STDLIB_H
#include <stddef.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 2147483647
void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
int atoi(const char *s);
long strtol(const char *restrict s, char **restrict end, int base);
unsigned long strtoul(const char *restrict s, char **restrict end, int base);
int abs(int x);
char *getenv(const char *name);
extern char **environ;
void exit(int status) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));
#endif
