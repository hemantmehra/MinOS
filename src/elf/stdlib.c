#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Block
{
    size_t size;
    struct Block *next;
    int available;
    int padding;
} Block;

static Block *blocks;
static Block *last;

static void *grow_heap(void)
{
    unsigned int address;
    __asm__ volatile ("int $0x80" : "=a"(address) : "a"(36) : "memory", "cc");
    return (void *)address;
}

void *malloc(size_t size)
{
    Block *block;
    size_t needed;

    if (size == 0) size = 1;
    if (size > 16U * 1024U * 1024U - sizeof(Block))
    { errno = ENOMEM; return 0; }
    needed = (size + 15) & ~(size_t)15;
    for (;;)
    {
        for (block = blocks; block; block = block->next)
        {
            if (block->available && block->size >= needed)
            {
                if (block->size >= needed + sizeof(Block) + 16)
                {
                    Block *split = (Block *)((char *)(block + 1) + needed);
                    split->size = block->size - needed - sizeof(Block);
                    split->next = block->next;
                    split->available = 1;
                    block->next = split;
                    block->size = needed;
                    if (last == block) last = split;
                }
                block->available = 0;
                return block + 1;
            }
        }
        {
            void *page = grow_heap();
            if (!page) { errno = ENOMEM; return 0; }
            if (last && last->available &&
                (char *)(last + 1) + last->size == (char *)page)
            {
                last->size += 4096;
            }
            else
            {
                block = page;
                block->size = 4096 - sizeof(Block);
                block->next = 0;
                block->available = 1;
                if (last) last->next = block;
                else blocks = block;
                last = block;
            }
        }
    }
}

void free(void *ptr)
{
    Block *block;
    if (!ptr) return;
    ((Block *)ptr - 1)->available = 1;
    for (block = blocks; block && block->next;)
    {
        Block *next = block->next;
        if (block->available && next->available &&
            (char *)(block + 1) + block->size == (char *)next)
        {
            block->size += sizeof(Block) + next->size;
            block->next = next->next;
            if (last == next) last = block;
        }
        else block = next;
    }
}

void *calloc(size_t count, size_t size)
{
    void *result;
    if (size && count > (size_t)-1 / size) { errno = ENOMEM; return 0; }
    size *= count;
    result = malloc(size);
    if (result) memset(result, 0, size);
    return result;
}

void *realloc(void *ptr, size_t size)
{
    void *result;
    size_t old_size;
    if (!ptr) return malloc(size);
    if (!size) { free(ptr); return 0; }
    old_size = ((Block *)ptr - 1)->size;
    if (size <= old_size) return ptr;
    result = malloc(size);
    if (result) { memcpy(result, ptr, old_size); free(ptr); }
    return result;
}

static unsigned long parse_integer(
    const char *s, char **end, int base, int is_signed)
{
    const char *start = s;
    unsigned long value = 0, limit;
    int negative = 0, digits = 0, overflow = 0;
    if (base != 0 && (base < 2 || base > 36))
    {
        errno = EINVAL;
        if (end) *end = (char *)start;
        return 0;
    }
    while (*s == ' ' || (*s >= '\t' && *s <= '\r')) ++s;
    if (*s == '-' || *s == '+') { negative = *s == '-'; ++s; }
    if ((base == 0 || base == 16) && s[0] == '0' &&
        (s[1] == 'x' || s[1] == 'X') &&
        ((s[2] >= '0' && s[2] <= '9') ||
         (s[2] >= 'a' && s[2] <= 'f') || (s[2] >= 'A' && s[2] <= 'F')))
    { base = 16; s += 2; }
    if (!base) base = *s == '0' ? 8 : 10;
    limit = is_signed ? (negative ? (unsigned long)LONG_MAX + 1 : LONG_MAX)
                      : ULONG_MAX;
    for (;; ++s)
    {
        int c = (unsigned char)*s;
        int digit = c >= '0' && c <= '9' ? c - '0' :
            c >= 'a' && c <= 'z' ? c - 'a' + 10 :
            c >= 'A' && c <= 'Z' ? c - 'A' + 10 : 36;
        if (digit >= base) break;
        ++digits;
        if (value > limit / (unsigned)base ||
            (value == limit / (unsigned)base && (unsigned)digit > limit % (unsigned)base))
            overflow = 1;
        else if (!overflow) value = value * (unsigned)base + (unsigned)digit;
    }
    if (end) *end = (char *)(digits ? s : start);
    if (overflow) { errno = ERANGE; return is_signed && negative ? (unsigned long)LONG_MIN : limit; }
    return negative ? 0UL - value : value;
}

long strtol(const char *restrict s, char **restrict end, int base)
{
    return (long)parse_integer(s, end, base, 1);
}
unsigned long strtoul(const char *restrict s, char **restrict end, int base)
{
    return parse_integer(s, end, base, 0);
}
int atoi(const char *s) { return (int)strtol(s, 0, 10); }
int abs(int x) { return x < 0 ? -x : x; }
extern char **environ;
char *getenv(const char *name)
{
    size_t length = strlen(name);
    char **entry;
    if (!length || strchr(name, '=')) return 0;
    for (entry = environ; *entry; ++entry)
        if (strncmp(*entry, name, length) == 0 && (*entry)[length] == '=')
            return *entry + length + 1;
    return 0;
}
void exit(int status)
{
    fflush(0);
    __asm__ volatile ("int $0x80" : : "a"(0), "b"(status) : "memory", "cc");
    for (;;) {}
}
void abort(void)
{
    fputs("Aborted.\n", stderr);
    exit(EXIT_FAILURE);
}
