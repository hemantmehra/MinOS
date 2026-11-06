#include <errno.h>
#include <stdio.h>
#include <string.h>

struct minos_file
{
    int handle;
    unsigned char active, readable, writable, eof, error, standard, append;
};

static FILE streams[16] = {
    {0, 1, 1, 0, 0, 0, 1, 0},
    {1, 1, 0, 1, 0, 0, 1, 0},
    {2, 1, 0, 1, 0, 0, 1, 0}
};
FILE *stdin = &streams[0];
FILE *stdout = &streams[1];
FILE *stderr = &streams[2];

static int call1(int number, int arg)
{
    int result;
    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(number), "b"(arg)
                      : "memory", "cc");
    return result;
}

static int call3(int number, int handle, unsigned int argument, int count)
{
    int result;
    __asm__ volatile ("int $0x80" : "=a"(result)
        : "a"(number), "b"(handle), "c"(argument), "d"(count) : "memory", "cc");
    return result;
}

FILE *fopen(const char *restrict path, const char *restrict mode)
{
    int array[128];
    int handle, i, update = 0, binary = 0;
    FILE *stream = 0;
    char kind;

    if (!path || !mode || !(kind = *mode++) ||
        (kind != 'r' && kind != 'w' && kind != 'a'))
    { errno = EINVAL; return 0; }
    while (*mode)
    {
        if (*mode == '+' && !update) update = 1;
        else if (*mode == 'b' && !binary) binary = 1;
        else { errno = EINVAL; return 0; }
        ++mode;
    }
    for (i = 0; i < 127 && path[i]; ++i) array[i] = (unsigned char)path[i];
    if (path[i]) { errno = EINVAL; return 0; }
    array[i] = 0;
    for (i = 3; i < 16; ++i) if (!streams[i].active) { stream = &streams[i]; break; }
    if (!stream) { errno = EMFILE; return 0; }
    handle = call1(kind == 'r' ? 18 : 42, (int)array);
    if (handle < 0) { errno = EIO; return 0; }
    if ((kind != 'r' || update) && call3(22, handle, 0, 0) < 0)
    { call1(20, handle); errno = EIO; return 0; }
    if (kind == 'w' && call1(23, handle) < 0)
    { call1(20, handle); errno = EIO; return 0; }
    stream->handle = handle;
    stream->active = 1;
    stream->readable = kind == 'r' || update;
    stream->writable = kind != 'r' || update;
    stream->eof = stream->error = 0;
    stream->standard = 0;
    stream->append = kind == 'a';
    return stream;
}

int fclose(FILE *stream)
{
    int result;
    if (!stream || !stream->active) { errno = EBADF; return EOF; }
    result = stream->standard ? 0 : call1(20, stream->handle);
    if (result < 0) { stream->error = 1; errno = EIO; return EOF; }
    stream->active = 0;
    return 0;
}

size_t fread(void *restrict ptr, size_t size, size_t count, FILE *restrict stream)
{
    unsigned char *out = ptr;
    size_t total, done = 0;
    int buffer[256];
    if (!size || !count) return 0;
    if (count > (size_t)-1 / size) { errno = EINVAL; return 0; }
    if (!stream || !stream->active || !stream->readable)
    { if (stream) stream->error = 1; errno = EBADF; return 0; }
    if (stream->eof) return 0;
    total = size * count;
    while (done < total)
    {
        int n, i;
        int chunk = total - done > 256 ? 256 : (int)(total - done);
        n = call3(stream->standard ? 24 : 19, stream->handle, (unsigned int)buffer, chunk);
        if (stream->standard && n == 0) { call1(35, 1); continue; }
        if (stream->standard && n < 0 && call1(37, 0))
        { stream->eof = 1; break; }
        if (n < 0) { stream->error = 1; errno = EIO; break; }
        if (!n) { stream->eof = 1; break; }
        for (i = 0; i < n; ++i) out[done++] = (unsigned char)buffer[i];
    }
    return done / size;
}

size_t fwrite(const void *restrict ptr, size_t size, size_t count, FILE *restrict stream)
{
    const unsigned char *in = ptr;
    size_t total, done = 0;
    int buffer[256];
    if (!size || !count) return 0;
    if (count > (size_t)-1 / size) { errno = EINVAL; return 0; }
    if (!stream || !stream->active || !stream->writable)
    { if (stream) stream->error = 1; errno = EBADF; return 0; }
    total = size * count;
    while (done < total)
    {
        int i, n, chunk = total - done > 256 ? 256 : (int)(total - done);
        if (stream->append && call3(38, stream->handle, 0, SEEK_END) < 0)
        { stream->error = 1; errno = EIO; break; }
        for (i = 0; i < chunk; ++i) buffer[i] = in[done + i];
        n = call3(stream->standard ? 25 : 22, stream->handle, (unsigned int)buffer, chunk);
        if (n <= 0) { stream->error = 1; errno = EIO; break; }
        done += (size_t)n;
    }
    return done / size;
}

int fgetc(FILE *stream) { unsigned char c; return fread(&c, 1, 1, stream) == 1 ? c : EOF; }
int getchar(void) { return fgetc(stdin); }
int fputc(int c, FILE *stream) { unsigned char b = (unsigned char)c; return fwrite(&b, 1, 1, stream) == 1 ? b : EOF; }
int putchar(int c) { return fputc(c, stdout); }
char *fgets(char *restrict s, int size, FILE *restrict stream)
{
    int i = 0, c;
    if (size <= 0) return 0;
    while (i < size - 1 && (c = fgetc(stream)) != EOF)
    { s[i++] = (char)c; if (c == '\n') break; }
    if (!i && size != 1) return 0;
    s[i] = 0;
    return s;
}
int fputs(const char *restrict s, FILE *restrict stream)
{ size_t n = strlen(s); return fwrite(s, 1, n, stream) == n ? 0 : EOF; }
int puts(const char *s)
{ return fputs(s, stdout) == EOF || fputc('\n', stdout) == EOF ? EOF : 0; }
int fflush(FILE *stream)
{ if (stream && !stream->active) { errno = EBADF; return EOF; } return 0; }
int fseek(FILE *stream, long offset, int origin)
{
    if (!stream || !stream->active || stream->standard)
    { errno = EBADF; return -1; }
    if (origin != SEEK_SET && origin != SEEK_CUR && origin != SEEK_END)
    { errno = EINVAL; return -1; }
    if (call3(38, stream->handle, (unsigned int)offset, origin) < 0)
    { errno = EINVAL; return -1; }
    stream->eof = 0;
    return 0;
}
long ftell(FILE *stream)
{
    int result;
    if (!stream || !stream->active || stream->standard)
    { errno = EBADF; return -1; }
    result = call3(38, stream->handle, 0, SEEK_CUR);
    if (result < 0) errno = EIO;
    return result;
}
void rewind(FILE *stream) { if (fseek(stream, 0, SEEK_SET) == 0) clearerr(stream); }
int feof(FILE *stream) { return stream->eof != 0; }
int ferror(FILE *stream) { return stream->error != 0; }
void clearerr(FILE *stream) { stream->eof = stream->error = 0; }

static int print_number(FILE *stream, unsigned int value, unsigned int base)
{
    char digits[32];
    int n = 0, i;
    do { unsigned int d = value % base; digits[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); value /= base; } while (value);
    for (i = n; i; --i) if (fputc(digits[i - 1], stream) == EOF) return EOF;
    return n;
}

int vfprintf(FILE *restrict stream, const char *restrict format, va_list args)
{
    int count = 0;
    while (*format)
    {
        char c = *format++;
        if (c != '%') { if (fputc(c, stream) == EOF) return EOF; ++count; continue; }
        c = *format++;
        if (!c) { errno = EINVAL; return EOF; }
        if (c == 's')
        {
            const char *s = va_arg(args, const char *);
            size_t n;
            if (!s) s = "(null)";
            n = strlen(s);
            if (fwrite(s, 1, n, stream) != n) return EOF;
            count += (int)n;
        }
        else if (c == 'd' || c == 'i' || c == 'u' || c == 'x')
        {
            unsigned int value;
            int n;
            if (c == 'd' || c == 'i')
            {
                int signed_value = va_arg(args, int);
                value = (unsigned int)signed_value;
                if (signed_value < 0)
                { if (fputc('-', stream) == EOF) return EOF; ++count; value = 0U - value; }
            }
            else value = va_arg(args, unsigned int);
            n = print_number(stream, value, c == 'x' ? 16 : 10);
            if (n == EOF) return EOF;
            count += n;
        }
        else if (c == 'c' || c == '%')
        { if (fputc(c == 'c' ? va_arg(args, int) : '%', stream) == EOF) return EOF; ++count; }
        else { errno = EINVAL; return EOF; }
    }
    return count;
}
int fprintf(FILE *restrict stream, const char *restrict format, ...)
{
    va_list args; int result;
    va_start(args, format); result = vfprintf(stream, format, args); va_end(args);
    return result;
}
int printf(const char *restrict format, ...)
{
    va_list args; int result;
    va_start(args, format); result = vfprintf(stdout, format, args); va_end(args);
    return result;
}
void perror(const char *s)
{
    int saved = errno;
    if (s && *s) { fputs(s, stderr); fputs(": ", stderr); }
    fputs(strerror(saved), stderr);
    fputc('\n', stderr);
}
