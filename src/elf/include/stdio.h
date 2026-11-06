#ifndef MINOS_ELF_STDIO_H
#define MINOS_ELF_STDIO_H
#include <stddef.h>
#include <stdarg.h>
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
typedef struct minos_file FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;
FILE *fopen(const char *restrict path, const char *restrict mode);
int fclose(FILE *stream);
size_t fread(void *restrict ptr, size_t size, size_t count, FILE *restrict stream);
size_t fwrite(const void *restrict ptr, size_t size, size_t count, FILE *restrict stream);
int fgetc(FILE *stream);
int getchar(void);
char *fgets(char *restrict s, int size, FILE *restrict stream);
int fputc(int c, FILE *stream);
int putchar(int c);
int fputs(const char *restrict s, FILE *restrict stream);
int puts(const char *s);
int fflush(FILE *stream);
int fseek(FILE *stream, long offset, int origin);
long ftell(FILE *stream);
void rewind(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
int printf(const char *restrict format, ...);
int fprintf(FILE *restrict stream, const char *restrict format, ...);
int vfprintf(FILE *restrict stream, const char *restrict format, va_list args);
void perror(const char *s);
#endif
